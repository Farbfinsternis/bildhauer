# Sculptris `.sc1` file format

Reconstructed from `Sculptris.exe` (Alpha 6, build 2015-04-21, MSVC 2008, x86):
`writemesh` at `0x4419F0`, `readmesh` at `0x443912`, `WriteBufferRLE` at
`0x4416E0`, `ReadBufferRLE` at `0x4418B0`. Reference implementation:
[`tools/sc1.js`](../tools/sc1.js).

**Validation:** all four v8 sample files (`default_sphere`, `default_plane`,
`figure`, `head`) and a painted test file (default sphere with color paint
and one matcap, `re/_work/models/test01.sc1`, local only) are read to the
last byte and written back byte-identical. bildhauer's own C++ writer
(`encodeTexturing`, written from this description) re-encodes the painted
sample's texturing block byte for byte as well. That covers the texturing block
and the RLE encoder.
The v0 file `snok.sc1` is read completely. After mirroring, every mesh is a
closed, consistently oriented 2-manifold (each edge has exactly 2 triangles,
Euler characteristic 2).

The same routines also serialize into a memory stream for undo
(`quick update (undo for unchanged topology)`), so the file layout is the undo
snapshot layout.

All values are little-endian. `i32` = int32, `u8`/`u16` = unsigned,
`f32` = IEEE float.

## Versions

The file version is the last character of the 8-byte magic `SC1Fv00N`
(Alpha 6 writes `SC1Fv008`).

| Condition | Treatment |
|---|---|
| no `SC1Fv` magic | "invalid file, but assuming old version": version 0, old format |
| version < 6 | "old format": seek back to offset 0 and read without magic |
| version > 8 | "Unsupported file version" |

Independently, the extension block after the triangles has its own version
number (`extVersion`, 8 in Alpha 6). Several fields depend on it.

## Layout

```
[magic]       char[8]  "SC1Fv008"                  (absent in old format)
numVerts      i32      active vertices only
numTris       i32      active triangles only
symmetry      u8       != 0: only the half x >= 0 is stored

vertex[numVerts]:
  pos         f32[3]   vert+0x1C
  weight      u8       vert+0x18 (float) * 255, rounded   — old format: f32
  flags       u8       vert+0x06; bit0 = vertex lies on the symmetry plane (x == 0)

triangle[numTris]:
  v           i32[3]   vertex indices (position in the vertex list above)
  flag        u8       tri+0x04 (only version >= 7): triangle hidden; if set,
                       the three vertices get +0x07 = 1 (hidden) on load

-- end of file here => old file, no extensions, no paint data --

extVersion    i32      8
if extVersion >= 3:
  extraA      f32      global 0x4B0A20: OBJ export scale s (obj.md 2; new scene 0.01)
  extraVec    f32[3]   global 0x4FD3A0: OBJ export offset o (obj.md 2; new scene 0)
if extVersion == 6:
  (u8)                 read and ignored
if extVersion >= 7:
  hasPolyList u8       global 0x4FD0CC
  if hasPolyList:
    count     i32
    polyList  u8[count] one byte per triangle; pairs triangles into quads
                        (see "polygon list" / "Incoherent quad structure")
hasTexturing  u8       global 0x4B14DD (paint mode data present)
if hasTexturing: texturing block (below)
```

### Symmetry

With `symmetry != 0` the file holds one half of the mesh. Every vertex with
`flags & 1` lies exactly on x = 0 and is shared. To build the full mesh,
mirror all other vertices (`x -> -x`) and add each triangle again with the
mirrored indices and reversed winding.

### Texturing block (paint mode)

Verified against a painted file (26 patches, 721 texverts, 512x512 images,
1 material).

