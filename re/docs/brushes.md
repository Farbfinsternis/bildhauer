# Brushes

Behavior description of Sculptris Alpha 6's sculpting brushes, as the basis
for an independent implementation. Addresses refer to `Sculptris.exe`; vertex
fields as in [mesh-core.md](mesh-core.md), symmetry as in
[symmetry.md](symmetry.md).

Confidence: **[sure]** read directly from code; **[likely]** inferred from
code plus context; **[open]** not yet analyzed.

Notation: `p` vertex position (+0x1C), `ref` reference position (+0x28),
`n` vertex normal (+0x34), `w` vertex mask weight (+0x18, 1 = editable),
`c` dab center, `r` brush radius, `s` signed strength, `d = |p − c|`.

---

## 1. Tools and dispatch **[sure]**

The current tool is a global index (0x4FD130). The dispatcher
(`Brush_Dispatch`, 0x41A740) calls one function per dab:

| ID | Tool (UI key) | Function |
|---|---|---|
| 0 | Draw [D] | 0x417CA0 |
| 1 | Smooth [B] | 0x415D30 |
| 2 | Grab [G] | 0x4188B0 |
| 3 | Inflate [C] | 0x4160F0 |
| 4 | Flatten [F] | 0x4164D0 |
| 5 | Pinch [V] | 0x416D40 |
| 6 | Mask [M] | 0x4186D0 |
| 7 | Crease [E] | 0x4180C0 |
| 8 | Rotate [R] | 0x419AC0 |
| 9 | Scale [T] | 0x419020 |
| 10 | Reduce brush [Y] | 0x41A480 |

After each dab: if the global detail factor G (0x4B08B0) is ≥ 0.1, or the
tool is Reduce, the polygon list is deleted. **Sculpting with detail on
turns quads into triangles.**

Per-tool option toggles (UI labels; the same key toggles the option of the
current tool):

| Option | Flag | Tools |
|---|---|---|
| Clay [D] | 0x4FD153 | Draw |
| Soft [S] | 0x4FD154 | Draw (with Clay) |
| Lock plane [F] | 0x4FD151 | Flatten |
| Angle falloff [S] | 0x4FD152 | Flatten |
| Global [G/R/T] | 0x4FD150 | Grab, Rotate, Scale |
| Limit [S] | 0x4FD155 | Grab, Rotate, Scale |
| XYZ [S] | 0x4B08E4 | Scale |
| Brush mask, Enable | 0x4FD13F | all with a stencil (see 3.4) |

Global settings: respect the mask (0x4B08EF, default on), "Compensate
sculpting at symmetry line" (0x4B08E6, default on).

## 2. Stroke **[sure unless marked]**

### 2.1 Stroke record (0x51A048)

| Offset | Field |
|---|---|
| +0x00 | dab center `c` (x is mirrored to ≥ 0 with symmetry) |
| +0x0C | grab anchor (stroke start / previous dab) |
| +0x18 | copy of the center (region test) |
| +0x24 | center of the current work region |
| +0x30 | last stroke sample `B`; +0x3C the sample before it, `C` (see 2.3) |
| +0x60 | work region radius; +0x64 region valid |
| +0x68 | view-dependent direction used by Flatten weighting **[likely: view direction]** |
| +0x88 | view depth of the picked point (see 2.4 and 5) |
| +0x8C | dab was mirrored to the +x side |
| +0x90, +0x94 | mouse position; +0x9C, +0xA0 mouse at stroke start |
| +0xA8 | first dab of the stroke |
| +0xAC | radius `r` (world units) |
| +0xB0 | detail size (tessellation threshold, region size) |
| +0xB4 | strength `s` (signed; negative = inverted) |
| +0xB8 | dab spacing |

How +0xAC, +0xB0, +0xB4 and +0xB8 follow from the sliders, the view and the
pen is described in 2.4.

### 2.2 Stroke start

