#pragma once

#include <cstdint>

namespace mc::hook {

bool initialize();
bool create(void* target, void* detour, void** original);
void remove(void* target);
void shutdown();
void* vtableFunc(uintptr_t vtableRva, int slot);

}
