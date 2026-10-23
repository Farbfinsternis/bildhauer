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
    After the mapping is built, the patch frames are not used again: baking
    only checks each triangle's patch id ("### invalid patch_id in
    baketexels"). They are saved with the file **[likely: nothing else reads
    them]**.
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
| – | Fill/Clear [Ctrl+F] | the current tool's layer at once, see 3.5 |

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
| 0x4FD143 | Heightmask | amount depends on height relative to the dab area (color strokes only, 3.3) |
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
texels: the texels found by a spatial query with radius 1.3 · r around c
        (0x4245E0); each gets x = 2·d / (3·r), d = distance of its 3D
        position to c. The falloff would reach 1.5 r, but the query limits
        the dab to about 1.3 r  [likely: the query is not coarser]
```

**Stamp frame.** Every dab builds an orthonormal frame `(A, B, N)` (passed
to 0x426120 and kept at 0x519D3C, 0x51748C, 0x5174A4); `N` is the stamp
normal and points toward the viewer, `A` and `B` span the stamp. Inputs
are the view direction `w` of the stroke (stroke +0x68), the screen axes
`X`, `Y` (rows of the view rotation, 0x4BDC60) and the stamp angle `θ`
(0x4FD158):

- **Fixed (Directional off):** `U = sin θ · X − cos θ · Y` (the screen
  direction at angle θ), `A = normalize(w × U)`, `B = normalize(w × A)`,
  `N = normalize(B × A)`. The stamp lies in the screen plane and turns
  with θ; θ is set by dragging in the stamp preview of the UI.
- **Directional:** a direction `D` (static 0x51A7C4, starting at (1, 0, 0))
  follows the movement of the dab center: whenever the center has moved,
  `D += 0.3 · (Δ − D)` with Δ the movement. **Random** instead takes a new
  `D` per dab with components uniform in [−0.5, 0.5]. `D` is projected into
  the screen plane (`D −= w · (w · D)`) and normalized; `B = D`,
  `A = w × B`, `N = B × A` (0x415670, shared with the sculpt brush mask).
- A dab mapped from the mirrored half flips the frame's x components.

Before the per-texel pass, the dab (0x426120) walks the texels in range
with `x ≤ 1` and `w = 1 − x`:

```
n̄  = normalize(Σ w · texelNormal)              // mean normal, kept at 0x4FD3B8
if heightmask:
    h̄ = Σ w · h / Σ w                          // only with "Height from Bumpmap";
                                               // h read as unsigned 32 bit
    ē = Σ (n̄_prev · e) / count                 // only with "Height from Geometry";
                                               // plain mean, no weights
```

`n̄_prev` is the mean normal of the **previous** dab (whatever 0x4FD3B8 held
before; zero before the first dab ever), so `ē` and the per-texel depth
`n̄ · e` (3.3) use slightly different axes. With nothing in range `h̄` and
`ē` stay 0 and the normalize (0x402690) turns the zero sum into (1, 0, 0).
The 64 × 64 texel tiles that the dab touches are marked dirty and backed
up for undo (0x4248E0).

- **Color (tool 0):** target color = primary, or secondary if `k ≤ 0`
  (Ctrl inverts the strength). With "Combine color and bump" the same dab
  also paints bump (mode 1) with the strength of the **Paint bump** tool:
  `s_b = s · S(bumpStrength) / S(colorStrength)` (S as in brushes.md 2.4),
  `k_b = min(1, 0.6 · |s_b|)³ · sign(s_b)`.
- **Paint bump (tool 1):** mode 1. **Flatten bump (tool 4):** mode 2. The
  color tool samples the color stamp (0x51722C), the bump tools the bump
  stamp (0x5171F0), see 3.6.

### 3.3 Per texel (`PaintTexels`, 0x4257A0, multithreaded) **[sure unless marked]**

With `e` = texel position − c:

```
f = 1
if H < 1:  f = smooth((1 − x) / (1 − H))       // smooth as brushes.md 3.2;
                                               // H = 0 gives smooth(1 − x)
