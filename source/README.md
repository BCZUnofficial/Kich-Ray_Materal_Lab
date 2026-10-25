# Kich-Ray Material Lab

Standalone Windows / DirectX 11 material editor and OGF viewer for **S.T.A.L.K.E.R.: Shadow of Chernobyl**. Version **0.12.0**.

See [AUDIT_0.11.0_RU.md](AUDIT_0.11.0_RU.md) for the Russian audit, engine references and actual validation limits.
The startup and surface-stamp update is described in [UPDATE_0.12.0_RU.md](UPDATE_0.12.0_RU.md).

## Build and run

Open `KichRay_MaterialLab.sln` in **Visual Studio 2022**, choose **Release | x64**, and rebuild the solution; or run `build_vs2022.bat`. Required Visual Studio components: Desktop development with C++ and Windows SDK. Output: `bin/Release/x64/MaterialLab.exe`. Keep its `assets/Preview.hlsl` file beside the executable in the copied `assets` directory.

The transparent ML emblem is embedded in the EXE by `assets/MaterialLab.rc` in every Debug/Release and Win32/x64 configuration. The window uses the same resource for its large and small icons. The multi-resolution `assets/MaterialLab.ico` and editable PNG source are included; no external ICO is required at runtime.

On startup, the supplied `assets/StartupSplash.png` emblem fades in on a transparent window while the editor initializes D3D, shaders, fonts and its first viewport frames. A separate Win32 thread owns only the splash; the editor and its D3D context remain on the main thread. The 0.9-second fade is followed by a short hold and a 0.25-second fade-out once initialization finishes. The PNG is embedded in the EXE; no external splash file is required. Splash failure falls back to starting the editor normally.

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
| AP | Ctrl+S | Apply pending slider adjustments as one undo step |
| RS | Home | Reset view |
| Z / X / Y | Mouse | Frame selection/model along the chosen axis |

In flight mode, drag to look and use WASD/QE to move; Shift accelerates and Ctrl slows movement. Middle drag pans. In brush, light and model modes, right drag controls the camera. The wheel changes distance outside Brush mode. In Brush mode, the wheel over the model changes brush size. Hover any slider and turn the wheel to change its value; arrow keys and numeric keyboard entry do not adjust sliders. Choose brush direction with the Add/Remove controls. `H` hides the selected mesh; Shift+H shows all. Shortcuts do not interfere with text entry.

## Material workflow

1. Open an OGF or use the test cube. OGF texture references are resolved relative to the sibling `textures` directory; shared texture references share their material.
2. Load a diffuse image: if no bump exists, a gentle baseline is created automatically. An OGF material without bump creates this baseline on its first slider edit or brush activation. Optional normal/gloss/height inputs can be included by explicitly pressing **Создать bump**. Existing `_bump.dds` / `_bump#.dds` pairs load without regeneration.
3. **Материал → Правка текущих карт** edits current normal, gloss and height layers live, relative to their committed snapshot. **Ctrl+S / Применить** commits one undo step; **Отказаться** restores all channels including previous brush work. Changing tabs, material, tool or window asks Apply / Discard / Cancel if a draft exists. Ctrl+S without a pending draft saves the session.
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

Each mouse press creates one stroke. Repeated dabs within that stroke use maximum coverage; a new stroke accumulates on the previous result. Zero-effect strokes do not consume undo entries. Clear is undoable. Undo/redo retains up to 64 steps / 64 MiB, always retaining at least the latest step. Editing uses native texture dimensions up to 4096×4096; dragging uses a separate fast preview up to 768 pixels. After 250 ms without a drag, the viewport switches to decoded native-resolution BC3 data and the exact mip chain also used by export. The material information identifies which preview is active. Encoding large textures can briefly pause the UI.

Brush hits respect visible geometry, selection and diffuse alpha. Screen-space resampling avoids drawing lines between unrelated UV islands. Focus or capture loss completes an active freehand stroke. The **Кисть** subtab retains Add/Remove channels and file presets (`.krnp` and ordinary normal-map images).

The separate **Нанесение рельефа поверхности** subtab offers glass, fabric, metal, concrete and leather profiles. Selecting a profile activates the brush. Each left click applies exactly one normal-only stamp with one undo step; holding or moving the mouse does not paint. These profiles extract fine, medium and broad detail from the diffuse at the hit UV instead of inventing procedural fabric or leather. Detail stays aligned to the base texture when the brush radius changes. Existing normal detail is preserved and augmented; gloss and height remain untouched. A diffuse source is required, and a uniform or fully transparent area is a no-op with an explanatory status message. The profiles are artistic interpretations of diffuse contrast, not recovered physical geometry; transparency and the engine shader remain separate.

A `.krlab` session stores the original decoded bump pair, baseline layers and current edited layers with a checksum. It does not store the mesh, diffuse image, animation, or undo history. Open the corresponding model/material before loading the session. Saves use a temporary file and atomic replacement; failed loads keep the current document intact. Old `.krlt` temporary snapshots are not automatically imported. On a graphics-device failure, the application attempts to save dirty materials under `%TEMP%/MaterialLab-recovery` and reports the path.

