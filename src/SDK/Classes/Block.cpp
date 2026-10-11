#include "Block.hpp"

#include <excpt.h>

namespace mc {

int Block::id() const
{
    if (!addr)
        return -1;

    int result = -1;
    __try
    {
        for (uintptr_t off : off::block::IdCandidates)
        {
            if (!mem::isReadable(addr + off, sizeof(int)))
                continue;
            int v = mem::read<int>(addr + off);
            if (v >= off::block::IdMin && v <= off::block::IdMax)
            {
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
