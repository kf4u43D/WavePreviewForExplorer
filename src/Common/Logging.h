#pragma once
#include <string>

namespace wpv {
inline void LogDebug(const std::string&) noexcept {
  // TODO: ETW or rotating file log, disabled by default.
}
}