When a stroke starts: save an undo state, then **store the reference
state**: `ref = p`, reference normal = `n` for all work-set vertices
(`Job_StoreReference`, 0x40CE10). Several brushes (Draw, Inflate, Crease,
Grab) work relative to this state, so a stroke cannot build up beyond a
limit however often it passes over the same spot.

### 2.3 Dab path and spacing (in main_logic, around 0x454BD3) **[sure]**

The stroke keeps the last two pointer samples on the surface: `B` (+0x30)
and the one before, `C` (+0x3C). At stroke start both are the start point,
and the first dab is forced. Each frame with a new picked point `A` (x
negated when the pick hit the mirrored half):

**Path.** Dabs follow a curve that cuts the corner at `B`
(`DabCurve`, 0x44DD60, evaluated by the cubic Bézier helper 0x402C90):

```
k0 = C + 0.5  · (B − C)        // midpoint of C–B: curve start
k1 = C + 0.85 · (B − C)
k2 = A + 0.85 · (B − A)
k3 = A + 0.5  · (B − A)        // midpoint of B–A: curve end
q(t) = (1−t)³·k0 + 3t(1−t)²·k1 + 3t²(1−t)·k2 + t³·k3
```

The curve runs from the middle of the previous segment to the middle of the
current one, pulled toward `B`. With handles at 0.85 it is almost the
quadratic B-spline of the samples (exactly that at 5/6 ≈ 0.833); the stroke
therefore lags half a segment behind the pointer and never has corners.

**Spacing.**

```
n = floor(|A − C| · 10 / spacing)            // spacing = +0xB8
last = C
for i = 0..n:
    q = q(i/n)                                // i/n = 0 for n = 0
    if |q − last| > spacing or first dab of the stroke:
        last = q
        c = q; if symmetry on and c.x < 0: c.x = −c.x, set +0x8C
        Brush_Dispatch(tool, stroke)
if a dab was placed: C = B
B = A
```

`last` restarts at `C` every frame. `C` only advances in frames that placed
a dab, so slow pointer movement accumulates until the path is long enough.
With symmetry on, every dab is mapped to the stored +x half.

**Exceptions.** Grab, Rotate and Scale skip the path: they dispatch once per
frame at the picked point (see 4.9 and 4.10). With Shift held, the dab path
is used with the Smooth tool, whatever tool is selected.

### 2.4 Size, strength and spacing (main_logic, around 0x450A6A) **[sure unless marked]**

Each tool has its own **Size** and **Strength** slider, both in [0, 1]
(arrays 0x4B1E60 and 0x4B17F8, indexed by the tool ID). The values are
recomputed every frame while the pointer is over the viewport, so the radius
follows the depth under the cursor during a stroke.

**Radius.** Proportional to the view depth `z` of the point under the
cursor (stroke +0x88, eye-space distance along the view axis, see 5), so the
brush keeps its size on screen when zooming:

```
r = z · 0.12 · a(size) · (fov + 0.5)^1.45
a(size) = 1.7 · size^1.9 + 0.03           sculpt mode
        = 2 · size^3 + 0.015              paint mode
```

`fov` is the FOV slider (0x4B0900, [0, 1], default 0.5); the camera angle
is `15° + 40° · fov` (default 35°). The factor `(fov + 0.5)^1.45` (1 at the
default) roughly compensates the wider view, so a slider value gives a
similar screen size at every FOV. Example: size 0.3, default FOV, depth z →
`r ≈ 0.024 z`, about 8 % of half the viewport height.

**Strength.**

```
s = 1.15 · (strength + 0.3)² + 0.03        // 0.13 … 1.97
s = −s   if the invert toggle (0x4FD147) is on
```

With Shift held (temporary Smooth), the strength comes from the **Smooth**
slider, while the size stays that of the selected tool.

