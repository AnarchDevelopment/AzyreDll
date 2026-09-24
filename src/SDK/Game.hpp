#pragma once

#include "Framework/Memory.hpp"
#include "SDK/Classes/Actor.hpp"
#include "SDK/Classes/BlockSource.hpp"
#include "SDK/Classes/Chat.hpp"
#include "SDK/Classes/Client.hpp"
#include "SDK/Classes/GameMode.hpp"
#include "SDK/Classes/Level.hpp"
#include "SDK/Classes/LocalPlayer.hpp"
#include "SDK/Classes/MoveInputHandler.hpp"

#include <atomic>
#include <chrono>
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

private:
    bool isValidLocal(uintptr_t p) const;
    void applyBotFilter();

    uintptr_t local_ = 0;
    uintptr_t client_ = 0;
    uintptr_t level_ = 0;
    bool installed_ = false;
    bool botFilter_ = false;
    size_t rawCount_ = 0;
    size_t shownCount_ = 0;
    std::vector<Actor> remotes_;
    std::vector<Actor> remotesRaw_;
    std::atomic<bool> remotesWanted_{false};
    std::chrono::steady_clock::time_point lastSubmit_{};
    std::chrono::steady_clock::time_point lastRemotes_{};
};

}
