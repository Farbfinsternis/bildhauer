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
    paint.md          paint mode: color/bump painting, bump normals, matcap shading, material layers
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
src/                  implementation (C++20, OpenGL)
  app/                viewer (Win32 + OpenGL; GLSL paint shading when OpenGL 2.0
                      is there, matcaps computed or read via Windows Imaging)
  core/               mesh kernel (mesh.h), brushes (brush), picking (pick),
                      edge split + brush tessellation
                      (tessellate), collapse + reduction (reduce), relaxation +
                      beautify (relax), subdivision, symmetry, UV atlas
                      (texmap), painting (paint)
  io/                 .sc1 reader/writer, mesh loading
tests/
  sc1_test.cpp        validates io + mesh kernel against real .sc1 files
  ops_test.cpp        runs every mesh operation and checks validity and topology
  brush_test.cpp      picking and all 11 brushes on real meshes
  texmap_test.cpp     UV atlas on every sample, stored UVs of painted files
  paint_test.cpp      texel baking, range query, color/bump/flatten dabs, undo,
                      material masks, tangents, saving and loading paint data
  brush_bench.cpp     times strokes phase by phase (not a test; brush_bench <file.sc1>)
```

### Building and testing

Needs CMake ≥ 3.25, Ninja and MSVC; the VS Build Tools bring all three. From
a shell where `vcvars64.bat` has run:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

### Running the viewer

```bat
build\bildhauer.exe "F:\Grafik\Sculptris Alpha 6\models\head.sc1"
```

Without a file it starts with an icosphere. Mouse: LMB sculpt (Ctrl inverts;
with Mask, Ctrl unmasks; Shift smooths), RMB or Alt+LMB orbit, MMB pan, wheel
zoom. Keys as in Sculptris: D Draw, B Smooth, G Grab, C Inflate, F Flatten,
V Pinch, M Mask, E Crease, R Rotate, T Scale, Y Reduce brush; pressing a tool
key again toggles its option (Clay, Lock plane, Global). Each tool has its own
size and strength slider: `[` `]` (or `,` `.`) change the size, with Ctrl the
strength (also `-` `=`); the brush keeps its size on screen. W wireframe,
Ctrl+D subdivide, Ctrl+Y reduce all, Ctrl+Z undo, Ctrl+S save as
`<name>_bildhauer.sc1`.

Ctrl+P enters paint mode (one way, as in Sculptris; files with paint data
open in it): LMB paints, Ctrl inverts (secondary color, lowers the bump).
D Paint color, B Paint bump, F Flatten bump; `[` `]` size, with Ctrl
strength, with Shift hardness; K picks the primary color, Shift+K the
secondary; N shows the bump normal map; Ctrl+Z undo; Ctrl+S saves with the
paint data. Shading follows paint.md 4.1: the matcap lights the paint color
like a hard-light layer, with the bump normals. Materials: L adds a layer
(up to 15), 0-9 choose what Paint color paints (0 the color layer, n the
mask of layer n), M cycles the built-in matcaps of that slot, O loads a
matcap image for it, C toggles Colored, A additive blending, H hides the
layer. Matcap images named in a painted .sc1 are loaded from where the file
says, if they exist.

`--snapshot out.png [--demo] [--paint] [--bump-normals] [--save]` renders
one frame (after a few demo strokes, in paint mode with paint strokes and a
gold layer; --save saves first, as Ctrl+S) and exits.

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
| Brushes (all 11 tools) | documented, incl. slider mapping and dab path; Rotate/Scale symmetry details open | `Brush_Dispatch` 0x41A740 |
| Picking (depth buffer in sculpt mode, ray cast + tree in paint mode) | documented; tree maintenance open | `PickDepth` 0x409200, `PickSurface` 0x408DB0 |
| Paint mode: painting (color, bump, flatten), bump normals | documented; Fill/Clear and stamp details open | `PaintDab` 0x426B90, `PaintTexels` 0x4257A0 |
| Paint mode: UV atlas | pipeline outlined only; bildhauer uses its own unwrapping (decision 2026-10-18) | `BuildTextureMapping` 0x4275A0 |
| Matcap shading, material layers | documented | `data/*.gls` (behavior only), slots 0x5174B0 |
| `.sbm` format (`SBM-V12`) | not used by Alpha 6 (legacy sidecar of old files) | |

Implementation:

| Area | Status |
|---|---|
| Mesh kernel (stores, add/remove triangle, normals, validation) | done (`src/core`), tested on all samples |
| `.sc1` read/write, mesh loading, mirroring | done (`src/io`); v8 round trip byte-identical, v0 upgrades to v8 |
| Edge split, brush tessellation, collapse, reduction, beautify, subdivision (incl. quads) | done (`src/core`); topology kept on all samples and a quad cube |
| Brushes (all 11), strokes, picking | done (`src/core`); tested on all samples |
| Viewer: matcap rendering, mirrored half, sculpting, undo, save | done (`src/app`), first version |
| Symmetry switching with plane-valence reduction | open |
| Paint mode: texture mapping (own UV atlas; stored .sc1 UVs, images and materials decoded) | done (`src/core/texmap`, `src/io`); atlas checked on all samples, packing coverage 23-50 % |
| Paint mode: painting (color, bump, flatten), bump normals, tile undo | done (`src/core/paint`); formulas checked against paint.md, stored images load unchanged |
| Paint mode: matcap shading with bump, material layers, saving paint data | done (`src/app`, `src/core/paint`, `src/io`); saved files load back identically and open in Sculptris Alpha 6 without visible differences (checked by hand 2026-10-21) |
