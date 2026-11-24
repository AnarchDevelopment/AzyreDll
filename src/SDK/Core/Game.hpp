#pragma once

#include "Framework/Memory.hpp"
#include "SDK/Entity/Actor.hpp"
#include "SDK/World/BlockSource/BlockSource.hpp"
#include "SDK/UI/Chat.hpp"
#include "SDK/Client/Client.hpp"
#include "SDK/Gameplay/GameMode.hpp"
#include "SDK/World/Level.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Client/MoveInputHandler.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace mc {

class Game
{
public:
    static Game& get();

    void install();
    void uninstall();
    void update();

    bool localFound() const { return local_ != 0; }
    uintptr_t localAddr() const { return local_; }
    LocalPlayer localPlayer() const { return LocalPlayer(local_); }

    uintptr_t clientAddr() const { return client_; }
    Client client() const { return Client(client_); }

    uintptr_t levelAddr() const { return level_; }
    Level level() const { return Level(level_); }

    uintptr_t blockSourceAddr() const;
    BlockSource blockSource() const { return BlockSource(blockSourceAddr()); }

    // GameMode del cliente: la cadena level->SNH+0x20 cuando valida; sino
    // la instancia hallada por el scanner de vtables (en servidores la
    // cadena resuelve otra cosa — [SC] gm=...valid=0).
    uintptr_t gameModeAddr() const;
    GameMode gameMode() const { return GameMode(gameModeAddr()); }

    MoveInputHandler moveInput() const { return MoveInputHandler(LocalPlayer(local_).moveInputAddr()); }
    Chat chat() const { return Chat(Client(client_).chatAddr()); }

    const std::vector<Actor>& remotePlayers();
    std::vector<Actor> remotePlayersSnapshot();

    void setBotFilter(bool enabled);
    void refreshBotFilter();
    bool botFilterEnabled() const { return botFilter_; }
    size_t rawRemotesCount() const { return rawCount_; }
    size_t shownRemotesCount() const { return shownCount_; }

    void captureLocal(uintptr_t p);
    void captureClient(uintptr_t p);
    // Called by the player model setup hook; consumed on the Present thread.
    void observeRenderedPlayer(uintptr_t actorAddress);

private:
    bool isValidLocal(uintptr_t p) const;
    void applyBotFilter();

    uintptr_t local_ = 0;
    uintptr_t client_ = 0;
    uintptr_t level_ = 0;
    uintptr_t scannedGm_ = 0; // instancia GameMode del scanner de vtables
    bool installed_ = false;
    bool botFilter_ = false;
    size_t rawCount_ = 0;
    size_t shownCount_ = 0;
    std::vector<Actor> remotes_;
    std::vector<Actor> remotesRaw_;
    std::unordered_map<uintptr_t, int> missStreak_;
    std::mutex renderedPlayersMutex_;
    std::vector<uintptr_t> renderedPlayersPending_;
    std::unordered_map<uintptr_t, uint64_t> renderedPlayers_;
    std::atomic<bool> remotesWanted_{false};
    std::chrono::steady_clock::time_point lastSubmit_{};
    std::chrono::steady_clock::time_point lastRemotes_{};
};

}
