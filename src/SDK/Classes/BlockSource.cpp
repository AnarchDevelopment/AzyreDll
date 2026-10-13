#include "BlockSource.hpp"

#include "SDK/Classes/Block.hpp"

#include <excpt.h>

namespace mc {

// BlockSource::getBlock(int x, int y, int z) — devuelve const Block& (el
// puntero llega en rax con el ABI de MSVC). No hay overload confirmado en
// este build: si la firma real fuera getBlock(const BlockPos&) habria que
// pasar el struct empaquetado en rdx; ajustar aqui si getBlockPtr siempre
// devuelve 0.
using GetBlockFn = void*(__fastcall*)(void* blockSource, int x, int y, int z);

uintptr_t BlockSource::getBlockPtr(int x, int y, int z) const
{
    if (!addr)
        return 0;

    uintptr_t fnAddr = mem::resolve(off::fn::BlockSourceGetBlock);
    if (!fnAddr)
        return 0;
    auto fn = reinterpret_cast<GetBlockFn>(fnAddr);

    uintptr_t result = 0;
    __try
    {
        result = reinterpret_cast<uintptr_t>(fn(reinterpret_cast<void*>(addr), x, y, z));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = 0;
    }
    return result;
}

// Variante: getBlock(const BlockPos&) — BlockPos (3 ints) por referencia (rdx).
uintptr_t BlockSource::getBlockPtrRef(int x, int y, int z) const
{
    if (!addr)
        return 0;

    uintptr_t fnAddr = mem::resolve(off::fn::BlockSourceGetBlock);
    if (!fnAddr)
        return 0;

    struct RawBlockPos
    {
        int x, y, z;
    };
    using GetBlockRefFn = void*(__fastcall*)(void* blockSource, const RawBlockPos* pos);
    auto fn = reinterpret_cast<GetBlockRefFn>(fnAddr);

    RawBlockPos bp{x, y, z};
    uintptr_t result = 0;
    __try
    {
        result = reinterpret_cast<uintptr_t>(fn(reinterpret_cast<void*>(addr), &bp));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = 0;
    }
    return result;
}

int BlockSource::getBlockId(int x, int y, int z) const
{
    return Block(getBlockPtr(x, y, z)).id();
}

bool BlockSource::isAir(int x, int y, int z) const
{
    int id = getBlockId(x, y, z);
    return id == off::blocks::Air || id < 0;
}

}
