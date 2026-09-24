#include "Game.hpp"

#include "Framework/Log.hpp"
#include "Features/BotFilter.hpp"

#include <cmath>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace mc {

static bool validPos(float x, float y, float z)
{
    if (std::fabs(x) < 0.001f && std::fabs(y) < 0.001f && std::fabs(z) < 0.001f)
        return false;
    return x > -3.0e7f && x < 3.0e7f && y > -64.0f && y < 320.0f &&
           z > -3.0e7f && z < 3.0e7f;
}

static bool validAng(float yaw, float pitch)
{
    if (!std::isfinite(yaw) || !std::isfinite(pitch))
        return false;
    return pitch >= -90.5f && pitch <= 90.5f;
}

static bool isAliveActor(uintptr_t p)
{
    float height = 0.0f;
    if (mem::rawRead(&height, p + off::player::Height, sizeof(height)) && height < 0.4f)
        return false;
    int health = 0;
    if (mem::rawRead(&health, p + off::player::Health, sizeof(health)) && health <= 0)
        return false;
    return true;
}

static std::function<bool(uintptr_t)> actorValidator(uintptr_t expectedVtableRva, bool requireAlive)
{
    return [expectedVtableRva, requireAlive](uintptr_t p) -> bool {
        if ((p & 0xF) != 0)
            return false;
        if (!mem::isReadable(p, 0xE40))
            return false;
        if (mem::read<uintptr_t>(p) != mem::resolve(expectedVtableRva))
            return false;

        float x = mem::read<float>(p + off::player::PosX);
        float y = mem::read<float>(p + off::player::PosY);
        float z = mem::read<float>(p + off::player::PosZ);
        if (!validPos(x, y, z))
            return false;

        float yaw = mem::read<float>(p + off::player::Yaw);
        float pitch = mem::read<float>(p + off::player::Pitch);
        if (!validAng(yaw, pitch))
            return false;

        if (requireAlive && !isAliveActor(p))
            return false;

        return true;
    };
}

namespace {

struct ScanPacket
{
    uintptr_t local = 0;
    std::vector<uintptr_t> remotes;
    std::vector<uintptr_t> remotesAlt;
};

struct ScannerState
{
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    bool stop = false;
    bool pending = false;
    bool running = false;
    bool resultReady = false;
    ScanPacket result;
};

ScannerState g_scan;

ScanPacket runScanJob()
{
    ScanPacket packet;

    mem::ValidateFn validateLocal = actorValidator(vt::LocalPlayer, false);
    mem::ValidateFn validateRemote = actorValidator(vt::RemotePlayer, true);
    mem::ValidateFn validateRemoteAlt = actorValidator(vt::RemotePlayerAlt, true);

    std::vector<mem::VtableQuery> queries = {
        {vt::LocalPlayer, &validateLocal, 1},
        {vt::RemotePlayer, &validateRemote, 24},
        {vt::RemotePlayerAlt, &validateRemoteAlt, 24},
    };

    std::vector<std::vector<uintptr_t>> out;
    mem::scanMulti(queries, out);

    if (!out[0].empty())
        packet.local = out[0][0];
    packet.remotes = std::move(out[1]);
    packet.remotesAlt = std::move(out[2]);
    return packet;
}

void scanThreadMain()
{
    std::unique_lock<std::mutex> lock(g_scan.mutex);
    for (;;)
    {
        g_scan.cv.wait(lock, [] { return g_scan.pending || g_scan.stop; });
        if (g_scan.stop)
            break;

        g_scan.pending = false;
        g_scan.running = true;
        lock.unlock();

        ScanPacket packet = runScanJob();

        lock.lock();
        g_scan.result = std::move(packet);
        g_scan.resultReady = true;
        g_scan.running = false;
    }
}

bool workerIdle()
{
    std::lock_guard<std::mutex> lock(g_scan.mutex);
    return !g_scan.pending && !g_scan.running;
}

void requestScan()
{
    std::lock_guard<std::mutex> lock(g_scan.mutex);
    if (g_scan.pending || g_scan.running)
        return;
    g_scan.pending = true;
    g_scan.cv.notify_one();
}

void startScanner()
{
    std::lock_guard<std::mutex> lock(g_scan.mutex);
    if (g_scan.thread.joinable())
        return;
    g_scan.stop = false;
    g_scan.running = false;
    g_scan.resultReady = false;
    g_scan.thread = std::thread(scanThreadMain);
}

void stopScanner()
{
    {
        std::lock_guard<std::mutex> lock(g_scan.mutex);
        g_scan.stop = true;
        g_scan.pending = true;
    }
    g_scan.cv.notify_one();
    if (g_scan.thread.joinable())
        g_scan.thread.join();

    std::lock_guard<std::mutex> lock(g_scan.mutex);
    g_scan.pending = false;
    g_scan.running = false;
    g_scan.resultReady = false;
    g_scan.result = {};
    g_scan.stop = false;
}

}

Game& Game::get()
{
    static Game instance;
    return instance;
}