**Pen pressure.** With a tablet, the pressure `p` ∈ [0, 1] is shaped by the
"Pressure sensitivity" slider `σ` (0x4B08E0, default 0.35):

```
P = p^(1 − (σ − 0.5) · 0.8)                // σ = 0.35 → p^1.12
```

At the start of a stroke and on each frame of it (the stroke being active):

```
detailSize = r                              // +0xB0, before size pressure
if "Strength pressure" (0x4B08B9, default on):  s = s · P
if "Size pressure"     (0x4FD13C, default off): r = r · (0.98 · P + 0.02)
                                             (not for Grab, Rotate, Scale)
```

Without a tablet `P = 1`. Before the pressure is applied, `s` can be
negated once more for the stroke; the conditions involve Ctrl and a pen
button flag, which also negates `P`. How these combine is **[open]**; for
the implementation, "Ctrl held = inverted" is enough.

So the **detail size equals the radius** (before size pressure): with the
tessellation threshold of [mesh-core.md](mesh-core.md) section 3, triangles
under the brush are split until `3 · longest edge ≤ r / (2 · G)`, i.e.
longest edge ≤ `r / 6.6` at the default `G = 1.1`. Finer detail needs a
smaller brush or a higher detail factor.

**Spacing.** `spacing = f · r` with the "Brush spacing" factor `f`
(0x4B08D0, default 0.25). Its slider `u` maps as `f = 4.95 · u² + 0.05`
(0.05 … 5).

**Slider defaults** (from the reset routine 0x44B7C0, before any saved
settings are loaded **[likely: a saved settings-sculpt.dat overrides
them]**): size 0.3 for every tool except Smooth (0.2); strength 1.0 for
every tool except Smooth and Flatten (0.4).

**Keyboard.** `]`/`.` and `[`/`,` change the current tool's size by ±0.1;
with Ctrl held they change its strength. Values are clamped to [0, 1].
"Unify tool settings" (0x4FD15C) copies the current tool's size and strength
to all tools.

## 3. Shared building blocks

### 3.1 Work region (`Brush_UpdateRegion`, 0x414D00) **[sure]**

Brushes only touch vertices in the work set. The region is a box around the
center with half-size `R = 2.5 · detailSize` (+0xB0). It is rebuilt only
when the brush sphere leaves it:

```
if not valid or |c − regionCenter| + detailSize > R:
    regionCenter = c
    mark visible vertices inside the box (vertex +0x05)
    mark triangles with a vertex inside (triangle +0x07)
    rebuild the work sets
```

In "global" mode (0x4FD15D), every vertex with `w ≠ 0` is in the region.
Built multithreaded with `mt_draw`.

### 3.2 Falloff curves **[sure]**

```
smooth(x) = 0                    for x ≤ 0
          = 1                    for x ≥ 1
          = (1 − cos(π·x)) / 2   otherwise          (0x402C30)
```

Most brushes use `t = 1 − k·d/r` with k = 1.2 or 1.3. The effective
radius is therefore `r/k`. Some use `sin(t·π/2)` or `t²` instead.

### 3.3 Mask and dirty marking **[sure]**

- With "respect mask" on (default), every per-vertex factor is multiplied by
  `w`. A masked vertex (`w = 0`) does not move.
- Each moved vertex gets `relaxCountdown = 10` and is marked dirty
  (`Vertex_MarkDirty`), so the beautify pass relaxes the area afterwards.

### 3.4 Brush mask (stencil) **[likely]**

When a brush mask image is loaded and enabled (0x4FD163 and 0x4FD13F),
0x415B20 projects the vertex into the brush frame (center 0x4B0F14, axes at
0x4B0F24/0x4B0F30/0x4B0F3C, size 0x4B0F20 × 0.6) and samples the image
bilinearly. Outside it the value is 0. **The sample replaces the radial
falloff value `t`.**

### 3.5 Tessellation **[sure]**

