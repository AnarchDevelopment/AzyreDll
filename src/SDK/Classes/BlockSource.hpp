#pragma once

#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

namespace mc {

class BlockSource
{
public:
    BlockSource() = default;
    explicit BlockSource(uintptr_t a) : addr(a) {}

    bool valid() const
    {
        if (!addr || !mem::isReadable(addr, sizeof(uintptr_t)))
            return false;
        return mem::read<uintptr_t>(addr) == mem::resolve(vt::BlockSource);
    }
    uintptr_t address() const { return addr; }

    // Block* (o 0 si falla). Llama a BlockSource::getBlock via SEH.
    uintptr_t getBlockPtr(int x, int y, int z) const;
    // Variante con BlockPos pasado por referencia (getBlock(const BlockPos&)).
    uintptr_t getBlockPtrRef(int x, int y, int z) const;
    // Id numerico legacy del bloque (o -1 si no se pudo leer).
    int getBlockId(int x, int y, int z) const;
    bool isAir(int x, int y, int z) const;

private:
    uintptr_t addr = 0;
};

}
