#include "BlockSource.hpp"

#include "Framework/Log.hpp"
#include "SDK/Classes/Block.hpp"

#include <excpt.h>

namespace mc {

// Firma REAL de BlockSource::getBlock (sub_1405E73F0), confirmada por el
// decompile del orquestador (sub_1405113F0):
//   getBlock(this, Block* outResult, BlockPos const* pos)
// El ABI anterior del SDK lo llamaba como (this, x, y, z): rdx recibia la
// coordenada X como si fuera un puntero — la causa de TODOS los FATAL
// "READ @ 0x5 / 0x4A / 0xB" (la X como direccion de memoria).
using GetBlockRealFn = void(__fastcall*)(void* blockSource, void* outBlock,
                                         const int* pos);

static uintptr_t getBlockResult(uintptr_t bsAddr, int x, int y, int z)
{
    if (!bsAddr)
        return 0;

    uintptr_t fnAddr = mem::resolve(off::fn::BlockSourceGetBlock);
    if (!fnAddr)
        return 0;

    int pos[3] = {x, y, z};
    uintptr_t out = 0;
    auto fn = reinterpret_cast<GetBlockRealFn>(fnAddr);

    __try
    {
        fn(reinterpret_cast<void*>(bsAddr), &out, pos);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    return out;
}

// Block* del bloque en (x,y,z), o 0. API del SDK intacta.
uintptr_t BlockSource::getBlockPtr(int x, int y, int z) const
{
    return getBlockResult(addr, x, y, z);
}

// Variante historica por referencia: misma funcion real.
uintptr_t BlockSource::getBlockPtrRef(int x, int y, int z) const
{
    return getBlockResult(addr, x, y, z);
}

int BlockSource::getBlockId(int x, int y, int z) const
{
    if (!addr)
        return -1;

    uintptr_t fnAddr = mem::resolve(off::fn::BlockSourceGetBlock);
    if (!fnAddr)
        return -1;

    int pos[3] = {x, y, z};
    uintptr_t out = 0;
    auto fn = reinterpret_cast<uintptr_t(__fastcall*)(void*, void*, const int*)>(fnAddr);

    __try
    {
        fn(reinterpret_cast<void*>(addr), &out, pos);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }

    // [BSdbg] una vez por sesion: ver que retorna el out/rax en una
    // posicion real del mundo (para validar la interpretacion).
    static bool s_dbg = false;
    if (!s_dbg)
    {
        s_dbg = true;
        MC_LOG("[BSdbg] getBlock(%d,%d,%d): out=0x%llX outInt=%d",
               x, y, z, (unsigned long long)out,
               (int)(out & 0xFFFFFFFFULL));
    }

    // El OUT contiene el LEGACY ID del bloque en su primer int (el
    // decompile del orquestador lo usa asi: v31[0] es el byte bajo,
    // indexa la tabla qword_140FAC470 y se compara con el "aire" del
    // singleton qword_140FABC88). Interpretarlo como Block* (lo que
    // hacia el SDK) daba basura positiva: TODO parecia solido y el
    // espiral del Scaffold jamas encontraba celda colocable.
    int id = (int)(out & 0xFFFFFFFFULL);
    if (id >= 0 && id <= 4096)
        return id; // rango legacy-block-id valido
    return -1;
}

bool BlockSource::isAir(int x, int y, int z) const
{
    int id = getBlockId(x, y, z);
    return id == off::blocks::Air || id < 0;
}

}
