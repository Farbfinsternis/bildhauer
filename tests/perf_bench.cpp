// Measures what sculpting costs on this device: every brush at three sizes,
// the undo copy, loading, saving and memory. Not a test; tools/perf-bench.ps1
// runs it on the PC and the tablet with the same model files.
//
// Usage: perf_bench --generate <triangles> <file.sc1>
//        perf_bench <file.sc1> --device <name> --out <file.csv>
//
// Each model gets a process of its own, so that peak memory belongs to it.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#include "core/brush.h"
#include "core/brush_params.h"
#include "core/pick.h"
#include "core/relax.h"
#include "core/symmetry.h"
#include "io/sc1.h"
#include "test_util.h"

using namespace bh;
using Clock = std::chrono::steady_clock;

namespace {

double since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

double peakMemoryMiB() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS c{};
  GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof c);
  return double(c.PeakWorkingSetSize) / (1024 * 1024);
#else
  rusage u{};
  getrusage(RUSAGE_SELF, &u);
  return double(u.ru_maxrss) / 1024;
#endif
}

// The hottest thermal zone in degrees; 0 where the system has none to read.
double temperature() {
#ifdef __ANDROID__
  double hottest = 0;
  for (int zone = 0; zone < 64; ++zone) {
    std::ifstream in("/sys/class/thermal/thermal_zone" + std::to_string(zone) + "/temp");
    double t = 0;
    if (!(in >> t)) continue;
    if (t > 1000) t /= 1000;
    if (t < 150) hottest = std::max(hottest, t);
  }
  return hottest;
#else
  return 0;
#endif
}

