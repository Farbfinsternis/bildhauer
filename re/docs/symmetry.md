# Symmetry

Behavior description of Sculptris Alpha 6's mirror symmetry (plane x = 0), as
the basis for an independent implementation. Addresses refer to
`Sculptris.exe`; data structure fields as in [mesh-core.md](mesh-core.md).

Confidence: **[sure]** read directly from code; **[likely]** inferred from
code plus context; **[open]** not yet analyzed.

---

## 1. Model **[sure]**

With symmetry on (global flag 0x4B08A2, default on), Sculptris keeps **only
the +x half** of the mesh in memory. The other half is not stored. The
render, brush, export and paint code mirror it as needed.

- The cut runs along x = 0. Vertices on the cut have `onSymmetryPlane`
  (vertex +0x06) set and lie exactly at x = 0.
- The half is an open mesh: plane vertices and edges form its boundary.
- The `.sc1` file stores this half (`symmetry != 0`, see
  [sc1-format.md](sc1-format.md)).

## 2. Switching (`SetSymmetry(enable)`, 0x412170)

The UI asks for confirmation first, then saves an undo state, then calls
`SetSymmetry(!current)`:

- turn on: "This will remove asymmetric details, ok?"
- turn off: "This is typically a one-way operation, ok?"
- turn off in paint mode: "Double texture width and permanently disable
  symmetry, ok?" (the texture is doubled, see 2.2)

### 2.1 Turning on: cut away the −x half **[sure]**

1. Clear `onSymmetryPlane` on all vertices.
2. For each triangle with centroid x < 0 (centroid = mean of the three
   vertices): mark its three vertices, then remove it. Vertices without
   edges or triangles are deleted (`RemoveTriangle(t, removeOrphans = 1)`).
3. Every surviving marked vertex (it borders a removed triangle) is set to
   x = 0. These are the new plane vertices, and they keep the mark as
   `onSymmetryPlane`. **Asymmetric details on the −x side are lost**, and
   the +x side wins.
4. Remove triangles whose three vertices all have x ≤ 0 (now flat on the
   plane).
5. Polygon list: a quad whose partner triangle was removed becomes a plain
   triangle (marker 0). The count is reported as "N quads were converted to
   triangles".

### 2.2 Turning off: mirror the half **[sure unless marked]**

1. **Limit plane valence** ("fix beefy mirror verts"). Mirroring roughly
   doubles the valence of plane vertices, and the hard limit is 24 edges per
   vertex. For each plane vertex with **more than 10 edges**:
   1. Turn the quads around it into triangles.
   2. Among its adjacent triangles not yet tried, find the edge opposite the
      vertex (the one that does not touch it) and pick the shortest.
   3. Collapse that edge (`CollapseEdge`). This lowers the vertex valence by
      one.
   4. Repeat until the valence is ≤ 10 or no candidate is left.
2. **Tag:** a vertex that happens to lie at x = 0 without being a plane
   vertex is moved to x = 1e-5. This way it is mirrored rather than shared.
   Plane vertices are set exactly to x = 0.
3. **Copy vertices:** each vertex with x ≠ 0 gets a mirror vertex
   (−x, y, z) via `NewVertex`. Plane vertices are their own mirror (shared).
   The mapping is kept temporarily at vertex +0x130.
4. **Copy triangles:** for each original triangle (v0, v1, v2) at slot i,
   add (m(v2), m(v1), m(v0)) at slot N + i, where N = last used triangle slot
   + 1. The winding is reversed, so the mirrored half faces outward.
   Paint-mode attributes are copied, with the texvert order reversed to
   match.
5. **Polygon list:** the list grows to the triangle capacity, and entry
   N + i takes the marker of entry i. Quad pairs stay consecutive, and the
   mirrored diagonal (e2) is flagged as a quad diagonal again.
6. Clear `onSymmetryPlane` on all vertices.
7. Paint mode only: double the texvert array (mirrored), double the images
   (texture width × 2) and mirror the bump normals ("double images", "bump
   normals"). **[likely; details belong to the paint-mode analysis]**

### 2.3 Common tail **[sure]**

After either switch:

- set the symmetry flag;
- **clear the mask** (all weights = 1);
- rebuild the work sets and visible sets;
- mark everything dirty, update triangle geometry and vertex normals;
- if symmetry is on, snap the plane vertices (section 3);
- update the sculpt mapping and the paint-mode texture arrays.

## 3. Keeping the plane exact (`Symmetry_SnapPlaneVertices`, 0x411FF0) **[sure]**

For every active, dirty work-set vertex with `onSymmetryPlane`:

```
position.x = 0, reference position.x = 0
normal.x = 0, normalize;  reference normal.x = 0, normalize
```

Called with symmetry on:

- after every geometry/normal update (`Mesh_UpdateGeometryAndNormals`), so
  after every brush step;
- at the end of subdivision and symmetry switching;
- after an OBJ import.

The normal fix makes the surface meet its mirror image smoothly, so no crease
forms along the seam.

## 4. Interplay with other operations

- **Edge split** (`Tesselate`): the new vertex gets
  `onSymmetryPlane = A.onPlane AND B.onPlane`. There is **no** additional
  correction. The call to 0x43BD90 there is `render3D` with argument 1,
  which only clears a render flag and returns. Exactness comes from the snap
  in section 3. **[sure]**
- **Subdivision:** a midpoint between two plane vertices gets x = 0. After
  the rebuild, every vertex at x = 0 gets `onSymmetryPlane`.
  **[sure]**
- **Relaxation:** a boundary vertex only relaxes toward its boundary
  neighbors, so plane vertices stay on the seam (see
  [mesh-core.md](mesh-core.md) 5.1). **[sure]**
- **OBJ export** writes `# symmetry 1` and then the mirrored half as well
  (0x446E00 with mirror = 1). **OBJ import** reads that comment. **[sure]**

## 5. Brushes at the symmetry line **[likely]**

Brush functions (0x4175D0 and relatives) check whether the brush center
(stroke +0x00 = x) is closer to the plane than the radius (stroke +0xAC):
`x < r`. If so, the brush also acts from the mirrored center (−x, y, z), so
vertices near the plane also get the influence of the mirrored stroke.

Option **"Compensate sculpting at symmetry line"** (0x4B08E6, default on):
then the strength is multiplied by

```
f = 1 − 0.5 · (r − x) / r        // x = 0: f = 0.5,  x = r: f = 1
```

On the plane both copies act with half strength, so their sum is the normal
strength, and plane vertices are not sculpted twice.

Option **"Show symmetry line"** (0x4B08F1, default on) only affects
rendering.

The brush picking path (how a stroke on the mirrored, rendered half reaches
the stored +x half) belongs to the brush analysis. **[open]**

## 6. Notes for the implementation

- Store only the +x half when symmetry is on. That halves the work for
  every brush and tessellation step, and the seam stays exact by
  construction.
- The three invariants: plane vertices have x = 0 and normal.x = 0;
  no triangle lies in x < 0; plane vertices have at most ~10 edges before
  mirroring (so they stay under 24 afterwards).
- Turning symmetry on is lossy: the −x side is thrown away.

## 7. Open points

- Picking and rendering of the mirrored half (0x43BD90 `render3D`,
  0x43E3A0, 0x439EE0).
- Exact brush handling of the mirrored center (part of the brush analysis).
- Paint mode: doubling textures and texverts in detail.
