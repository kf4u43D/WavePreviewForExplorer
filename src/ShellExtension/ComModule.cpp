#include "ComModule.h"
#include <atomic>

namespace wpv::shell {
namespace {
std::atomic<unsigned long> g_objectCount{0};
std::atomic<unsigned long> g_lockCount{0};
}

void ComModuleAddObject() noexcept {
  g_objectCount.fetch_add(1, std::memory_order_relaxed);
}

void ComModuleReleaseObject() noexcept {
  g_objectCount.fetch_sub(1, std::memory_order_relaxed);
}

void ComModuleLock() noexcept {
  g_lockCount.fetch_add(1, std::memory_order_relaxed);
}

void ComModuleUnlock() noexcept {
  unsigned long current = g_lockCount.load(std::memory_order_relaxed);
  while (current > 0 &&
         !g_lockCount.compare_exchange_weak(current, current - 1, std::memory_order_relaxed)) {
  }
}

bool ComModuleCanUnload() noexcept {
  return g_objectCount.load(std::memory_order_relaxed) == 0 &&
         g_lockCount.load(std::memory_order_relaxed) == 0;
}

} // namespace wpv::shell