if paint mask enabled:  f *= mask[texel] / 255
m = 1
if brush mask or brush texture enabled (a stamp is used):
    u = (e · A) / (3r) + 0.5,  v = (e · B) / (3r) + 0.5
    outside [0, 1]² → skip                      // the stamp covers ±1.5 r
    q = (0.15 − |e · N| / (3r)) / 0.05          // depth along the stamp normal
    q ≤ 0 → skip                                 // full up to 0.3 r, gone at 0.45 r
    m = clamp(q · k, −1, 1)
if surface angle falloff (or a stamp):
    a = (dot(texelNormal, N) − 0.15) · 3        // N: the stamp normal, toward the viewer
    a < 0 → skip;  a < 1 → f *= a
if heightmask and mode 0:                        // color only, see below
    D = 2.5 − depth^1.5 · 2.48                  // depth = slider 0x4B08DC
    t = 1
    if from Bumpmap:
        b = (h − h̄) / 98304                     // 98304 = 3 · 32768
        t = clamp(cavity ? −b − 0.02 : b − 0.03, 0, 1)
    if from Geometry:
        g = (n̄ · e − ē) / (3r) · 4
        g = cavity ? −g + 0.05 − (1 − D) · 0.07
                   :  g + 0.25 − (2.5 − D) · 0.12
        t *= clamp(g, 0, 1)
    f *= clamp(t · D, 0, 1)
```

The angle falloff thus favors surfaces that face the viewer; texels turned
more than about 81° away (dot < 0.15) are not painted.

**Heightmask.** The checkbox (0x4FD143) only acts on color strokes: the
branch tests the mode (0x425C10) and bump strokes (modes 1 and 2, also the
bump half of "Combine color and bump") skip it. Texels above the dab's
average (bump height `h̄`, depth `ē` along the mean normal) get paint,
those below get none; Cavity mirrors this onto the low parts, with
slightly different offsets (0.02 instead of 0.03, 0.05 instead of 0.25).
Both sources multiply; with neither source checked `t = 1` and only the
cap `min(D, 1)` is left. The depth slider (range [0, 1]) sets `D` from 2.5
(depth 0) to 0.02 (depth 1), default 0.65 gives `D ≈ 1.20`. A larger depth
thus lowers the gain `D` and, for Geometry, also the offset, so fewer
texels pass and they get less paint. Examples for the default: Geometry
alone starts at `n̄ · e − ē ≈ −0.07 r` and reaches full paint at about
`+0.55 r`; Bumpmap alone starts 0.03 · 98304 ≈ 2950 above `h̄` (about an
eighth of a full-strength bump dab) and is full at about 0.86 · 98304
above it. The `f *= 0` case still writes, but with no effect.

Writing, without a stamp:

- **Mode 0 (color):** the layer keeps 16-bit channels per texel. With
  `A = round(|k| · 255)` the step is
  `c += (target − c) · (A·257 · round(f · 32768) >> 15) >> 16`, i.e. the
  color moves by about `|k| · f` of the way to the target.
- **Mode 1 (bump):** `h += (round(f · k · 24576) · 255) >> 8`
  (≈ 24 480 · f · k per dab; negative k lowers).
- **Mode 2 (flatten bump):** `h += (round(f · k · 255) · (2^28 − h)) >> 8`,
  i.e. h moves by `f · k` toward the neutral height.

With a stamp, the stamp sample `S = (RGB, α)` at (u, v) (0x41C940:
bilinear, u and v clamped to [0, 1], pixel coordinates `u · (w − 1)`,
`v · (h − 1)`) takes the place of `k`:

- **Mode 0:** `A = α`, the step uses `|f · m|` instead of `f`; the target
  is the stamp's RGB with Brush texture on, else the paint color as above.
- **Modes 1 and 2:** `f · k` becomes `(α / 255) · m · f`. Since `m` keeps
  the sign of k, a negative k lowers (mode 1) as without a stamp. (Earlier
  versions of this document read the lower clamp as 1/128: the constant at
  0x4AD610 is loaded as a float, −1.0, not as a double.)

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

### 3.5 Fill/Clear (0x424CD0) **[sure]**

Not a tool but an action: the paint toolbar's third button ("FILL/CLEAR
[CTRL+F]", tool-name table 0x4B2380, stride 0x20) or Ctrl+F in paint mode
runs it once per press (latch 0x51A682). It works on the layer of the
current paint tool, with `s` = that tool's **raw Strength slider** (array
0x4B17F8, not the curve of brushes.md 2.4):

```
for every texel that a triangle covers (bake result != 0), that is not
hidden, and, with the paint mask on, whose mask · s is not 0:
    a = s,  or (mask / 255) · s with the paint mask on
    back up its 64 x 64 tile for undo, mark it dirty
    Paint color:  target = primary, or secondary with Invert on (0x4FD147);
                  the color step of 3.3 with A = 255 (alpha 0xFFFF) and
                  f = a:  c += (t − c) · ((65535 · round(a · 32768)) >> 15) >> 16
    bump tools:   h += (round(a · 255) · (2^28 − h)) >> 8
