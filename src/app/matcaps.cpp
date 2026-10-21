#include "app/matcaps.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>

#include "core/vec3.h"

namespace bh {

namespace {

struct Builtin {
  const char* name;
  Vec3 base;
  float ambient, specular, shininess, rim;
};

const Builtin kBuiltins[] = {
    {"bildhauer:clay", {0.80f, 0.66f, 0.56f}, 0.22f, 0.30f, 24, 0.25f},
    {"bildhauer:white", {0.88f, 0.88f, 0.86f}, 0.30f, 0.35f, 30, 0.20f},
    {"bildhauer:redwax", {0.72f, 0.16f, 0.12f}, 0.18f, 0.45f, 40, 0.15f},
    {"bildhauer:gold", {0.86f, 0.62f, 0.22f}, 0.10f, 0.90f, 14, 0.35f},
    {"bildhauer:jade", {0.30f, 0.62f, 0.44f}, 0.25f, 0.50f, 50, 0.20f},
    {"bildhauer:slate", {0.25f, 0.27f, 0.32f}, 0.30f, 0.40f, 20, 0.30f},
};

void computeBuiltin(const Builtin& b, MatcapImage& out) {
  const int n = 256;
  out.width = out.height = n;
  out.rgb.resize(size_t(n) * n * 3);
  const Vec3 light = normalize({-0.45f, 0.65f, 0.6f});
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      float u = (x + 0.5f) / n * 2 - 1, v = (y + 0.5f) / n * 2 - 1;
      float r2 = std::fmin(u * u + v * v, 1.0f);
      Vec3 nrm{u, v, std::sqrt(1 - r2)};
      float diff = std::fmax(0.0f, dot(nrm, light));
      Vec3 refl = nrm * (2 * dot(nrm, light)) - light;
      float spec = std::pow(std::fmax(0.0f, refl.z), b.shininess);
      float rim = std::pow(1 - nrm.z, 3.0f) * b.rim;
      Vec3 c = b.base * (b.ambient + (1 - b.ambient) * diff) + Vec3{1, 1, 1} * (b.specular * spec + rim);
      uint8_t* o = &out.rgb[(size_t(y) * n + x) * 3];
      o[0] = uint8_t(std::fmin(c.x, 1.0f) * 255);
      o[1] = uint8_t(std::fmin(c.y, 1.0f) * 255);
      o[2] = uint8_t(std::fmin(c.z, 1.0f) * 255);
    }
}

template <class T>
void release(T*& p) {
  if (p) p->Release();
  p = nullptr;
}

bool loadImageFile(const std::string& path, MatcapImage& out, std::string* error) {
  IWICImagingFactory* factory = nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  IWICFormatConverter* converter = nullptr;
  std::wstring wpath(MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0), L'\0');
  MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wpath.data(), int(wpath.size()));
  bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
                                                         WICDecodeMetadataCacheOnDemand, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat24bppRGB, WICBitmapDitherTypeNone, nullptr, 0,
                                            WICBitmapPaletteTypeCustom));
  UINT w = 0, h = 0;
  if (ok) ok = SUCCEEDED(converter->GetSize(&w, &h)) && w > 0 && h > 0 && w <= 8192 && h <= 8192;
  std::vector<uint8_t> topDown;
  if (ok) {
    topDown.resize(size_t(w) * h * 3);
    ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 3, UINT(topDown.size()), topDown.data()));
  }
  release(converter);
  release(frame);
  release(decoder);
  release(factory);
  if (!ok) {
    if (error) *error = "cannot read " + path;
    return false;
  }
  out.width = int(w);
  out.height = int(h);
  out.rgb.resize(topDown.size());
  for (UINT y = 0; y < h; ++y)  // bottom row first
    std::copy_n(&topDown[size_t(h - 1 - y) * w * 3], w * 3, &out.rgb[size_t(y) * w * 3]);
  return true;
}

}  // namespace

const std::vector<std::string>& builtinMatcaps() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> v;
    for (const Builtin& b : kBuiltins) v.push_back(b.name);
    return v;
  }();
  return names;
}

bool loadMatcap(const std::string& name, bool gray, MatcapImage& out, std::string* error) {
  const Builtin* builtin = nullptr;
  for (const Builtin& b : kBuiltins)
    if (name == b.name) builtin = &b;
  if (builtin) computeBuiltin(*builtin, out);
  else if (!loadImageFile(name, out, error)) return false;
  if (gray)
    for (size_t i = 0; i + 2 < out.rgb.size(); i += 3) {
      uint8_t g = uint8_t((2 * out.rgb[i] + 3 * out.rgb[i + 1] + out.rgb[i + 2]) / 6);
      out.rgb[i] = out.rgb[i + 1] = out.rgb[i + 2] = g;
    }
  return true;
}

}  // namespace bh