All brushes except Smooth and Reduce call `TesselateForBrush(stroke, 1.0)`
after updating the region (see [mesh-core.md](mesh-core.md) section 3).
Grab, Rotate and Scale skip it in Global mode. Reduce with negative strength
calls it with `|s|` instead.

### 3.6 Symmetry **[sure]**

`mirror = symmetry on and c.x < r`. Brushes with a mirror pass also evaluate
the vertex against the mirrored center `(−c.x, c.y, c.z)`, using mirrored
normals and reference positions (x negated). With compensation on, both
contributions are multiplied by `1 − 0.5 · (r − c.x)/r` (see
[symmetry.md](symmetry.md)).

## 4. The brushes

Constants are exact; the formulas are **[sure]** unless marked.

### 4.1 Draw (0x417CA0, jobs 0x417210 and 0x4175D0)

Two passes, both multithreaded:

**Pass 1, brush plane.** For vertices with `d < r`:
`a = (1 − d/r) · w`. Accumulate `Σ a·n`, `Σ a·ref` and `Σ a`. The mirror pass
adds mirrored vertices too. If `Σ a > 0.01`:

```
N = normalize(Σ a·n)            // mean normal
R = Σ a·ref / Σ a               // mean reference position
```

**Strength:**

```
normal:  S = |s|^1.3 · 0.6;   k = 0.1 · S;        h = r
Clay:    S = |s|^0.8 · 0.7;   a = S
         Soft:  h = r · (1 + a) · 0.1;   k = 0.8 · a
         else:  h = r · 0.2 · a;         k = 0.6 · √a
         then (S > 0):  h = 1.5 · h
```

**Pass 2, move.** For vertices with `t = 1 − 1.2·d/r > 0`:

```
f    = smooth(t) · w                       (brush mask replaces t)
G    = p − N·dot(N, p − R) + sign(s)·N·h   // plane through R, raised by h
Δ    = G − p
if dot(n, Δ) · s > 0:  p += Δ · f · k      // only outward for s > 0
```

The mirror pass works the same against the mirrored center, with
`t = 1 − 1.3·d/r`.

Draw thus pulls the surface toward a layer at height `h` above the plane of
the stroke-start surface. A normal stroke targets one brush radius, Clay a
height proportional to strength, which gives flat, clay-like build-up.
**[likely: interpretation]**

### 4.2 Inflate (0x4160F0)

Radius `r' = 1.5 r`. For vertices with `t = 1 − 1.3·d/r' > 0`:

```
G  = ref + n · r' · sin(t·π/2)
p += (G − p) · t · w · 0.02 · s
```

Each vertex moves along its own current normal toward a dome over its
stroke-start position. A mirror pass applies the same against the mirrored
center.

### 4.3 Pinch (0x416D40)

Radius `r' = 1.5 r`. Only runs if the weighted sum `Σ (1 − d/r')·w` over the
area exceeds 0.01. Then for `t = 1 − 1.3·d/r' > 0`:

```
p += (c − p) · t² · w · 0.1 · s
```

Vertices are pulled toward the dab center. There is no mirror pass for the
movement; plane vertices are kept on the plane by snapping.

### 4.4 Crease (0x4180C0)

**Pass 1** (`d < r`): `a = (1 − d/r)² · w`, giving `N` (mean normal) and `R`
(mean reference position). The mirror pass covers non-plane vertices.

```
s' = s                       for s ≥ 0
   = −√|s| · 1.4             for s < 0
r' = 1.6 r
G  = R − N · (r'/6) · s'     // below the surface for s > 0, above for s < 0
```

**Pass 2** (`t = 1 − 1.3·d/r' > 0`), with `f = t² · w`:

```
p += (G − p) · f³ · |s| · 0.35
```

Vertices are drawn toward a point under the stroke, which forms a sharp
groove (s < 0: a ridge).

### 4.5 Flatten (0x4164D0)

Radius `r' = 1.5 r`.

