#include "MaterialDocument.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
bool same(const lab::Image& a, const lab::Image& b) {
    return a.w == b.w && a.h == b.h && a.pixels.size() == b.pixels.size() &&
        (a.pixels.empty() || std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size() * sizeof(lab::Pixel)) == 0);
}
bool same(const lab::BumpPair& a, const lab::BumpPair& b) {
    return same(a.bump, b.bump) && same(a.bumpX, b.bumpX);
}
lab::BumpPair material(uint32_t w = 70, uint32_t h = 39) {
    lab::BumpPair pair;
    pair.bump.w = pair.bumpX.w = w; pair.bump.h = pair.bumpX.h = h;
    pair.bump.pixels.assign(size_t(w) * h, {160, 250, 115, 157});
    pair.bumpX.pixels.assign(size_t(w) * h, {128, 128, 128, 128});
    return pair;
}
lab::Image preset() {
    lab::Image image; image.w = image.h = 8; image.pixels.assign(64, {180, 128, 242, 255}); return image;
}
bool stroke(lab::MaterialDocument& document, lab::BrushOperation operation,
            float u = .5f, float v = .5f, float strength = .12f, float radius = 7.f) {
    const lab::BrushSettings settings{radius, strength, .6f};
    if (!document.BeginStroke(operation, settings)) return false;
    document.Stamp(u, v, preset());
    return document.EndStroke();
}
void losslessAndNoOp() {
    std::string error; lab::MaterialDocument document; const auto original = material(17, 81);
    check(document.Load(original, error), "load valid pair");
    check(!document.IsDirty() && !document.HasEdits() && document.UndoCount() == 0, "import starts clean");
    check(same(document.Preview(0), original), "unchanged rectangular material preserves original size and bytes");
    const lab::BrushSettings zero{5, 0, .5f};
    check(!document.BeginStroke(lab::BrushOperation::ReliefAdd, zero), "zero force cannot create a stroke");
    check(!document.Stamp(.5f, .5f, preset()) && !document.EndStroke(), "no implicit stroke");
    check(!document.IsDirty() && same(document.Preview(0), original), "zero force leaves preview, dirty flag and history intact");
    lab::Image flat = preset(); for (auto& p : flat.pixels) p = {128, 128, 255, 255};
    check(document.BeginStroke(lab::BrushOperation::ReliefAdd, {5, .8f, .5f}), "flat preset stroke begins");
    check(!document.Stamp(.5f, .5f, flat) && !document.EndStroke(), "flat add preset is no op");
    check(!document.IsDirty() && document.UndoCount() == 0, "empty stroke cannot consume undo");
    lab::BumpPair invalid = material(); invalid.bumpX.h = 20;
    check(!document.Load(invalid, error) && same(document.Preview(0), original), "invalid replacement preserves material");
    const auto large = material(2057, 9);
    check(document.Load(large, error) && document.Width() == 2057 &&
          document.HeightPixels() == 9 && same(document.Preview(0), large), "import never silently caps an authored texture to 1024");
}
void strokesAndHistory() {
    std::string error; lab::MaterialDocument document; const auto original = material();
    check(document.Load(original, error), "load history fixture");
    check(stroke(document, lab::BrushOperation::HeightAdd), "first stroke changes height");
    const auto first = document.Height().pixels;
    check(stroke(document, lab::BrushOperation::HeightAdd), "second stroke accumulates");
    const auto second = document.Height().pixels;
    check(first != second && document.UndoCount() == 2 && document.IsDirty(), "independent strokes have independent baselines");
    document.MarkSaved();
    check(!document.IsDirty() && document.UndoCount() == 2, "marking applied does not erase undo");
    check(document.Undo() && document.Height().pixels == first && document.IsDirty(), "undo after apply restores previous state");
    check(document.Redo() && document.Height().pixels == second && !document.IsDirty(), "redo restores the saved state");
    check(document.Undo() && document.Undo(), "both strokes undo");
    check(!document.HasEdits() && same(document.Preview(0), original), "all undone restores original packed channels");
    check(stroke(document, lab::BrushOperation::GlossRemove) && !document.CanRedo(), "new real edit invalidates redo");
    const auto beforeClear = document.Preview(0);
    check(document.Clear() && !document.HasEdits() && same(document.Preview(0), original), "clear restores the baseline");
    check(document.Undo() && same(document.Preview(0), beforeClear), "clear itself is undoable");
    check(document.Redo() && !document.HasEdits(), "clear can be redone");
}
void strokeCancelAndCoverage() {
    std::string error; lab::MaterialDocument document; const auto original = material();
    check(document.Load(original, error), "load coverage fixture");
    check(document.BeginStroke(lab::BrushOperation::ReliefAdd, {9, .2f, .6f}), "begin coverage fixture");
    check(document.Stamp(.98f, .02f, preset()), "wrapped corner stamp changes normal");
    const auto first = document.Normal();
    check(!document.Stamp(.98f, .02f, preset()) && same(first, document.Normal()), "same stroke does not accumulate equal coverage");
    document.CancelStroke();
    check(!document.InStroke() && !document.IsDirty() && !document.CanUndo() && same(document.Preview(0), original), "cancel restores every wrapped NPOT tile");
    check(stroke(document, lab::BrushOperation::ReliefAdd, .98f, .02f, .25f, 9), "normal stroke can complete");
    check(stroke(document, lab::BrushOperation::GlossAdd, .01f, .98f, .2f, 9), "scalar stroke can complete");
    check(document.Undo() && document.Undo() && same(document.Preview(0), original), "channel-specific undo covers NPOT wrapped edges");
}
void memoryBudget() {
    std::string error; lab::MaterialDocument document;
    check(document.Load(material(128, 128), error), "load budget fixture");
    document.SetHistoryBudget(1, 1);
    check(stroke(document, lab::BrushOperation::HeightAdd, .25f, .25f), "first budget stroke");
    const auto first = document.Height().pixels;
    check(stroke(document, lab::BrushOperation::HeightAdd, .75f, .75f), "second budget stroke");
    check(document.UndoCount() == 1 && document.HistoryBytes() > 1, "memory budget retains the most recent stroke even if larger");
    check(document.Undo() && document.Height().pixels == first, "retained latest stroke remains undoable");
}
std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate); check(bool(in), "open test session bytes");
    const size_t size = size_t(in.tellg()); in.seekg(0); std::vector<uint8_t> bytes(size);
    in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())); check(bool(in), "read test session bytes"); return bytes;
}
void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); check(bool(out), "write malformed test session");
}
void persistence(const std::filesystem::path& directory) {
    std::string error; lab::MaterialDocument document;
    check(document.Load(material(129, 65), error, true) && document.IsDirty(), "new generated base starts unsaved");
    check(stroke(document, lab::BrushOperation::HeightAdd), "edit persistent fixture");
    const auto first = document.Preview(0);
    const auto file = directory / "material.krlab", malformed = directory / "malformed.krlab";
    check(document.Save(file, error) && !document.IsDirty() && document.CanUndo(), "save session without erasing undo");
    check(document.Undo() && document.IsDirty(), "undo before saved state is dirty");
    check(document.Redo() && !document.IsDirty(), "redo saved state is clean");
    check(stroke(document, lab::BrushOperation::ReliefRemove), "edit before failed save");
    const auto beforeFailure = document.Preview(0); const auto history = document.UndoCount();
    check(!document.Save(directory, error) && document.IsDirty() && document.UndoCount() == history && same(beforeFailure, document.Preview(0)), "failed replacement preserves edits, dirty state and undo");
    lab::MaterialDocument reopened;
    check(reopened.Open(file, error) && !reopened.IsDirty() && reopened.HasEdits() && same(reopened.Preview(0), first), "session reopens edited layers and original pair exactly");
    check(reopened.Clear() && reopened.Undo() && same(reopened.Preview(0), first), "reopened document can clear and restore edits");
    const auto bytes = read(file);
    auto bad = bytes; bad.resize(bad.size() - 1); write(malformed, bad);
    check(!reopened.Open(malformed, error) && same(reopened.Preview(0), first), "truncated session does not replace current material");
    bad = bytes; bad.back() ^= 1; write(malformed, bad);
    check(!reopened.Open(malformed, error) && same(reopened.Preview(0), first), "corrupt payload does not replace current material");
    bad = bytes; bad[12] = bad[13] = bad[14] = bad[15] = 255; write(malformed, bad);
    check(!reopened.Open(malformed, error) && same(reopened.Preview(0), first), "hostile dimensions rejected before allocating");
    bad = bytes; bad.push_back(0); write(malformed, bad);
    check(!reopened.Open(malformed, error), "trailing session data rejected");
    check(document.Save(file, error) && !document.IsDirty(), "existing session atomically replaced");
    lab::MaterialDocument replacement; check(replacement.Open(file, error) && same(replacement.Preview(0), beforeFailure), "replacement contains latest material");
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        check(entry.path().filename().string().find(".writing-") == std::string::npos, "staging files cleaned after both success and failure");
}
void diffuseHeightLift(const std::filesystem::path& directory) {
    std::string error;
    auto original=material(6,1);
    const uint8_t heights[]={30,50,70,90,110,230};
    for(size_t i=0;i<6;++i)original.bumpX.pixels[i].a=heights[i];
    lab::MaterialDocument document;
    check(document.Load(original,error),"load diffuse height fixture");
    const auto normal=document.Normal();const auto gloss=document.Gloss().pixels;
    lab::Image diffuse;diffuse.w=6;diffuse.h=1;
    diffuse.pixels={{0,0,0,255},{64,64,64,255},{128,128,128,255},
                    {255,255,255,255},{255,255,255,0},{255,255,255,128}};
    lab::MaterialAdjustments settings;
    settings.heightFromDiffuse=true;settings.diffuseHeightStrength=.5f;
    check(!document.Adjust(settings)&&same(document.Preview(0),original)&&!document.HasPendingAdjustments(),
          "height lift requires diffuse without opening or changing a draft");
    check(document.Adjust(settings,&diffuse)&&document.HasPendingAdjustments(),"diffuse height lift creates a live draft");
    const std::vector<uint8_t> expected{30,82,134,218,110,255};
    check(document.Height().pixels==expected,"height lift adds masked luminance to existing height and clamps saturation");
    check(same(document.Normal(),normal)&&document.Gloss().pixels==gloss,"height lift preserves authored normal and gloss layers");
    check(document.UndoCount()==0&&!document.BeginStroke(lab::BrushOperation::HeightAdd,{2,.1f,.5f}),
          "pending height lift keeps ordinary draft and stroke rules");
    const auto first=document.Preview(0);
    check(document.Adjust(settings,&diffuse)&&same(document.Preview(0),first),"repeated height previews cannot accumulate");
    lab::Image invalid;
    check(!document.Adjust(settings,&invalid)&&same(document.Preview(0),first),"invalid diffuse leaves an existing draft intact");
    auto bad=settings;bad.diffuseHeightStrength=std::numeric_limits<float>::quiet_NaN();
    check(!document.Adjust(bad,&diffuse)&&same(document.Preview(0),first),"NaN lift cannot corrupt a pending draft");
    bad=settings;bad.diffuseHeightStrength=-1;
    check(!document.Adjust(bad,&diffuse)&&same(document.Preview(0),first),"negative lift cannot lower a height map accidentally");
    settings.diffuseHeightStrength=.25f;
    check(document.Adjust(settings,&diffuse)&&document.Height().pixels==std::vector<uint8_t>({30,66,102,154,110,255}),
          "lowering lift strength recomputes from the immutable pre-adjustment height");
    settings.heightFromDiffuse=false;
    check(document.Adjust(settings)&&!document.HasPendingAdjustments()&&same(document.Preview(0),original),
          "unchecking height lift restores the baseline without requiring diffuse or consuming history");
    settings.heightFromDiffuse=true;settings.diffuseHeightStrength=.5f;
    check(document.Adjust(settings,&diffuse),"restart lift after checkbox cancellation");
    document.CancelAdjustments();
    check(!document.HasPendingAdjustments()&&!document.IsDirty()&&same(document.Preview(0),original),
          "Discard restores all original packed channels");
    check(document.Adjust(settings,&diffuse)&&document.CommitAdjustments()&&document.UndoCount()==1,
          "Apply stores one complete lift edit");
    check(document.Undo()&&same(document.Preview(0),original),"height lift undo restores pre-adjustment height");
    check(document.Redo()&&document.Height().pixels==expected&&same(document.Normal(),normal)&&document.Gloss().pixels==gloss,
          "height lift redo restores only its intended layer changes");
    const auto applied=document.Preview(0);
    const auto path=directory/"diffuse-height.krlab";
    check(document.Save(path,error),"lifted height saves in the ordinary material session");
    lab::MaterialDocument reopened;
    check(reopened.Open(path,error)&&same(reopened.Preview(0),applied),"lifted material reopens without needing the diffuse source");
    // The authored height multiplier and the new positive lift are independent:
    // a multiplier of zero first flattens at 128, then adds texture-derived lift.
    check(document.Load(original,error),"reload fixture for combined height controls");
    settings.height=0;settings.diffuseHeightStrength=.25f;
    check(document.Adjust(settings,&diffuse)&&document.Height().pixels==std::vector<uint8_t>({128,144,160,192,128,160}),
          "texture lift applies after the existing height-range multiplier");
    document.CancelAdjustments();
    settings.height=1;settings.diffuseHeightStrength=0;
    check(document.Adjust(settings,&diffuse)&&!document.HasPendingAdjustments()&&!document.IsDirty(),
          "zero lift is byte-exact and consumes no history");

    lab::Image edge;edge.w=2;edge.h=1;
    edge.pixels={{0,0,0,255},{255,255,255,0}};
    auto field=lab::MakeDiffuseHeightLift(edge,1,1);
    check(field.valid()&&field.pixels[0]==0,"transparent white RGB cannot create relief after downsampling");
    edge.pixels={{255,255,255,255},{0,0,0,0}};
    field=lab::MakeDiffuseHeightLift(edge,1,1);
    check(field.valid()&&field.pixels[0]==128,"opaque light contributes its coverage exactly once during downsampling");
    field=lab::MakeDiffuseHeightLift(edge,4,1);
    check(field.valid()&&field.w==4&&field.h==1&&field.pixels==std::vector<uint8_t>({255,191,64,0}),
          "diffuse-to-bump upsampling aligns texel centres with bilinear filtering");
    edge.pixels={{255,255,255,0},{200,100,30,0}};
    field=lab::MakeDiffuseHeightLift(edge,3,5);
    check(field.valid()&&std::all_of(field.pixels.begin(),field.pixels.end(),[](uint8_t p){return p==0;}),
          "fully transparent diffuse remains a no-op at a different resolution");
    check(!lab::MakeDiffuseHeightLift(edge,0,1).valid()&&!lab::MakeDiffuseHeightLift(edge,4097,1).valid(),
          "invalid height output dimensions are rejected before allocation");

    // Existing brush height, not the originally imported height, is the baseline
    // for a later slider adjustment and must survive both Discard and Undo.
    check(document.Load(material(),error)&&stroke(document,lab::BrushOperation::HeightAdd),"paint height before a lift");
    const auto painted=document.Preview(0);const auto paintedHeight=document.Height().pixels;
    diffuse.w=diffuse.h=1;diffuse.pixels={{128,128,128,255}};
    settings.diffuseHeightStrength=.125f;
    check(document.Adjust(settings,&diffuse),"lift a material with existing brush edits");
    for(size_t i=0;i<paintedHeight.size();++i)
        check(document.Height().pixels[i]==std::min(255,int(paintedHeight[i])+16),"diffuse lift preserves prior per-pixel brush height");
    check(document.CommitAdjustments()&&document.UndoCount()==2&&document.Undo()&&same(document.Preview(0),painted),
          "undoing a later lift retains the earlier height brush stroke");
}
}

int main() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("material-lab-document-tests-" + std::to_string(suffix));
    try {
        std::filesystem::create_directories(directory);
        losslessAndNoOp(); strokesAndHistory(); strokeCancelAndCoverage(); memoryBudget(); persistence(directory); diffuseHeightLift(directory);
        std::filesystem::remove_all(directory);
        std::cout << "PASS: material document, native dimensions, per-stroke undo/redo, no-op, clear/cancel, history budget, transactional sessions, diffuse-derived height lift\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n'; return 1;
    }
}