bool Game::isValidLocal(uintptr_t p) const
{
    if (!p || (p & 0xF) != 0)
        return false;
    if (!mem::isReadable(p, 0xE40))
        return false;
    if (mem::read<uintptr_t>(p) != mem::resolve(vt::LocalPlayer))
        return false;
    float x = mem::read<float>(p + off::player::PosX);
    float y = mem::read<float>(p + off::player::PosY);
    float z = mem::read<float>(p + off::player::PosZ);
    if (!validPos(x, y, z))
        return false;
    float yaw = mem::read<float>(p + off::player::Yaw);
    float pitch = mem::read<float>(p + off::player::Pitch);
    return validAng(yaw, pitch);
}

void Game::install()
{
    if (installed_)
        return;
    startScanner();
    installed_ = true;
    MC_LOG("[Game] Background scanner started");
}

void Game::uninstall()
{
    if (!installed_)
        return;
    stopScanner();
    installed_ = false;
    local_ = 0;
    client_ = 0;
    level_ = 0;
    remotes_.clear();
    remotesRaw_.clear();
    rawCount_ = 0;
    shownCount_ = 0;
    MC_LOG("[Game] Background scanner stopped");
}

void Game::captureLocal(uintptr_t p)
{
    if (p && isValidLocal(p))
    {
        if (local_ != p)
            MC_LOG("[Game] LocalPlayer captured: 0x%p", (void*)p);
        local_ = p;
    }
}

void Game::captureClient(uintptr_t p)
{
    if (p && mem::isReadable(p, 0x300))
        client_ = p;
}

void Game::update()
{
    auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(g_scan.mutex);
        if (g_scan.resultReady)
        {
            ScanPacket packet = std::move(g_scan.result);
            g_scan.resultReady = false;

            if (packet.local && isValidLocal(packet.local))
            {
                if (local_ != packet.local)
                    MC_LOG("[Game] LocalPlayer found: 0x%p", (void*)packet.local);
                local_ = packet.local;
            }

            remotesRaw_.clear();
            auto addUnique = [this](uintptr_t p) {
                if (!p || p == local_)
                    return;
                for (const Actor& a : remotesRaw_)
                {
                    if (a.address() == p)
                        return;
                }
                remotesRaw_.emplace_back(p);
            };
            for (uintptr_t p : packet.remotes)
                addUnique(p);
            for (uintptr_t p : packet.remotesAlt)
                addUnique(p);
            applyBotFilter();

            lastRemotes_ = now;
        }
    }

    if (local_ && !isValidLocal(local_))
    {
        local_ = 0;
        remotes_.clear();
        remotesRaw_.clear();
        rawCount_ = 0;
        shownCount_ = 0;
    }

    if (local_)
    {
        level_ = mem::read<uintptr_t>(local_ + off::player::LevelPtr);
        uintptr_t client = mem::read<uintptr_t>(local_ + off::player::MinecraftClientPtr);
        if (client && mem::isReadable(client, 0x300))
            client_ = client;
    }

    if (!workerIdle())
        return;

    if (!local_)
    {
        if (now - lastSubmit_ > std::chrono::seconds(2))
        {
            lastSubmit_ = now;
            requestScan();
        }
        return;
    }

    if (now - lastRemotes_ > std::chrono::milliseconds(600) &&
        now - lastSubmit_ > std::chrono::milliseconds(600) &&
        remotesWanted_.exchange(false))
    {
        lastSubmit_ = now;
        requestScan();
    }
}

const std::vector<Actor>& Game::remotePlayers()
{
    remotesWanted_ = true;
    return remotes_;
}

std::vector<Actor> Game::remotePlayersSnapshot()
{
    remotesWanted_ = true;
    std::lock_guard<std::mutex> lock(g_scan.mutex);
    return remotes_;
}

void Game::applyBotFilter()
{
    rawCount_ = remotesRaw_.size();
    remotes_.clear();
    for (const Actor& a : remotesRaw_)
    {
        if (botFilter_ && botfilter::isBot(a.name()))
            continue;
        remotes_.push_back(a);
    }
    shownCount_ = remotes_.size();
}

void Game::setBotFilter(bool enabled)
{
    botFilter_ = enabled;
    applyBotFilter();
}

void Game::refreshBotFilter()
{
    applyBotFilter();
}

uintptr_t Game::blockSourceAddr() const
{
    if (level_)
    {
        uintptr_t bs = mem::read<uintptr_t>(level_ + off::chain::LevelBlockSource);
        if (bs)
            return bs;
    }
    if (local_)
    {
        uintptr_t bs = mem::read<uintptr_t>(local_ + off::player::BlockSourceAlt);
        if (bs)
            return bs;
    }
    return 0;
}

uintptr_t Game::gameModeAddr() const
{
    if (!level_)
        return 0;
    uintptr_t snh = mem::read<uintptr_t>(level_ + off::chain::LevelNetworkHandler);
    if (!snh)
        return 0;
    return mem::read<uintptr_t>(snh + off::chain::NetworkHandlerGameMode);
}

}