```

So Fill/Clear **fills** the color layer (strength 1: the primary color
everywhere) and **clears** the bump (strength 1: flat, 255/256 of the way per
press). Afterwards the bump normals are recomputed when the tool is not Paint
color, and the images are uploaded. While a material mask is edited (4.2),
the mask sits in the color layer's place, so Fill/Clear with Paint color
fills that mask.

### 3.6 Stamps: brush mask and brush texture (0x423D00) **[sure]**

Two images can be loaded from files (the samples ship in `textures/`):
a **brush texture** (0x5171B4, RGBA as loaded) and a **brush mask**
(0x517178). On loading, the mask becomes a gray value in alpha:
`g = (2R + 3G + B) / 6`, `α = ((α_file + 1) · g) >> 8`, RGB white
(0x4236A0, 0x4237E0). Whenever an option changes, 0x423D00 builds the two
stamps:

```
color stamp = brush texture (RGBA) if Brush texture is on, else white, α = 255
bump stamp  = brush texture made gray as the mask above (α = luminance · α)
              if Brush texture is on, else white, α = 255
with Brush mask on, for both stamps:
    if the stamp has fewer pixels (w + h) than the mask: resample it to the
    mask's size (bilinear)
    α = ((maskα(x / w, y / h) + 1) · α) >> 8   // mask sampled bilinearly
Invert (brush mask) turns the mask's α into 255 − α for this.
```

So the mask shapes where the stamp paints, the texture what it paints: its
colors with Paint color, its brightness as height with the bump tools.

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
painted. The layer pass uses the same hard-light mix as the base pass,
only with the slot's matcap; the mask texture is uploaded with
`alpha = min(255, mask · 33 / 32)` (0x4437E0), so a mask byte of 248 or
more is fully opaque. With **Edit** (slot > 0) the color tool paints into that slot's
mask instead of the color layer ("Edit material mask"; 0x423500 /
0x4233F0 switch the target): the mask is expanded into a gray image that
takes the place of the color layer, with its own 16-bit planes, while the
primary and secondary colors are set aside. If the texture resolution
changes, the masks are freed.

On loading, the matcap of a slot is read from the stored file name (an
absolute path into Sculptris' `materials` folder in the samples). A name
that is no file (bildhauer writes `bildhauer:clay` and the like) does not
stop the load: a file saved by bildhauer opens in Alpha 6 in paint mode and
looks the same (checked by hand, 2026-10-21). What Sculptris shows in
place of the missing matcap **[open]**.

## 5. Open points

- UI label of material flag +0x24A.
- The UV atlas itself (section 2) is not reconstructed on purpose:
  bildhauer uses its own unwrapping and keeps the UVs stored in .sc1.
