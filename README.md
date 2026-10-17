# bildhauer

A new implementation of a Sculptris-style sculpting program: dynamic
tessellation, symmetric sculpting, matcap rendering, paint mode. Sculptris was
bought by Pixologic and discontinued; reference is the installed Alpha 6
(`F:\Grafik\Sculptris Alpha 6`, build 2015-04-21).

## Approach (clean room)

1. **Analysis (`re/`)**: understand Sculptris' behavior, data formats and
   algorithms and **describe** them in `re/docs/`.
2. **Implementation**: written fresh from the docs. No code, shaders, images
   or matcaps from Pixologic are copied into the implementation.

The disassembly (`re/_work/`) stays local and is not committed. Decompiling to
read Sculptris' own file format is covered by interoperability
(§69e UrhG); the implementation itself is based only on the descriptions.

## Layout

```
re/
  docs/
    sc1-format.md     .sc1 file format (fully reconstructed, validated)
    mesh-core.md      mesh data structure, edge split, brush tessellation, collapse, reduction, beautify, subdivision
    symmetry.md       mirror symmetry: half-mesh model, switching on/off, plane snapping
    brushes.md        all 11 brushes, stroke/dab model, falloff, mask, picking
    function-map.md   functions in Sculptris.exe with their debug strings
  tools/
    sc1.js            .sc1 reader/writer, OBJ export (node tools/sc1.js)
    annotate.js       annotates dumpbin listings (strings, imports, labels)
    fileonly.js       strips undo memory-stream branches from readmesh/writemesh
    funcmap.js        generates docs/function-map.md
    names.json        function names, shown by annotate.js
    fn.js, clean.sh   print one function (clean.sh strips debug boilerplate)
    const.js          read float constants from the EXE
  _work/              disassembly and intermediate results (local only)
src/                  implementation (C++20, OpenGL planned for the viewer)
  core/               mesh kernel (mesh.h), symmetry (mirrorHalf)
  io/                 .sc1 reader/writer, mesh loading
tests/
  sc1_test.cpp        validates io + mesh kernel against real .sc1 files
```

### Building and testing

Needs CMake ≥ 3.25, Ninja and MSVC; the VS Build Tools bring all three. From
a shell where `vcvars64.bat` has run:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The test data is not part of the repository. By default the tests use the
`.sc1` files from `BILDHAUER_SCULPTRIS_DIR` (`data/`, `models/`) and
`re/_work/models/`. Pass other files with
`-DBILDHAUER_SC1_SAMPLES="a.sc1;b.sc1"`.

### Rebuilding the analysis environment

```bash
DUMPBIN="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x86/dumpbin.exe"
"$DUMPBIN" /DISASM:NOBYTES /SECTION:.text "F:/Grafik/Sculptris Alpha 6/Sculptris.exe" /OUT:re/_work/sculptris.asm
node re/tools/annotate.js "F:/Grafik/Sculptris Alpha 6/Sculptris.exe" re/_work/sculptris.asm re/_work/sculptris.ann.asm
node re/tools/funcmap.js re/_work/sculptris.ann.asm > re/docs/function-map.md
```

[REA](https://github.com/morluto/rea) (`npm i -g rea-agents`) is registered as
an MCP server. Without a native engine it only covers engine-free PE checks
(e.g. resources); decompiling needs Ghidra 12.1.x via `GHIDRA_INSTALL_DIR`
(check with `rea doctor`).

## Status / roadmap

| Area | Status | Key functions |
|---|---|---|
| `.sc1` format | done, round-trip byte-identical | `writemesh` 0x4419F0, `readmesh` 0x443912 |
| Mesh data structure (vertex/edge/triangle stores) | documented | `AddTriangle` 0x40B2A0, `VertexAddTriangle` 0x40A010 |
| Dynamic tessellation (splitting edges under the brush) | documented | `Tesselate` 0x40D220, `TesselateForBrush` 0x415000 |
| Reduction (collapsing edges), beautify | documented | `CollapseEdge` 0x40E5F0, `ReduceMesh` 0x4105F0 |
| Subdivision (triangles and quads, smooth/flat) | documented | `SubdivideMesh` 0x40F8C0 |
| Symmetry (half mesh, on/off, seam) | documented; brush picking on the mirror side open | `SetSymmetry` 0x412170, `Symmetry_SnapPlaneVertices` 0x411FF0 |
| Brushes (all 11 tools) | documented; slider mapping, Rotate/Scale details open | `Brush_Dispatch` 0x41A740 |
| Picking (ray cast, spatial tree) | partly documented; tree maintenance open | `PickSurface` 0x408DB0, 0x41BA80 |
| Paint mode (UV patches, color/bump baking) | open | "building texture mapping" |
| Matcap rendering | open | `data/*.gls` (behavior only) |
| `.sbm` format (`SBM-V12`) | open | |

Implementation:

| Area | Status |
|---|---|
| Mesh kernel (stores, add/remove triangle, normals, validation) | done (`src/core`), tested on all samples |
| `.sc1` read/write, mesh loading, mirroring | done (`src/io`); v8 round trip byte-identical, v0 upgrades to v8 |
| Edge split, tessellation, reduction, subdivision | next |
| Brushes, picking, OpenGL viewer | open |
