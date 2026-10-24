#include "Game.hpp"

#include "Framework/Log.hpp"
#include "Features/BotFilter.hpp"
#include "SDK/VTables.hpp"

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
    // Instancias de GameMode halladas por vtable (scanner).
    uintptr_t gmSurvival = 0;
    uintptr_t gmCreative = 0;
    uintptr_t gmBase = 0;
};

struct ScannerState
{
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    bool stop = false;
    bool pending = false;
    bool running = false;
    bool localOnly = false;
    bool resultReady = false;
    ScanPacket result;
};

ScannerState g_scan;

ScanPacket runScanJob(bool localOnly)
{
    ScanPacket packet;

    // Los validadores DEBEN vivir en el scope de la funcion: queries guarda
    // punteros a ellos, y si se declaran dentro del else quedan destruidos
    // antes de que scanMulti los use (std::bad_function_call -> crash).
    mem::ValidateFn validateLocal = actorValidator(vt::LocalPlayer, false);
    mem::ValidateFn validateRemote = actorValidator(vt::RemotePlayer, true);
    mem::ValidateFn validateRemoteAlt = actorValidator(vt::RemotePlayerAlt, true);
    // Instancias de GameMode: la cadena level->SNH+0x20 resuelve otra cosa
    // en servidores (gm=...valid=0 en el [SC]); el GameMode REAL es el que
    // apunta a una de estas vtables (tecnica de searchGM.py). Validacion
    // minima: legible.
    mem::ValidateFn validateGm = [](uintptr_t p) {
        return p && mem::isReadable(p, 0x100) != 0;
    };

    std::vector<mem::VtableQuery> queries;
    if (localOnly)
    {
        // Busqueda rapida: solo el LocalPlayer (maxCount 1 + early-exit del
        // scanner al encontrarlo).
        queries = {
            {vt::LocalPlayer, &validateLocal, 1},
        };
    }
    else
    {
        queries = {
            {vt::LocalPlayer, &validateLocal, 1},
            {vt::RemotePlayer, &validateRemote, 64},
            {vt::RemotePlayerAlt, &validateRemoteAlt, 64},
            {vt::SurvivalMode, &validateGm, 1},
            {vt::CreativeMode, &validateGm, 1},
            {vt::GameMode, &validateGm, 1},
        };
    }

    std::vector<std::vector<uintptr_t>> out;
    mem::scanMulti(queries, out);

    if (!out.empty() && !out[0].empty())
        packet.local = out[0][0];
    if (out.size() > 2)
    {
        packet.remotes = std::move(out[1]);
        packet.remotesAlt = std::move(out[2]);
    }
    if (out.size() > 3)
    {
        if (!out[3].empty())
            packet.gmSurvival = out[3][0];
        if (out.size() > 4 && !out[4].empty())
            packet.gmCreative = out[4][0];
        if (out.size() > 5 && !out[5].empty())
            packet.gmBase = out[5][0];
    }
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

        bool localOnly = g_scan.localOnly;
        g_scan.pending = false;
        g_scan.running = true;
        lock.unlock();

        ScanPacket packet;
        bool ok = false;
        try
        {
            packet = runScanJob(localOnly);
            ok = true;
        }
        catch (const std::exception& e)
        {
            MC_LOG_ERROR("[Scan] C++ exception in scan job: %s", e.what());
        }
        catch (...)
        {
            MC_LOG_ERROR("[Scan] Unknown C++ exception in scan job");
        }

        lock.lock();
        if (ok)
        {
            g_scan.result = std::move(packet);
            g_scan.resultReady = true;
        }
        g_scan.running = false;
    }
}

bool workerIdle()
{
    std::lock_guard<std::mutex> lock(g_scan.mutex);
    return !g_scan.pending && !g_scan.running;
}

