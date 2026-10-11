#pragma once

#include "Modules/Module.hpp"

#include <imgui.h>
#include <map>
#include <mutex>
#include <tuple>

namespace mc {

class PlaceHighlights : public Module
{
public:
    PlaceHighlights();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void onRender() override;
    void drawSettings() override;

// Llamado desde el hook de BlockItem::useOn (hilo del juego).
void addPlacedBlock(int x, int y, int z, int face);

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"duration", duration_},
            {"maxOpacity", maxOpacity_},
            {"filled", filled_},
            {"colorMode", colorMode_},
            {"color", {color_.x, color_.y, color_.z, color_.w}},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        duration_ = j.value("duration", duration_);
        maxOpacity_ = j.value("maxOpacity", maxOpacity_);
        filled_ = j.value("filled", filled_);
        colorMode_ = j.value("colorMode", colorMode_);
        if (colorMode_ < 0 || colorMode_ > 1)
            colorMode_ = 0;
        if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
        {
            color_.x = j["color"][0].get<float>();
            color_.y = j["color"][1].get<float>();
            color_.z = j["color"][2].get<float>();
            color_.w = j["color"][3].get<float>();
        }
    }

private:
    float duration_ = 2500.0f;
    float maxOpacity_ = 0.7f;
    bool filled_ = true;
    int colorMode_ = 0; // 0 = Custom, 1 = Theme (Solstice palette)
    ImVec4 color_ = ImVec4(21.0f / 255.0f, 1.0f, 0.0f, 1.0f); // 0x15FF00

    std::mutex mapMutex_;
    std::map<std::tuple<int, int, int, int>, unsigned long long> placedBlocks_;

    float fov_ = 110.0f;
    bool fovInited_ = false;
};

}
