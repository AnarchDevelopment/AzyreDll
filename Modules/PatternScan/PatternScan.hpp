/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>

// Generic Pattern Scanning Utility
class PatternScan {
public:
    // Scan for a byte pattern in memory
    // Returns: Address of pattern if found, 0 if not found
    static uintptr_t Scan(uintptr_t start, size_t size, const BYTE* pattern, size_t patternSize);
};
