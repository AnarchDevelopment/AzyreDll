#pragma once

#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"

namespace mc {

// Envoltura minima del Block* devuelto por BlockSource::getBlock. El layout
// del objeto no esta confirmado en este build: id() prueba los offsets
// candidatos de off::block y devuelve el primer valor plausible (o -1).
class Block
{
public:
    Block() = default;
    explicit Block(uintptr_t a) : addr(a) {}

    bool valid() const { return addr && mem::isReadable(addr, 0x40); }
    uintptr_t address() const { return addr; }

    int id() const;

    bool isAir() const { return id() == off::blocks::Air; }
    bool isLiquid() const
    {
        int i = id();
        return i == off::blocks::Water || i == off::blocks::Lava;
    }

private:
    uintptr_t addr = 0;
};

}
