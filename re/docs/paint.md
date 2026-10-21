# Paint mode

Behavior description of Sculptris Alpha 6's paint mode (color and bump
painting on a texture atlas, matcap materials), as the basis for an
independent implementation. Addresses refer to `Sculptris.exe`; the file
side is in [sc1-format.md](sc1-format.md) (texturing block).

Confidence: **[sure]** read directly from code; **[likely]** inferred from
code plus context; **[open]** not yet analyzed.

---

## 1. Overview

Entering paint mode is a one-way step ("You can't go back to sculpt after
painting"). Sculptris then cuts the mesh into **patches**, flattens each
patch by projecting it onto its own plane, packs the patches into one
texture atlas, and paints into two images: an RGBA **color map** and an i32
**bump map** (neutral 2^28). Each triangle stores its patch and three
**texture vertices** (texverts: UV plus a packed normal and the mesh
vertex). A texvert exists per patch and vertex, so a vertex on a patch
border has one texvert per adjacent patch.

`.sbm` files: Alpha 6 neither reads nor writes them (no `SBM` signature in
the executable). The one sample (`models/snok.sc1.sbm`, 39 bytes, header
`SBM-V12`) belongs to an old-format file from an earlier version and can be
ignored. **[sure]**

## 2. Building the texture mapping (`BuildTextureMapping`, 0x4275A0)

Called once from `EnterPaintMode` (main loop, "## main_logic calling
EnterPaintMode()"; preparation in 0x448DF0). The steps, in order, each
logged with a progress string **[sure for the order and the calls]**. The
inner algorithms of the atlas (steps 4-6 and 10-12) are deliberately not
reconstructed: bildhauer builds its own UV atlas and takes over the UVs
stored in .sc1 (decision 2026-10-18).

1. **clear**: every triangle gets patch id −1 and no texverts; the texvert
   array is reallocated (one slot per vertex as a start) and the patch array
   too (500 patches, stride 0x1E0, grown as needed).
2. Unhide everything (mesh-core.md 6.3) and rebuild the element sets. The
   reference position of every vertex is set to its position.
3. **preprocess**: `n = min(15, numVerts / 1000)` passes of an umbrella
   smoothing over all vertices (0x40F700; boundary vertices skipped, plane
   vertices keep x = 0). Patches are then built on this smoothed shape; the
   positions are restored from the reference positions later
   **[likely: the restore]**. Then normals are updated, and a scale is
   derived from the bounding box diagonal / 3 (0x4FD278).
4. **build patches**: every triangle without a patch seeds a new patch,
   which grows over its neighbors (0x41E870) **[open: growth criterion]**.
5. **absorb small patches** (0x41F220) **[open: threshold]**.
6. **clean up patch edges** (0x41EF50, per patch) **[open]**.
7. **force quads**: with a polygon list, the second triangle of a quad pair
   takes the patch of the first, so quads never straddle a patch border.
8. **disable empty patches**: patches without triangles are switched off.
9. **assign texture vertices** (0x41EEA0, per patch).
10. **align patches** (0x41F2B0, per patch): the projection frame
    (patch center + axes, saved in .sc1) **[open]**.
11. **arrange patches** (0x420CC0; "arrange - build polygons", "fit
    polygons", "rotate", "invert matrices"): packs the flattened patches
    into the atlas **[open]**.
12. **calculate texmap size** and **scale vertices** (0x41F9F0, factor
    0.48 / largest extent) **[open in detail]**.
13. UV deformation mode (option "UV deformation mode (use right before
    paint mode)"): restores the positions saved when the option was turned
    on **[likely]**.
14. Normals, then **BuildTangents** (0x422100).
15. **bake texels** (0x4229A0): assigns texels to triangles ("bake
    buckets"); the result gives the "Texel coverage: %i%%" log line.
16. **update colormap**: computes the bump normal map (0x424170, see 3.4)
    and uploads the images (0x424790).

**Layers and their start state** **[sure]**. The texture size comes from a
dialog (`InitTextures(w, h)`, 0x4289E0; default **[open]**). It allocates
the bump heights (all 2^28) and the color layer, which keeps an RGBA image
(`0xAABBGGRR`) plus three 16-bit channel planes (R, G, B) that the paint
code works on; the image gets the planes' high bytes. Whenever the image is
restored (undo, switching to a material mask), the planes are rebuilt as
`byte · 257` (0x41C6E0). A fresh color layer is a faint gray checkerboard
of 8 × 8 texel squares, `0x505050` / `0x575757` (alpha 0); after baking,
every texel no triangle covers is set to gray `0x6666` in the 16-bit planes.
The hardness sliders reset to 0.5.

## 3. Painting

### 3.1 Tools and settings **[sure]**

| ID | Tool (key) | Writes |
|---|---|---|
| 0 | Paint color [D] | color layer (0x4B0B14), mode 0 |
| 1 | Paint bump [B] | bump layer (0x4B0AD8), mode 1 |
| 4 | Flatten bump [F] | bump layer, mode 2 |
| – | Fill/Clear [Ctrl+F] | whole layer **[open: details]** |

Every paint tool has **Size**, **Strength** and **Hardness** sliders in
[0, 1] (hardness array 0x4BD940). Size and strength map as in
[brushes.md](brushes.md) 2.4, with the paint-mode size curve
`a(size) = 2 · size³ + 0.015`. Two colors: **primary** (0x4B08C4, initial
RGB 221, 102, 34) and **secondary** (0x4B08C8, initial RGB 34, 102, 221).

Options (flag → meaning):

| Flag | UI | Effect |
|---|---|---|
| 0x4FD13F / 0x4FD140 | Brush mask: Enable / Invert | grayscale stamp scales the amount |
| 0x4FD141 | Brush texture: Enable | the stamp's colors replace the paint color |
| 0x4FD229 / 0x4FD22A | Directional / Random | stamp rotation along the stroke / random |
| 0x4FD143 | Heightmask | amount depends on height relative to the dab area |
| 0x4FD144 / 0x4B08BA | Height from: Bumpmap / Geometry (default on) | sources for the heightmask |
| 0x4FD145 | Cavity | inverts the heightmask (paints the low parts) |
| 0x4B08DC | Heightmask depth (default 0.65) | range of the heightmask |
| 0x4B08BB | Surface angle falloff (default on) | see 3.3 |
| 0x4FD146 | Combine color and bump | color strokes also paint bump |
| 0x4FD0CE | Enable mask (paint mask) | per-texel byte mask (0x4B0C38) scales the amount |
| 0x4FD13D | Airbrush | no dab spacing: one dab per frame while the button is held |
| 0x4FD157 | Lazy | lazy mouse (also in sculpt mode) |

Flag 0x4FD142 is read by the paint code (a per-dab coverage buffer that is
filtered before it is applied, 0x4255D0/0x425410) but never set anywhere:
a disabled feature. **[sure]**

### 3.2 Dab (`PaintDab`, 0x426B90) **[sure unless marked]**

Paint strokes use the same dab path and spacing as sculpting
([brushes.md](brushes.md) 2.3; with Airbrush one dab per frame). Per dab,
with `r` = brush radius and `s` = signed strength:

```
k  = min(1, 0.6 · |s|) · sign(s)          // amount, −1 … 1
H  = hardness² · 0.95
stamp frame: center c, normal and two axes from the view, rotated by the
             stroke direction (Directional) or randomly (Random); mirrored
             dabs flip it
texels: the texels found by a spatial query with radius 1.3 · r around c
        (0x4245E0); each gets x = 2·d / (3·r), d = distance of its 3D
        position to c. The falloff would reach 1.5 r, but the query limits
        the dab to about 1.3 r  [likely: the query is not coarser]
```

- **Color (tool 0):** target color = primary, or secondary if `k ≤ 0`
  (Ctrl inverts the strength). With "Combine color and bump" the same dab
  also paints bump (mode 1) with the strength of the **Paint bump** tool:
  `s_b = s · S(bumpStrength) / S(colorStrength)` (S as in brushes.md 2.4),
  `k_b = min(1, 0.6 · |s_b|)³ · sign(s_b)`.
- **Paint bump (tool 1):** mode 1. **Flatten bump (tool 4):** mode 2.

Before the per-texel pass, the dab averages over the texels in range,
weighted by `1 − x`: the texel normals (stamp frame for the angle
falloff), the bump heights (Heightmask from Bumpmap) and the depths along
the mean normal (Heightmask from Geometry). The 64 × 64 texel tiles that
the dab touches are marked dirty and backed up for undo (0x4248E0).

### 3.3 Per texel (`PaintTexels`, 0x4257A0, multithreaded) **[sure unless marked]**

```
f = 1
if H < 1:  f = smooth((1 − x) / (1 − H))       // smooth as brushes.md 3.2;
                                               // H = 0 gives smooth(1 − x)
if paint mask enabled:  f *= mask[texel] / 255
if brush mask or brush texture:
    (u, v) = projection of the texel onto the stamp axes, scaled so the
             stamp covers ±1.5 r, + 0.5; outside [0, 1]² → skip
    depth falloff from the distance to the stamp plane  [likely]
if surface angle falloff (or a stamp):
    a = (dot(texelNormal, stampNormal) − 0.15) · 3
    a < 0 → skip;  a < 1 → f *= a
if heightmask:
    from the bump height and/or the geometric depth relative to the dab
    averages, ramped by the heightmask depth (2.5 − depth^1.5 · 2.48);
    Cavity inverts it; f *= clamp(…, 0, 1)  [exact ramps likely]
```

Writing:

- **Mode 0 (color):** the layer keeps 16-bit channels per texel. With
  `A = round(|k| · 255)` the step is
  `c += (target − c) · (A·257 · round(f · 32768) >> 15) >> 16`, i.e. the
  color moves by about `|k| · f` of the way to the target. With a brush
  texture the target is the sampled stamp color; with a brush mask the
  stamp's gray value scales the amount.
- **Mode 1 (bump):** `h += (round(f · k · 24576) · 255) >> 8`
  (≈ 24 480 · f · k per dab; negative k lowers).
- **Mode 2 (flatten bump):** `h += (round(f · k · 255) · (2^28 − h)) >> 8`,
  i.e. h moves by `f · k` toward the neutral height.

### 3.4 Bump to normals (`UpdateBumpNormals`, 0x424170) **[sure]**

For every texel of a dirty tile, from the heights `h` (W = texture width):

```
dx = h(x, y) − h(x+1, y)
dy = h(x, y) − h(x, y+1)
dz = 10 · 2^20 / W
n  = normalize(dx, dy, dz)
normal texel = (128 + 127·n.x, 128 + 127·n.y, 128 + 127·n.z)   (bytes 0, 1, 2)
```

So a height step of `10 · 2^20 / W` between neighboring texels tilts the
normal by 45°: the bump unit is tied to the texel size, not to mesh units.

## 4. Materials (matcaps)

### 4.1 Shading **[sure]**

Sculptris loads its GLSL shaders from `data/clay_*.gls` at start (ARB
shader extensions, 0x405960). Described here is what they compute, not
their code.

- **Matcap lookup:** the shading normal in view space gives the matcap
  coordinate `(n.x · 0.495 + 0.5, −(n.y · 0.495 + 0.5))` (0.497 in the
  sculpt shader). Only the normal's direction relative to the camera
  matters; there is no light.
- **Sculpt mode:** fragment = matcap(normal) × vertex color.
- **Paint mode:** the shading normal comes from the bump normal map in
  tangent space (`t = 2 · texel − 1`, basis tangent / binormal = n × t /
  vertex normal, tangents from `BuildTangents`). The normal map is sampled
  half a texel up and left of the color map (it is a forward difference,
  see 3.4). The matcap value `m` then lights the paint color `c` like a
  *hard light* layer:

  ```
  mult   = min(2m, 1)
  screen = max(2m − 1, 0)
  out    = (1 − (1 − c) · (1 − screen)) · mult
  ```

  A matcap gray of 0.5 leaves the paint color as it is; darker parts
  multiply it down, brighter parts screen it up toward white.
- **Back faces** (when shown): gray, at 10 % of the summed color channels.

### 4.2 Material slots **[sure unless marked]**

Up to 16 slots (0x5174B0, stride 0x288), saved in .sc1 from extVersion 8:

| Field | Meaning |
|---|---|
| +0x48 | matcap file name |
| +0x248 | slot in use |
| +0x249 | **Colored**: keep the matcap's colors. Off (paint mode): the matcap is converted to gray on load, `(2R + 3G + B) / 6`, so the paint color shows through |
| +0x24A | blending of the layer: set = additive (`SRC_ALPHA, ONE`), clear = normal alpha blending. UI label **[open]** |
| +0x24B | **Hide** (not saved) |
| +0x274, +0x278, +0x280 | mask size and mask bytes (texture resolution) |

Slot 0 is the base material and covers the whole model. Every further slot
in use and not hidden is a **layer**: the mesh is drawn again with that
slot's matcap, and the alpha comes from the slot's mask (texture unit 3,
read in color-map UV). So a layer shows its matcap where its mask is
painted. With **Edit** (slot > 0) the color tool paints into that slot's
mask instead of the color layer ("Edit material mask"; 0x423500 /
0x4233F0 switch the target). If the texture resolution changes, the masks
are freed.

## 5. Open points

- Fill/Clear [Ctrl+F] in detail.
- Exact stamp depth falloff and heightmask ramps (3.3).
- UI label of material flag +0x24A.
- How the dab's stamp frame is built from the view (rotation, Directional,
  Random).
- The UV atlas itself (section 2) is not reconstructed on purpose:
  bildhauer uses its own unwrapping and keeps the UVs stored in .sc1.
