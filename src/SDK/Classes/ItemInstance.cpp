#include "ItemInstance.hpp"

#include <excpt.h>

namespace mc {

int ItemInstance::itemId() const
{
    uintptr_t ip = item();
    if (!ip)
        return -1;

    int result = -1;
    __try
    {
        for (uintptr_t off : off::item::IdCandidates)
        {
            if (!mem::isReadable(ip + off, sizeof(int)))
                continue;
            int v = mem::read<int>(ip + off);
            if (v >= off::item::IdMin && v <= off::item::IdMax)
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