void requestScan(bool localOnly)
{
    std::lock_guard<std::mutex> lock(g_scan.mutex);
    if (g_scan.pending || g_scan.running)
        return;
    g_scan.pending = true;
    g_scan.localOnly = localOnly;
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

    bool scanOnLocalFound = false;
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
                bool wasMissing = (local_ == 0);
                local_ = packet.local;

                // Entrada al mundo: scan completo inmediato. Se difiere a
                // despues del unlock: requestScan toma el mismo mutex y
                // std::mutex no es recursivo (deadlock si se llama dentro).
                if (wasMissing)
                    scanOnLocalFound = true;
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

            // GameMode del scanner (fallback cuando la cadena no valida —
            // en servidores resuelve otra cosa).
            scannedGm_ = packet.gmSurvival ? packet.gmSurvival
                        : packet.gmCreative ? packet.gmCreative
                                            : packet.gmBase;

            // Anti-parpadeo: actores previos validos que el scan no vio se
            // conservan hasta 2 ciclos (reubicaciones temporales del heap).
            {
                std::unordered_map<uintptr_t, int> newStreak;
                for (const Actor& a : remotes_)
                {
                    if (!a.valid())
                        continue;
                    bool inScan = false;
                    for (const Actor& r : remotesRaw_)
                    {
                        if (r.address() == a.address())
                        {
                            inScan = true;
                            break;
                        }
                    }
                    if (inScan)
                        continue;

                    auto it = missStreak_.find(a.address());
                    int misses = (it != missStreak_.end() ? it->second : 0) + 1;
                    if (misses <= 2)
                    {
                        newStreak[a.address()] = misses;
                        remotesRaw_.emplace_back(a.address());
                    }
                }
                missStreak_ = std::move(newStreak);
            }

            applyBotFilter();

            lastRemotes_ = now;
        }
    }

    // Fuera del lock: el scan de entrada al mundo.
    if (scanOnLocalFound)
    {
        lastSubmit_ = now;
        requestScan(false);
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

    // Poda continua por frame: actores que dejaron de ser validos (objeto
    // liberado/reubicado) desaparecen al instante, sin esperar al scan.
    if (!remotes_.empty())
    {
        size_t w = 0;
        for (size_t i = 0; i < remotes_.size(); ++i)
        {
            if (remotes_[i].valid())
                remotes_[w++] = remotes_[i];
            else
                missStreak_.erase(remotes_[i].address());
        }
        remotes_.resize(w);
        shownCount_ = remotes_.size();
    }

    if (!workerIdle())
        return;

    if (!local_)
    {
        // Reintento frecuente en modo local-only (rapido, con early-exit).
        if (now - lastSubmit_ > std::chrono::milliseconds(750))
        {
            lastSubmit_ = now;
            requestScan(true);
        }
        return;
    }

    if (now - lastRemotes_ > std::chrono::milliseconds(300) &&
        now - lastSubmit_ > std::chrono::milliseconds(300) &&
        remotesWanted_.exchange(false))
    {
        lastSubmit_ = now;
        requestScan(false);
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
    auto isBlockSource = [](uintptr_t p) {
        return p && mem::isReadable(p, sizeof(uintptr_t)) &&
               mem::read<uintptr_t>(p) == mem::resolve(vt::BlockSource);
    };

    // Candidatos: en algunos builds `level_` ES el block source; en otros
    // cuelga de level+0x198 o de local+0x1F0. Se valida por vtable.
    uintptr_t candidates[4] = {0, 0, 0, 0};
    int n = 0;
    if (level_)
    {
        candidates[n++] = level_;
        candidates[n++] = mem::read<uintptr_t>(level_ + off::chain::LevelBlockSource);
    }
    if (local_)
        candidates[n++] = mem::read<uintptr_t>(local_ + off::player::BlockSourceAlt);

    for (int i = 0; i < n; ++i)
        if (isBlockSource(candidates[i]))
            return candidates[i];

    // Fallback (aunque la vtable no cuadre): logica original.
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
    // Cadena clasica — SOLO si el objeto apunta a una vtable de GameMode
    // real (en servidores snh+0x20 resuelve otra cosa: [SC] valid=0).
    if (level_)
    {
        uintptr_t snh = mem::read<uintptr_t>(level_ + off::chain::LevelNetworkHandler);
        if (snh)
        {
            uintptr_t gm = mem::read<uintptr_t>(snh + off::chain::NetworkHandlerGameMode);
            if (gm && mem::isReadable(gm, sizeof(uintptr_t)))
            {
                uintptr_t v = mem::read<uintptr_t>(gm);
                if (v == mem::resolve(vt::CreativeMode) ||
                    v == mem::resolve(vt::SurvivalMode) ||
                    v == mem::resolve(vt::GameMode))
                    return gm;
            }
        }
    }
    // Instancia hallada por el scanner de vtables (searchGM.py style).
    return scannedGm_;
}

}
