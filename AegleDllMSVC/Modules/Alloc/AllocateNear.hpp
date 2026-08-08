/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>

// Memory allocation utility
class AllocateNear {
public:
    // Allocates executable memory within 32-bit relative jump range
    // Tries different offsets from a reference address
    static void* Allocate(uintptr_t reference, size_t size);
};
