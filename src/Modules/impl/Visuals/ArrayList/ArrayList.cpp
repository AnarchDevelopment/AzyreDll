#include "ArrayList.hpp"

#include "Framework/ImTween.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace mc {

namespace {

struct RowState
{
    float anim = 0.0f; // 0 = fuera, 1 = dentro
    std::unique_ptr<ImTween<float>> tween;
};

std::map<const Module*, RowState>& rowStates()
{
    static std::map<const Module*, RowState> s;
    return s;
}

float easeOutCubic(float t)
{
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

} // namespace

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
    auto& states = rowStates();

    // --- Actualizar tweens de todos los modulos ---
    std::vector<const Module*> dead;
    for (auto& up : ModuleManager::get().all())
    {
        const Module* m = up.get();
        auto it = states.find(m);
        if (it == states.end())
        {
            it = states.emplace(m, RowState{}).first;
            RowState& st = it->second;
            // La copia vendoreada tiene el miembro por valor: la factory es
            // segura (el original colgaba la referencia).
            st.tween = std::make_unique<ImTween<float>>(
                ImTween<float>::Tween(std::make_tuple(0.0f, 1.0f, &st.anim)));
            st.tween->OfType(ImTween<float>::PingPong).When([m] { return m->enabled(); });
        }

        RowState& st = it->second;
        // Velocidad escalada por dt (independiente de los FPS del juego).
        st.tween->Speed(0.09f * io.DeltaTime * 60.0f);
        st.tween->Tick();

        if (!m->enabled() && st.anim <= 0.0f)
            dead.push_back(m);
    }
    for (const Module* m : dead)
        states.erase(m);

    // --- Filas visibles: anim > 0, ordenadas por ancho (las mas anchas arriba) ---
    struct Row
    {
        const Module* m;
        float w;
        float h;
        float eased;
    };
    std::vector<Row> rows;
    rows.reserve(states.size());
    for (auto& [m, st] : states)
    {
        if (st.anim <= 0.001f)
            continue;
        ImVec2 ts = ImGui::CalcTextSize(m->name().c_str());
        rows.push_back({m, ts.x, ts.y + 7.0f, easeOutCubic(st.anim)});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.w > b.w; });

    if (rows.empty())
        return;

    float y = 14.0f;
    float right = sw - 12.0f;

    for (const Row& row : rows)
    {
        // Desplazamiento deslizante desde la derecha + colapso vertical suave.
        float slideOff = (1.0f - row.eased) * (row.w + 40.0f);
        float x0 = right - row.w - 16.0f + slideOff;
        ImVec2 rMin(x0, y);
        ImVec2 rMax(right + (1.0f - row.eased) * (row.w + 40.0f), y + row.h);

        if (row.eased > 0.01f)
            widgets::AcrylicPanel(d, rMin, rMax, 4.0f, row.eased, sw, sh, 26.0f, 8.0f);

        d->AddRectFilled(ImVec2(rMax.x - 3.0f, y), ImVec2(rMax.x, y + row.h),
                         theme::AccentU32(row.eased), 4.0f);
        d->AddText(ImVec2(x0 + 7.0f, y + 3.0f),
                   IM_COL32(240, 242, 245, (int)(255 * row.eased)), row.m->name().c_str());

        y += (row.h + 4.0f) * row.eased;
    }

    (void)now;
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