### SoC DDS convention

`_bump`: R = encoded gloss, G = normal Z, B = normal Y, A = normal X. `_bump#`: RGB = normal correction, A = height. The SoC preview reconstructs `bump.abg + bumpX.rgb - 1`, then applies the engine's intentional `normal.z *= 0.5` boost once. Gloss is squared in the shader.

With Gloss enabled, an untouched DDS pair imported during the current session is copied byte for byte on export, including its original mip chain. Gloss is supported in the red channel of `_bump.dds`; disabling it writes zero gloss at every mip without deleting the working gloss layer. Edited/generated materials are encoded as BC3/DXT5 with mipmaps at native resolution. BC3 is lossy; an edited export is not expected to match original SDK bytes. After reopening a `.krlab` session, export uses its decoded data and re-encodes DDS. Both output files are staged before replacement; ordinary replacement failure rolls back the pair. Two separate files cannot provide a single crash-atomic filesystem transaction.

### Engine lighting and paths

Direct light uses the pinned KichRay/SoC R2 material lookup table and `u_diffuse2s` intensity formula, with no minimum fake specular contribution. Light color, intensity and viewport background are editable and remembered in `MaterialLab.ini`. The directional source has no distance attenuation, as in R2 `plight_infinity`.

The diffuse `.thm`, when available beside its texture, supplies the material class plus blend weight and optional custom bump filename. A Scene override is available for inspection; the editor does not rewrite THM. A THM with bump disabled produces a warning: exporting a pair alone does not activate it in the engine. `models\selflight` and `models\selflightl` receive the source shader's flat-normal emission. Other shader variants, `textures.ltx` overrides, level environment maps, shadows, parallax, detail textures, exposure and bloom are not reproduced. Gloss is a highlight mask, not an emission map.

OGF selection remembers its folder. Export starts in the model's resolved texture-reference folder, then falls back to the last successful export folder, then the OGF folder. Automatic naming uses the model texture name even when a different diffuse was loaded manually. A custom bump name in THM still needs to match the selected export name.

### Diffuse alpha

Alpha is enabled by default. **Авто** selects cutout for binary alpha and blending for fractional alpha; explicit cutout threshold and blend modes are available. Picking and outlines respect diffuse opacity. Bump alpha components retain their normal/height meanings. Opaque/cutout geometry writes depth first; translucent geometry is sorted back to front. Intersecting transparent surfaces retain the usual limitations of sorted transparency.

## Models and animation

Supported target: OGF v4 static meshes and SoC 1-link / 2-link skeletal geometry, embedded child meshes, material references, bind skeleton and individual motion preview. OMF is searched beside the OGF and one directory above it. Animation uses 30 FPS keys, quaternion interpolation and quantized translations. Clips are sorted alphabetically (including Cyrillic case folding), retain their original engine index, scroll, and reopen around the last selected clip. Selection is remembered per model across restarts. Leaving the Animation tab stops playback and restores bind pose; playback locks material editing.

External `OGF_CHILDREN_L` references, runtime animation mixing, gameplay IK, FX blending and the complete engine lighting pipeline are not implemented. The viewer is an inspection tool, not an exact in-game screenshot renderer.

## Validation

Portable tests cover minimal baselines, live slider apply/discard, export/preview mip parity, gloss switching, five texture-derived surface profiles, radius-independent UV alignment, one-click history and alpha, pinned R2 lookup values, THM metadata, animation/path selection, camera/framing, brush mathematics, native-size document state and undo/redo, transactional sessions, malformed DDS/OGF/OMF inputs, and the production ImGui layout with mouse input and scrolling. They can run without Windows:

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

`tools/MaterialReferenceAudit.cpp` measures SoC-decoded normal angular error and scalar error after a BC3 round trip. The user's reference DDS files are not redistributed in this repository. See `AUDIT_0.11.0_RU.md` for the actual checks performed and remaining validation limits.

The GitHub workflow packages verified x64/Win32 executables and their shader under Actions artifacts after successful tests. It is configured to build MSVC x64/Win32, run portable regressions, and invoke the application with `--smoke-test` on Windows. That mode uses D3D11 WARP and writes `smoke-tests.txt` next to the executable. A successful cross-build does not by itself validate Windows rendering or driver behavior.

## Source references and licenses

- Dear ImGui: https://github.com/ocornut/imgui/tree/v1.91.9b — included under MIT, see `third_party/imgui/LICENSE.txt`.
- X-Ray SoC shader and format reference: https://github.com/OpenXRay/xray/tree/d7b23596a70374d8a7ffda0e98852f93ce985182/trunk — `resources/shaders/r2/sload.h`, `xrDXT/NormalMapGen.cpp`, `xr_3da/SkeletonMotions.cpp`.

The Kich-Ray engine repository is maintained separately from this standalone editor. This audit uses `BCZUnofficial/kichray` commit `01b239eec4f3b612089a198f13ebfea88ef981ca`; exact source links and preset formulas are in `AUDIT_0.11.0_RU.md`.
