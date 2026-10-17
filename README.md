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
    function-map.md   functions in Sculptris.exe with their debug strings
  tools/
    sc1.js            .sc1 reader/writer, OBJ export (node tools/sc1.js)
    annotate.js       annotates dumpbin listings (strings, imports, labels)
    fileonly.js       strips undo memory-stream branches from readmesh/writemesh
    funcmap.js        generates docs/function-map.md
  _work/              disassembly and intermediate results (local only)
```

### Rebuilding the analysis environment

```bash
DUMPBIN="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x86/dumpbin.exe"
"$DUMPBIN" /DISASM:NOBYTES /SECTION:.text "F:/Grafik/Sculptris Alpha 6/Sculptris.exe" /OUT:re/_work/sculptris.asm
node re/tools/annotate.js "F:/Grafik/Sculptris Alpha 6/Sculptris.exe" re/_work/sculptris.asm re/_work/sculptris.ann.asm
node re/tools/funcmap.js re/_work/sculptris.ann.asm > re/docs/function-map.md
```

## Status / roadmap

| Area | Status | Key functions |
|---|---|---|
| `.sc1` format | done, round-trip byte-identical | `writemesh` 0x4419F0, `readmesh` 0x443912 |
| Mesh data structure (vertex/edge/triangle stores) | open | `AddTriangle` 0x40B2A0, `VertexAddTriangle` 0x40A010 |
| Dynamic tessellation (splitting edges under the brush) | open | `Tesselate` 0x40D220, `TesselateForBrush` 0x415000 |
| Reduction (collapsing edges) | open | `CollapseEdge` 0x40E5F0, `ReduceMesh` 0x4105F0 |
| Subdivision | open | `SubdivideMesh` 0x40F8C0 |
| Symmetry | open | `SetSymmetry` 0x412170 |
| Brushes (Draw, Crease, Flatten, Inflate, Pinch, Grab, Smooth) | open | brush UI 0x433700 |
| Octree / picking | open | 0x404510, 0x41BA80 |
| Paint mode (UV patches, color/bump baking) | open | "building texture mapping" |
| Matcap rendering | open | `data/*.gls` (behavior only) |
| `.sbm` format (`SBM-V12`) | open | |
