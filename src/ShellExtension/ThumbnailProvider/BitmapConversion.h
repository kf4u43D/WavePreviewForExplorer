#pragma once

#include "Render/WaveformBitmapRenderer.h"
#include "Result.h"
#include <windows.h>

namespace wpv::shell {

// The caller owns the returned HBITMAP and must release it with DeleteObject.
Result<HBITMAP> CreateHBitmapFromRgb(const audio::RgbBitmap& bitmap);

} // namespace wpv::shell
