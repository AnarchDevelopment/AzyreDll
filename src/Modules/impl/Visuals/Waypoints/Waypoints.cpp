#include "Waypoints.hpp"

#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"
#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <cctype>

namespace mc {

Waypoints::Waypoints()
    : Module("Waypoints", "Per-server waypoints with on-screen markers",
             Category::Visuals, 0)
{
    markHasSettings();
    ensureProfile();
}

void Waypoints::ensureProfile()
{
    if (profiles_.find(profile_) == profiles_.end())
        profiles_[profile_] = {};
}

void Waypoints::addCurrent(const std::string& name)
{
    Game& game = Game::get();
    if (!game.localFound())
        return;
    Vec3 p = game.localPlayer().pos();
    Waypoint wp;
    wp.name = name.empty() ? "waypoint" : name;
    wp.pos = p;
    profiles_[profile_].push_back(wp);
    ModuleManager::get().notifyInfo("Waypoint added: " + wp.name);
}

void Waypoints::onRender()
{
    auto it = profiles_.find(profile_);
    if (it == profiles_.end() || it->second.empty())
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    if (!fovInited_)
    {
        fov_ = readGameFov();
        fovInited_ = true;
    }

    LocalPlayer lp = game.localPlayer();
    CameraView cam = getCameraView(lp, game.client());
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImVec2 origin(sw * 0.5f, sh);
    ImU32 col = ImGui::ColorConvertFloat4ToU32(color_);
    Vec3 player = lp.pos();

    for (const Waypoint& wp : it->second)
    {
        float dist = distance3D(player, wp.pos);
        if (maxDistance_ > 0.0f && dist > maxDistance_)
            continue;

        Vec3 s;
        if (!worldToScreen(wp.pos, cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, s))
            continue;

        ImVec2 p(s.x, s.y);
        float r = 6.0f;
        ImVec2 q[4] = {ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y),
                       ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y)};
        d->AddQuadFilled(q[0], q[1], q[2], q[3], col);
        for (int i = 0; i < 4; ++i)
            d->AddLine(q[i], q[(i + 1) % 4], IM_COL32(0, 0, 0, 200), 1.5f);

        if (tracers_)
            d->AddLine(origin, p, (col & 0x00FFFFFF) | (120u << 24), 1.5f);

        if (labels_)
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s  %.0fm", wp.name.c_str(), dist);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            d->AddText(ImVec2(p.x - ts.x * 0.5f + 1.0f, p.y - r - ts.y - 3.0f),
                       IM_COL32(0, 0, 0, 200), buf);
            d->AddText(ImVec2(p.x - ts.x * 0.5f, p.y - r - ts.y - 4.0f), col, buf);
        }
    }
}

