#pragma once

// ============================================================================
// Components — datos que el ECS adjunta a las entities. Son structs POD
// (o casi) para maximizar cache locality en los views.
//
// Los modules NUNCA leen offsets del juego directamente: el ECS se sincroniza
// desde el scanner del Game (una vez por frame) y los modules consumen
// components via registry.view<>().
// ============================================================================

#include "Framework/Math.hpp"

#include <imgui.h>
#include <string>

namespace mc::ecs {

struct Transform
{
    Vec3 pos;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float height = 1.8f;
    bool onGround = false;
    Vec3 velocity;
};

struct PlayerName
{
    std::string value;
};

struct Health
{
    int current = 20;
};

struct IsLocal
{
};

struct IsFriend
{
};

struct IsHurt
{
    int time = 0;
};

// Cache de proyección (lo llena el sistema de render, no el module):
// evita que ESP/NameTags/Tracers calculen worldToScreen por separado.
struct ScreenPos
{
    ImVec2 pos;
    bool visible = false;
};

// Ring buffer de posiciones pasadas (lo llena el sync cada tick):
// apuntar a donde ESTABAN los players, no donde estan. El ECS siempre
// mantiene el historial; los modules deciden si lo aplican o no.
struct HistoryPos
{
    static constexpr int kMax = 100; // 5 segundos a 20 tps

    struct Entry
    {
        Vec3 pos;
        ULONGLONG tick;
    };

    Entry entries[kMax] = {};
    int head = 0;   // proximo slot a escribir
    int filled = 0; // entradas validas

    void push(const Vec3& p, ULONGLONG now)
    {
        entries[head].pos = p;
        entries[head].tick = now;
        head = (head + 1) % kMax;
        if (filled < kMax)
            ++filled;
    }

    // Posicion de hace `ticksAgo` ticks (nullptr si no hay historial).
    const Entry* at(int ticksAgo) const
    {
        if (ticksAgo < 1 || ticksAgo > filled)
            return nullptr;
        int slot = (head - ticksAgo + kMax * 2) % kMax;
        return &entries[slot];
    }

    void clear()
    {
        head = 0;
        filled = 0;
    }
};

// ============================================================================
// Bloques — registro EnTT de bloques del mundo (paralelo a las players).
//
// Los bloques viven como entities con BlockCoord (posicion) + BlockData (id).
// El sync los escanea alrededor del local player y emite BlockPlaced/Broken.
// Un modulo futuro (BlockESP, Scaffold, pattern detection...) consume el view
// <BlockCoord, BlockData> en vez de leer offsets de bloques a mano.
// ============================================================================

struct BlockCoord
{
    BlockPos pos;
};

struct BlockData
{
    int id = -1;
    unsigned long long lastSeenMs = 0;
};

// Marca de bloque colocado por el jugador local (via hook de BlockItem::useOn).
struct BlockPlacedByLocal
{
    unsigned long long timeMs = 0;
};

}
