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
