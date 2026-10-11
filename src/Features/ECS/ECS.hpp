#pragma once

// ============================================================================
// ECS — Singleton con el registry + dispatcher de EnTT.
//
// Flujo:
//   Game::update() → ecs::sync(game) → components actualizados
//   Module::onRender() → ecs::get().registry.view<Transform, PlayerName>()
//   Hooks → ecs::get().dispatcher.enqueue<HurtEvent>(...) → update()
//
// Los modules NUNCA tocan mem::read directamente para iterar players.
// ============================================================================

#include "Components.hpp"
#include "Events.hpp"

#include "Features/Friends.hpp"
#include "SDK/Game.hpp"

#include <entt/entt.hpp>

#include <windows.h>

#include <cmath>
#include <mutex>
#include <unordered_map>

namespace mc::ecs {

class ECSSystem
{
public:
    static ECSSystem& get()
    {
        static ECSSystem instance;
        return instance;
    }

    entt::registry registry;
    entt::dispatcher dispatcher;

    // Registry sinks: on_construct/on_destroy of PlayerName translate the
    // entity lifecycle (join/leave the render distance) into dispatcher
    // events so modules can subscribe without diffing lists themselves.
    void connectSignals()
    {
        if (signalsConnected_)
            return;
        signalsConnected_ = true;
        registry.on_construct<PlayerName>().connect<&ECSSystem::onNameConstructed>(*this);
        registry.on_destroy<PlayerName>().connect<&ECSSystem::onNameDestroyed>(*this);
    }

    void onNameConstructed(entt::registry& reg, entt::entity e)
    {
        if (suppressSignals_)
            return;
        if (e == local_ || reg.all_of<IsLocal>(e))
            return;
        dispatcher.enqueue<PlayerJoined>(e, reg.get<PlayerName>(e).value);
    }

    void onNameDestroyed(entt::registry& reg, entt::entity e)
    {
        if (suppressSignals_)
            return;
        const auto* n = reg.try_get<PlayerName>(e);
        if (!n || n->value.empty())
            return;
        // The local player's entity keeps its name after handleLostLocal
        // (only the tags are removed); skip it when it finally gets pruned.
        if (e == local_ || n->value == lastLocalName_)
            return;
        dispatcher.enqueue<PlayerLeft>(n->value);
    }

    // Llamar una vez por frame DESPUÉS de Game::update().
    // Sincroniza el estado del scanner con el registry.
    void sync(Game& game)
    {
        connectSignals();

        if (!game.localFound())
        {
            handleLostLocal(game);
            return;
        }

        LocalPlayer local = game.localPlayer();
        syncLocal(local);

        for (const Actor& other : game.remotePlayers())
        {
            if (!other.valid())
                continue;
            syncRemote(other);
        }

        pruneStale(game);
    }

    // ========================================================================
    // Bloques — sincroniza el registry de bloques alrededor del local player.
    // Escaneo throttled (kBlockScanMs) con un volumen acotado; emite
    // BlockPlaced/Broken. Cualquier modulo puede leer el view de bloques.
    // ========================================================================
    void setBlocksEnabled(bool on) { blocksEnabled_ = on; }
    bool blocksEnabled() const { return blocksEnabled_; }

    entt::entity findBlock(const BlockPos& pos)
    {
        auto it = blockToEntity_.find(pos);
        return it != blockToEntity_.end() ? it->second : entt::null;
    }

    size_t blockCount() const { return blockToEntity_.size(); }

    // Lo llama el hook de colocacion (BlockItem::useOn), que corre en el hilo
    // del juego: solo encola (thread-safe). El registry lo absorbe en el hilo
    // de Present a traves de drainBlockPlacements().
    void reportBlockPlaced(const BlockPos& pos, int id)
    {
        std::lock_guard<std::mutex> lock(placedQueueMutex_);
        placedQueue_.emplace_back(pos, id);
    }

    void clearBlocks()
    {
        std::vector<entt::entity> all;
        registry.view<BlockCoord>().each([&](entt::entity e, BlockCoord&) {
            all.push_back(e);
        });
        for (entt::entity e : all)
            registry.destroy(e);
        blockToEntity_.clear();
    }

