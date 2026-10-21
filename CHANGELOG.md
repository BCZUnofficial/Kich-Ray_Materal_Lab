# Changelog

## 0.8.3

### Memory / responsiveness

- Reduced editable brush resolution cap from 4096 to 2048.
- Normal remains RGBA, while editable gloss and height are now stored as single-channel 8-bit maps. A 2048 material edit set is roughly 24 MiB instead of three RGBA maps.
- Undo now stores only the channel changed by the stroke and its memory budget is capped at 24 MiB.
- Brush live preview uses a fast non-BC3 path; full BC3 encode/decode remains for explicit generation/export.
- Reused existing D3D11 bump textures with `UpdateSubresource` when dimensions match instead of recreating SRVs/textures on every brush preview.
- OGF bump pairs are resized one at a time during preview loading to reduce transient import peaks.
- Animated skinning writes directly into the mapped dynamic vertex buffer instead of allocating a complete temporary GPU vertex array every frame.

### Brush transaction

- Brush painting is now assignment/idempotent rather than cumulative blending for identical settings. Repainting the same region with `Блеск 4%` keeps it at 4% instead of layering another 4% pass.
- Relief stamps likewise apply the selected preset directly instead of progressively increasing it on repeated passes.
- Added **Применить** and `Ctrl+S` as the confirmation/checkpoint for the current brush edits.
- `Ctrl+Z` continues to undo completed strokes before confirmation.

## 0.8.2

### Brush

- Brush target mode is now explicit: **Рельеф**, **Блеск**, or **Высота**.
- Added a dedicated **Высота кисти** control; gloss and height can now be painted independently.
- Height painting is stored as a separate editable height layer and participates in live bump regeneration/export.
- `Ctrl+Z` now restores all painted brush data for the stroke, including normal, gloss, and height tiles.
- Brush size remains visible directly in the Brush tab.

### Normal / gloss generation

- Reworked generated relief to combine painted/imported normals with an additional Sobel relief pass from height or diffuse data, making painted height changes much more visible.
- Added **Blur** and **Sharp** controls inspired by common normal-map generators, allowing softer or crisper relief extraction without replacing the existing workflow.
- Boosted live gloss preview for generated brush materials so local shiny/matte variation is easier to see during painting.

### Selection

- Reduced the selection inverted-hull outline again, from `0.0065` to `0.0038`, so it obscures even less of the surface.

## 0.8.1

### Brush

- Added `Ctrl+Z` undo by complete brush strokes. Undo stores only touched 64x64 normal/gloss tiles, with a bounded history to avoid large full-texture snapshots.
- Added a surface brush-radius ring derived from the hit triangle UV derivatives.
- Removed brush pressure/opacity. A stroke now applies the selected normal preset directly, with only the preset/radial edge mask used for feathering.
- Added per-brush local gloss painting. Low values produce matte/rough-looking areas; high values produce smoother/shinier areas in the SoC gloss model.
- Existing `_bump` gloss is reconstructed from packed `R` (`gloss = R^2`) before painting, so untouched areas retain their original gloss.
- `.krnp` preset format v2 stores the normal preset together with brush gloss; v1 remains backward compatible.

### Selection

- Reduced the selection inverted-hull extrusion from 0.018 to 0.0065 so the outline obscures less of the relief silhouette.

### Gloss

- Explicit gloss maps are now treated linearly before SoC square-root packing; a neutral generation setting preserves locally painted gloss values.

## 0.8

### Selection / mesh controls

- Replaced selection tint with an inverted-hull outline pass so bump relief remains visible.
- Added a Scene mesh list with per-part selection and visibility checkboxes.
- Added Show All and Hide Selected controls.
- Hidden mesh parts are ignored by picking and selection.

### Memory

- `build_vs2022.bat` now builds Release x64 by default.
- Viewport textures are capped at 1024 on the longest side.
- Repeated OGF materials share D3D shader-resource views instead of uploading duplicate textures.
- Static CPU skin/source vertex copies are released after GPU upload; skeletal source data is retained only when animation needs it.
- OGF/motion working buffers and duplicate model data are released after import.
- Brush layers are lazy and capped at 4096 on the longest side.
- Manual export no longer copies all full-resolution source images before encoding.

