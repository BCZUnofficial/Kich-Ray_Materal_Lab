# Changelog

## 0.8.9

### Interface

- Replaced per-control tab visibility toggling with real child page containers. Each tab now owns a separate page window, so controls from different tabs cannot overlap or bleed through during redraws.
- Page notifications are forwarded to the main window, preserving existing buttons, sliders and mesh-list behavior.

### Brush precision

- Reworked the brush strength slider to a nonlinear cubic curve with a true zero endpoint. The lower part of the slider now provides extremely small strengths for subtle height/gloss/relief adjustments.
- Added a dedicated **Резкость края** (brush hardness) control. It changes the radial feather independently from the Sharp/Blur material operations.
- Reduced the minimum brush footprint again to about 1.35 editable texels, while keeping a guard against single-pixel degeneration.

## 0.8.8

### Generated base / brush pipeline

- Split material editing into explicit stages: **Generated Base -> Brush Composite -> Export**.
- `Создать bump` now freezes the exact slider-generated result into `temp/<model>/<material>_base.krlt`. The temporary snapshot stores unpacked normal, gloss and height layers without an additional BC3 round trip. Slider dragging previews in memory; the temp snapshot is written only when tracking ends, avoiding repeated disk writes.
- Entering the brush no longer regenerates the slider result from diffuse/height. The brush always starts from the frozen `base` snapshot, preventing the previous double application of height/Sobel relief.
- `Ctrl+S / Применить` writes the confirmed composite into a separate `*_brush.krlt` snapshot. Clearing the brush returns to `*_base.krlt` instead of running the normal generator again.
- Creating a new generated base for a material starts a new brush stage for that material and clears the old brush working layer.

### Direct layer packing

- Added a direct X-Ray bump packing path for already-authored normal/gloss/height layers. Painted layers are no longer sent back through the general diffuse/height normal generator.
- Final export of painted material uses the direct packer, so slider-generated normal and brush edits are combined once into a single `_bump.dds` / `_bump#.dds` pair.
- Brush height remains visually active by deriving normals only from the **height delta relative to the frozen base height**. The base height itself is not converted into relief a second time.

## 0.8.7

### Interface

- Removed fixed-width tabs. Tab headers now size from their text and are forced into a single line, preventing long labels such as **Настройка** / **Анимация** from wrapping beneath another tab.
- Tab switching now hides every old page first and only then shows the new page, with redraw suspended during the transition. This removes the visual bleed where controls from one page could briefly remain visible under another.

### Model rotation

- Added a dedicated model-rotation mode on **R**.
- Press **R** while the viewport has focus, then drag with LMB to rotate the model independently from the orbit camera.
- Model rotation uses a real world transform. Lighting normals, tangents/binormals, outline, skeleton, brush cursor and UV picking remain aligned with the rotated model.
- Model rotation is unavailable on the Animation page and while an animation is playing.

### Animation isolation

- Animation pose evaluation is now limited to the **Анимация** tab.
- Leaving the Animation tab immediately stops playback, resets the timeline and restores the bind/T-pose.
- Entering the Animation tab starts in bind/T-pose; animation is shown only after play/scrub/select operations inside that page.
- While playback is running, all model-editing commands are hard-blocked: material controls, brush operations, selection/mesh editing, Ctrl+Z/Ctrl+S and model rotation.
- Camera orbit, light orbit, pan and zoom remain available during playback because they do not modify the model.


## 0.8.6

### Interface

- Reworked the side panel layout with Segoe UI, grouped sections, wider tabs and cleaner spacing.
- Import, export, material, scene and brush controls are visually grouped instead of appearing as one continuous control stack.
- Brush controls now show their own **Размер** and **Сила** values explicitly.

### Independent brush tool

