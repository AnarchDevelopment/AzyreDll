#pragma once

#include <cstddef>
#include <cstdint>

namespace mc::off {

// World, block and chunk layouts for the active game build.
namespace blocks
{
    constexpr int Air = 0;
    constexpr int Stone = 1;
    constexpr int Grass = 2;
    constexpr int Dirt = 3;
    constexpr int Cobblestone = 4;
    constexpr int Planks = 5;
    constexpr int Bedrock = 7;
    constexpr int Water = 8;
    constexpr int Lava = 10;
    constexpr int Sand = 12;
    constexpr int Gravel = 13;
    constexpr int GoldOre = 14;
    constexpr int IronOre = 15;
    constexpr int CoalOre = 16;
    constexpr int Log = 17;
    constexpr int Leaves = 18;
    constexpr int LapisOre = 21;
    constexpr int Sandstone = 24;
    constexpr int Chest = 54;
    constexpr int DiamondOre = 56;
    constexpr int RedstoneOre = 73;
    constexpr int EmeraldOre = 129;
    constexpr int EnderChest = 130;
    constexpr int AncientDebris = 526;
    constexpr int CopperOre = 566;
}

namespace block
{
    inline constexpr uintptr_t IdCandidates[] = {
        0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24,
        0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C};
    inline constexpr int IdMin = 0;
    inline constexpr int IdMax = 4096;
}

namespace chunk
{
    constexpr uintptr_t BlockSourceChunkSource = 0x18; // BlockSource -> ChunkSource
    constexpr uintptr_t ChunkSourceMain = 0x20;        // ChunkSource -> MainChunkSource
    constexpr uintptr_t MainChunkSourceHashMap = 0x38; // hash_map de chunks

    // std::unordered_map (layout de este build)
    constexpr uintptr_t HashMapHead = 0x00; // _Myhead (nodo centinela)
    constexpr uintptr_t HashMapSize = 0x08; // _Mysize (numero de chunks)

    // Nodo de la lista
    constexpr uintptr_t NodeNext = 0x00;  // _Next
    constexpr uintptr_t NodeKeyX = 0x10;  // chunkX (key)
    constexpr uintptr_t NodeKeyZ = 0x14;  // chunkZ (key)
    constexpr uintptr_t NodeValue = 0x20; // LevelChunk*

    // LevelChunk
    constexpr uintptr_t X = 0x28;      // chunkX
    constexpr uintptr_t Z = 0x2C;      // chunkZ
    constexpr uintptr_t State = 0x80;  // state (>= 2 = cargado)
    constexpr uintptr_t Blocks = 2704; // 0xA90 — u8[32768] (Y 0..127)

    constexpr int BoundsXZ = 16;
    constexpr int Height = 128;
    constexpr int Volume = 16 * 16 * 128;
}

}