**Plane** (over `d < r'/2`): `a = (1 − d/(r'/2)) · (dot(V, n) + 0.5) · w`,
where V = stroke +0x68 (view direction, so front-facing surface counts more
**[likely]**). Then `N = normalize(Σ a·n)` and `P = Σ a·p / Σ a` (current
positions). With **Lock plane** the plane is computed only at the first dab
of the stroke and kept (0x4B0E68 normal, 0x4FD3C4 point).

**Move** (`t = 1 − 1.3·d/r' > 0`), with `f = smooth(t)`:

- Lock plane: `g = (1 − |dot(p − P, N)|/r') · 1.2`. Skip the vertex if
  `g ≤ 0`; otherwise `f *= min(g, 1)`. Only vertices near the plane are
  affected.
- Otherwise, if `s > 0`: skip vertices below the plane (`dot(p − P, N) < 0`).
  Flatten then only cuts peaks down; it never fills.
- Angle falloff: `a = (dot(n, N) − 0.3) · 1.5`. Skip the vertex if `a ≤ 0`;
  otherwise `f *= min(a, 1)`.

```
G  = p − N · dot(N, p − P)          // projection onto the plane
p += (G − p) · f · w · 0.25 · s
```

### 4.6 Smooth (0x415D30)

Two iterations per dab; no tessellation. `σ = |s|`. For vertices with
`t = 1 − 1.3·d/r > 0`:

```
neighbors = other endpoints of the vertex's edges,
            without quad diagonals (when the mesh has quads);
            for a boundary vertex (an edge with only one triangle, and not
            on the symmetry plane): boundary neighbors only
avg = Σ w_j · p_j / Σ w_j            (w_j = neighbor mask weight)
on the symmetry plane: avg.x = 0
p  += (avg − p) · sin(t·π/2) · w · 0.7 · σ
```

The sign of the strength does not matter.

### 4.7 Mask (0x4186D0)

Paints the vertex weight `w`, not the position (it still tessellates). For
`t = 1 − 1.3·d/r > 0`:

```
k = |s|^1.5
g = 0.3 · sin²(t·π/2)
target = 1.1 if s > 0, −0.1 if s < 0
w += (target − w) · k · g;   clamp w to [0, 1]
```

The targets overshoot so that `w` actually reaches 0 or 1 at the clamp.
Which sign the UI uses for "paint mask" is **[open]**.

### 4.8 Reduce brush (0x41A480)

- `s < 0`: no reduction; it runs `TesselateForBrush(stroke, |s|)` (adds
  detail).
- `s > 0`: collapses short edges under the brush:

```
first dab of a stroke: minCost = 100000
T = (0.8 · s + 1) · minCost
for each unmasked vertex with d < r:
    for each edge (A, B):
        cost = ((1 − dot(nA, nB)) · 40 + 1) · length
        minCost = min(minCost, cost)
        if second phase:
            k = valence(A) + valence(B) − 12
            if k > 0: cost *= 0.25·k + 1
            if cost < T: CollapseEdge; next vertex
```

On the first dab the loop runs twice: phase 1 only finds the smallest edge
cost, and phase 2 collapses. The threshold therefore adapts to the finest
edge under the brush. **[sure]**

### 4.9 Grab (0x4188B0) **[sure unless marked]**

Radius `r' = 1.5 r`. Anchor A = stroke +0x0C, set to `c` at the first dab.
`Δ = c − A`. Strength `k = min(1, (1.2·|s|)²)`.

- **Normal:** for vertices with `x = 1.1·d/r' < 1`:
  `p += Δ · smooth(1 − x) · k · w`. Afterwards `A = c`, so each dab moves by
  the mouse movement since the previous dab.
- **Limit:** `Δ` is softened to `normalize(Δ) · r' · atan(|Δ|/r')`. The
  falloff distance is measured between A (fixed for the stroke) and
  `ref`, and the result is absolute: `p = ref + Δ · f`.
