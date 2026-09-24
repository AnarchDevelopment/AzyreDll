#include "MoveInputHandler.hpp"

#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"

#include <excpt.h>

namespace mc {

void MoveInputHandler::click()
{
    using Fn = void(__fastcall*)(void* self);
    auto fn = reinterpret_cast<Fn>(mem::resolve(off::fn::InputWrapper1));
    if (!fn || !valid())
        return;
    void* self = reinterpret_cast<void*>(addr);
    __try
    {
        fn(self);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void MoveInputHandler::clickFast()
{
    using Fn = void(__fastcall*)(void* self);
    auto fn = reinterpret_cast<Fn>(mem::resolve(off::fn::InputWrapper2));
    if (!fn || !valid())
        return;
    void* self = reinterpret_cast<void*>(addr);
    __try
    {
        fn(self);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

}