### OGF

- Verified the SoC `vertBoned2W` layout against `SkeletonX.h`: bone IDs at 0/2, position at 4, weight at 52 and UV at 56, stride 64.
- Removed the overly strict 0..1 rejection for 2-link weights; finite values are clamped for preview instead of rejecting the entire model.

### Gloss

- Made gloss preview more readable while keeping SoC `Nu.x * Nu.x` packed gloss semantics.
- Generated gloss uses a slightly broader response; imported bump gloss can be preview-adjusted without rewriting the source pair.

### Normal brush

- Added a UV-space normal brush painted directly through the 3D model.
- Added radius and opacity controls and interpolated continuous strokes.
- Existing `_bump/_bump#` normals are reconstructed as the editable base so painting does not flatten untouched detail.
- Added reusable normal brush presets with `.krnp` save/load, plus import of normal-map images as presets.
- Painted normal layers participate in preview and bump export.

## 0.7.1

### Skeleton / animation fixes

- Replaced DirectXMath quaternion-to-matrix conversion in animation skinning with the exact X-Ray `Fmatrix::mk_xform` convention.
- Bone hierarchy composition and model-to-bone skin matrices now follow SoC `mul_43` ordering used by `SkeletonAnimated.cpp`.
- SoC quaternion keys are read as `s16 / 32767` without pre-normalizing them, matching `QR2Quat`.
- Loading an OGF no longer immediately applies frame 0 of the first motion; the model stays in bind pose until an animation is selected or started.

### Bump / normal generation fixes

- Replaced the two-sample diffuse gradient with a tileable 3x3 Sobel normal generator.
- `Высота` now changes generated relief instead of affecting only bump# height storage.
- `Блеск` and `Высота` regenerate generated bump pairs live, including multi-material OGF previews.
- Bump normal-error compensation now uses the classic BumpX/SDK-style `error * 2 + 128` packing.
- Height remains stored in bump# alpha; gloss remains packed as square-root data for the SoC R2 `Nu.x * Nu.x` path.

## 0.7

### Animation preview

- Added SoC OGF4 embedded motion parsing from `OGF_S_SMPARAMS` and `OGF_S_MOTIONS`.
- Added external OMF loading when embedded motions are absent.
- OMF lookup checks the model directory and one directory above it.
- Added conditional **Анимация** tab: it is present only when animations were detected.
- Added motion list, play/pause, looping, timeline scrubbing and playback speed.
- Added CPU skinning for SoC 1-link and 2-link skeletal vertices.
- Added quaternion SLERP and translation interpolation matching the SoC 30 FPS runtime path.
- Animated bind skeleton overlay follows the selected motion.

### Bump generation

- Fixed accidental bump generation from the preview checker/cube material.
- A valid diffuse texture is now mandatory for bump generation and export.
- Normal/height data without diffuse can no longer start bump generation.
- Multi-material OGF bump generation now uses the real diffuse assigned to each mesh part.
- Selected geometry can receive a generated bump without replacing the loaded diffuse texture.

### Scene

- Loading a new model no longer resets the light orbit.
- Light azimuth and elevation are persisted in `MaterialLab.ini` next to the executable.
- RMB light rotation updates and saves the persisted slider values.
- Camera reset on model load remains independent from the light state.

### UI

- Added a fifth tab slot for animation while keeping it hidden for models without compatible motions.
- Reduced tab width so all five tabs fit the side panel when animation preview is available.

## 0.6

- Texture lookup changed from a hard-coded `gamedata` assumption to `meshes` -> sibling `textures` resolution.
- Export moved to `export/<model name>/` next to the executable.
- Generated bump assignment no longer replaces an already loaded diffuse texture.
- Model preview is centered around the scene origin on load.

## 0.5

- Added mesh-part geometry selection and mirrored-part selection.
- Restored normal strength editing for automatically loaded bump maps.
- Added MMB vertical camera movement.
- Simplified the main UI labels.
- Added automatic export naming from texture/model names.

## 0.4

- Added multi-mesh / multi-material OGF preview.
- Added bind skeleton import and overlay without animation playback.
- Added tabbed Import / Export / Settings / Scene interface.
