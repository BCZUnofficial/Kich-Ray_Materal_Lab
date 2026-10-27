#pragma once
#include "Core.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace lab {

enum class BrushOperation {
    ReliefAdd, ReliefRemove, HeightAdd, HeightRemove,
    GlossAdd, GlossRemove, Sharpen, Smooth
};

struct BrushSettings {
    float radiusPixels = 24.f;
    float strength = .35f;
    float hardness = .65f;
};

// The editor's material state has no window handles or rendering dependencies.
// Original packed channels are immutable; a tool switch cannot change pixels.
class MaterialDocument {
public:
    static constexpr uint32_t MaximumDimension = 4096;

    bool Load(const BumpPair& pair, std::string& error, bool unsaved = false);
    bool Open(const std::filesystem::path& path, std::string& error);
    bool Save(const std::filesystem::path& path, std::string& error);

    bool Valid() const { return normal_.valid(); }
    uint32_t Width() const { return normal_.w; }
    uint32_t HeightPixels() const { return normal_.h; }
    bool InStroke() const { return inStroke_; }
    bool BeginStroke(BrushOperation operation, const BrushSettings& settings);
    bool Stamp(float u, float v, const Image& preset);
    // One mouse press is one complete, undoable normal edit. Never inherits
    // the freehand brush's channel, removal mode or in-progress stroke.
    bool ApplySurfaceRelief(const Image& diffuse, SurfacePreset preset, float u, float v, const BrushSettings& settings);
    bool EndStroke();
    void CancelStroke();
    bool Undo();
    bool Redo();
    bool Clear();
    // Height lift reads diffuse colour/alpha at the material's texel scale;
    // every preview is relative to the pre-adjustment snapshot, never itself.
    bool Adjust(const MaterialAdjustments& settings, const Image* diffuse = nullptr);
    bool CommitAdjustments();
    void CancelAdjustments();
    bool HasPendingAdjustments() const { return adjustmentChanged_; }

    bool CanUndo() const { return !undo_.empty() || strokeChanged_; }
    bool CanRedo() const { return !inStroke_ && !redo_.empty(); }
    bool HasEdits() const { return modifiedPixels_ != 0 || strokeChanged_ || adjustmentChanged_; }
    bool IsDirty() const { return state_ != savedState_ || strokeChanged_ || adjustmentChanged_; }
    void MarkSaved();
    size_t UndoCount() const { return undo_.size(); }
    size_t RedoCount() const { return redo_.size(); }
    size_t HistoryBytes() const { return historyBytes_; }
    void SetHistoryBudget(size_t bytes, size_t maximumSteps = 64);

    const BumpPair& Original() const { return original_; }
    const Image& Normal() const { return normal_; }
    const GrayImage& Gloss() const { return gloss_; }
    const GrayImage& Height() const { return height_; }
    const Image& BaseNormal() const { return baseNormal_; }
    const GrayImage& BaseGloss() const { return baseGloss_; }
    const GrayImage& BaseHeight() const { return baseHeight_; }
    BumpPair Preview(uint32_t maximumDimension = 768) const;

private:
    enum class Channel : uint8_t { Normal, Gloss, Height };
    struct Tile {
        uint32_t x = 0, y = 0, w = 0, h = 0;
        Channel channel = Channel::Normal;
        std::vector<Pixel> normalBefore, normalAfter;
        std::vector<uint8_t> scalarBefore, scalarAfter;
        size_t Bytes() const;
    };
    struct Change {
        std::vector<Tile> tiles;
        uint64_t beforeState = 1, afterState = 1;
        size_t beforeModified = 0, afterModified = 0;
        size_t Bytes() const;
    };

    static constexpr uint32_t TileSize = 64;
    BumpPair original_;
    Image normal_, baseNormal_;
    GrayImage gloss_, height_, baseGloss_, baseHeight_;
    Image adjustmentNormal_;
    GrayImage adjustmentGloss_, adjustmentHeight_;
    bool adjustmentChanged_ = false;
    uint64_t state_ = 1, savedState_ = 1, nextState_ = 2;
    size_t modifiedPixels_ = 0;
    std::deque<Change> undo_, redo_;
    size_t historyBytes_ = 0, historyBudget_ = 64u * 1024u * 1024u;
    size_t maximumSteps_ = 64;

    bool inStroke_ = false, strokeChanged_ = false;
    BrushOperation operation_ = BrushOperation::ReliefAdd;
    BrushSettings settings_;
    Image strokeNormal_;
    GrayImage strokeScalar_, coverage_;
    Change stroke_;
    std::unordered_set<uint64_t> capturedTiles_;

    Channel ActiveChannel() const;
    Tile CaptureTile(Channel channel, uint32_t x, uint32_t y) const;
    bool FinishTile(Tile& tile) const;
    void RestoreTile(const Tile& tile, bool after);
    size_t CountModified(const Tile& tile, bool after) const;
    void CaptureStamp(float u, float v);
    void ResetStroke();
    void Record(Change&& change);
    void TrimHistory();
};
}
