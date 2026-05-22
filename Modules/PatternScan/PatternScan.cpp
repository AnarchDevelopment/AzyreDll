/*
Under an4rch Development Public Source License 1.0
*/

#include "PatternScan.hpp"
#include <cstring>

uintptr_t PatternScan::Scan(uintptr_t start, size_t size, const BYTE* pattern, size_t patternSize) {
    for (size_t i = 0; i <= size - patternSize; ++i) {
        if (memcmp((void*)(start + i), pattern, patternSize) == 0) {
            return start + i;
        }
    }
    return 0;
}