- Brush operations no longer consume the global material `Normal / Gloss / Height / Blur / Sharp` settings during live preview or export of painted layers.
- Added independent brush operations: **Рельеф**, **Высота +**, **Впадина**, **Блеск +**, **Блеск -**, **Sharp**, and **Blur**.
- Added an independent brush **Сила** control.
- Added **Инвертировать действие**: height/gloss add/sub and sharp/blur swap direction; relief inverts the tangent-space XY direction.
- Each uncommitted brush settings session uses its own baseline snapshot, so repeated passes do not accumulate. Changing operation/strength/size/inversion starts a fresh brush session baseline.
- Local Sharp/Blur are applied only inside the brush footprint and use the brush preset alpha as the falloff mask.

### Precision editing

- Brush radius minimum is reduced to about a 2-texel radius on the 1024 editable layer; it never drops to a single-pixel stamp.
- Camera zoom range is extended from `1.6` minimum distance to `0.32`, and the near clip plane is reduced to `0.005` for close material inspection.

## 0.8.5

### Selection workflow

- Renamed the selection toggle to **Режим выделения**.
- Hovering a visible mesh in selection mode now gives a light material highlight without an outline.
- LMB fixes the mesh selection; the outline appears only after selection.
- RMB clears the current selection while selection mode is active instead of rotating the light.
- Switching tabs exits selection mode but preserves the current selected mesh and outline.
- While a mesh is selected, bump/normal shading on other visible mesh parts is disabled so editing is visually isolated to the selected part(s).

### Mesh availability / bump generation

- Hidden mesh parts are excluded from brush editing, bump generation target lists, texture-name selection and default export targeting.
- `Создать bump` now opens a mesh target list when more than one visible mesh with a diffuse texture is available. The user can select one target or use **Все**.
- Brush editing respects an active mesh selection and cannot silently jump to another visible part.

### Blur / normal processing

- Blur and Sharp now post-process the composed tangent-space normal field as well as the scalar height source. This makes Blur visibly affect imported/painted normal layers instead of only diffuse-derived height.

### Preview performance / quality

- Viewport material textures are capped at 768 instead of 1024 and live brush preview at 384 instead of 512. This slightly lowers preview smoothing/detail while reducing GPU uploads and CPU conversion work. Export resolution is unchanged.

## 0.8.4

### Brush correctness

- Brush editing is now allowed only on a material that already has a valid `_bump/_bump#` pair or on a material for which Material Lab has explicitly created a bump.
- Added a per-session coverage layer: overlapping passes with identical brush settings use maximum coverage instead of repeatedly mixing into the same pixel. `Ctrl+S` / **Применить** clears the coverage layer and starts a new application layer.
- Local gloss is treated as an authored absolute value during bump packing, so a brush value of 4% remains 4% rather than being multiplied again by the global gloss generator.
- Changing brush target, radius, gloss, height, or preset starts a fresh coverage layer. Undo also resets coverage so an undone area can be painted again.

### Performance / memory

- Editable brush layers are capped at 1024 on the longest side. Normal uses RGBA while gloss/height remain compact 8-bit grayscale. Undo is capped at 12 MiB / 16 strokes.
- Interactive brush preview now runs at 512 maximum dimension and updates at a lower cadence; a 1024 preview is generated only on Ctrl+S / Применить, avoiding 512↔1024 D3D texture churn after every stroke.
- Reworked the Sobel/Blur/Sharp generator to precompute the scalar height field once instead of repeatedly bilinear-sampling it for every Sobel tap.
- Added mesh AABB rejection and selected-part-first picking, avoiding full triangle scans across unrelated mesh parts during continuous brush movement.
- Brush activation stops animation playback to avoid simultaneous CPU skinning and UV painting work.

### Validation

- 512x512 synthetic bump-preview benchmark decreased from roughly 1238 ms in the previous path to roughly 83 ms on the same local test.
- Repeated identical normal and scalar layer stamps are bit-identical after the first application.

## 0.8.3

### Memory / responsiveness

- Reduced editable brush resolution cap from 4096 to 2048.
- Normal remains RGBA, while editable gloss and height are now stored as single-channel 8-bit maps. A 2048 material edit set is roughly 24 MiB instead of three RGBA maps.
- Undo stores only the channel changed by the stroke. In 0.8.4 its memory budget is capped at 12 MiB / 16 strokes.
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
