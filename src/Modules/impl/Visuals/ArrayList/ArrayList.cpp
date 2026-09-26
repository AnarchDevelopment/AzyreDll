#include "ArrayList.hpp"

#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"

#include <imgui.h>
#include <windows.h>
#include <algorithm>
#include <map>
#include <vector>

namespace mc {

ArrayList::ArrayList()
    : Module("ArrayList", "On-screen list of enabled modules", Category::Visuals, 0)
{
    markHasSettings();
    setEnabled(true);
}

void ArrayList::onRender()
{
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 1.0f || sh <= 1.0f)
        return;

    ULONGLONG now = GetTickCount64();
    ImDrawList* d = ImGui::GetForegroundDrawList();

    static std::map<const Module*, ULONGLONG> shownSince;
    std::vector<Module*> active;
    for (auto& m : ModuleManager::get().all())
    {
        if (m->enabled())
            active.push_back(m.get());
        else
            shownSince.erase(m.get());
    }

    struct Row
    {
        Module* m;
        float w;
        float h;
        float a;
    };
    std::vector<Row> rows;
    rows.reserve(active.size());
    for (Module* m : active)
    {
        if (shownSince.find(m) == shownSince.end())
            shownSince[m] = now;
        float age = (float)(now - shownSince[m]);
        float a = age / 220.0f;
        if (a > 1.0f)
            a = 1.0f;
        if (a < 0.0f)
            a = 0.0f;

        ImVec2 ts = ImGui::CalcTextSize(m->name().c_str());
        rows.push_back({m, ts.x, ts.y + 7.0f, a});
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.w > b.w; });

    if (rows.empty())
        return;

    float y = 14.0f;
    float right = sw - 12.0f;

    for (const Row& row : rows)
    {
        float x0 = right - row.w - 16.0f;
        ImVec2 rMin(x0, y);
        ImVec2 rMax(right, y + row.h);

        // Fondo por modulo: panel acrilico individual (como el original).
        widgets::AcrylicPanel(d, rMin, rMax, 4.0f, row.a, sw, sh, 26.0f, 8.0f);

        d->AddRectFilled(ImVec2(right - 3.0f, y), ImVec2(right, y + row.h),
                         theme::AccentU32(row.a), 4.0f);
        d->AddText(ImVec2(x0 + 7.0f, y + 3.0f),
                   IM_COL32(240, 242, 245, (int)(255 * row.a)), row.m->name().c_str());
        y += row.h + 4.0f;
    }
}

void ArrayList::drawSettings()
{
    ImGui::ColorEdit4("Accent##al", (float*)&theme::Accent);
}

nlohmann::json ArrayList::saveSettings() const
{
    return nlohmann::json{
        {"accent", {theme::Accent.x, theme::Accent.y, theme::Accent.z, theme::Accent.w}},
    };
}

void ArrayList::loadSettings(const nlohmann::json& j)
{
    if (j.contains("accent") && j["accent"].is_array() && j["accent"].size() == 4)
    {
        theme::Accent.x = j["accent"][0].get<float>();
        theme::Accent.y = j["accent"][1].get<float>();
        theme::Accent.z = j["accent"][2].get<float>();
        theme::Accent.w = j["accent"][3].get<float>();
    }
}

MC_REGISTER_MODULE(ArrayList);

}
