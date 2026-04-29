#include "BitmapConversion.h"
#include <cstddef>
#include <cstdint>
#include <limits>

namespace wpv::shell {

Result<HBITMAP> CreateHBitmapFromRgb(const audio::RgbBitmap& bitmap) {
  if (bitmap.width == 0 || bitmap.height == 0) {
    return Result<HBITMAP>::Error("Invalid bitmap dimensions");
  }
  if (bitmap.width > static_cast<unsigned>(std::numeric_limits<LONG>::max()) ||
      bitmap.height > static_cast<unsigned>(std::numeric_limits<LONG>::max())) {
    return Result<HBITMAP>::Error("Bitmap dimensions exceed Win32 limits");
  }

  const auto expectedBytes = static_cast<std::uint64_t>(bitmap.width) * bitmap.height * 3u;
  if (expectedBytes != bitmap.pixels.size()) {
    return Result<HBITMAP>::Error("RGB bitmap has an unexpected pixel buffer size");
  }

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = static_cast<LONG>(bitmap.width);
  info.bmiHeader.biHeight = -static_cast<LONG>(bitmap.height);
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  void* dibBits = nullptr;
  HBITMAP hbitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &dibBits, nullptr, 0);
  if (hbitmap == nullptr || dibBits == nullptr) {
    return Result<HBITMAP>::Error("CreateDIBSection failed");
  }

  auto* out = static_cast<std::uint8_t*>(dibBits);
  for (unsigned y = 0; y < bitmap.height; ++y) {
    for (unsigned x = 0; x < bitmap.width; ++x) {
      const auto rgbOffset = (static_cast<std::size_t>(y) * bitmap.width + x) * 3u;
      const auto bgraOffset = (static_cast<std::size_t>(y) * bitmap.width + x) * 4u;
      out[bgraOffset + 0] = bitmap.pixels[rgbOffset + 2];
      out[bgraOffset + 1] = bitmap.pixels[rgbOffset + 1];
      out[bgraOffset + 2] = bitmap.pixels[rgbOffset + 0];
      out[bgraOffset + 3] = 0xFF;
    }
  }

  return Result<HBITMAP>::Ok(hbitmap);
}

} // namespace wpv::shell
