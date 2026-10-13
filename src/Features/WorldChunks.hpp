#pragma once

// ============================================================================
// WorldChunks — lectura de bloques por chunks (LevelChunk).
//
// Cadena: BlockSource +0x18 -> ChunkSource (PlayerChunkSource). De ahi salen
// dos fuentes de chunks:
//     cs +0x18 -> (FlatLevelSource / etc)
//     cs +0x20 -> MainChunkSource (local)  |  NetworkChunkSource (servidores)
//
// El std::unordered_map<ChunkPos, LevelChunk*> NO tiene un offset fijo (en local
// esta en la fuente +0x38; en servidor, en NetworkChunkSource +0x88). Por eso se
// AUTO-DETECTA: se prueban ambas fuentes y varios offsets buscando un mapa cuyos
// nodos apunten a LevelChunks validos. El resultado se cachea.
//
// Nodo: _Next@0x00, chunkX@0x10, chunkZ@0x14, LevelChunk*@0x20.
// LevelChunk: chunkX@0x28, chunkZ@0x2C, state@0x80, bloques@2704 (u8[32768]).
// ============================================================================

#include "Framework/Memory.hpp"
#include "SDK/Classes/BlockSource.hpp"
#include "SDK/Offsets.hpp"

#include <cstdint>
#include <vector>

