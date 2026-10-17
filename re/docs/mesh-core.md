# Mesh core: data structure, dynamic tessellation, reduction, subdivision

Behavior description of Sculptris Alpha 6's mesh kernel, as the basis for an
independent implementation. Addresses refer to `Sculptris.exe` (see
[function-map.md](function-map.md); names in `tools/names.json`).

Confidence: **[sure]** read directly from code; **[likely]** inferred from
code plus context; **[open]** not yet analyzed.

---

## 1. Data structure

Sculptris uses an explicit vertex/edge/triangle structure with back-pointers,
not a half-edge structure. All three element types live in fixed arrays of
slots with an `active` flag. Freed slots are reused; a round-robin cursor
looks for the next free slot (`NewVertex` 0x409DE0, `NewEdge` 0x40AD30,
`AddTriangle` 0x40B2A0). When an array is full, it is reallocated with more
space ("ran out of … storage, allocate more"). **[sure]**

### Vertex (320 bytes) **[sure unless marked]**

| Offset | Field |
|---|---|
| +0x00 | `active` |
| +0x02 | `dirty` (needs normal/relax update) |
| +0x03 | `relaxCountdown`: remaining beautify iterations (new vertex: 5, after collapse: 10) **[likely]** |
| +0x06 | `onSymmetryPlane` (x == 0) |
| +0x07 | `hidden` (hidden via H+click; hidden elements are not in the work sets) **[likely]** |
| +0x0C | index (temporary, for saving) |
| +0x18 | `weight` f32: 1 = editable, 0 = masked. Masked regions are neither tessellated nor reduced; new vertices get the mean of the edge endpoints **[likely]** |
| +0x1C | position |
| +0x28 | reference position (rest shape; carried along in splits) **[likely]** |
| +0x34 | normal = normalized sum of the adjacent face normals (unweighted) |
| +0x40 | reference normal **[likely]** |
| +0x4C | up to 24 triangle pointers, count at +0xC8 |
| +0xCC | up to 24 edge pointers, count at +0x12C |