```
numPatches    i32      global 0x4BFF58, patch stride 0x1E0
patch[numPatches]:
  uvOrigin    f32[3]   patch+0x08: (u, v, 0) of the patch in the texture
  center      f32[3]   patch+0x28: 3D position (in mesh units)
  axes        f32[3][3] patch+0x34, +0x40, +0x4C: projection frame
                       (rows of a matrix; "arrange - invert matrices")

numTexverts   i32      global 0x4B8D58, texvert stride 0x28
texvert[numTexverts]:
  extVersion >= 6:
    u, v      f32, f32 texvert+0x04, +0x08 (+0x0C = 0)
    normal    i32      packed vec3 at texvert+0x10 (see below)
    vertex    i32      index of the mesh vertex
  extVersion < 6:
    uvw       f32[3]   texvert+0x04
    vec       f32[3]   texvert+0x10, unpacked
    vertex    i32

per triangle (numTris):
  texverts    i32[3]   texvert indices
  patch       u16      tri+0x48 (extVersion < 6: i32)

color image:  w i32, h i32, then w*h x 32-bit pixels     buffer 0x4FD120
              bytes R, G, B, A (uploaded as GL_RGBA/GL_UNSIGNED_BYTE);
              as a little-endian i32: 0xAABBGGRR. Unpainted = 0xff666666
bump image:   w i32, h i32, then w*h x i32 heights       buffer 0x4B0AE8
              neutral (flat) = 0x10000000 (2^28); higher/lower values raise or
              lower the surface. "Flatten bump" blends toward it per pixel:
              v += ((0x10000000 - v) * a) >> 8, with a in 0..256
              extVersion >= 4: pixels RLE-coded (elemSize 4)
              extVersion <  4: raw i32[w*h]

if extVersion >= 8:
  numMaterials i32     used slots out of 16 (slot table 0x5174B0, stride 0x288)
  material[numMaterials]:
    slot      i32      0..15
    filename  char[256] absolute path of the matcap image (slot+0x48),
                       e.g. "F:\Grafik\Sculptris Alpha 6\materials/JG_Drink01.png"
    flags     u8[3]    slot+0x249, +0x24A, +0x24B
    w, h      i32, i32 slot+0x274, +0x278
    mask      w*h bytes, RLE-coded (elemSize 1)    slot+0x280
              (with one material all 255: the material covers everything)
```

Packed vector (`0x402A80` / `0x402B20`): each component `c` in [-1, 1] is
stored as `round(c * 125 + 128)` in one byte, packed as
`(x << 16) | (y << 8) | z`.

### RLE (`WriteBufferRLE` / `ReadBufferRLE`)

Literal and run segments alternate, always starting with a literal. Every
segment begins with a `u8` count (0..255):

- **Literal:** count, then `count` elements of 1 or 4 bytes.
- **Run:** count only; repeats the last literal element `count` times.

A count of 0 is legal (e.g. after a full 255-element literal). The encoder
ends a literal before an element that equals its predecessor; that element
opens the run. At the end of the buffer the element stays in the literal and
no run follows. `Writer.rle` in `tools/sc1.js` is a direct port of the
original state machine.

## In-memory structures (as far as visible from I/O)

| Structure | Global array / count | Stride | Known fields |
|---|---|---|---|
| vertex | `0x4FD088` / `0x4FD054` | 0x140 (320) | +0x00 active, +0x06 flags, +0x07 hidden, +0x0C file index (temp), +0x18 weight f32, +0x1C pos, +0xC4 state |
| triangle | `0x4FD090` / `0x4FD05C` | 0x64 (100) | +0x00 active, +0x04 hidden, +0x08 vertex* [3], +0x48 patch id, +0x4C texvert* [3] |
| edge | count `0x4FD058` | ? | allocated as 2x vertex count on load |
| texvert | `0x4FD12C` / `0x4B8D58` | 0x28 | +0x00 index, +0x04 u, +0x08 v, +0x10 vec3, +0x1C vertex* |
| patch | `0x4FD10C` / `0x4BFF58` | 0x1E0 | +0x08 uv origin, +0x28 center, +0x34..+0x57 axes |
| material slot | `0x5174B0`, 16 entries | 0x288 | +0x48 name[256], +0x248 used, +0x274 w, +0x278 h, +0x280 mask* |

Limits from the log strings: at most 24 triangles/edges per vertex
("Too many connections to a vertex (max 24)"), at most 2 triangles per edge.

## Open questions

- Meaning of `weight` in the old format (f32; 0.0 in the v0 sample). Taken
  literally it would mask the whole mesh, so the bildhauer loader uses 1.0 for
  files older than version 6. (`extraA` and `extraVec` are the OBJ export
  transform, see obj.md 2.)
- The polygon-list byte values (the reader treats 2 specially). The
  triangle flag is the hidden state (see mesh-core.md 1).
- Bump scale (answered in paint.md 3.4): heights are relative to the texel
  size. A difference of 10 · 2^20 / W between neighboring texels means a
  45° slope.
- Meaning of the three material flag bytes (all 0 in the sample) and the gap
  patch+0x14..+0x27.
- How the axes and the patch center map 3D points into the patch's UV area.
