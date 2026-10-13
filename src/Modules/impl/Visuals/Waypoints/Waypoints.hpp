#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

#include <imgui.h>

#include <map>
#include <string>
#include <vector>

namespace mc {

// Waypoints — puntos de interes por perfil (cada servidor usa su perfil).
// Comandos: .wp add <nombre> / .wp del <nombre> / .wp profile <nombre> /
//           .wp list / .wp clear
class Waypoints : public Module
{
public:
    Waypoints();

    void onRender() override;
    void drawSettings() override;
    void onCommand(const std::string& cmd) override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    struct Waypoint
    {
        std::string name;
        Vec3 pos;
    };

    void addCurrent(const std::string& name);
    void ensureProfile();

    std::map<std::string, std::vector<Waypoint>> profiles_;
    std::string profile_ = "default";

    char wpName_[48] = {};
    char newProfile_[48] = {};

    bool tracers_ = true;
    bool labels_ = true;
    float maxDistance_ = 0.0f; // 0 = sin limite
    ImVec4 color_ = ImVec4(0.55f, 0.85f, 1.0f, 1.0f);
    float fov_ = 110.0f;
    bool fovInited_ = false;
};

}