// A geodesic sphere with gentle bumps: valence 5 and 6 only, like a sculpted
// surface after tessellation, and symmetric like every new model.
void generate(int triangles, const std::string& path) {
  const int f = std::max(1, int(std::lround(std::sqrt(2.0 * triangles / 20))));
  const double t = (1 + std::sqrt(5.0)) / 2;
  const double corners[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                                 {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  const int faces[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                            {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                            {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
  Mesh m;
  std::unordered_map<uint64_t, int> shared;
  auto point = [&](const int c[3], int a, int b, int d) {
    double p[3];
    for (int k = 0; k < 3; ++k) p[k] = (corners[c[0]][k] * a + corners[c[1]][k] * b + corners[c[2]][k] * d) / f;
    const double l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    const double x = p[0] / l, y = p[1] / l, z = p[2] / l;
    const double bump = 1 + 0.03 * std::sin(3 * x + 1) * std::sin(4 * y + 2) * std::sin(5 * z + 3) +
                        0.01 * std::sin(17 * x) * std::sin(19 * y) * std::sin(13 * z);
    return m.addVertex({float(x * bump), float(y * bump), float(z * bump)});
  };
  // Points on a corner or an edge of the icosahedron belong to several faces.
  auto vertex = [&](const int c[3], int a, int b, int d) {
    const int weight[3] = {a, b, d};
    int used[3], n = 0;
    for (int k = 0; k < 3; ++k)
      if (weight[k]) used[n++] = k;
    if (n == 3) return point(c, a, b, d);
    uint64_t key;
    if (n == 1) key = uint64_t(c[used[0]]);
    else {
      int lo = used[0], hi = used[1];
      if (c[lo] > c[hi]) std::swap(lo, hi);
      key = 16 + (uint64_t(c[lo]) * 12 + uint64_t(c[hi])) * uint64_t(f + 1) + uint64_t(weight[hi]);
    }
    auto found = shared.find(key);
    if (found != shared.end()) return found->second;
    const int v = point(c, a, b, d);
    shared.emplace(key, v);
    return v;
  };
  for (const auto& c : faces) {
    std::vector<int> rows(size_t(f + 1) * size_t(f + 1), -1);
    auto at = [&](int i, int j) -> int& { return rows[size_t(i) * size_t(f + 1) + size_t(j)]; };
    for (int i = 0; i <= f; ++i)
      for (int j = 0; i + j <= f; ++j) at(i, j) = vertex(c, f - i - j, i, j);
    for (int i = 0; i < f; ++i)
      for (int j = 0; i + j < f; ++j) {
        m.addTriangle(at(i, j), at(i + 1, j), at(i, j + 1));
        if (i + j + 1 < f) m.addTriangle(at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
      }
  }
  m.updateGeometry();
  symmetryOn(m);
  m.updateGeometry();
  Sc1File file;
  meshToSc1(m, file);
  std::vector<uint8_t> bytes;
  std::string error;
  if (!writeSc1(file, bytes, &error) || !writeBinaryFile(path, bytes)) {
    std::fprintf(stderr, "cannot write %s: %s\n", path.c_str(), error.c_str());
    std::exit(1);
  }
  std::printf("%s: %d triangles, %.1f MiB\n", path.c_str(), m.numTriangles(), double(bytes.size()) / (1024 * 1024));
}

struct Stats {
  double median = 0, p95 = 0, worst = 0;
  int count = 0;
};

Stats stats(std::vector<double> v) {
  Stats s;
  if (v.empty()) return s;
  std::sort(v.begin(), v.end());
  s.count = int(v.size());
  s.median = v[v.size() / 2];
  s.p95 = v[std::min(v.size() - 1, v.size() * 95 / 100)];
  s.worst = v.back();
  return s;
}

// The model as the app frames it: seen from +z at 1.6 times its size.
struct Scene {
  Vec3 center, eye;
  float size = 1;
};

Scene frameOf(const Mesh& m) {
  Scene s;
  Vec3 extent = bhtest::bboxDiagonal(m, &s.center);
  if (m.symmetric) {
    s.center.x = 0;
    extent.x *= 2;
  }
  s.size = length(extent);
  s.eye = s.center + Vec3{0, 0, 1.6f * s.size};
  return s;
}

StrokeInput inputAt(const Mesh& m, const Scene& scene, const Vec3& target, int sample) {
  StrokeInput in;
  in.rayOrigin = scene.eye;
  in.rayDir = normalize(target - scene.eye);
  in.viewDir = {0, 0, -1};
  in.camRight = {1, 0, 0};
  in.camUp = {0, 1, 0};
  in.mouseX = float(100 + 4 * sample);
  in.mouseY = 100;
  const PickHit h = pick(m, in.rayOrigin, in.rayDir);
  in.point = h.visiblePoint();
  in.hit = h.hit;
  return in;
}

struct StrokeTimes {
  std::vector<double> samples;  // one input sample: pick and brush, what a stroke waits for
  double tessellate = 0, tool = 0, geometry = 0, beautify = 0, pick = 0;
  int dabs = 0, frames = 0, misses = 0, trianglesAfter = 0;
};

constexpr int kSamples = 50;
constexpr double kStrokeBudget = 2500;  // ms; a stroke this slow has said enough

// A stroke across the model, a sample a frame, with the frame's beautify pass.
void stroke(const Mesh& base, const Scene& scene, Tool tool, float sliderSize, StrokeTimes& out) {
  Mesh m = base;
  const Vec3 from = scene.center + Vec3{-0.15f * scene.size, 0.05f * scene.size, 0};
  StrokeInput in = inputAt(m, scene, from, 0);
  if (!in.hit) { ++out.misses; return; }
  BrushSettings s;
  s.tool = tool;
  s.radius = s.detailSize = brushRadius(sliderSize, dot(in.point - scene.eye, Vec3{0, 0, -1}));
  s.strength = brushStrength(defaultSliders(tool).strength);
  if (tool == Tool::Mask) s.strength = -s.strength;
  // One dab per sample: at a smaller step, the samples without a dab would
  // make a large brush look cheaper than a small one.
  const Vec3 step{s.spacingFactor * s.radius * 1.05f, 0, 0};
  const int samples = std::min(kSamples, std::max(8, int(0.3f * scene.size / step.x)));
  const auto start = Clock::now();
  Stroke brush(m, s, in);
  for (int i = 1; i <= samples && since(start) < kStrokeBudget; ++i) {
    const auto sample = Clock::now();
    in = inputAt(m, scene, from + step * float(i), i);
    out.pick += since(sample);
    out.misses += !in.hit;
    brush.moveTo(in);
    out.samples.push_back(since(sample));
    const auto frame = Clock::now();
    beautifyPass(m);
    out.beautify += since(frame);
    ++out.frames;
  }
  const DabTimes& t = brush.times();
  out.tessellate += t.tessellate;
  out.tool += t.tool;
  out.geometry += t.geometry;
  out.dabs += brush.dabCount();
  out.trianglesAfter = m.numTriangles();
}

struct Report {
  FILE* file = nullptr;
  std::string device, model;
  int triangles = 0;
  void row(const char* section, const char* name, float size, const Stats& s, double tessellate = 0, double tool = 0,
           double geometry = 0, double beautify = 0, double pick = 0, int dabs = 0, int after = 0, double extra = 0) {
    std::fprintf(file, "%s,%s,%d,%s,%s,%.2f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%.2f\n", device.c_str(),
                 model.c_str(), triangles, section, name, double(size), s.count, s.median, s.p95, s.worst, tessellate,
                 tool, geometry, beautify, pick, dabs, after, extra);
    std::fflush(file);
  }
};

template <class Work>
Stats timed(int repeats, Work work) {
  std::vector<double> v;
  for (int i = 0; i < repeats; ++i) {
    const auto t = Clock::now();
    work();
    v.push_back(since(t));
  }
  return stats(v);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 4 && std::strcmp(argv[1], "--generate") == 0) {
    generate(std::atoi(argv[2]), argv[3]);
    return 0;
  }
  std::string path, csv, device = "unknown";
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--device" && i + 1 < argc) device = argv[++i];
    else if (a == "--out" && i + 1 < argc) csv = argv[++i];
    else path = a;
  }
  if (path.empty() || csv.empty()) {
    std::fprintf(stderr, "usage: perf_bench <file.sc1> --device <name> --out <file.csv>\n");
    return 2;
  }
  Report report;
  report.file = std::fopen(csv.c_str(), "a");
  if (!report.file) return 1;
  report.device = device;
  report.model = path.substr(path.find_last_of("/\\") + 1);

  Mesh base;
  std::string error;
  const Stats load = timed(3, [&] {
    std::vector<uint8_t> bytes;
    Sc1File file;
    base = Mesh{};
    if (!readBinaryFile(path, bytes) || !readSc1(bytes, file, &error) || !sc1ToMesh(file, base, &error)) {
      std::fprintf(stderr, "cannot load %s: %s\n", path.c_str(), error.c_str());
      std::exit(1);
    }
  });
  report.triangles = base.numTriangles();
  report.row("load", "sc1", 0, load);
  const std::string temporary = csv + ".tmp.sc1";
  report.row("save", "sc1", 0, timed(3, [&] {
    Sc1File file;
    meshToSc1(base, file);
    std::vector<uint8_t> bytes;
    if (!writeSc1(file, bytes, &error) || !writeBinaryFile(temporary, bytes)) std::exit(1);
  }));
  std::remove(temporary.c_str());
  report.row("undo", "copy", 0, timed(5, [&] { Mesh copy = base; }), 0, 0, 0, 0, 0, 0, 0,
             double(base.storageBytes()) / (1024 * 1024));

  const Scene scene = frameOf(base);
  // The same stroke first and last: if the end is slower, the device has
  // throttled, and what lies between is not the code alone.
  auto canary = [&](const char* name) {
    StrokeTimes t;
    for (int i = 0; i < 3; ++i) stroke(base, scene, Tool::Draw, 0.3f, t);
    report.row("canary", name, 0.3f, stats(t.samples), 0, 0, 0, 0, 0, 0, 0, temperature());
  };
  canary("start");
  for (int i = 0; i <= int(Tool::Reduce); ++i) {
    const Tool tool = Tool(i);
    for (float size : {0.2f, 0.3f, 0.5f}) {
      StrokeTimes t;
      for (int run = 0; run < 3; ++run) stroke(base, scene, tool, size, t);
      const int dabs = std::max(1, t.dabs), frames = std::max(1, t.frames);
      report.row("stroke", toolName(tool), size, stats(t.samples), t.tessellate / dabs, t.tool / dabs, t.geometry / dabs,
                 t.beautify / frames, t.pick / frames, t.dabs, t.trianglesAfter, t.misses);
      std::printf("  %-8s %.1f: median %.2f, worst %.1f ms\n", toolName(tool), double(size), stats(t.samples).median,
                  stats(t.samples).worst);
      std::fflush(stdout);
    }
  }
  canary("end");
  report.row("memory", "peak", 0, Stats{}, 0, 0, 0, 0, 0, 0, 0, peakMemoryMiB());
  std::fclose(report.file);
  return 0;
}
