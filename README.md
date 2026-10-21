# Kich-Ray Material Lab

Standalone DirectX 11 material and OGF preview tool for **S.T.A.L.K.E.R.: Shadow of Chernobyl 1.0006** and the Kich-Ray engine workstream.

Current development version: **0.7.1**.

## Features

- OGF v4 preview for static and SoC skeletal models.
- Multi-mesh / multi-material OGF loading: every embedded child mesh keeps its own texture and shader reference.
- Automatic texture lookup from the OGF path: the tool finds the `meshes` directory and uses the sibling `textures` directory one level above it.
- Automatic `_bump.dds` and `_bump#.dds` lookup for every loaded material.
- R2-inspired DX11 material preview using the SoC bump channel layout.
- Bump generation and BC3/DXT5 export with mipmaps.
- Bump generation requires a real diffuse image. A checker/fallback texture, normal map, or height map alone cannot create/export a bump pair.
- Geometry selection for applying normal/bump preview settings to individual mesh parts, including mirrored-part selection.
- Bind skeleton preview for skeletal OGF models.
- SoC skeletal animation preview for embedded motions and external OMF motion sets.
- Light orbit controls with persistent azimuth/elevation stored in `MaterialLab.ini` next to the executable.
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

## Bump workflow

`Создать bump` only works when an actual diffuse texture is available.

When a normal map is not supplied, Material Lab derives tangent-space normals from the diffuse/height source with a tileable 3x3 Sobel filter. `Высота` controls the generated relief and `Блеск` controls the packed gloss intensity; changing either value regenerates a bump pair that was created by Material Lab. Existing imported `_bump/_bump#` files are left untouched until a new pair is generated.

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

The batch file builds `Release | Win32` and launches the result. The solution also contains x64 configurations for development/testing.

## Current limitations

- OGF v4 is the target format; the current loader is focused on Shadow of Chernobyl assets.
- Skeletal skinning currently supports SoC 1-link and 2-link vertex formats.
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