    void syncBlocks(Game& game)
    {
        drainBlockPlacements();

        if (!blocksEnabled_ || !game.localFound())
            return;

        unsigned long long now = GetTickCount64();
        if (now - lastBlockScanMs_ < kBlockScanMs)
            return;
        lastBlockScanMs_ = now;

        BlockSource bs = game.blockSource();
        if (!bs.valid())
            return;

        Vec3 pp = game.localPlayer().pos();
        int cx = (int)std::floor(pp.x);
        int cy = (int)std::floor(pp.y);
        int cz = (int)std::floor(pp.z);

        for (int dx = -kBlockRadiusXZ; dx <= kBlockRadiusXZ; ++dx)
        {
            for (int dz = -kBlockRadiusXZ; dz <= kBlockRadiusXZ; ++dz)
            {
                for (int dy = -kBlockRadiusY; dy <= kBlockRadiusY; ++dy)
                {
                    int bx = cx + dx, by = cy + dy, bz = cz + dz;
                    int id = bs.getBlockId(bx, by, bz);
                    if (id <= 0)
                        continue;

                    BlockPos p{bx, by, bz};
                    auto it = blockToEntity_.find(p);
                    if (it == blockToEntity_.end())
                    {
                        entt::entity e = registry.create();
                        blockToEntity_[p] = e;
                        registry.emplace<BlockCoord>(e, BlockCoord{p});
                        registry.emplace<BlockData>(e, BlockData{id, now});
                        dispatcher.enqueue<BlockPlacedEvent>(e, p, id);
                    }
                    else
                    {
                        auto* bd = registry.try_get<BlockData>(it->second);
                        if (bd)
                        {
                            bd->id = id;
                            bd->lastSeenMs = now;
                        }
                        else
                        {
                            registry.emplace<BlockData>(it->second, BlockData{id, now});
                        }
                    }
                }
            }
        }

        // Roturas / fuera de rango: revisar los bloques ya rastreados.
        std::vector<entt::entity> dead;
        registry.view<BlockCoord, BlockData>().each(
            [&](entt::entity e, BlockCoord& c, BlockData& d) {
                int ax = std::abs(c.pos.x - cx);
                int az = std::abs(c.pos.z - cz);
                int ay = std::abs(c.pos.y - cy);
                if (ax > kBlockRadiusXZ + 1 || az > kBlockRadiusXZ + 1 ||
                    ay > kBlockRadiusY + 1)
                {
                    dead.push_back(e);
                    return;
                }
                if (d.lastSeenMs == now)
                    return; // visto en este scan
                int id = bs.getBlockId(c.pos.x, c.pos.y, c.pos.z);
                if (id <= 0)
                {
                    dispatcher.enqueue<BlockBrokenEvent>(c.pos, d.id);
                    dead.push_back(e);
                }
                else
                {
                    d.id = id;
                    d.lastSeenMs = now;
                }
            });

        for (entt::entity e : dead)
            destroyBlock(e);
    }

    // View helpers
    entt::entity local()
    {
        return local_;
    }

    bool hasLocal() const
    {
        return local_ != entt::null;
    }

    entt::entity findByAddress(uintptr_t addr)
    {
        auto it = addrToEntity_.find(addr);
        return it != addrToEntity_.end() ? it->second : entt::null;
    }

    entt::entity findByName(const std::string& name)
    {
        auto view = registry.view<PlayerName>();
        for (auto e : view)
        {
            if (view.get<PlayerName>(e).value == name)
                return e;
        }
        return entt::null;
    }

    void clear()
    {
        // Mass destruction (world unload/shutdown): sink noise suppressed.
        suppressSignals_ = true;
        registry.clear();
        suppressSignals_ = false;
        addrToEntity_.clear();
        local_ = entt::null;
        rawCount_ = 0;
        shownCount_ = 0;
        blockToEntity_.clear();
        lastBlockScanMs_ = 0;
    }

    size_t rawCount() const { return rawCount_; }
    size_t shownCount() const { return shownCount_; }

private:
    ECSSystem() = default;

    std::unordered_map<uintptr_t, entt::entity> addrToEntity_;
    entt::entity local_ = entt::null;
    size_t rawCount_ = 0;
    size_t shownCount_ = 0;
    bool signalsConnected_ = false;
    bool suppressSignals_ = false;
    std::string lastLocalName_;

    // ---- Bloques ----
    static constexpr int kBlockRadiusXZ = 4;
    static constexpr int kBlockRadiusY = 3;
    static constexpr unsigned long long kBlockScanMs = 200;
    std::unordered_map<BlockPos, entt::entity, BlockPosHash> blockToEntity_;
    unsigned long long lastBlockScanMs_ = 0;
    bool blocksEnabled_ = true;
    std::mutex placedQueueMutex_;
    std::vector<std::pair<BlockPos, int>> placedQueue_;

    void destroyBlock(entt::entity e)
    {
        const auto* c = registry.try_get<BlockCoord>(e);
        if (c)
            blockToEntity_.erase(c->pos);
        if (registry.valid(e))
            registry.destroy(e);
    }

    void drainBlockPlacements()
    {
        std::vector<std::pair<BlockPos, int>> pending;
        {
            std::lock_guard<std::mutex> lock(placedQueueMutex_);
            pending.swap(placedQueue_);
        }
        if (pending.empty())
            return;

        unsigned long long now = GetTickCount64();
        for (const auto& [pos, id] : pending)
        {
            entt::entity e = entt::null;
            auto it = blockToEntity_.find(pos);
            if (it == blockToEntity_.end())
            {
                e = registry.create();
                blockToEntity_[pos] = e;
                registry.emplace<BlockCoord>(e, BlockCoord{pos});
            }
            else
            {
                e = it->second;
            }

            registry.emplace_or_replace<BlockData>(e, BlockData{id, now});
            registry.emplace_or_replace<BlockPlacedByLocal>(e, BlockPlacedByLocal{now});
            dispatcher.enqueue<BlockPlacedEvent>(e, pos, id);
        }
    }

