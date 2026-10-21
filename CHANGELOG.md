# Changelog

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
