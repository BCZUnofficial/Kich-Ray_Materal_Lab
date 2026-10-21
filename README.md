# Kich-Ray Material Lab

Standalone DirectX 11 material and OGF preview tool for **S.T.A.L.K.E.R.: Shadow of Chernobyl 1.0006** and the Kich-Ray engine workstream.

Current development version: **0.8.5**.

## Features

- OGF v4 preview for static and SoC skeletal models.
- Multi-mesh / multi-material OGF loading: every embedded child mesh keeps its own texture and shader reference.
- Automatic texture lookup from the OGF path: the tool finds the `meshes` directory and uses the sibling `textures` directory one level above it.
- Automatic `_bump.dds` and `_bump#.dds` lookup for every loaded material.
- R2-inspired DX11 material preview using the SoC bump channel layout.
- Bump generation and BC3/DXT5 export with mipmaps.
- Bump generation requires a real diffuse image. A checker/fallback texture, normal map, or height map alone cannot create/export a bump pair.
- Geometry selection uses a thin outline pass, so the material and relief remain unobscured.
- Scene mesh list supports selecting and hiding individual mesh parts.
- Bind skeleton preview for skeletal OGF models.
- SoC skeletal animation preview for embedded motions and external OMF motion sets.
- Light orbit controls with persistent azimuth/elevation stored in `MaterialLab.ini` next to the executable.
- Texture-space material brush: paint relief normals, local gloss, or local height directly on the 3D model through its UVs, with save/loadable `.krnp` presets and Ctrl+Z stroke undo.
- Export to `export/<model name>/` next to `MaterialLab.exe`.

## Animation preview

The **Анимация** tab appears only when compatible motions were found.

Material Lab first uses motions embedded in the opened OGF. If the OGF contains motion references and no embedded motions, external `.omf` files are searched:

1. next to the opened model;
2. one directory above the model directory.

A same-name `.omf` is also tried when the model has no explicit motion reference.

The 0.7.1 preview implements the SoC OGF4 motion path used by `SkeletonMotions.cpp` / `SkeletonAnimated.cpp`:

- 30 FPS motion keys;
- constant or per-frame quaternion rotation keys;
- quantized translation keys;
- SLERP rotation interpolation and linear translation interpolation;
- 1-link and 2-link skeletal vertex skinning;
- play/pause, loop, timeline and playback speed controls.

Animation preview intentionally does not reproduce the complete runtime `CBlend` system. It plays one selected motion for inspection.

## Controls

- **LMB drag** — orbit camera.
- **RMB drag** — orbit light.
- **MMB drag up/down** — move camera vertically.
- **Mouse wheel** — zoom.
- **Выделение** — click mesh parts to select geometry for local material preview changes.
- **Ctrl+Z** — undo the last completed brush stroke.
- **Ctrl+S** — confirm/apply the current brush edit session.


## Mesh selection and brush

### Selection mode

**Режим выделения** is intentionally separate from normal camera control:

- hovering a visible mesh gives a light highlight;
- LMB selects it and shows the thin outline;
- RMB clears selection;
- changing tabs turns selection mode off but keeps the current selection fixed;
- while selection exists, other visible mesh parts are rendered without bump/normal shading so material work is isolated to the selected mesh;
- hidden mesh parts cannot be used as brush, bump-generation or default export targets.

When several visible meshes have diffuse textures, **Создать bump** opens a target list. One or several meshes can be checked, or **Все** can be used.


The **Сцена** tab contains a mesh list. Selecting a row selects that mesh part in the viewport; its checkbox controls visibility. Selection is rendered as an outline instead of a color tint.

The **Кисть** tab paints material data in texture space while the user paints directly on the 3D surface:

- the ray hit is converted to interpolated triangle UV coordinates;
- a surface ring shows the approximate current UV brush footprint;
- strokes are interpolated between mouse samples to avoid dotted gaps;
- the brush mode can target **Рельеф**, **Блеск**, or **Высота**; there is no pressure/opacity control;
- repeated passes with the same settings are assignment-based and do not accumulate: e.g. gloss 4% remains 4% where repainted;
- overlapping passes inside one uncommitted brush layer use a coverage mask, so they do not mix or build up; **Ctrl+S / Применить** starts a new layer;
- brush editing is disabled for materials that do not have an imported or explicitly generated bump pair;
- **Рельеф** stamps the selected normal preset directly into the editable normal layer;
- **Блеск** paints a dedicated local gloss layer, so different areas of one texture can be matte/rough or smooth/shiny;
- **Высота** paints a dedicated local height layer that is regenerated back into visible relief live;
- if the material already has `_bump/_bump#`, the existing normal, packed gloss, and bump# alpha height are reconstructed as the editable base;
- painting is shared by mesh parts that reference the same texture;
- `Ctrl+Z` undoes complete brush strokes; undo stores only touched 64x64 tiles for the channel actually changed and is capped to 24 MiB;
- **Применить** or `Ctrl+S` confirms the current brush edit session;
- preset images can be loaded from normal-map images or `.krnp`;
- `.krnp` v2 stores the reusable RGBA normal stamp plus its brush gloss value; v1 presets remain loadable.

The brush layer is created lazily. Editable layers are capped at 1024 on the longest side; normal uses RGBA while gloss and height use compact single-channel 8-bit storage. Interactive brush preview runs at up to 512 while a 1024 preview is refreshed only on **Ctrl+S / Применить**. Live brush preview skips BC3 round-trips and reuses D3D11 textures when possible; exact BC3 processing is retained for explicit generation/export.

## Bump workflow

`Создать bump` only works when an actual diffuse texture is available.

When a normal map is not supplied, Material Lab derives tangent-space normals from the diffuse/height source with a tileable 3x3 Sobel filter. Blur/Sharp now also filter the composed tangent-space normal field, so they remain visible when an imported or painted normal layer already exists. The generator now also supports **Blur** and **Sharp** shaping controls, inspired by common normal-map tools, and combines painted/imported normals with additional relief extracted from height where available. `Высота` controls generated relief and `Блеск` controls packed gloss intensity; changing these values regenerates a bump pair that was created by Material Lab. Existing imported `_bump/_bump#` files are left untouched until a new pair is generated.

For an OGF with several material slots, Material Lab generates the bump preview independently from each slot's loaded diffuse texture. If geometry is selected, generation is limited to the selected part(s).

Exported files are written as:

```text
MaterialLab.exe
export/
  <model name>/
    <texture or chosen name>_bump.dds
    <texture or chosen name>_bump#.dds
```

## Build

Requirements:

- Windows 10/11;
- Visual Studio 2022;
- Desktop development with C++ workload;
- Windows SDK;
- DirectX 11-capable GPU.

Open `KichRay_MaterialLab.sln` and build, or run:

```bat
build_vs2022.bat
```

The batch file builds **`Release | x64`** and launches the result. x64 is the recommended configuration because OGF geometry, decoded DDS data, brush layers and animation data can exceed the practical 32-bit address-space budget.

## Current limitations

- OGF v4 is the target format; the current loader is focused on Shadow of Chernobyl assets.
- Skeletal skinning currently supports SoC 1-link and 2-link vertex formats. Finite 2-link weights are clamped for preview, matching the engine's permissive loading behavior more closely.
- `OGF_CHILDREN_L` external visual references are not expanded yet.
- Animation blending, motion partitions/channels as a runtime blend system, FX mixing and gameplay IK are not reproduced.
- Full Windows/MSVC validation should be performed after changes because development tooling may also run outside a Windows SDK environment.

## Format references

Implementation is checked against the Kich-Ray / X-Ray SoC code, especially:

- `xr_3da/SkeletonX.h` / `SkeletonX.cpp`;
- `xr_3da/SkeletonCustom.cpp`;
- `xr_3da/SkeletonMotions.h` / `SkeletonMotions.cpp`;
- `xr_3da/SkeletonAnimated.cpp`;
- `resources/shaders/r2/sload.h`.

Additional format references used during development:

- https://github.com/VaIeroK/OGF-tool
- https://github.com/justtails/OGFViewer


Normal-generation behavior was also compared against:

- https://github.com/cpetry/NormalMap-Online
- https://github.com/Theverat/NormalmapGenerator
- https://github.com/iOrange/bumpx

The Kich-Ray engine repository is maintained separately from this standalone tool.