    entt::entity getOrCreate(uintptr_t addr)
    {
        auto it = addrToEntity_.find(addr);
        if (it != addrToEntity_.end())
            return it->second;

        auto e = registry.create();
        addrToEntity_[addr] = e;
        return e;
    }

    void syncLocal(LocalPlayer& local)
    {
        uintptr_t addr = local.address();

        if (local_ == entt::null || !registry.valid(local_))
        {
            // LocalPlayer encontrado por primera vez (o tras perderlo)
            local_ = getOrCreate(addr);
            registry.emplace_or_replace<IsLocal>(local_);
            registry.emplace<PlayerName>(local_, PlayerName{local.name()});
            registry.emplace<Health>(local_, Health{local.health()});
            registry.emplace<Transform>(local_);
            registry.emplace<ScreenPos>(local_);
            registry.emplace_or_replace<IsHurt>(local_, IsHurt{0});
            dispatcher.enqueue<LocalPlayerFound>(local_);
        }
        else if (registry.get_or_emplace<Transform>(local_).pos.x == 0.0f)
        {
            // Entity reutilizada: re-tag como local
            registry.emplace_or_replace<IsLocal>(local_);
        }

        // Actualizar components del local
        auto& t = registry.get_or_emplace<Transform>(local_);
        t.pos = local.pos();
        t.yaw = local.yaw();
        t.pitch = local.pitch();
        t.height = local.height();
        t.onGround = local.onGround();
        t.velocity = local.velocity();

        if (auto* h = registry.try_get<Health>(local_))
            h->current = local.health();
        else
            registry.emplace<Health>(local_, Health{local.health()});

        if (auto* n = registry.try_get<PlayerName>(local_))
        {
            n->value = local.name();
            lastLocalName_ = n->value;
        }
        else
            registry.emplace<PlayerName>(local_, PlayerName{local.name()});

        if (local.isHurt())
        {
            registry.emplace_or_replace<IsHurt>(local_, IsHurt{local.hurtTime()});
            dispatcher.enqueue<HurtEvent>(local_, 0.0f);
        }
        else if (registry.try_get<IsHurt>(local_))
        {
            registry.remove<IsHurt>(local_);
        }
    }

    void syncRemote(const Actor& actor)
    {
        uintptr_t addr = actor.address();
        if (addr == 0)
            return;

        bool isFriend = friends::isFriend(actor.name());

        auto e = getOrCreate(addr);

        // Components base
        auto& t = registry.get_or_emplace<Transform>(e);
        t.pos = actor.pos();
        t.yaw = actor.yaw();
        t.pitch = actor.pitch();
        t.height = actor.height();
        t.onGround = actor.onGround();

        if (auto* h = registry.try_get<Health>(e))
            h->current = actor.health();
        else
            registry.emplace<Health>(e, Health{actor.health()});

        if (auto* n = registry.try_get<PlayerName>(e))
            n->value = actor.name();
        else
            registry.emplace<PlayerName>(e, PlayerName{actor.name()});

        if (isFriend)
            registry.emplace_or_replace<IsFriend>(e);
        else
            registry.remove<IsFriend>(e);

        if (!registry.try_get<ScreenPos>(e))
            registry.emplace<ScreenPos>(e);

        // Historial de posiciones (una entrada por sync = un frame de juego,
        // que a 20 tps coincide con un tick aproximado).
        auto& hist = registry.get_or_emplace<HistoryPos>(e);
        hist.push(t.pos, GetTickCount64());

        ++rawCount_;
        if (!isFriend || true) // shownCount se calcula en el caller si hace falta
            ++shownCount_;
    }

    void handleLostLocal(Game& game)
    {
        (void)game;
        if (local_ != entt::null && registry.valid(local_))
        {
            registry.remove<IsLocal>(local_);
            registry.remove<IsHurt>(local_);
            dispatcher.enqueue<LocalPlayerLost>();
        }
        local_ = entt::null;
    }

    void pruneStale(Game& game)
    {
        (void)game;
        // Eliminar entities cuya direccion ya no es valida
        std::vector<entt::entity> dead;
        for (auto& [addr, e] : addrToEntity_)
        {
            if (!registry.valid(e))
                continue;
            if (!mem::isReadable(addr, 8))
            {
                dead.push_back(e);
            }
        }
        for (auto e : dead)
        {
            // Buscar la direccion para borrar del map
            for (auto it = addrToEntity_.begin(); it != addrToEntity_.end(); ++it)
            {
                if (it->second == e)
                {
                    addrToEntity_.erase(it);
                    break;
                }
            }
            registry.destroy(e);
        }
    }
};

// Convenience: acceso directo al singleton
inline ECSSystem& get()
{
    return ECSSystem::get();
}

}
