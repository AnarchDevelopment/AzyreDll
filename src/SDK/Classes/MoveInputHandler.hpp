#pragma once

#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

namespace mc {

class MoveInputHandler
{
public:
    MoveInputHandler() = default;
    explicit MoveInputHandler(uintptr_t a) : addr(a) {}

    bool valid() const
    {
        if (!addr || !mem::isReadable(addr, sizeof(uintptr_t)))
            return false;
        return mem::read<uintptr_t>(addr) == mem::resolve(vt::MoveInputHandler);
    }
    uintptr_t address() const { return addr; }

    uintptr_t innerAddr() const { return mem::read<uintptr_t>(addr + off::input::InnerHandler); }

    int clickCooldown() const
    {
        uintptr_t inner = innerAddr();
        if (!inner || !mem::isReadable(inner + off::input::InnerClickCooldown, sizeof(int)))
            return -1;
        return mem::read<int>(inner + off::input::InnerClickCooldown);
    }

    void setClickCooldown(int ms)
    {
        uintptr_t inner = innerAddr();
        if (inner && mem::isReadable(inner + off::input::InnerClickCooldown, sizeof(int)))
            mem::write<int>(inner + off::input::InnerClickCooldown, ms);
    }

    void resetClickCounter()
    {
        uintptr_t inner = innerAddr();
        if (inner && mem::isReadable(inner + off::input::InnerClickCounter, sizeof(int)))
            mem::write<int>(inner + off::input::InnerClickCounter, 0);
    }

    void click();
    void clickFast();

private:
    uintptr_t addr = 0;
};

}
