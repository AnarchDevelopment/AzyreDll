#include "ItemInstance.hpp"

#include <excpt.h>

namespace mc {

int ItemInstance::itemId() const
{
    uintptr_t ip = item();
    if (!ip || !mem::isReadable(ip + off::item::LegacyId, sizeof(int16_t)))
        return -1;

    int result = -1;
    __try
    {
        result = static_cast<int>(mem::read<int16_t>(ip + off::item::LegacyId));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = -1;
    }
    return result;
}

}