namespace mc::world {

struct ChunkRef
{
    int cx = 0;
    int cz = 0;
    uintptr_t addr = 0;
};

inline bool validChunkPtr(uintptr_t chunk, int& cx, int& cz)
{
    if (!chunk || !mem::isReadable(chunk + off::chunk::Z + 4, 4))
        return false;
    cx = mem::read<int>(chunk + off::chunk::X);
    cz = mem::read<int>(chunk + off::chunk::Z);
    return cx > -30000 && cx < 30000 && cz > -30000 && cz < 30000;
}

// Recorre un candidato de mapa (head sentinela + nodos) y cuenta chunks validos.
inline int probeMap(uintptr_t source, uintptr_t mapOff, uintptr_t& headOut)
{
    uintptr_t head = mem::read<uintptr_t>(source + mapOff);
    headOut = head;
    if (head < 0x10000 || head > 0x7FFFFFFFFFFF || !mem::isReadable(head, 0x30))
        return 0;

    uintptr_t node = mem::read<uintptr_t>(head + off::chunk::NodeNext);
    int good = 0;
    for (int i = 0; i < 8 && node && node != head; ++i)
    {
        if (!mem::isReadable(node, off::chunk::NodeValue + sizeof(uintptr_t)))
            break;
        uintptr_t chunk = mem::read<uintptr_t>(node + off::chunk::NodeValue);
        int cx = 0, cz = 0;
        if (validChunkPtr(chunk, cx, cz))
            ++good;
        uintptr_t next = mem::read<uintptr_t>(node + off::chunk::NodeNext);
        if (next == node)
            break;
        node = next;
    }
    return good;
}

// Localiza la fuente + offset del mapa de chunks (cacheado).
inline bool findChunkMap(const BlockSource& bs, uintptr_t& sourceOut, uintptr_t& mapOffOut)
{
    if (!bs.address())
        return false;

    uintptr_t cs = mem::read<uintptr_t>(bs.address() + off::chunk::BlockSourceChunkSource);
    if (!cs)
        return false;

    uintptr_t sources[2] = {
        mem::read<uintptr_t>(cs + 0x18),
        mem::read<uintptr_t>(cs + 0x20),
    };

    for (uintptr_t ctx : sources)
    {
        if (!ctx || !mem::isReadable(ctx, 0x100))
            continue;
        for (uintptr_t off = 0; off <= 0x200; off += 8)
        {
            uintptr_t head = 0;
            if (probeMap(ctx, off, head) >= 2)
            {
                sourceOut = ctx;
                mapOffOut = off;
                return true;
            }
        }
    }
    return false;
}

inline bool enumerateChunks(const BlockSource& bs, std::vector<ChunkRef>& out)
{
    out.clear();

    static uintptr_t s_source = 0;
    static uintptr_t s_off = 0;

    // Revalidar el cache: el primer nodo debe seguir apuntando a un LevelChunk.
    bool cacheOk = false;
    if (s_source && s_off)
    {
        uintptr_t head = mem::read<uintptr_t>(s_source + s_off);
        uintptr_t node = head ? mem::read<uintptr_t>(head + off::chunk::NodeNext) : 0;
        uintptr_t chunk = node ? mem::read<uintptr_t>(node + off::chunk::NodeValue) : 0;
        int cx = 0, cz = 0;
        cacheOk = validChunkPtr(chunk, cx, cz);
    }

    if (!cacheOk)
    {
        if (!findChunkMap(bs, s_source, s_off))
        {
            s_source = 0;
            s_off = 0;
            return false;
        }
    }

    uintptr_t head = mem::read<uintptr_t>(s_source + s_off);
    if (!head)
        return false;

    uintptr_t node = mem::read<uintptr_t>(head + off::chunk::NodeNext);
    int guard = 0;
    while (node && node != head && guard < 5000)
    {
        ++guard;
        if (!mem::isReadable(node, off::chunk::NodeValue + sizeof(uintptr_t)))
            break;

        uintptr_t chunk = mem::read<uintptr_t>(node + off::chunk::NodeValue);
        int cx = 0, cz = 0;
        if (validChunkPtr(chunk, cx, cz))
            out.push_back({cx, cz, chunk});

        uintptr_t next = mem::read<uintptr_t>(node + off::chunk::NodeNext);
        if (next == node)
            break;
        node = next;
    }

    return !out.empty();
}

inline bool readChunkBlocks(uintptr_t chunkAddr, uint8_t* buf)
{
    if (!chunkAddr || !buf)
        return false;
    return mem::rawRead(buf, chunkAddr + off::chunk::Blocks, off::chunk::Volume);
}

inline int blockIndex(int xLocal, int zLocal, int y)
{
    return (zLocal + 16 * xLocal) * off::chunk::Height + y;
}

// Resuelve (y cachea) la fuente + offset del mapa de chunks.
inline bool resolveChunkMap(const BlockSource& bs, uintptr_t& source, uintptr_t& off)
{
    static uintptr_t s_source = 0;
    static uintptr_t s_off = 0;

    if (s_source && s_off)
    {
        uintptr_t head = mem::read<uintptr_t>(s_source + s_off);
        uintptr_t node = head ? mem::read<uintptr_t>(head + off::chunk::NodeNext) : 0;
        uintptr_t chunk = node ? mem::read<uintptr_t>(node + off::chunk::NodeValue) : 0;
        int cx = 0, cz = 0;
        if (validChunkPtr(chunk, cx, cz))
        {
            source = s_source;
            off = s_off;
            return true;
        }
    }

    if (!findChunkMap(bs, s_source, s_off))
    {
        s_source = 0;
        s_off = 0;
        return false;
    }
    source = s_source;
    off = s_off;
    return true;
}

// Lee un bloque individual (id numerico). Devuelve -1 si el chunk no esta
// cargado y 0 si es aire.
inline int getBlockId(const BlockSource& bs, int x, int y, int z)
{
    if (y < 0 || y > 255)
        return 0;

    uintptr_t source = 0, off = 0;
    if (!resolveChunkMap(bs, source, off))
        return -1;

    int cx = x >> 4;
    int cz = z >> 4;

    uintptr_t head = mem::read<uintptr_t>(source + off);
    uintptr_t node = head ? mem::read<uintptr_t>(head + off::chunk::NodeNext) : 0;
    int guard = 0;
    while (node && node != head && guard < 5000)
    {
        ++guard;
        uintptr_t chunk = mem::read<uintptr_t>(node + off::chunk::NodeValue);
        int ccx = 0, ccz = 0;
        if (validChunkPtr(chunk, ccx, ccz) && ccx == cx && ccz == cz)
        {
            uint8_t b = 0;
            uintptr_t addr = chunk + off::chunk::Blocks +
                             blockIndex(x & 15, z & 15, y);
            if (!mem::rawRead(&b, addr, 1))
                return -1;
            return (int)b;
        }
        uintptr_t next = mem::read<uintptr_t>(node + off::chunk::NodeNext);
        if (next == node)
            break;
        node = next;
    }
    return -1;
}

inline bool isSolid(const BlockSource& bs, int x, int y, int z)
{
    int id = getBlockId(bs, x, y, z);
    return id > 0 && id != 8 && id != 10; // no aire, no agua/lava
}

// Lee un bloque a partir de una lista de chunks ya enumerada (via
// enumerateChunks). Evita recorrer el hash map en cada lectura.
inline int getBlockIdFromChunks(const std::vector<ChunkRef>& chunks, int x, int y, int z)
{
    if (y < 0 || y > 255)
        return 0;
    int cx = x >> 4;
    int cz = z >> 4;
    for (const ChunkRef& c : chunks)
    {
        if (c.cx == cx && c.cz == cz)
        {
            uint8_t b = 0;
            uintptr_t a = c.addr + off::chunk::Blocks + blockIndex(x & 15, z & 15, y);
            if (!mem::rawRead(&b, a, 1))
                return -1;
            return (int)b;
        }
    }
    return -1;
}

}