**Limit:** at most 24 edges per vertex ("Too many connections to a vertex
(max 24)"). Operations that would raise the valence keep a safety margin of 18
(see split and collapse).

### Edge (52 bytes) **[sure]**

| Offset | Field |
|---|---|
| +0x00 | `active`; +0x02 `dirty`; +0x04 quad diagonal (set by subdivision, see 6.3) **[likely]**; +0x05 split state (0 = no, 1 = yes, 2 = unchecked) |
| +0x08 | slot index (temporary, set by subdivision) |
| +0x0C, +0x10 | the (at most) 2 adjacent triangles; one 0 → boundary edge |
| +0x18, +0x1C | endpoints A, B |
| +0x24 | cached length \|B − A\| |

### Triangle (100 bytes) **[sure]**

| Offset | Field |
|---|---|
| +0x00 | `active`; +0x01 `dirty`; +0x03 "done/out of range" (tessellation flag); +0x04 mask flag (saved); +0x06 longest edge checked; +0x07 visible |
| +0x08 | vertices v0, v1, v2 |
| +0x18 | edges e0 = (v0,v1), e1 = (v1,v2), e2 = (v2,v0) |
| +0x24 | face normal normalize((v1 − v0) × (v2 − v0)) |
| +0x30 | own slot index |
| +0x34 | size = 3 × longest edge |
| +0x38 | centroid (v0 + v1 + v2) / 3 |
| +0x44 | bounding radius = max distance centroid → vertex |
| +0x48 | UV patch id (−1 = none); +0x4C texvert pointers (paint mode) |

### Building a triangle (`AddTriangle`) **[sure]**

1. Reject degenerate input (two equal vertices).
2. Take a free slot (or the given slot when reusing, e.g. right after
   `RemoveTriangle`).
3. Compute the face normal. If a reference normal is given and the new normal
   points the other way (dot < 0), swap v0/v2. This keeps orientation
   consistent during retriangulation.
4. Register the triangle with each vertex.
5. For each of the three edges, find an existing edge via `VertexHasEdge`,
   otherwise create one (`NewEdge`, also computes the length). Register the
   triangle with the edge (max. 2).
6. Update centroid, bounding radius and size; add the triangle to the
   visible and work sets unless a vertex is hidden.

`RemoveTriangle` (0x40BE00) is the reverse: deregister from vertices and
edges; edges without a triangle are removed. The slot index is returned so
the caller can reuse it.

### Element sets **[likely]**

Besides the arrays there are index lists for vertices/edges/triangles:
**visible set** (rendering, picking) and **work set** (operations). Brush and
reduction operations iterate over the work set only.

---

## 2. Splitting an edge (`Tesselate`, 0x40D220)

Input: an edge A–B with one or two adjacent triangles.

### 2.1 Position of the new vertex: curved midpoint **[sure]**

The new vertex does **not** sit on the straight edge midpoint. The edge is
treated as a cubic Bézier curve whose tangents come from the vertex normals:

```
L     = |B - A|
d     = normalize(B - A)
tA    = normalize(d - nA * dot(nA, d))       // d projected into A's tangent plane
tB    = normalize(-d - nB * dot(nB, -d))     // the same from B
P1    = A + tA * L/3
P2    = B + tB * L/3
M     = Bezier(A, P1, P2, B; t = 0.5)
```

The normal of the new vertex: curve tangent from `Bezier(t=0.51) −
Bezier(t=0.49)`, combined with `normalize(nA + nB)` via two cross products
into a vector perpendicular to the curve tangent, flipped toward the mean
normal if needed. **[likely in detail; the principle is sure]**

The same computation is repeated for the reference position (+0x28) using the
reference normals (+0x40). **[sure]**

Further attributes of the new vertex **[sure]**:
- `weight` = mean of the endpoints
- `hidden` = A.hidden OR B.hidden
- `onSymmetryPlane` = A.onPlane AND B.onPlane (then an extra symmetry
  correction runs, 0x43BD90, **[open]**)

### 2.2 Topology **[sure]**

1. Collect the outer edges of the adjacent triangles (1 triangle → 2 outer
   edges, 2 triangles → 4).
2. Remove the adjacent triangles and keep their slots.
3. For each outer edge (a, b), create a triangle (a, b, M), reusing the slots
   and keeping the orientation of the old triangle.

Result: 2 triangles → 4 (interior edge), 1 → 2 (boundary edge). M has
valence 4 (or 3 at the boundary); each of the opposite vertices gains one
edge.

### 2.3 Splittability (`Edge_CanSplit`, 0x40D190) **[sure]**

An edge may only be split if **each** vertex opposite it has at most 18 edges
(it gains one more; hard limit 24).

---

## 3. Detail under the brush (`TesselateForBrush`, 0x415000)

Called per brush step, before the deformation. **[sure unless marked]**

**Preconditions:** not in paint mode, and global detail factor `G` > 0.15
(global 0x4B08B0, default 1.1; probably the "Detail" slider, **[likely]**).

**Detail threshold:**

```
threshold = brush.detailSize / (2 * s) / G
```

`brush.detailSize` (brush+0xB0) and `s` (call argument) come from the brush
settings and view scale **[open: exact meaning]**. A triangle is "too
coarse" when `size = 3 × longest edge > threshold`.

**Candidates:** visible triangles that are visible, not completely masked
(at least one vertex weight ≠ 0) and **inside the brush radius**:
`|brush.center − centroid| ≤ boundingRadius + brush.radius` (brush+0xAC).

**Loop (greedy, at most 3000 iterations):**

```
candidates = coarse triangles in range
repeat:
    pick, over all candidates, the longest edge that is splittable
    (per triangle only its longest edge counts; splittability is cached on the
    edge: unchecked → Edge_CanSplit)
    none left → stop
    split this edge (Tesselate)
    new candidates = all triangles around the new vertex (marked dirty)
                   + the remaining old candidates that are still coarse
```

So the mesh is refined from coarse to fine; the globally longest edge is
always split first. That yields an even triangle size within the brush radius.
The 3000 limit ("TesselateForBrush bail!!") caps the work per step. After the
loop the beautify pass (section 5) runs on the new vertices, setting
"Beautify/relax mesh (always used when adding detail)" **[likely]**.

---

## 4. Reduction

### 4.1 Collapsing an edge (`CollapseEdge`, 0x40E5F0) **[sure]**

Edge v1–v2 with adjacent triangles t1, t2. **Rejected** if:

1. **Link condition violated:** v1 and v2 share a neighbor other than the
   vertices opposite in t1/t2 (would create a non-manifold mesh).
   Implementation: mark v1's neighbors, mark v2's neighbors, unmark t1/t2's
   vertices, look for overlaps.
2. v1 and v2 lie on different sides of the symmetry plane (`onSymmetryPlane`
   differs).
3. `valence(v1) + valence(v2) − 4 > 18`.

Execution:

1. Remove t1 and t2.
2. Move v1 to the **curved midpoint** of the edge (same Bézier construction
   as in the split).
3. Rebuild every remaining triangle of v2 with v1 in place of v2 (remove,
   then `AddTriangle` with slot reuse and orientation check).
4. Deactivate v2.
5. Recompute the normal of v1 and all its neighbors; for non-boundary
   vertices apply a **tangential relaxation with factor 0.7** (section 5.1).
   Vertices on the symmetry plane get x = 0.
6. v1: relax countdown = 10, mark the neighborhood dirty.

### 4.2 Reducing an area (`ReduceMesh`, 0x4105F0; "REDUCE SELECTED", "REDUCE BRUSH") **[sure]**

Works on the vertices of the work set whose weight ≠ 0 (not masked). Requires
a triangle mesh (quads are converted to triangles first).

Edge cost (curvature- and valence-dependent):

```
c        = 1200 / k,   k = 1 + 0.5 * pass          (pass = 0, 1, 2)
bend     = 1 - dot(nA, nB)                          (0 = flat)
val      = valence(A) + valence(B) - 12
cost     = length
         * (1 + 0.25 * val)       if val > 0
         * (1 + c * bend)
```

Flat regions with low valence are cheap and get reduced first; curved
regions are protected (factor up to ~2400).

Procedure:

1. Statistics pass: minimum, mean and a weighted mean (weights cost³) of the
   edge costs.
2. Threshold `T = (strength × 0.7 + X) × weighted mean`, strength = global
   0x4B08D8 (default 0.4, probably "Reduce strength") **[X open]**.
3. For each vertex, try its edges in order; the first edge with `cost < T`
   is collapsed (`CollapseEdge`), then move on to the next vertex.
4. If fewer than 20 collapses succeeded and pass < 3: repeat with the next
   pass (lower curvature protection).
5. Then run 10 beautify passes (section 5) unless in paint mode, then update
   normals and buffers.

---

## 5. Smoothing the mesh

### 5.1 Tangential relaxation (`Vertex_RelaxTangential`, 0x40CA60) **[sure]**

```
c = mean position of the neighbors
    (boundary vertex: boundary neighbors only)
d = c - p
d = d - n * dot(n, d)          // tangential part only
p = p + d * f                  // f = factor, e.g. 0.7
```

Vertices move along the surface toward an even distribution; the shape is
preserved to first order.

### 5.2 Beautify pass (`Beautify_Pass`, 0x40F210) **[likely]**

Per dirty vertex of the work set with `relaxCountdown > 0`, not on the
boundary:

```
flat   = |mean(neighbor normals)|              // 1 = flat, smaller = curved
f      = clamp((flat - 0.5) * 2.1, 0, 1)^2
relax tangentially toward the neighbor centroid, scaled by f
```

So relaxation happens mainly in flat areas; sharp creases and peaks are
preserved. The countdown drops per pass. Neighbors of a changed vertex get at
least `countdown − 2` (`Vertex_MarkDirty` 0x409D30), so the relaxation
spreads over a few rings and then stops. Optionally multithreaded
(`mt_beautify` in config.txt).

---

## 6. Subdivision (`SubdivideMesh`, 0x40F8C0; "SUBDIVIDE ALL")

Subdivision rebuilds the whole mesh: every edge gets a midpoint and every face
is split into four. Original vertices stay where they are; it is an
*interpolating* scheme without a smoothing step (no Loop or Catmull–Clark
vertex rule). **[sure]**

The caller saves an undo state first (`writemesh` into the undo ring),
then calls `SubdivideMesh`. **[sure]**

### 6.1 Edge midpoints **[sure]**

Per active edge A–B:

- Option **"Smooth subdivide (turn off to preserve corners)"** on (default):
  the same curved midpoint as in edge splitting (section 2.1): cubic Bézier
  from A to B with tangents projected into the vertex tangent planes,
  control points at L/3, evaluated at t = 0.5. Only the position is computed;
  no normal and no reference position.
- Option off: the straight midpoint (A + B) / 2. The surface keeps its exact
  shape, so corners and flat facets stay intact.
- If A and B are both on the symmetry plane, the midpoint's x is set to 0.

### 6.2 Topology **[sure]**

The new mesh has V + E vertices: the old vertices (compacted, same order)
followed by one midpoint per edge. Faces are generated per old face:

**Triangle** (v0, v1, v2) with edge midpoints m0 = mid(v0,v1),
m1 = mid(v1,v2), m2 = mid(v2,v0) gives 4 triangles:

```
(v0, m0, m2)   (m0, v1, m1)   (m1, v2, m2)   (m0, m1, m2)
```

**Quad** (only when the mesh has a polygon list, see sc1-format.md): two
consecutive triangle slots marked 1 and 2 form the quad (v0, v1, v2, q), with
T1 = (v0, v1, v2) and T2 = (v2, q, v0); the shared diagonal is v2–v0. With
m0, m1 as above, n0 = mid(v2,q), n1 = mid(q,v0) and **c = midpoint of the
diagonal** it gives 4 quads:

```
(v0, m0, c, n1)   (m0, v1, m1, c)   (m1, v2, n0, c)   (c, n0, q, n1)
```

The quad center is therefore not the average of the four corners but the
(possibly curved) midpoint of the diagonal. A triangle marked 0 in a mesh
with a polygon list is split as a triangle. A triangle marked 1 whose
following slot is inactive is dropped (likely never happens).

### 6.3 Rebuilding the mesh **[sure unless marked]**

1. Deactivate all vertices, edges and triangles and reset the counters.
2. Grow the arrays if needed: vertices to V + E, triangles to the new count,
   edges to 4 · E (estimate).
3. Create the vertices in order with `NewVertex`, so new index = slot.
   `NewVertex` sets the reference position to the position. Subdivision
   therefore resets the rest shape to the current shape.
4. Create the faces with `AddTriangle` (no reference normal, so no
   orientation flip). A quad (a, b, c, d) becomes the triangles (a, b, c) and
   (c, d, a). Their shared edge c–a is marked as a quad diagonal
   (edge +0x04 **[likely]**), and the new polygon list gets 1, 2 for the pair
   and 0 for each plain triangle.
5. Afterwards:
   - unhide everything (`0x4060B0`) **[likely]**;
   - rebuild the work sets with all active elements;
   - with symmetry on: vertices with x = 0 get `onSymmetryPlane`, and
     those vertices are snapped onto the plane (x of position, reference
     position and both normals set to 0, normals renormalized; `0x411FF0`);
   - update all triangle geometry and vertex normals (`0x40C810`);
   - reset the dirty flags;
   - **clear the mask**: every vertex gets weight 1 (`0x40CC30`).

The new vertex count is V + E; with T plain triangles and Q quads the
face count is 4 · (T + Q) (as triangles: 4 · T + 8 · Q). If the midpoint buffer overflows, Sculptris
logs "sd_verts overflow" and exits.

### 6.4 Notes for the implementation

- No beautify or relaxation step runs after subdivision; every edge is
  roughly halved.
- In each step, edge midpoints only depend on the old mesh. The result
  therefore does not depend on processing order.

---

## 7. Open points

- Exact meaning of the brush parameters `+0xAC` (radius, confirmed by usage)
  and `+0xB0`, and of the call argument `s` in `TesselateForBrush`.
- Symmetry correction 0x43BD90 for new vertices on the plane.
- Summand X in the reduction threshold.
- `AlignTriangle` (0x40C150): makes orientation consistent; details open.
- `SetSymmetry` (0x412170), brushes.
- Subdivision: the job of `0x405E10` in the unhide step.
