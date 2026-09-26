#include "Friends.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "Modules/ModuleManager.hpp"
#include "Features/Friends.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

Friends::Friends()
    : Module("Friends", "Middle click adds/removes the player under your crosshair", Category::Misc, 0)
{
    markHasSettings();
}

void Friends::onDisable()
{
    prevMiddle_ = false;
}

void Friends::onTick()
{
    bool middle = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    bool pressed = middle && !prevMiddle_;
    prevMiddle_ = middle;
    if (!pressed)
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    Vec3 eye = local.eyePos();
    float myYaw = local.yaw();
    float myPitch = local.pitch();

    float bestDist = range_;
    std::string bestName;
    for (const Actor& other : game.remotePlayersSnapshot())
    {
        if (!other.valid())
            continue;
        Vec3 tEye = other.eyePos();
        float dist = distance3D(eye, tEye);
        if (dist > bestDist || dist < 0.1f)
            continue;
        AimAngles a = calcAngles(eye, tEye);
        if (std::fabs(angleDiff(myYaw, a.yaw)) > fov_ * 0.5f)
            continue;
        if (std::fabs(a.pitch - myPitch) > fov_ * 0.5f)
            continue;
        bestDist = dist;
        bestName = other.name();
    }

    if (bestName.empty())
        return;

    bool added = friends::toggle(bestName);
    ModuleManager::get().notify("Friend " + bestName, added);
}

void Friends::drawSettings()
{
    widgets::CSlider("Range##fr", &range_, 2.0f, 12.0f, "%.1f");
    widgets::CSlider("FOV##fr", &fov_, 5.0f, 120.0f, "%.0f");

    ImGui::Separator();
    auto& s = friends::list();
    ImGui::TextDisabled("Friends: %d", (int)s.size());
    int shown = 0;
    for (const std::string& n : s)
    {
        ImGui::BulletText("%s", n.c_str());
        if (++shown >= 8)
            break;
    }
    if (!s.empty() && ImGui::Button("Clear friends##fr"))
    {
        friends::clear();
        ModuleManager::get().notify("Friends cleared", false);
    }
}

nlohmann::json Friends::saveSettings() const
{
    nlohmann::json j;
    j["range"] = range_;
    j["fov"] = fov_;
    nlohmann::json arr = nlohmann::json::array();
    for (const std::string& n : friends::list())
        arr.push_back(n);
    j["friends"] = arr;
    return j;
}

void Friends::loadSettings(const nlohmann::json& j)
{
    range_ = j.value("range", range_);
    fov_ = j.value("fov", fov_);
    if (j.contains("friends") && j["friends"].is_array())
    {
        friends::clear();
        for (const auto& n : j["friends"])
        {
            if (n.is_string())
                friends::list().insert(n.get<std::string>());
        }
    }
}

MC_REGISTER_MODULE(Friends);

}
