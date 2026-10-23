#include "MaterialDocument.h"
#include "ViewportTools.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lab {
namespace {
bool equal(Pixel a, Pixel b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
bool equal(const std::vector<Pixel>& a, const std::vector<Pixel>& b) {
    return a.size() == b.size() && (a.empty() ||
        std::memcmp(a.data(), b.data(), a.size() * sizeof(Pixel)) == 0);
}
bool dimensions(uint32_t w, uint32_t h) {
    return w != 0 && h != 0 && w <= MaterialDocument::MaximumDimension &&
        h <= MaterialDocument::MaximumDimension;
}
uint64_t hashBytes(uint64_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    return hash;
}
constexpr uint64_t HashSeed = 14695981039346656037ull;
constexpr size_t HeaderSize = 40;
constexpr uint8_t Magic[8] = {'K','R','L','A','B',0,'\r','\n'};
void put32(uint8_t* destination, uint32_t value) {
    for (int i = 0; i < 4; ++i) destination[i] = uint8_t(value >> (i * 8));
}
uint32_t get32(const uint8_t* source) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) result |= uint32_t(source[i]) << (i * 8);
    return result;
}
void put64(uint8_t* destination, uint64_t value) {
    for (int i = 0; i < 8; ++i) destination[i] = uint8_t(value >> (i * 8));
}
uint64_t get64(const uint8_t* source) {
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) result |= uint64_t(source[i]) << (i * 8);
    return result;
}
struct StagingFile {
    std::filesystem::path path;
    ~StagingFile() { std::error_code ignored; std::filesystem::remove(path, ignored); }
};
std::filesystem::path stagingPath(const std::filesystem::path& path) {
    static std::atomic<uint64_t> sequence{0};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    auto result = path;
    result += ".writing-" + std::to_string(tick) + "-" + std::to_string(++sequence);
    return result;
}
bool replaceFile(const std::filesystem::path& staging,
                 const std::filesystem::path& destination, std::string& error) {
#ifdef _WIN32
    if (!MoveFileExW(staging.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot replace the session file (Windows error " +
            std::to_string(GetLastError()) + ")";
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(staging, destination, ec);
    if (ec) { error = "Cannot replace the session file: " + ec.message(); return false; }
#endif
    return true;
}
}

size_t MaterialDocument::Tile::Bytes() const {
    return (normalBefore.size() + normalAfter.size()) * sizeof(Pixel) +
        scalarBefore.size() + scalarAfter.size();
}
size_t MaterialDocument::Change::Bytes() const {
    size_t result = 0;
    for (const auto& tile : tiles) result += tile.Bytes();
    return result;
}

bool MaterialDocument::Load(const BumpPair& pair, std::string& error, bool unsaved) {
    error.clear();
    if (!pair.bump.valid() || !pair.bumpX.valid() ||
        pair.bump.w != pair.bumpX.w || pair.bump.h != pair.bumpX.h ||
        !dimensions(pair.bump.w, pair.bump.h)) {
        error = "A matching bump/bump# pair between 1 and 4096 pixels is required";
        return false;
    }
    try {
        MaterialDocument next;
        next.historyBudget_ = historyBudget_;
        next.maximumSteps_ = maximumSteps_;
        next.original_ = pair;
        next.baseNormal_ = UnpackNormalMap(pair.bump, pair.bumpX);
        next.baseGloss_ = UnpackGlossGray(pair.bump);
        next.baseHeight_ = UnpackHeightGray(pair.bumpX);
        if (!next.baseNormal_.valid() || !next.baseGloss_.valid() || !next.baseHeight_.valid()) {
            error = "The material pair could not be decoded";
            return false;
        }
        next.normal_ = next.baseNormal_;
        next.gloss_ = next.baseGloss_;
        next.height_ = next.baseHeight_;
        next.savedState_ = unsaved ? 0 : 1;
        *this = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

MaterialDocument::Channel MaterialDocument::ActiveChannel() const {
    switch (operation_) {
    case BrushOperation::HeightAdd: case BrushOperation::HeightRemove: return Channel::Height;
    case BrushOperation::GlossAdd: case BrushOperation::GlossRemove: return Channel::Gloss;
    default: return Channel::Normal;
    }
}

bool MaterialDocument::BeginStroke(BrushOperation operation, const BrushSettings& settings) {
    if (!Valid() || !std::isfinite(settings.radiusPixels) || settings.radiusPixels < .5f ||
        !std::isfinite(settings.strength) || settings.strength <= 0.f ||
        !std::isfinite(settings.hardness) ||
        int(operation) < int(BrushOperation::ReliefAdd) || int(operation) > int(BrushOperation::Smooth))
        return false;
    EndStroke();
    operation_ = operation;
    settings_ = settings;
    settings_.radiusPixels = std::min(settings_.radiusPixels, float(MaximumDimension));
    settings_.strength = std::clamp(settings_.strength, 0.f, 1.f);
    settings_.hardness = std::clamp(settings_.hardness, 0.f, 1.f);
    // Filters read their neighbours from the immutable pre-stroke image. Using
    // the output as the source would make a pass depend on scan direction.
    if (ActiveChannel() == Channel::Normal) strokeNormal_ = normal_;
    else strokeScalar_ = ActiveChannel() == Channel::Gloss ? gloss_ : height_;
    stroke_.beforeState = state_;
    stroke_.beforeModified = modifiedPixels_;
    inStroke_ = true;
    return true;
}

MaterialDocument::Tile MaterialDocument::CaptureTile(Channel channel, uint32_t x, uint32_t y) const {
    Tile result;
    result.channel = channel;
    result.x = x; result.y = y;
    result.w = std::min(TileSize, Width() - x);
    result.h = std::min(TileSize, HeightPixels() - y);
    const size_t count = size_t(result.w) * result.h;
    if (channel == Channel::Normal) result.normalBefore.resize(count);
    else result.scalarBefore.resize(count);
    for (uint32_t row = 0; row < result.h; ++row) {
        const size_t source = size_t(y + row) * Width() + x;
        const size_t destination = size_t(row) * result.w;
        if (channel == Channel::Normal)
            std::copy_n(normal_.pixels.begin() + source, result.w, result.normalBefore.begin() + destination);
        else {
            const auto& layer = channel == Channel::Gloss ? gloss_ : height_;
            std::copy_n(layer.pixels.begin() + source, result.w, result.scalarBefore.begin() + destination);
        }
    }
    return result;
}

void MaterialDocument::CaptureStamp(float u, float v) {
    const int x = int(std::floor((u - std::floor(u)) * Width()));
    const int y = int(std::floor((v - std::floor(v)) * HeightPixels()));
    const int radius = int(std::ceil(settings_.radiusPixels));
    const auto columns = WrappedBrushTiles(x, radius, int(Width()), int(TileSize));
    const auto rows = WrappedBrushTiles(y, radius, int(HeightPixels()), int(TileSize));
    for (const int row : rows) for (const int column : columns) {
        const uint64_t id = (uint64_t(uint32_t(row)) << 32) | uint32_t(column);
        if (capturedTiles_.insert(id).second)
            stroke_.tiles.push_back(CaptureTile(ActiveChannel(), uint32_t(column) * TileSize,
                                               uint32_t(row) * TileSize));
    }
}

bool MaterialDocument::Stamp(float u, float v, const Image& preset) {
    if (!inStroke_ || !std::isfinite(u) || !std::isfinite(v) || !preset.valid()) return false;
    CaptureStamp(u, v);
    bool changed = false;
    const auto& s = settings_;
    switch (operation_) {
    case BrushOperation::ReliefAdd: case BrushOperation::ReliefRemove:
        changed = StampNormalReliefLayer(normal_, strokeNormal_, coverage_, preset, u, v,
            s.radiusPixels, s.strength, operation_ == BrushOperation::ReliefRemove, s.hardness); break;
    case BrushOperation::HeightAdd: case BrushOperation::HeightRemove:
        changed = StampScalarDeltaLayer(height_, strokeScalar_, coverage_, preset, u, v,
            s.radiusPixels, operation_ == BrushOperation::HeightRemove ? -s.strength : s.strength, s.hardness); break;
    case BrushOperation::GlossAdd: case BrushOperation::GlossRemove:
        changed = StampScalarDeltaLayer(gloss_, strokeScalar_, coverage_, preset, u, v,
            s.radiusPixels, operation_ == BrushOperation::GlossRemove ? -s.strength : s.strength, s.hardness); break;
    case BrushOperation::Sharpen: case BrushOperation::Smooth:
        changed = StampNormalFilterLayer(normal_, strokeNormal_, coverage_, preset, u, v,
            s.radiusPixels, s.strength, operation_ == BrushOperation::Sharpen, s.hardness); break;
    }
    strokeChanged_ = strokeChanged_ || changed;
    return changed;
}

bool MaterialDocument::FinishTile(Tile& tile) const {
    const size_t count = size_t(tile.w) * tile.h;
    if (tile.channel == Channel::Normal) tile.normalAfter.resize(count);
    else tile.scalarAfter.resize(count);
    for (uint32_t row = 0; row < tile.h; ++row) {
        const size_t source = size_t(tile.y + row) * Width() + tile.x;
        const size_t destination = size_t(row) * tile.w;
        if (tile.channel == Channel::Normal)
            std::copy_n(normal_.pixels.begin() + source, tile.w, tile.normalAfter.begin() + destination);
        else {
            const auto& layer = tile.channel == Channel::Gloss ? gloss_ : height_;
            std::copy_n(layer.pixels.begin() + source, tile.w, tile.scalarAfter.begin() + destination);
        }
    }
    return tile.channel == Channel::Normal ? !equal(tile.normalBefore, tile.normalAfter)
                                          : tile.scalarBefore != tile.scalarAfter;
}

size_t MaterialDocument::CountModified(const Tile& tile, bool after) const {
    size_t count = 0;
    for (uint32_t y = 0; y < tile.h; ++y) for (uint32_t x = 0; x < tile.w; ++x) {
        const size_t local = size_t(y) * tile.w + x;
        const size_t original = size_t(tile.y + y) * Width() + tile.x + x;
        if (tile.channel == Channel::Normal) {
            const auto& values = after ? tile.normalAfter : tile.normalBefore;
            count += !equal(values[local], baseNormal_.pixels[original]);
        } else {
            const auto& values = after ? tile.scalarAfter : tile.scalarBefore;
            const auto& base = tile.channel == Channel::Gloss ? baseGloss_ : baseHeight_;
            count += values[local] != base.pixels[original];
        }
    }
    return count;
}

void MaterialDocument::RestoreTile(const Tile& tile, bool after) {
    for (uint32_t row = 0; row < tile.h; ++row) {
        const size_t destination = size_t(tile.y + row) * Width() + tile.x;
        const size_t source = size_t(row) * tile.w;
        if (tile.channel == Channel::Normal) {
            const auto& values = after ? tile.normalAfter : tile.normalBefore;
            std::copy_n(values.begin() + source, tile.w, normal_.pixels.begin() + destination);
        } else {
            const auto& values = after ? tile.scalarAfter : tile.scalarBefore;
            auto& layer = tile.channel == Channel::Gloss ? gloss_ : height_;
            std::copy_n(values.begin() + source, tile.w, layer.pixels.begin() + destination);
        }
    }
}

void MaterialDocument::ResetStroke() {
    inStroke_ = strokeChanged_ = false;
    strokeNormal_ = {}; strokeScalar_ = {}; coverage_ = {};
    stroke_ = {}; capturedTiles_.clear();
}

void MaterialDocument::Record(Change&& change) {
    const size_t bytes = change.Bytes();
    // Allocate the history slot before discarding redo. If allocation fails,
    // the active stroke still owns its before-images and can be cancelled.
    undo_.push_back(std::move(change));
    for (const auto& previous : redo_) historyBytes_ -= previous.Bytes();
    redo_.clear();
    historyBytes_ += bytes;
    TrimHistory();
}

bool MaterialDocument::EndStroke() {
    if (!inStroke_) return false;
    if (!strokeChanged_) { ResetStroke(); return false; }
    size_t modified = stroke_.beforeModified;
    for (auto it = stroke_.tiles.begin(); it != stroke_.tiles.end();) {
        if (!FinishTile(*it)) { it = stroke_.tiles.erase(it); continue; }
        modified -= CountModified(*it, false);
        modified += CountModified(*it, true);
        ++it;
    }
    if (stroke_.tiles.empty()) { ResetStroke(); return false; }
    const uint64_t next = modified ? nextState_ : 1;
    stroke_.afterState = next;
    stroke_.afterModified = modified;
    Record(std::move(stroke_));
    modifiedPixels_ = modified; state_ = next;
    if (modified) ++nextState_;
    ResetStroke();
    return true;
}

void MaterialDocument::CancelStroke() {
    if (!inStroke_) return;
    for (const auto& tile : stroke_.tiles) RestoreTile(tile, false);
    ResetStroke();
}

bool MaterialDocument::Undo() {
    EndStroke();
    if (undo_.empty()) return false;
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    const Change& change = redo_.back();
    for (const auto& tile : change.tiles) RestoreTile(tile, false);
    state_ = change.beforeState; modifiedPixels_ = change.beforeModified;
    return true;
}

bool MaterialDocument::Redo() {
    EndStroke();
    if (redo_.empty()) return false;
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    const Change& change = undo_.back();
    for (const auto& tile : change.tiles) RestoreTile(tile, true);
    state_ = change.afterState; modifiedPixels_ = change.afterModified;
    return true;
}

bool MaterialDocument::Clear() {
    EndStroke();
    if (!Valid() || !modifiedPixels_) return false;
    Change change;
    change.beforeState = state_; change.beforeModified = modifiedPixels_;
    for (Channel channel : {Channel::Normal, Channel::Gloss, Channel::Height}) {
        for (uint32_t y = 0; y < HeightPixels(); y += TileSize) {
            for (uint32_t x = 0; x < Width(); x += TileSize) {
                Tile tile = CaptureTile(channel, x, y);
                if (!CountModified(tile, false)) continue;
                if (channel == Channel::Normal) tile.normalAfter.resize(size_t(tile.w) * tile.h);
                else tile.scalarAfter.resize(size_t(tile.w) * tile.h);
                for (uint32_t row = 0; row < tile.h; ++row) {
                    const size_t source = size_t(y + row) * Width() + x;
                    const size_t destination = size_t(row) * tile.w;
                    if (channel == Channel::Normal)
                        std::copy_n(baseNormal_.pixels.begin() + source, tile.w, tile.normalAfter.begin() + destination);
                    else {
                        const auto& base = channel == Channel::Gloss ? baseGloss_ : baseHeight_;
                        std::copy_n(base.pixels.begin() + source, tile.w, tile.scalarAfter.begin() + destination);
                    }
                }
                change.tiles.push_back(std::move(tile));
            }
        }
    }
    // Build the whole undo record before changing a pixel, so allocation
    // failure cannot leave a partially cleared material.
    change.afterState = 1;
    change.afterModified = 0;
    Record(std::move(change));
    for (const auto& tile : undo_.back().tiles) RestoreTile(tile, true);
    state_ = 1; modifiedPixels_ = 0;
    return true;
}

void MaterialDocument::TrimHistory() {
    while (undo_.size() > 1 && (historyBytes_ > historyBudget_ || undo_.size() > maximumSteps_)) {
        historyBytes_ -= undo_.front().Bytes(); undo_.pop_front();
    }
    while (!redo_.empty() && (historyBytes_ > historyBudget_ || undo_.size() + redo_.size() > maximumSteps_)) {
        historyBytes_ -= redo_.front().Bytes(); redo_.pop_front();
    }
}
void MaterialDocument::SetHistoryBudget(size_t bytes, size_t maximumSteps) {
    historyBudget_ = bytes; maximumSteps_ = std::max(size_t(1), maximumSteps); TrimHistory();
}
void MaterialDocument::MarkSaved() { EndStroke(); savedState_ = state_; }

BumpPair MaterialDocument::Preview(uint32_t maximumDimension) const {
    if (!Valid()) return {};
    if (!HasEdits()) {
        if (!maximumDimension) return original_;
        return {ResizeImageMax(original_.bump, maximumDimension),
                ResizeImageMax(original_.bumpX, maximumDimension)};
    }
    return CompositeBumpLayersPreview(normal_, gloss_, height_, baseNormal_, baseGloss_,
                                     baseHeight_, original_, maximumDimension);
}

bool MaterialDocument::Save(const std::filesystem::path& path, std::string& error) {
    error.clear();
    if (!Valid() || path.empty()) { error = "No material or session filename"; return false; }
    try {
        EndStroke();
        static_assert(sizeof(Pixel) == 4, "Session normal and bump pixels are RGBA8");
        const size_t count = normal_.pixels.size();
        const std::array<std::pair<const void*, size_t>, 5> blocks = {{
            {original_.bump.pixels.data(), count * 4}, {original_.bumpX.pixels.data(), count * 4},
            {normal_.pixels.data(), count * 4}, {gloss_.pixels.data(), count}, {height_.pixels.data(), count}
        }};
        std::array<uint8_t, HeaderSize> header{};
        std::copy_n(Magic, 8, header.begin());
        put32(header.data() + 8, 1); put32(header.data() + 12, Width());
        put32(header.data() + 16, HeightPixels());
        put64(header.data() + 24, uint64_t(count) * 14);
        uint64_t digest = hashBytes(HashSeed, header.data(), 32);
        for (const auto& block : blocks) digest = hashBytes(digest, block.first, block.second);
        put64(header.data() + 32, digest);
        StagingFile staging{stagingPath(path)};
        std::ofstream out(staging.path, std::ios::binary | std::ios::trunc);
        if (!out) { error = "Cannot create the session file"; return false; }
        out.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
        for (const auto& block : blocks)
            out.write(static_cast<const char*>(block.first), std::streamsize(block.second));
        out.flush(); const bool written = bool(out); out.close();
        if (!written || !out) { error = "Session write failed"; return false; }
        if (!replaceFile(staging.path, path, error)) return false;
        savedState_ = state_;
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool MaterialDocument::Open(const std::filesystem::path& path, std::string& error) {
    error.clear();
    try {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) { error = "Cannot open the session file"; return false; }
        const auto length = in.tellg();
        if (length < std::streamoff(HeaderSize)) { error = "Truncated session header"; return false; }
        in.seekg(0);
        std::array<uint8_t, HeaderSize> header{};
        in.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size()));
        const uint32_t width = get32(header.data() + 12), height = get32(header.data() + 16);
        const uint64_t payload = get64(header.data() + 24);
        if (!in || std::memcmp(header.data(), Magic, 8) != 0 || get32(header.data() + 8) != 1 ||
            get32(header.data() + 20) != 0 || !dimensions(width, height) ||
            payload != uint64_t(width) * height * 14 ||
            uint64_t(length) != HeaderSize + payload) {
            error = "Invalid, unsupported or truncated material session"; return false;
        }
        const size_t count = size_t(width) * height;
        BumpPair pair;
        pair.bump.w = pair.bumpX.w = width; pair.bump.h = pair.bumpX.h = height;
        pair.bump.pixels.resize(count); pair.bumpX.pixels.resize(count);
        in.read(reinterpret_cast<char*>(pair.bump.pixels.data()), std::streamsize(count * 4));
        in.read(reinterpret_cast<char*>(pair.bumpX.pixels.data()), std::streamsize(count * 4));
        MaterialDocument next;
        next.historyBudget_ = historyBudget_; next.maximumSteps_ = maximumSteps_;
        if (!next.Load(pair, error)) return false;
        in.read(reinterpret_cast<char*>(next.normal_.pixels.data()), std::streamsize(count * 4));
        in.read(reinterpret_cast<char*>(next.gloss_.pixels.data()), std::streamsize(count));
        in.read(reinterpret_cast<char*>(next.height_.pixels.data()), std::streamsize(count));
        if (!in) { error = "Truncated material session"; return false; }
        uint64_t digest = hashBytes(HashSeed, header.data(), 32);
        digest = hashBytes(digest, pair.bump.pixels.data(), count * 4);
        digest = hashBytes(digest, pair.bumpX.pixels.data(), count * 4);
        digest = hashBytes(digest, next.normal_.pixels.data(), count * 4);
        digest = hashBytes(digest, next.gloss_.pixels.data(), count);
        digest = hashBytes(digest, next.height_.pixels.data(), count);
        if (digest != get64(header.data() + 32)) { error = "Material session checksum mismatch"; return false; }
        for (size_t i = 0; i < count; ++i) {
            next.modifiedPixels_ += !equal(next.normal_.pixels[i], next.baseNormal_.pixels[i]);
            next.modifiedPixels_ += next.gloss_.pixels[i] != next.baseGloss_.pixels[i];
            next.modifiedPixels_ += next.height_.pixels[i] != next.baseHeight_.pixels[i];
        }
        next.state_ = next.modifiedPixels_ ? 2 : 1;
        next.savedState_ = next.state_; next.nextState_ = 3;
        *this = std::move(next);
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
}
