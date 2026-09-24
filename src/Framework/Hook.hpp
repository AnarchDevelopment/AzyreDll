#pragma once

#include <cstdint>

namespace mc::hook {

bool initialize();
bool create(void* target, void* detour, void** original);
void shutdown();
void* vtableFunc(uintptr_t vtableRva, int slot);

}
