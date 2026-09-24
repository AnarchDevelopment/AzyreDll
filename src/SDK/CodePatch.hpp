#pragma once

#include <cstdint>
#include <cstddef>

namespace mc::patch {

uintptr_t patternScanImage(const unsigned char* pattern, size_t len);

struct CodePatch
{
    uintptr_t addr = 0;
    size_t overwriteLen = 0;
    unsigned char backup[16]{};
    void* cave = nullptr;
    bool enabled = false;

    bool init(uintptr_t address, size_t overwriteLength);
    bool allocCave(size_t size = 1024);
    uintptr_t caveAddr() const { return (uintptr_t)cave; }
    bool writeCave(size_t offset, const void* data, size_t len);
    bool enableJmpToCave();
    bool enableInline(const unsigned char* replacement, size_t len);
    bool disable();
    void freeCave();
};

}
