#pragma once

#include "Framework/Memory.hpp"
#include "Framework/Math.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"


namespace mc {

class Client
{
public:
    Client() = default;
    explicit Client(uintptr_t a) : addr(a) {}

    bool valid() const
    {
        if (!addr || !mem::isReadable(addr, sizeof(uintptr_t)))
            return false;
        return mem::read<uintptr_t>(addr) == mem::resolve(vt::MinecraftClient);
    }
    uintptr_t address() const { return addr; }

    // The active-screen stack is a std::vector<std::shared_ptr<Screen>> in
    // the Minecraft client object. sub_14004A9B0 uses +0x148 as begin and
    // +0x150 as end; the last shared_ptr owns the visible screen. Read only
    // the vector metadata and validate the object vtable. Do not dispatch a
    // screen virtual here: the name getter is not a Client method.
    bool chatOpen() const
    {
        if (!valid())
            return false;

        uintptr_t begin = 0;
        uintptr_t end = 0;
        if (!mem::rawRead(&begin, addr + 0x148, sizeof(begin)) ||
            !mem::rawRead(&end, addr + 0x150, sizeof(end)))
            return false;
        if (!begin || end <= begin || end - begin < 0x10 ||
            end - begin > 0x1000 || ((end - begin) & 0x0F) != 0)
            return false;
        if (!mem::isReadable(end - 0x10, 0x10))
            return false;

        uintptr_t screen = mem::read<uintptr_t>(end - 0x10);
        if (!screen || !mem::isReadable(screen, sizeof(uintptr_t)))
            return false;
        return mem::read<uintptr_t>(screen) == mem::resolve(vt::ChatScreen);
    }

    uintptr_t rendererAddr() const { return mem::read<uintptr_t>(addr + off::chain::ClientLevelRenderer); }
    uintptr_t chatAddr() const { return mem::read<uintptr_t>(addr + off::chain::ClientChat); }

    uintptr_t dimensionAddr() const
    {
        uintptr_t renderer = rendererAddr();
        if (!renderer)
            return 0;
        return mem::read<uintptr_t>(renderer + off::chain::RendererDimension);
    }

    uintptr_t levelAddrFromDimension() const
    {
        uintptr_t dim = dimensionAddr();
        if (!dim)
            return 0;
        return mem::read<uintptr_t>(dim + off::chain::DimensionLevel);
    }

    Vec4 camera() const
    {
        uintptr_t renderer = rendererAddr();
        if (!renderer)
            return {};
        return mem::read<Vec4>(renderer + off::chain::RendererCamera);
    }

private:
    uintptr_t addr = 0;
};

}
