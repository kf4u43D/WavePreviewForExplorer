#pragma once

namespace wpv::shell {

void ComModuleAddObject() noexcept;
void ComModuleReleaseObject() noexcept;
void ComModuleLock() noexcept;
void ComModuleUnlock() noexcept;
bool ComModuleCanUnload() noexcept;

} // namespace wpv::shell
