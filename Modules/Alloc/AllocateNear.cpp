/*
Under an4rch Development Public Source License 1.0
*/

#include "AllocateNear.hpp"
#include <windows.h>

void* AllocateNear::Allocate(uintptr_t reference, size_t size) {
    intptr_t deltas[] = { 0x1000000, 0x2000000, 0x4000000, -0x1000000, -0x2000000, -0x4000000 };
    for (int i = 0; i < 6; i++) {
        uintptr_t target = reference + deltas[i];
        void* addr = VirtualAlloc((void*)target, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (addr) {
            // Check if within 32-bit range
            int64_t jmpRel = (uintptr_t)addr - (reference + 5);
            if (jmpRel >= -0x80000000LL && jmpRel <= 0x7FFFFFFFLL) {
                return addr;
            }
            VirtualFree(addr, 0, MEM_RELEASE);
        }
    }
    return NULL; // No near address found
}
