# OBJ export

Behavior of Sculptris Alpha 6 when it exports a mesh as Wavefront OBJ.
Describes behavior only; the implementation is written from this text.

Status markers: **[sure]** read from code plus context; **[as read]** read,
but looks like a slip in Sculptris; **[open]** not yet analyzed.

## 1. Entry (0x44B160, 0x447590) **[sure]**

Export picks the format by the file extension: `obj` writes OBJ (0x447590),
`goz` writes a GoZ file for ZBrush (0x449D20, not covered here); anything
else ends in "Unknown file format". The file is opened as text and starts
with:

```
# OBJ Exported from DrPetter's Sculptris
# symmetry <0|1>
usemtl (null)
s 0
```

There is no `mtllib`, no material and no texture file: paint data stays in
Sculptris. Then the mesh is written once as stored (0x446E00, pass 1) and,
with symmetry on, once more mirrored (pass 2).

## 2. Coordinates **[sure]**

Every position is written as `p' = s · p + o`, with `s` = global 0x4B0A20
and `o` = global 0x4FD3A0. Both are saved in the .sc1 file (`extraA`,
`extraVec` in [sc1-format.md](sc1-format.md)):

- A new scene sets `s = 0.01`, `o = (0, 0, 0)`.
- **OBJ import** (0x447A10) moves the imported mesh so
  that its bounding box is centered at the origin and scales it so that half
  the box diagonal `h` becomes 150 units; it keeps `o` = the box center and
  `s = h / 150`.

So an imported OBJ exports at its original size and place, and native
Sculptris models come out at 1/100 of Sculptris' internal units.

## 3. Pass 1: the stored mesh (0x446E00) **[sure]**

- **Vertices:** every active vertex in slot order, `v %.6f %.6f %.6f`. Its
  OBJ index (from 1) is its position in this order.
- **Normals** only with the option "Export vertex normals in OBJ" (global
  0x4FD097, off unless the user turns it on; kept in the settings file): one
  `vn %.6f %.6f %.6f` per active vertex, the vertex normal, not transformed.
- **Texture coordinates** only in paint mode: every texvert in order,
  `vt %.6f %.6f` with u and v as stored (no flip).
- **Faces:** every active triangle in slot order, corners in stored order
  (`v0 v1 v2`), as `f a b c`, `f a//a b//b c//c` (normals: the vertex index
  doubles as normal index), `f a/ta b/tb c/tc` (paint mode) or `f a/ta/a …`
  (both). Hidden triangles are written too.
- **Quads:** with a polygon list (0x4FD0CC), a triangle marked as the first
  of a quad is written together with the next slot as one four-corner face,
  in the same four variants; the partner slot is skipped. The exact corner
  order is **[open]**; any order around the quad that keeps the triangles'
  winding describes the same face.

## 4. Pass 2: the mirrored half (symmetry on) **[sure unless marked]**

- **Vertices:** the active vertices **not on the symmetry plane**, x negated
  before the transform (`p' = s · (−x, y, z) + o`), get new indices following
  pass 1. Vertices on the plane keep their pass-1 index, so both halves share
  them and the surface is closed.
- **Normals** (with the option): written for **every** active vertex, x
  negated, including those on the plane, although only the off-plane
  vertices got new positions. The normal indices of the mirrored half
  therefore do not line up with the faces' vertex indices **[as read]**.
- **Texture coordinates** are not written again; mirrored faces use the same
  texverts as their originals, so both halves share the texture.
- **Faces:** every triangle again with mirrored indices and reversed winding
  (`v2 v1 v0`), quads likewise.

## 5. Notes for the implementation

- bildhauer writes normals for exactly the vertices it writes, so `v` and
  `vn` indices always match (fixing 4.).
- Faces reference only vertices of their own pass or shared plane vertices;
  an importer gets one closed mesh with the full model.
