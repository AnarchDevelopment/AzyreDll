#include "Backtrack.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <cmath>

namespace mc {

Backtrack::Backtrack()
    : Module("Backtrack", "Lag-compensated hits: rewinds entity position on attack",
             Category::Combat, 0)
{
    markHasSettings();
}

void Backtrack::onEnable()
{
    history_.clear();
    prevClick_ = false;
    hasApplied_ = false;
}

void Backtrack::onDisable()
{
    // Sin restauracion agresiva: el server restablece via packet.
    history_.clear();
    hasApplied_ = false;
}

void Backtrack::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
    {
        history_.clear();
        return;
    }

    LocalPlayer local = game.localPlayer();
    Vec3 eye = local.eyePos();
    ULONGLONG now = GetTickCount64();

    // Click edge detection: solo aplicar en el frame del click.
    bool clickNow = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool clickEdge = clickNow && !prevClick_;
    prevClick_ = clickNow;

    hasApplied_ = false;

    for (const Actor& other : game.remotePlayers())
    {
        if (!other.valid())
            continue;

        uintptr_t addr = other.address();
        Vec3 currentPos = other.pos();

        // Sanity: NaN/Inf = skip.
        if (!std::isfinite(currentPos.x) || !std::isfinite(currentPos.y) ||
            !std::isfinite(currentPos.z))
            continue;

        // === Fase 1: grabar (solo lectura, siempre) ===
        auto& hist = history_[addr];
        hist.push_back({currentPos, now});
        if ((int)hist.size() > kMaxHistory)
            hist.erase(hist.begin());

        // === Fase 2: aplicar SOLO en el click edge ===
        if (!clickEdge || (int)hist.size() <= ticks_)
            continue;

        const Vec3& backtracked = hist[hist.size() - 1 - ticks_].pos;

        // Distancia del player a la posicion actual y a la backtrackeada.
        float distCurrent = distance3D(eye, currentPos);
        float distBacktrack = distance3D(eye, backtracked);

        // La backtrackeada debe estar en rango de ataque.
        if (distBacktrack < minDist_ || distBacktrack > maxDist_)
            continue;

        // Smart: solo si la posicion vieja esta MAS CERCA que la actual
        // (previene backtrack inutil cuando el target viene hacia ti).
        if (smart_ && distBacktrack >= distCurrent)
            continue;

        // Delta guard: no teleportar mas de 10 bloques.
        Vec3 delta = currentPos - backtracked;
        if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z > 100.0f)
            continue;

        // Escribir la posicion backtrackeada en la memoria real del actor.
        // El raycast y reach check del juego usan estos bytes. El server
        // restaura la posicion real en el proximo MoveActorAbsolutePacket.
        __try
        {
            mem::write<float>(addr + off::player::PosX, backtracked.x);
            mem::write<float>(addr + off::player::PosY, backtracked.y);
            mem::write<float>(addr + off::player::PosZ, backtracked.z);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        lastAppliedPos_ = backtracked;
        hasApplied_ = true;
    }
}

void Backtrack::onRender()
{
    if (!visualize_ || !hasApplied_ || menu::visible())
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;

    // Proyectar la caja backtrackeada.
    LocalPlayer local = game.localPlayer();
    CameraView cam = computeView(local, game.client(), off::gameconst::EyeHeight);
    float fov = readGameFov();

    Vec3 bmin = lastAppliedPos_;
    Vec3 bmax(lastAppliedPos_.x + 0.6f, lastAppliedPos_.y + 1.8f, lastAppliedPos_.z + 0.6f);

    Vec3 corners[8];
    int idx = 0;
    for (int yi = 0; yi <= 1; ++yi)
        for (int zi = 0; zi <= 1; ++zi)
            for (int xi = 0; xi <= 1; ++xi)
                corners[idx++] = {bmin.x + (xi ? 0.6f : 0.0f),
                                 bmin.y + (yi ? 1.8f : 0.0f),
                                 bmin.z + (zi ? 0.6f : 0.0f)};

    Vec3 proj[8];
    bool allVisible = true;
    for (int i = 0; i < 8; ++i)
    {
        if (!worldToScreen(corners[i], cam.pos, cam.yaw, cam.pitch, fov, sw, sh, proj[i]))
        {
            allVisible = false;
            break;
        }
    }
    if (!allVisible)
        return;

    ImDrawList* d = ImGui::GetBackgroundDrawList();
    ImU32 col = theme::AccentU32(0.5f);
    static const int edges[12][2] = {
        {0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3},
        {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7},
    };
    for (auto& e : edges)
        d->AddLine(ImVec2(proj[e[0]].x, proj[e[0]].y),
                   ImVec2(proj[e[1]].x, proj[e[1]].y), col, 1.5f);
}

void Backtrack::drawSettings()
{
    widgets::CSlider("Ticks##bt", &ticks_, 1, 20);
    ImGui::TextDisabled("1 tick = 50ms  |  default 3 = 150ms rewind");
    ImGui::Separator();
    widgets::Checkbox("Smart##bt", &smart_);
    ImGui::SameLine();
    ImGui::TextDisabled("(only when past pos is closer)");
    widgets::Checkbox("Visualize##bt", &visualize_);
    ImGui::Indent();
    widgets::CSlider("Min distance##bt", &minDist_, 0.5f, 6.0f, "%.1f");
    widgets::CSlider("Max distance##bt", &maxDist_, 1.0f, 6.0f, "%.1f");
    ImGui::Unindent();
}

nlohmann::json Backtrack::saveSettings() const
{
    return nlohmann::json{
        {"ticks", ticks_},
        {"smart", smart_},
        {"visualize", visualize_},
        {"minDist", minDist_},
        {"maxDist", maxDist_},
    };
}

void Backtrack::loadSettings(const nlohmann::json& j)
{
    ticks_ = j.value("ticks", ticks_);
    if (ticks_ < 1)
        ticks_ = 1;
    if (ticks_ > 20)
        ticks_ = 20;
    smart_ = j.value("smart", smart_);
    visualize_ = j.value("visualize", visualize_);
    minDist_ = j.value("minDist", minDist_);
    if (minDist_ < 0.5f)
        minDist_ = 0.5f;
    maxDist_ = j.value("maxDist", maxDist_);
    if (maxDist_ < minDist_)
        maxDist_ = minDist_;
    if (maxDist_ > 6.0f)
        maxDist_ = 6.0f;
}

MC_REGISTER_MODULE(Backtrack);

}
