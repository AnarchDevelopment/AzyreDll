#include "Block.hpp"

#include <excpt.h>

namespace mc {

namespace {

// El offset of the block id is the same for every Block object of this build:
// once a candidate offset validates, remember it so subsequent lookups cost a
// single read instead of scanning every candidate.
uintptr_t g_cachedIdOffset = (uintptr_t)-1;

} // namespace

int Block::id() const
{
    if (!addr)
        return -1;

    int result = -1;
    __try
    {
        if (g_cachedIdOffset != (uintptr_t)-1 &&
            mem::isReadable(addr + g_cachedIdOffset, sizeof(int)))
        {
            int v = mem::read<int>(addr + g_cachedIdOffset);
            if (v >= off::block::IdMin && v <= off::block::IdMax)
                return v;
        }

        for (uintptr_t off : off::block::IdCandidates)
        {
            if (!mem::isReadable(addr + off, sizeof(int)))
                continue;
            int v = mem::read<int>(addr + off);
            if (v >= off::block::IdMin && v <= off::block::IdMax)
            {
                g_cachedIdOffset = off;
                result = v;
                break;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = -1;
    }
    return result;
}

}
