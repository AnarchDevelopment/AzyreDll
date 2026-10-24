#include "Blink.hpp"
#include "GUI/Widgets.hpp"

#include "Features/Smoothing.hpp"
#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "SDK/Camera.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <cmath>

namespace mc {

Blink::Blink()
    : Module("Blink", "Congela tus paquetes de movimiento: el server te ve clavado donde estabas mientras te mueves libre",
             Category::Movement, 0)
{
    markHasSettings();
}

void Blink::onEnable()
{
    // El hook de sendToServer debe estar vivo para capturar.
    chathook::install();
    activatedAt_ = GetTickCount64();
    smooth::clear(smooth::channel::BlinkBox); // caja nueva sin heredar historia
    blink::activate();
}

void Blink::onDisable()
{
    blink::deactivate(); // drena: rafaga al server
}

void Blink::onShutdown()
{
    blink::deactivate();
}

void Blink::onTick()
{
    // Auto-release: si el usuario se olvido el blink puesto, liberar al
    // vencer la duracion maxima (los servers cortan conexiones mudas).
    if (maxDurationMs_ > 0.0f)
    {
        ULONGLONG now = GetTickCount64();
        if (now - activatedAt_ > (ULONGLONG)maxDurationMs_)
        {
            blink::deactivate();
            activatedAt_ = now;
            blink::activate(); // ciclo: blink continuo por tramos
        }
    }
}

void Blink::onRender()
{
    if (!blink::active() || menu::visible())
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;

    CameraView cam = getCameraView(local, game.client());

    // La caja del "tu donde el server te ve": en la ultima posicion
    // liberada. Encuadre IDENTICO al ESP:
    //   * Espacio: lastSent viene del MovePlayerPacket = espacio mPosition
    //     (+0x0C) = CENTRO horizontal, que es donde el motor ancla el modelo
    //     (corregi ESP+camara a centro; antes todo estaba en +0xB4). Solo hay
    //     que mapear la Y a pies: yOff = local.pos().y - local.posExact().y.
    //   * Altura: height() (+0xD4) en vez de 1.8 fijo.
    //   * Tiempo: interp (hand-off seamless, QPC, sin delay) en vez del
    //     buffer de 60ms — sin desfase temporal respecto al modelo.
    float yOff = local.pos().y - local.posExact().y;
    Vec3 last = blink::lastSent();
    Vec3 blinkFeet{last.x, last.y + yOff, last.z};
    Vec3 pos = smooth::interp(smooth::channel::BlinkBox, blinkFeet);
    float ht = local.height();
    if (!(ht > 0.1f))
        ht = 1.8f;
    Vec3 bmin(pos.x - 0.3f, pos.y, pos.z - 0.3f);
    Vec3 bmax(pos.x + 0.3f, pos.y + ht, pos.z + 0.3f);

    Vec3 corners[8];
    int idx = 0;
    for (int yi = 0; yi <= 1; ++yi)
        for (int zi = 0; zi <= 1; ++zi)
            for (int xi = 0; xi <= 1; ++xi)
                corners[idx++] = {bmin.x + (xi ? (bmax.x - bmin.x) : 0.0f),
                                  bmin.y + (yi ? (bmax.y - bmin.y) : 0.0f),
                                  bmin.z + (zi ? (bmax.z - bmin.z) : 0.0f)};

    Vec3 proj[8];
    bool allVisible = true;
    for (int i = 0; i < 8; ++i)
    {
        if (!worldToScreen(corners[i], cam.pos, cam.yaw, cam.pitch,
                           readGameFov(), sw, sh, proj[i]))
        {
            allVisible = false;
            break;
        }
    }
    if (!allVisible)
        return;

    ImDrawList* d = ImGui::GetBackgroundDrawList();
    ImU32 col = theme::AccentU32(0.7f);
    static const int edges[12][2] = {
        {0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3},
        {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7},
    };
    for (auto& e : edges)
        d->AddLine(ImVec2(proj[e[0]].x, proj[e[0]].y),
                   ImVec2(proj[e[1]].x, proj[e[1]].y), col, 1.5f);

    // Contador de paquetes retenidos sobre la caja.
    char label[32];
    snprintf(label, sizeof(label), "%zu", blink::pendingCount());
    d->AddText(ImVec2(proj[0].x, proj[0].y - 18.0f), col, label);
}

nlohmann::json Blink::saveSettings() const
{
    return nlohmann::json{
        {"maxDurationMs", maxDurationMs_},
    };
}

void Blink::loadSettings(const nlohmann::json& j)
{
    maxDurationMs_ = j.value("maxDurationMs", maxDurationMs_);
    if (maxDurationMs_ < 0.0f)
        maxDurationMs_ = 0.0f;
    if (maxDurationMs_ > 10000.0f)
        maxDurationMs_ = 10000.0f;
}

void Blink::drawSettings()
{
    widgets::CSlider("Max duration (ms)##bl", &maxDurationMs_, 0.0f, 10000.0f,
                     "%.0f ms");
    ImGui::TextDisabled("0 = manual (toggle). El blink se libera y");
    ImGui::TextDisabled("reinicia al vencer el limite (rafaga continua).");
    ImGui::TextDisabled("Retenidos: %zu paquetes", blink::pendingCount());
}

MC_REGISTER_MODULE(Blink);

}
