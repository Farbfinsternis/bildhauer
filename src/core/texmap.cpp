#include "core/texmap.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace bh {

namespace {

constexpr float kPi = 3.14159265f;

Vec3 areaNormal(const Mesh& m, int t) {
  const Triangle& tri = m.triangle(t);
  const Vec3& a = m.vertex(tri.v[0]).pos;
  return cross(m.vertex(tri.v[1]).pos - a, m.vertex(tri.v[2]).pos - a);  // length = 2 * area
}

Vec3 unitNormal(const Mesh& m, int t) {
  Vec3 n = areaNormal(m, t);
  float l = length(n);
  return l > 0 ? n / l : Vec3{0, 0, 1};
}

int across(const Mesh& m, int t, int k) {
  const Edge& e = m.edge(m.triangle(t).e[k]);
  return e.tri[0] == t ? e.tri[1] : e.tri[0];
}

// Charts: triangle -> chart id, grown over neighbors within the normal cone.
// Quad pairs always share a chart.
std::vector<int> buildCharts(const Mesh& m, const AtlasSettings& s, int& numCharts) {
  const int nt = m.triangleSlots();
  std::vector<int> partner(nt, -1);
  if (!m.quadMarkers.empty())
    for (int t = 0; t + 1 < nt && t + 1 < int(m.quadMarkers.size()); ++t)
      if (m.quadMarkers[t] == 1 && m.quadMarkers[t + 1] == 2 && m.triangle(t).active && m.triangle(t + 1).active) {
        partner[t] = t + 1;
        partner[t + 1] = t;
      }

  std::vector<Vec3> normal(nt);
  for (int t = 0; t < nt; ++t)
    if (m.triangle(t).active) normal[t] = unitNormal(m, t);

  const float cosMax = std::cos(s.maxAngle * kPi / 180);
  std::vector<int> chart(nt, -1);
  std::vector<Vec3> sum;  // area-weighted normal sum per chart

  auto grow = [&](int seed) {
    const int id = int(sum.size());
    sum.push_back({});
    std::deque<int> queue;
    auto add = [&](int t) {
      chart[t] = id;
      sum[id] += areaNormal(m, t);
      queue.push_back(t);
    };
    add(seed);
    if (partner[seed] >= 0 && chart[partner[seed]] < 0) add(partner[seed]);
    while (!queue.empty()) {
      int t = queue.front();
      queue.pop_front();
      for (int k = 0; k < 3; ++k) {
        int o = across(m, t, k);
        if (o < 0 || chart[o] >= 0) continue;
        Vec3 n = normalize(sum[id]);
        int p = partner[o];
        if (dot(normal[o], n) < cosMax) continue;
        if (p >= 0 && chart[p] < 0 && dot(normal[p], n) < cosMax) continue;
        add(o);
        if (p >= 0 && chart[p] < 0) add(p);
      }
    }
  };

  // Grow, then free triangles that ended up facing away from their chart's
  // final normal (the normal drifts while a chart grows) and grow again.
  for (int round = 0; round < 6; ++round) {
    for (int t = 0; t < nt; ++t)
      if (m.triangle(t).active && chart[t] < 0) grow(t);
    bool changed = false;
    for (int t = 0; t < nt; ++t) {
      if (chart[t] < 0) continue;
      if (dot(normal[t], normalize(sum[chart[t]])) >= 0.25f) continue;
      if (round == 5) {  // last round: a chart of its own (with its quad partner)
        int id = int(sum.size());
        sum.push_back({});
        for (int u : {t, partner[t]}) {
          if (u < 0) continue;
          sum[chart[u]] -= areaNormal(m, u);
          chart[u] = id;
          sum[id] += areaNormal(m, u);
        }
        continue;
      }
      for (int u : {t, partner[t]}) {
        if (u < 0 || chart[u] < 0) continue;
        sum[chart[u]] -= areaNormal(m, u);
        chart[u] = -1;
      }
      changed = true;
    }
    if (!changed) break;
  }

  // Merge small charts into a neighbor that can take all their triangles.
  std::vector<std::vector<int>> members(sum.size());
  for (int t = 0; t < nt; ++t)
    if (chart[t] >= 0) members[chart[t]].push_back(t);
  const float cosMerge = std::cos(std::min(s.maxAngle + 20.0f, 75.0f) * kPi / 180);
  for (int c = 0; c < int(members.size()); ++c) {
    if (members[c].empty() || int(members[c].size()) >= s.minChartTris) continue;
    int best = -1;
    float bestDot = -2;
    for (int t : members[c])
      for (int k = 0; k < 3; ++k) {
        int o = across(m, t, k);
        if (o < 0 || chart[o] == c) continue;
        Vec3 n = normalize(sum[chart[o]]);
        float worst = 2;
        for (int u : members[c]) worst = std::min(worst, dot(normal[u], n));
        if (worst >= cosMerge && worst > bestDot) {
          bestDot = worst;
          best = chart[o];
        }
      }
    if (best < 0) continue;
    for (int t : members[c]) {
      chart[t] = best;
      sum[best] += areaNormal(m, t);
      members[best].push_back(t);
    }
    members[c].clear();
    sum[c] = {};
  }

  // Compact ids.
  std::vector<int> remap(sum.size(), -1);
  numCharts = 0;
  for (int c = 0; c < int(members.size()); ++c)
    if (!members[c].empty()) remap[c] = numCharts++;
  for (int t = 0; t < nt; ++t)
    if (chart[t] >= 0) chart[t] = remap[chart[t]];
  return chart;
}

struct Chart {
  std::vector<int> tris;
  std::vector<int> verts;
  std::vector<float> x, y;  // projected vertex coordinates (mesh units), parallel to verts
  float w = 0, h = 0;       // extent after alignment
  int px = 0, py = 0;       // packed position (texels)
};

// Shelf packing at `scale` texels per mesh unit. Returns false if it does not fit.
bool pack(std::vector<Chart>& charts, const std::vector<int>& order, float scale, int size, int pad) {
  int x = 0, y = 0, rowH = 0;
  for (int c : order) {
    Chart& ch = charts[c];
    int w = int(std::ceil(ch.w * scale)) + 2 * pad;
    int h = int(std::ceil(ch.h * scale)) + 2 * pad;
    if (w > size) return false;
    if (x + w > size) {
      x = 0;
      y += rowH;
      rowH = 0;
    }
    if (y + h > size) return false;
    ch.px = x;
    ch.py = y;
    x += w;
    rowH = std::max(rowH, h);
  }
  return true;
}

}  // namespace