void Waypoints::drawSettings()
{
    ensureProfile();

    // ---- Perfil ----
    std::vector<std::string> names;
    for (auto& kv : profiles_)
        names.push_back(kv.first);
    int idx = 0;
    for (int i = 0; i < (int)names.size(); ++i)
        if (names[i] == profile_)
            idx = i;

    if (widgets::CCombo("Profile##wp", &idx, names))
        profile_ = names[idx];

    ImGui::SameLine();
    if (ImGui::Button("Delete profile##wp") && profiles_.size() > 1)
    {
        profiles_.erase(profile_);
        profile_ = profiles_.begin()->first;
    }

    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputTextWithHint("##wpnew", "New profile (server)", newProfile_, sizeof(newProfile_));
    ImGui::SameLine();
    if (ImGui::Button("Add##wpnewp") && newProfile_[0] != '\0')
    {
        profile_ = newProfile_;
        ensureProfile();
        newProfile_[0] = '\0';
    }

    ImGui::Separator();

    // ---- Agregar waypoint ----
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputTextWithHint("##wpname", "Waypoint name", wpName_, sizeof(wpName_));
    ImGui::SameLine();
    if (ImGui::Button("Add here##wpadd"))
    {
        addCurrent(wpName_[0] ? wpName_ : "waypoint");
        wpName_[0] = '\0';
    }

    // ---- Lista ----
    auto& list = profiles_[profile_];
    if (list.empty())
        ImGui::TextDisabled("No waypoints in this profile.");
    for (size_t i = 0; i < list.size(); ++i)
    {
        ImGui::PushID((int)i);
        bool sel = false;
        char line[128];
        snprintf(line, sizeof(line), "%s  (%.0f, %.0f, %.0f)", list[i].name.c_str(),
                 list[i].pos.x, list[i].pos.y, list[i].pos.z);
        if (ImGui::Selectable(line, sel, 0, ImVec2(ImGui::GetContentRegionAvail().x - 28.0f, 0)))
        {
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 8.0f);
        if (ImGui::SmallButton("X##wpdel"))
        {
            list.erase(list.begin() + (ptrdiff_t)i);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    widgets::Checkbox("Tracers##wp", &tracers_);
    widgets::Checkbox("Labels##wp", &labels_);
    widgets::CSlider("Max distance (0 = off)##wp", &maxDistance_, 0.0f, 512.0f, "%.0f");
    widgets::ColorEdit4("Color##wp", (float*)&color_);
}

void Waypoints::onCommand(const std::string& cmd)
{
    // Quitar prefijo de comando (ej. "." o "-").
    std::string s = cmd;
    size_t start = 0;
    while (start < s.size() && !std::isalnum((unsigned char)s[start]) && s[start] != ' ')
        ++start;
    s = s.substr(start);

    std::vector<std::string> t;
    std::string cur;
    for (char c : s)
    {
        if (c == ' ')
        {
            if (!cur.empty()) { t.push_back(cur); cur.clear(); }
        }
        else
            cur.push_back(c);
    }
    if (!cur.empty())
        t.push_back(cur);
    if (t.empty())
        return;
    if (t[0] != "wp" && t[0] != "waypoint")
        return;

    auto joinFrom = [&](size_t i) {
        std::string out;
        for (size_t k = i; k < t.size(); ++k)
        {
            if (!out.empty()) out += ' ';
            out += t[k];
        }
        return out;
    };

    if (t.size() >= 2 && (t[1] == "add" || t[1] == "set"))
    {
        addCurrent(joinFrom(2));
    }
    else if (t.size() >= 3 && (t[1] == "del" || t[1] == "remove"))
    {
        std::string name = joinFrom(2);
        auto& list = profiles_[profile_];
        auto it = std::remove_if(list.begin(), list.end(),
                                 [&](const Waypoint& w) { return w.name == name; });
        if (it != list.end())
        {
            list.erase(it, list.end());
            ModuleManager::get().notifyInfo("Waypoint removed: " + name);
        }
    }
    else if (t.size() >= 3 && t[1] == "profile")
    {
        profile_ = joinFrom(2);
        ensureProfile();
        ModuleManager::get().notifyInfo("Waypoint profile: " + profile_);
    }
    else if (t.size() >= 2 && t[1] == "list")
    {
        for (const Waypoint& w : profiles_[profile_])
            ModuleManager::get().notifyInfo(w.name);
    }
    else if (t.size() >= 2 && t[1] == "clear")
    {
        profiles_[profile_].clear();
        ModuleManager::get().notifyInfo("Waypoints cleared: " + profile_);
    }
}

nlohmann::json Waypoints::saveSettings() const
{
    nlohmann::json profs = nlohmann::json::object();
    for (const auto& kv : profiles_)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const Waypoint& w : kv.second)
            arr.push_back({{"n", w.name}, {"x", w.pos.x}, {"y", w.pos.y}, {"z", w.pos.z}});
        profs[kv.first] = arr;
    }
    return nlohmann::json{
        {"profile", profile_}, {"profiles", profs}, {"tracers", tracers_},
        {"labels", labels_}, {"maxDistance", maxDistance_},
        {"color", {color_.x, color_.y, color_.z, color_.w}},
    };
}

void Waypoints::loadSettings(const nlohmann::json& j)
{
    profile_ = j.value("profile", profile_);
    tracers_ = j.value("tracers", tracers_);
    labels_ = j.value("labels", labels_);
    maxDistance_ = j.value("maxDistance", maxDistance_);
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
    {
        color_.x = j["color"][0].get<float>();
        color_.y = j["color"][1].get<float>();
        color_.z = j["color"][2].get<float>();
        color_.w = j["color"][3].get<float>();
    }
    profiles_.clear();
    if (j.contains("profiles") && j["profiles"].is_object())
    {
        for (auto& [key, arr] : j["profiles"].items())
        {
            std::vector<Waypoint> list;
            if (arr.is_array())
            {
                for (const auto& e : arr)
                {
                    Waypoint w;
                    w.name = e.value("n", "waypoint");
                    w.pos = {e.value("x", 0.0f), e.value("y", 0.0f), e.value("z", 0.0f)};
                    list.push_back(w);
                }
            }
            profiles_[key] = std::move(list);
        }
    }
    ensureProfile();
}

MC_REGISTER_MODULE(Waypoints);

}