- **Global:** every unmasked vertex is translated rigidly: `p = ref + Δ`.
  The moved pivot is stored for Rotate/Scale (0x4FD3D0).
- Symmetry: plane vertices are handled specially (their x movement is
  suppressed) **[likely; details open]**.

### 4.10 Rotate (0x419AC0) and Scale (0x419020) **[likely]**

Transform tools driven by the mouse movement on screen since stroke start
(+0x90/+0x94 against +0x9C/+0xA0), around the anchor or the global pivot.
Both work from `ref` with the falloff
`f = sin(min(1, 1.5·(1 − 1.3·d/r'))·π/2)` and `r' = 1.5 r`.

- **Scale:** factor `σ = (2^(Δx/200) + 2^(−Δy/200)) / 2`;
  `p = ref + ((pivot + (ref − pivot)·σ) − ref) · f`. **XYZ** scales along the
  screen axes separately.
- **Rotate:** the angle comes from `atan2` of the mouse vector, and the axis
  is the view axis (camera matrix 0x4BDC60). The rotation helper is 0x402EA0.
- **Global:** applies to all unmasked vertices without falloff and updates
  their normals directly.

## 5. Picking **[sure unless marked]**

- **Surface under the mouse** (`PickSurface`, 0x408DB0): a ray from the
  camera through the mouse position (field of view via `tan`). Candidate
  triangles come from a spatial tree (global 0x4FD0E0, built by 0x41BA80
  over the mesh bounding box, query 0x4045E0). Each candidate is tested
  with a ray/triangle intersection (0x4089F0), and the nearest hit wins.
  Triangle +0x06 serves as a "visited" mark during the query. With symmetry
  on, the mirrored ray (x negated) is tested too, so strokes on the mirrored
  half hit the stored half.
- **Sculpt mode picks through the depth buffer, not the tree.** 0x408CB0
  reads a 10×10 block of depth values around the mouse after rendering
  (`glReadPixels`, GL_DEPTH_COMPONENT/GL_FLOAT). `PickDepth` (0x409200) takes
  the value at the mouse pixel and converts it to eye-space depth:
  `z = 2·n·f / ((f + n) − (f − n)·(2d − 1))` (n, f near/far plane). If
  `z < 0.99·f` the pick hits; the point is `eye + z · D`, where `D` is the
  ray through the pixel scaled to unit length along the view axis
  (`(x·tan(fov/2), y·tan(fov/2), 1)` in camera space). `z` is stored for the
  brush radius (stroke +0x88, see 2.4). The tree-based `PickSurface` is used
  only in paint mode (when the tree exists). With symmetry, the mirrored
  half is rendered too, so a depth pick can land on it; the dab is then
  mirrored to +x (2.3).
- How the spatial tree is kept current while tessellation changes the mesh
  is **[open]**. The octree builder (`Octree_Build`, 0x404510) is called on
  load, on symmetry switch and for paint mode.

## 6. Notes for the implementation

- The reference state stored at stroke start is central: Draw, Inflate,
  Crease and Grab compute their targets from it. Without it, repeated dabs
  would grow without limit.
- Every brush follows the same pattern: update region → tessellate →
  (optional plane/mean pass) → per-vertex move with falloff × mask × rate →
  mark dirty. Normals and beautify follow afterwards.
- Effective radii differ per brush: Draw ≈ r/1.2, Smooth/Mask ≈ r/1.3,
  Inflate/Pinch/Flatten ≈ 1.5r/1.3, Grab ≈ 1.5r/1.1, Crease ≈ 1.6r/1.3.

## 7. Open points

- Grab/Rotate/Scale symmetry handling in detail; Rotate's helper 0x402EA0.
- Picking tree maintenance (paint mode only, see 5).
- Mask brush sign convention in the UI.
- How Ctrl, the pen button flag and the invert toggle combine into the sign
  of `s` (2.4).