TextureMapping buildAtlas(const Mesh& m, const AtlasSettings& s) {
  TextureMapping map;
  map.size = s.size;
  const int nt = m.triangleSlots();
  map.chart = buildCharts(m, s, map.numCharts);
  map.corners.assign(nt, {-1, -1, -1});

  std::vector<Chart> charts(map.numCharts);
  for (int t = 0; t < nt; ++t)
    if (map.chart[t] >= 0) charts[map.chart[t]].tris.push_back(t);

  // Project every chart onto its plane and turn it along its principal axis,
  // lying (wider than high).
  std::vector<int> slotOf(m.vertexSlots(), -1), stamp(m.vertexSlots(), -1);
  for (int c = 0; c < map.numCharts; ++c) {
    Chart& ch = charts[c];
    Vec3 n;
    for (int t : ch.tris) n += areaNormal(m, t);
    n = normalize(n);
    Vec3 a = std::fabs(n.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    Vec3 u = normalize(cross(a, n)), v = cross(n, u);  // u x v = n: front faces stay counterclockwise
    for (int t : ch.tris)
      for (int vi : m.triangle(t).v) {
        if (stamp[vi] == c) continue;
        stamp[vi] = c;
        slotOf[vi] = int(ch.verts.size());
        ch.verts.push_back(vi);
        ch.x.push_back(dot(m.vertex(vi).pos, u));
        ch.y.push_back(dot(m.vertex(vi).pos, v));
      }
    const size_t nv = ch.verts.size();
    double mx = 0, my = 0;
    for (size_t i = 0; i < nv; ++i) { mx += ch.x[i]; my += ch.y[i]; }
    mx /= double(nv);
    my /= double(nv);
    double cxx = 0, cyy = 0, cxy = 0;
    for (size_t i = 0; i < nv; ++i) {
      double dx = ch.x[i] - mx, dy = ch.y[i] - my;
      cxx += dx * dx;
      cyy += dy * dy;
      cxy += dx * dy;
    }
    const float angle = 0.5f * float(std::atan2(2 * cxy, cxx - cyy));
    const float ca = std::cos(angle), sa = std::sin(angle);
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (size_t i = 0; i < nv; ++i) {
      float dx = float(ch.x[i] - mx), dy = float(ch.y[i] - my);
      float rx = dx * ca + dy * sa, ry = -dx * sa + dy * ca;  // rotation by -angle
      ch.x[i] = rx;
      ch.y[i] = ry;
      minX = std::min(minX, rx);
      maxX = std::max(maxX, rx);
      minY = std::min(minY, ry);
      maxY = std::max(maxY, ry);
    }
    if (maxY - minY > maxX - minX) {  // quarter turn: (x, y) -> (-y, x)
      for (size_t i = 0; i < nv; ++i) {
        float rx = -ch.y[i], ry = ch.x[i];
        ch.x[i] = rx;
        ch.y[i] = ry;
      }
      float nminX = -maxY, nmaxX = -minY;
      minY = minX;
      maxY = maxX;
      minX = nminX;
      maxX = nmaxX;
    }
    for (size_t i = 0; i < nv; ++i) {
      ch.x[i] -= minX;
      ch.y[i] -= minY;
    }
    ch.w = maxX - minX;
    ch.h = maxY - minY;

    // Texverts of this chart, in vertex order.
    const int base = int(map.texverts.size());
    for (int vi : ch.verts) map.texverts.push_back({0, 0, vi});
    for (int t : ch.tris)
      for (int k = 0; k < 3; ++k) map.corners[t][k] = base + slotOf[m.triangle(t).v[k]];
  }

  // Pack at the largest common scale that fits (binary search).
  std::vector<int> order(map.numCharts);
  for (int c = 0; c < map.numCharts; ++c) order[c] = c;
  std::sort(order.begin(), order.end(), [&](int a, int b) { return charts[a].h > charts[b].h; });
  double areaSum = 0;
  for (const Chart& ch : charts) areaSum += double(ch.w) * ch.h;
  int pad = s.padding;
  float lo = 0, hi = areaSum > 0 ? float(s.size / std::sqrt(areaSum)) : 1.0f;
  while (pad > 0 && !pack(charts, order, 1e-6f, s.size, pad)) --pad;  // too many charts for the padding
  for (int i = 0; i < 40; ++i) {
    float mid = (lo + hi) / 2;
    if (pack(charts, order, mid, s.size, pad)) lo = mid;
    else hi = mid;
  }
  pack(charts, order, lo, s.size, pad);

  int base = 0;
  for (Chart& ch : charts) {
    for (size_t i = 0; i < ch.verts.size(); ++i) {
      TexVert& tv = map.texverts[base + i];
      tv.u = (ch.px + pad + ch.x[i] * lo) / float(s.size);
      tv.v = (ch.py + pad + ch.y[i] * lo) / float(s.size);
    }
    base += int(ch.verts.size());
  }
  return map;
}

AtlasStats atlasStats(const Mesh& m, const TextureMapping& map) {
  AtlasStats st;
  st.charts = map.numCharts;
  for (const TexVert& tv : map.texverts)
    if (!(tv.u >= 0 && tv.u <= 1 && tv.v >= 0 && tv.v <= 1)) ++st.outOfRange;

  std::vector<int> positive(map.numCharts, 0), negative(map.numCharts, 0);
  std::vector<std::array<float, 4>> box(map.numCharts, {1e30f, 1e30f, -1e30f, -1e30f});
  std::vector<float> density;
  double uvArea = 0;
  for (int t = 0; t < m.triangleSlots(); ++t) {
    const Triangle& tri = m.triangle(t);
    if (!tri.active) continue;
    if (t >= int(map.chart.size()) || map.chart[t] < 0 || map.chart[t] >= map.numCharts) {
      ++st.unmapped;
      continue;
    }
    bool ok = true;
    for (int k = 0; k < 3; ++k) {
      int i = map.corners[t][k];
      if (i < 0 || i >= int(map.texverts.size()) || map.texverts[i].vertex != tri.v[k]) ok = false;
    }
    if (!ok) {
      ++st.unmapped;
      continue;
    }
    const TexVert& a = map.texverts[map.corners[t][0]];
    const TexVert& b = map.texverts[map.corners[t][1]];
    const TexVert& c = map.texverts[map.corners[t][2]];
    float signedArea = 0.5f * ((b.u - a.u) * (c.v - a.v) - (c.u - a.u) * (b.v - a.v));
    int ch = map.chart[t];
    (signedArea >= 0 ? positive : negative)[ch]++;
    for (const TexVert* p : {&a, &b, &c}) {
      box[ch][0] = std::min(box[ch][0], p->u);
      box[ch][1] = std::min(box[ch][1], p->v);
      box[ch][2] = std::max(box[ch][2], p->u);
      box[ch][3] = std::max(box[ch][3], p->v);
    }
    uvArea += std::fabs(signedArea);
    float area = 0.5f * length(areaNormal(m, t));
    if (area > 1e-12f) density.push_back(std::sqrt(std::fabs(signedArea) / area));
  }
  for (int c = 0; c < map.numCharts; ++c) st.flipped += std::min(positive[c], negative[c]);
  st.coverage = float(uvArea);
  for (int a = 0; a < map.numCharts; ++a)
    for (int b = a + 1; b < map.numCharts; ++b) {
      const float eps = 1e-6f;
      if (box[a][0] < box[b][2] - eps && box[b][0] < box[a][2] - eps && box[a][1] < box[b][3] - eps &&
          box[b][1] < box[a][3] - eps)
        ++st.overlaps;
    }
  if (!density.empty()) {
    std::vector<float> sorted = density;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    float median = sorted[sorted.size() / 2];
    for (float d : density)
      if (median > 0 && d > 0) st.maxStretch = std::max(st.maxStretch, std::max(d / median, median / d));
  }
  return st;
}

}  // namespace bh
