#include "StaffDetector.hpp"

#include "Config/Config.hpp"
#include "Features/ECS/ECS.hpp"
#include "Framework/Sound.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"
#include "SDK/ChatHook.hpp"

#include <windows.h>

#include <cctype>

namespace mc {

namespace {

bool equalsCI(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    return true;
}

} // namespace

StaffDetector::StaffDetector()
    : Module("StaffDetector", "Priority alerts when a staff member enters your render distance",
             Category::Misc, 0)
{
    markHasSettings();
}

void StaffDetector::onEnable()
{
    auto& d = ecs::get().dispatcher;
    d.sink<ecs::PlayerJoined>().connect<&StaffDetector::onPlayerJoined>(*this);
    d.sink<ecs::PlayerLeft>().connect<&StaffDetector::onPlayerLeft>(*this);
}

void StaffDetector::onDisable()
{
    auto& d = ecs::get().dispatcher;
    d.sink<ecs::PlayerJoined>().disconnect<&StaffDetector::onPlayerJoined>(*this);
    d.sink<ecs::PlayerLeft>().disconnect<&StaffDetector::onPlayerLeft>(*this);
}

bool StaffDetector::isStaff(const std::string& name) const
{
    for (const std::string& s : staff_)
        if (equalsCI(s, name))
            return true;
    return false;
}

void StaffDetector::alert(const std::string& name, bool joined)
{
    std::string msg = name + (joined ? " joined" : " left") + " the render distance";

    // The toast system keeps the accent color; this is a priority alert so
    // it also goes to the chat (persistent, with the red STAFF tag).
    ModuleManager::get().notifyInfo(std::string("STAFF: ") + msg);
    if (chatAlert_)
        chathook::display("§c[STAFF]§f " + msg);
    if (soundAlert_)
        sound::playAlert();
}

void StaffDetector::onPlayerJoined(const ecs::PlayerJoined& ev)
{
    if (alertJoin_ && !ev.name.empty() && isStaff(ev.name))
        alert(ev.name, true);
}

void StaffDetector::onPlayerLeft(const ecs::PlayerLeft& ev)
{
    if (alertLeave_ && !ev.name.empty() && isStaff(ev.name))
        alert(ev.name, false);
}

void StaffDetector::drawSettings()
{
    widgets::Checkbox("Alert on join##sd", &alertJoin_);
    widgets::Checkbox("Alert on leave##sd", &alertLeave_);
    widgets::Checkbox("Chat alert##sd", &chatAlert_);
    widgets::Checkbox("Sound alert##sd", &soundAlert_);

    ImGui::Separator();

    // Add staff member
    if (widgets::InputText("Name##sd", input_, sizeof(input_)))
    {
    }
    ImGui::SameLine();
    if (ImGui::Button("Add##sd") && input_[0] != '\0')
    {
        staff_.insert(input_);
        input_[0] = '\0';
        config::saveAll();
    }

    // Staff currently in range (live EnTT view over the registry).
    auto& ecs = ecs::get();
    std::vector<std::string> inRange;
    auto view = ecs.registry.view<ecs::PlayerName>();
    for (auto e : view)
    {
        if (e == ecs.local())
            continue;
        const std::string& n = view.get<ecs::PlayerName>(e).value;
        if (!n.empty() && isStaff(n))
            inRange.push_back(n);
    }
    if (!inRange.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "IN RANGE: %d",
                           (int)inRange.size());
        for (const std::string& n : inRange)
            ImGui::BulletText("%s", n.c_str());
    }
    else
        ImGui::TextDisabled("Staff in range: none");

    // Staff list with removal
    ImGui::TextDisabled("Staff list: %d", (int)staff_.size());
    int shown = 0;
    for (auto it = staff_.begin(); it != staff_.end() && shown < 8; ++it, ++shown)
    {
        ImGui::PushID(it->c_str());
        ImGui::BulletText("%s", it->c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove##sd"))
        {
            staff_.erase(it);
            config::saveAll();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
}

nlohmann::json StaffDetector::saveSettings() const
{
    nlohmann::json j;
    j["alertJoin"] = alertJoin_;
    j["alertLeave"] = alertLeave_;
    j["chatAlert"] = chatAlert_;
    j["soundAlert"] = soundAlert_;
    nlohmann::json arr = nlohmann::json::array();
    for (const std::string& s : staff_)
        arr.push_back(s);
    j["staff"] = arr;
    return j;
}

void StaffDetector::loadSettings(const nlohmann::json& j)
{
    alertJoin_ = j.value("alertJoin", alertJoin_);
    alertLeave_ = j.value("alertLeave", alertLeave_);
    chatAlert_ = j.value("chatAlert", chatAlert_);
    soundAlert_ = j.value("soundAlert", soundAlert_);
    staff_.clear();
    if (j.contains("staff") && j["staff"].is_array())
    {
        for (const auto& s : j["staff"])
            if (s.is_string())
                staff_.insert(s.get<std::string>());
    }
}

MC_REGISTER_MODULE(StaffDetector);

}
