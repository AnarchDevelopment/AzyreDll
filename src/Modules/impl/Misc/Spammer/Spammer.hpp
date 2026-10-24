#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

#include <imgui.h>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>

namespace mc {

class Spammer : public Module
{
public:
    Spammer();

    void onEnable() override;
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
            {"obfMode", obfMode_},
            {"order", order_},
            {"debug", debug_},
            {"lines", lines},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        minDelay_ = clampf(j.value("minDelay", minDelay_), 0.5f, 120.0f);
        maxDelay_ = clampf(j.value("maxDelay", maxDelay_), 0.5f, 300.0f);
        if (maxDelay_ < minDelay_)
            maxDelay_ = minDelay_;
        obfMode_ = j.value("obfMode", obfMode_);
        if (obfMode_ < 0 || obfMode_ > 3) obfMode_ = 0;
        order_ = j.value("order", order_);
        if (order_ < 0 || order_ > 1) order_ = 1;
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

    std::string pickMessage();
    void displayDebug(const std::string& msg);

    char lines_[kLines][128] = {};
    float minDelay_ = 3.0f;
    float maxDelay_ = 6.0f;
    int obfMode_ = 2; // 0 None, 1 Invisible, 2 Suffix, 3 Full
    int order_ = 1;   // 0 Sequential, 1 Random
    bool debug_ = false;

    int seq_ = 0;
    ULONGLONG nextSendMs_ = 0;
    uint32_t rng_ = 0x51A9D0F1u;
    bool warnedNoTpl_ = false;
};

}
