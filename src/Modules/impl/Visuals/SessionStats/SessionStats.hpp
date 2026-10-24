#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

#include <imgui.h>

#include <atomic>
#include <string>
#include <thread>

namespace mc {

// SessionStats — panel HUD con estadisticas de sesion (tiempo, kills,
// deaths, bloques colocados) y opcionalmente stats de la API de Hive.
class SessionStats : public Module
{
public:
    SessionStats();
    ~SessionStats();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

    std::string suffix() const override;

private:
    void fetchHive();
    void resetStats();

    // --- stats de sesion ---
    unsigned long long sessionStart_ = 0;
    int kills_ = 0;
    int deaths_ = 0;
    int blocksPlaced_ = 0;
    int lastHealth_ = 20;

    // --- stats de Hive (opcional) ---
    bool useHive_ = false;
    char gamertag_[64] = {};
    int hiveKills_ = 0;
    int hiveDeaths_ = 0;
    int hivePlayed_ = 0;
    bool hiveReady_ = false;
    bool hiveFetching_ = false;
    unsigned long long lastFetch_ = 0;

    // --- render ---
    float panelW_ = 190.0f;
    float panelH_ = 110.0f;
    bool showKD_ = true;
    bool showBlocks_ = true;
    ImVec4 titleColor_ = ImVec4(0.85f, 0.88f, 0.95f, 1.0f);

    // --- fetch thread ---
    std::thread fetchThread_;
    std::atomic<bool> fetchAbort_{false};
    void fetchThreadMain();
};

}
