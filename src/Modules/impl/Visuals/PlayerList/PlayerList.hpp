#pragma once

#include "Modules/Module.hpp"

#include <imgui.h>

#include <string>
#include <vector>

namespace mc {

// PlayerList — overlay tipo TAB (Java) con cabezas de Steve, fuente Mojangles
// y enfoque animado. Los jugadores que estan usando Azyre (detectados por IRC)
// muestran su nombre con un gradiente rosa -> morado azulado.
class PlayerList : public Module
{
public:
    PlayerList();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"key", key_},
            {"toggleMode", toggleMode_},
            {"glass", glass_},
            {"showHeads", showHeads_},
            {"showHealth", showHealth_},
            {"showCount", showCount_},
            {"scale", scale_},
            {"colA", {colA_.x, colA_.y, colA_.z, colA_.w}},
            {"colB", {colB_.x, colB_.y, colB_.z, colB_.w}},
        };
    }

    void loadSettings(const nlohmann::json& j) override;

private:
    struct Row
    {
        std::string name;
        bool local = false;
        bool azyre = false;
        int health = 20;
        float dist = 0.0f;
    };

    void collect(std::vector<Row>& out);

    int key_ = 0x09;            // VK_TAB
    bool toggleMode_ = false;
    bool toggled_ = false;
    bool capturing_ = false;
    bool wasDown_ = false;
    bool showHeads_ = true;
    bool showHealth_ = true;
    bool showCount_ = true;
    bool glass_ = true;
    float scale_ = 1.0f;
    float open_ = 0.0f;
    ImVec4 colA_ = ImVec4(1.00f, 0.43f, 0.78f, 1.0f); // rosa
    ImVec4 colB_ = ImVec4(0.55f, 0.59f, 1.00f, 1.0f); // morado azulado
};

}
