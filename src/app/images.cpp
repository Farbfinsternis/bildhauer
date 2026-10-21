#include "app/images.h"

#include <windows.h>
#include <wincodec.h>

namespace bh::viewer {

namespace {

template <class T>
void release(T*& p) {
  if (p) p->Release();
  p = nullptr;
}

}  // namespace

bool loadImage(const std::string& path, Image& out, std::string* error) {
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
            SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                            WICBitmapPaletteTypeCustom));
  UINT w = 0, h = 0;
  if (ok) ok = SUCCEEDED(converter->GetSize(&w, &h)) && w > 0 && h > 0 && w <= 8192 && h <= 8192;
  if (ok) {
    out.width = int(w);
    out.height = int(h);
    out.rgba.assign(size_t(w) * h, 0);
    ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4, UINT(out.rgba.size() * 4),
                                         reinterpret_cast<BYTE*>(out.rgba.data())));
  }
  release(converter);
  release(frame);
  release(decoder);
  release(factory);
  if (!ok && error) *error = "cannot read " + path;
  return ok;
}

}  // namespace bh::viewer
