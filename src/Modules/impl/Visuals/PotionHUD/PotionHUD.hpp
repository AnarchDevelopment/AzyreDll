#pragma once

#include "Modules/Module.hpp"

#include <vector>

namespace mc {

class PotionHUD : public Module
{
public:
    PotionHUD();

    void onRender() override;
    void onDisable() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"scale", scale_},
            {"blink", blink_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        scale_ = j.value("scale", scale_);
        if (scale_ < 0.7f)
            scale_ = 0.7f;
        if (scale_ > 1.6f)
            scale_ = 1.6f;
        blink_ = j.value("blink", blink_);
    }

private:
    struct Card
    {
        int id = 0;
        int amp = 0;
        float appear = 0.0f;  // entrada: 0 -> 1
        float exit = 1.0f;    // salida: 1 -> 0 (solo cuando leaving)
        bool leaving = false;
        float ticks = 0.0f;   // ticks restantes
        float maxTicks = 0.0f; // ticks al aparecer (para la barra)
    };

    std::vector<Card> cards_;
    float scale_ = 1.0f;
    bool blink_ = true;
};

}
