#include "ArrayList.hpp"

#include "GUI/Theme.hpp"
#include "Modules/ModuleManager.hpp"

#include <imgui.h>
#include <windows.h>
#include <algorithm>
#include <map>
#include <vector>

namespace mc {

ArrayList::ArrayList()
    : Module("ArrayList", "Lista de modulos activos en pantalla", Category::Visuals, 0)
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
    };
    std::vector<Row> rows;
    rows.reserve(active.size());
    for (Module* m : active)
    {
        if (shownSince.find(m) == shownSince.end())
            shownSince[m] = now;
        rows.push_back({m, ImGui::CalcTextSize(m->name().c_str()).x});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.w > b.w; });

    float y = 14.0f;
    float right = sw - 12.0f;
    for (const Row& row : rows)
    {
        float age = (float)(now - shownSince[row.m]);
        float a = age / 220.0f;
        if (a > 1.0f)
            a = 1.0f;
        if (a < 0.0f)
            a = 0.0f;

        ImVec2 ts = ImGui::CalcTextSize(row.m->name().c_str());
        float h = ts.y + 7.0f;
        float x0 = right - ts.x - 16.0f;

        d->AddRectFilled(ImVec2(x0, y), ImVec2(right, y + h),
                         IM_COL32(10, 12, 15, (int)(170 * a)), 5.0f);
        d->AddRectFilled(ImVec2(right - 3.0f, y), ImVec2(right, y + h),
                         theme::AccentU32(a), 5.0f);
        d->AddText(ImVec2(x0 + 7.0f, y + 3.0f),
                   IM_COL32(240, 242, 245, (int)(255 * a)), row.m->name().c_str());
        y += h + 4.0f;
    }
}

void ArrayList::drawSettings()
{
    ImGui::ColorEdit4("Accent##al", (float*)&theme::Accent);
}

MC_REGISTER_MODULE(ArrayList);

}
