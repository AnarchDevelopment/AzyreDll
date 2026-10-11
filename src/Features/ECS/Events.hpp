#pragma once

// ============================================================================
// Events — structs que el dispatcher emite y a los que los modules se
// suscriben con dispatcher.sink<Event>().connect<&Class::onEvent>(this).
//
// Ventaja sobre virtual methods: un module solo escucha lo que le importa
// y añadir un event nuevo NO toca Module.hpp ni ModuleManager.cpp.
// ============================================================================

#include <entt/entt.hpp>

#include "Framework/Math.hpp"

namespace mc::ecs {

struct TickEvent
{
    float dt;
};

struct RenderEvent
{
    float dt;
};

struct HurtEvent
{
    entt::entity actor;
    float damage;
};

struct ChatMessageEvent
{
    entt::entity sender;
    std::string text;
};

struct CommandEvent
{
    std::string command;
};

struct LocalPlayerFound
{
    entt::entity entity;
};

struct LocalPlayerLost
{
};

// Player joined / left the render distance. Emitted by the registry sinks
// (on_construct/on_destroy of PlayerName) during sync — modules subscribe
// via dispatcher.sink<PlayerJoined>().connect<&Class::fn>(*this).
struct PlayerJoined
{
    entt::entity entity;
    std::string name;
};

struct PlayerLeft
{
    std::string name;
};

// ---- Bloques ----
// BlockPlaced: aparece un bloque no-aire en el scan (o lo coloco el local).
// BlockBroken: un bloque rastreado paso a aire.
struct BlockPlacedEvent
{
    entt::entity entity;
    BlockPos pos;
    int id;
};

struct BlockBrokenEvent
{
    BlockPos pos;
    int id;
};

}
