#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

#include <imgui.h>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace mc {

class LocalPlayer;

class Killsults : public Module
{
public:
    Killsults();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        static const char* kObf[] = {"", "ZWSP", "Tag", "Full"};
        return obfMode_ >= 0 && obfMode_ <= 3 ? kObf[obfMode_] : "";
    }

    nlohmann::json saveSettings() const override
    {
        nlohmann::json lines = nlohmann::json::array();
        for (int i = 0; i < kLines; ++i)
            lines.push_back(std::string(lines_[i]));
        return nlohmann::json{
            {"minDelay", minDelay_},
            {"maxDelay", maxDelay_},
            {"killDistance", killDistance_},
            {"skipFriends", skipFriends_},
            {"obfMode", obfMode_},
            {"debug", debug_},
            {"lines", lines},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        minDelay_ = clampf(j.value("minDelay", minDelay_), 0.0f, 10.0f);
        maxDelay_ = clampf(j.value("maxDelay", maxDelay_), 0.0f, 20.0f);
        if (maxDelay_ < minDelay_)
            maxDelay_ = minDelay_;
        killDistance_ = clampf(j.value("killDistance", killDistance_), 1.0f, 32.0f);
        skipFriends_ = j.value("skipFriends", skipFriends_);
        obfMode_ = j.value("obfMode", obfMode_);
        if (obfMode_ < 0 || obfMode_ > 3) obfMode_ = 0;
        debug_ = j.value("debug", debug_);
        if (j.contains("lines") && j["lines"].is_array())
        {
            for (int i = 0; i < kLines && i < (int)j["lines"].size(); ++i)
            {
                if (!j["lines"][i].is_string())
                    continue;
                std::string s = j["lines"][i].get<std::string>();
                if (s.size() >= sizeof(lines_[i]))
                    s.resize(sizeof(lines_[i]) - 1);
                std::snprintf(lines_[i], sizeof(lines_[i]), "%s", s.c_str());
            }
        }
    }

private:
    static constexpr int kLines = 8;

    struct Track
    {
        int lastHp = -1;
        int samples = 0;
        float lastDist = 0.0f;
        ULONGLONG lastInsultMs = 0;
        ULONGLONG lastSeenMs = 0;
    };

    struct Pending
    {
        ULONGLONG at;
        std::string text;
    };

    void tickKills(LocalPlayer& local);
    void queueInsult();
    std::string pickMessage();
    void displayDebug(const std::string& msg);

    char lines_[kLines][128] = {};
    float minDelay_ = 0.3f;
    float maxDelay_ = 0.9f;
    float killDistance_ = 6.0f;
    bool skipFriends_ = true;
    int obfMode_ = 2;
    bool debug_ = false;

    std::unordered_map<uintptr_t, Track> track_;
    std::vector<Pending> queue_;
    ULONGLONG lastQueuedMs_ = 0;
    ULONGLONG lastPruneMs_ = 0;
    uint32_t rng_ = 0x7A3F19C7u;
    bool warnedNoTpl_ = false;
};

}
