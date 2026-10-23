# Kich-Ray Material Lab

Standalone Windows / DirectX 11 material editor and OGF viewer for **S.T.A.L.K.E.R.: Shadow of Chernobyl**. Version **0.10.0**.

## Build and run

Open `KichRay_MaterialLab.sln` in **Visual Studio 2022**, choose **Release | x64**, and rebuild the solution; or run `build_vs2022.bat`. Required Visual Studio components: Desktop development with C++ and Windows SDK. Output: `bin/Release/x64/MaterialLab.exe`. Keep its `assets/Preview.hlsl` file beside the executable in the copied `assets` directory.

Dear ImGui **v1.91.9b** sources and its MIT license are included in `third_party/imgui`; no package manager or additional UI installation is required. Windows 10/11 and a DirectX 11 device are the intended runtime. x64 is recommended for large textures and skeletal models.

## Interface and navigation

The editor uses a single Dear ImGui frame: one active inspector page, a separate scene image, and a toolbar **below** the scene. Each inspector page has independent scrolling. Controls wrap or clip within their own panel, including when resizing and changing DPI. The old native child-window page system is no longer used.

Toolbar buttons have delayed Russian tooltips. Active buttons are blue; unavailable tools are disabled.

| Button | Shortcut | Action |
| --- | --- | --- |
| FC | 1 / Escape | Free flight; default tool |
| OC | 2 | Orbit camera |
| CM | 3 / C | Choose mesh; right click clears selection |
| OL | O | Selection outline |
| RL | 4 | Rotate light |
| BR | 5 / B | Brush |
| RM | 6 / R | Rotate model |
| FT | F | Frame selected mesh or visible model |
| IS | I | Isolate selection / restore previous visibility |
| WF | V | Wireframe |
| UN | Ctrl+Z | Undo stroke |
| RE | Ctrl+Y | Redo stroke |
| AP | Ctrl+S | Save editable `.krlab` material session |
| RS | Home | Reset view |

In flight mode, drag to look and use WASD/QE to move; Shift accelerates and Ctrl slows movement. Middle drag pans. In brush, light and model modes, right drag controls the camera. The wheel changes distance; Ctrl+wheel or `[ / ]` changes brush size. `X` switches brush direction. `H` hides the selected mesh; Shift+H shows all. Shortcuts do not interfere with text entry.

## Material workflow

1. Open an OGF or use the test cube. OGF texture references are resolved relative to the sibling `textures` directory; shared texture references share their material.
2. Load a diffuse image and optional normal/gloss/height inputs, then explicitly press **Создать bump**; or load an existing `_bump.dds` / `_bump#.dds` pair. The companion file is searched beside the selected file.
3. Use **Материал → Просмотр материала** to adjust the preview. Preview strength, gloss and Y orientation do not alter source data. Generation sliders only configure the next explicit generation.
4. Paint the selected material with **BR**. Switching tools or pages never regenerates its normals.
5. Save a `.krlab` session to continue editing later, or use **Экспорт** to choose a destination for the DDS pair.

An imported bump pair does not require diffuse to be exported. Generating a new base requires a real diffuse image. New generation replaces the previous base for explicitly chosen materials; save the existing session first if it is still needed.

### Brush and history

| Channel | Add | Remove |
| --- | --- | --- |
| Relief | Add normal detail from the preset | Flatten local normal slopes |
| Height | Raise surface | Lower surface |
| Gloss | Increase gloss | Decrease gloss |
| Sharpness | Sharpen normal detail | Smooth normal detail |

Each mouse press creates one stroke. Repeated dabs within that stroke use maximum coverage; a new stroke accumulates on the previous result. Zero-effect strokes do not consume undo entries. Clear is undoable. Undo/redo retains up to 64 steps / 64 MiB, always retaining at least the latest step. Editing uses native texture dimensions up to 4096×4096; the viewport uses a separate preview up to 768 pixels.

Brush hits respect visible geometry, selection and diffuse alpha. Screen-space resampling avoids drawing lines between unrelated UV islands. Focus or capture loss completes the active stroke. Presets support `.krnp` and ordinary normal-map images.

A `.krlab` session stores the original decoded bump pair, baseline layers and current edited layers with a checksum. It does not store the mesh, diffuse image, animation, or undo history. Open the corresponding model/material before loading the session. Saves use a temporary file and atomic replacement; failed loads keep the current document intact. Old `.krlt` temporary snapshots are not automatically imported. On a graphics-device failure, the application attempts to save dirty materials under `%TEMP%/MaterialLab-recovery` and reports the path.

### SoC DDS convention

`_bump`: R = encoded gloss, G = normal Z, B = normal Y, A = normal X. `_bump#`: RGB = normal correction, A = height. The SoC preview reconstructs `bump.agb + bumpX.rgb - 1`, then applies the engine's intentional `normal.z *= 0.5` boost once. Gloss is squared in the shader.

An untouched DDS pair imported during the current session is copied byte for byte on export, including its original mip chain. Edited/generated materials are encoded as BC3/DXT5 with mipmaps at native resolution. BC3 is lossy; an edited export is not expected to match original SDK bytes. After reopening a `.krlab` session, export uses its decoded data and re-encodes DDS. Both output files are staged before replacement; ordinary replacement failure rolls back the pair. Two separate files cannot provide a single crash-atomic filesystem transaction.

### Diffuse alpha

Alpha is enabled by default. **Авто** selects cutout for binary alpha and blending for fractional alpha; explicit cutout threshold and blend modes are available. Picking and outlines respect diffuse opacity. Bump alpha components retain their normal/height meanings. Opaque/cutout geometry writes depth first; translucent geometry is sorted back to front. Intersecting transparent surfaces retain the usual limitations of sorted transparency.

## Models and animation

Supported target: OGF v4 static meshes and SoC 1-link / 2-link skeletal geometry, embedded child meshes, material references, bind skeleton and individual motion preview. OMF is searched beside the OGF and one directory above it. Animation uses 30 FPS keys, quaternion interpolation and quantized translations. Leaving the Animation tab stops playback and restores bind pose; playback locks material editing.

External `OGF_CHILDREN_L` references, runtime animation mixing, gameplay IK, FX blending and the complete engine lighting pipeline are not implemented. The viewer is an inspection tool, not an exact in-game screenshot renderer.

## Validation

Portable tests cover camera/framing, brush mathematics, native-size document state and undo/redo, transactional sessions, malformed DDS/OGF/OMF inputs, and the production ImGui layout with mouse input and scrolling. They can run without Windows:

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

`tools/MaterialReferenceAudit.cpp` measures SoC-decoded normal angular error and scalar error after a BC3 round trip. The user's reference DDS files are not redistributed in this repository. See `AUDIT_0.10.0_RU.md` for the actual checks performed and remaining validation limits.

The GitHub workflow is configured to build MSVC x64/Win32, run portable regressions, and invoke the application with `--smoke-test` on Windows. That mode uses D3D11 WARP and writes `smoke-tests.txt` next to the executable. A successful cross-build does not by itself validate Windows rendering or driver behavior.

## Source references and licenses

- Dear ImGui: https://github.com/ocornut/imgui/tree/v1.91.9b — included under MIT, see `third_party/imgui/LICENSE.txt`.
- X-Ray SoC shader and format reference: https://github.com/OpenXRay/xray/tree/d7b23596a70374d8a7ffda0e98852f93ce985182/trunk — `resources/shaders/r2/sload.h`, `xrDXT/NormalMapGen.cpp`, `xr_3da/SkeletonMotions.cpp`.

The Kich-Ray engine repository is maintained separately from this standalone editor.
