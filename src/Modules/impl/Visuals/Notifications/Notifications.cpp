#include "Notifications.hpp"

#include "Config/Config.hpp"
#include "Features/HudRegistry.hpp"
#include "Framework/Math.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Interface/Style/sRise/sRise.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Render/DX11.hpp"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace mc {

namespace {

float easeOutExpoF(float t)
{
    return (t >= 1.0f) ? 1.0f : 1.0f - powf(2.0f, -10.0f * t);
}

// Glow coloreado: mismo falloff cubico por capas que widgets::SoftShadow,
// pero con el color pasado (el acento de la notificacion).
void toastGlow(ImDrawList* d, const ImVec2& min, const ImVec2& max,
               ImU32 col, float strength, float spread, float rounding)
{
    const int layers = 9;
    for (int i = 0; i < layers; ++i)
    {
        float t = (float)i / (float)(layers - 1);
        float pad = spread * t;
        float f = 1.0f - t;
        f = f * f * f;
        int a = (int)(strength * f);
        if (a <= 0)
            continue;
        d->AddRectFilled(ImVec2(min.x - pad, min.y - pad),
                         ImVec2(max.x + pad, max.y + pad),
                         (col & 0x00FFFFFFu) | ((unsigned)a << 24), rounding + pad);
    }
}

} // namespace

Notifications::Notifications()
    : Module("Notifications", "On-screen notifications (Azyre / Rise styles)",
             Category::Visuals, 0)
{
    markHasSettings();
    setEnabled(true);
}

void Notifications::onRender()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
        return;

    ModuleManager::get().tickNotifications(io.DeltaTime);

    if (mode_ == 1)
        renderRise(io.DeltaTime);
    else
        renderAzyre();
}

// =============== MODO AZYRE (toasts diagonales, el original) ===============
// La notificacion entra en diagonal desde la esquina inferior derecha. La
// caja se divide en dos sectores: uno de color (accent) que crece de
// izquierda a derecha con el progreso del tiempo y otro negro con el tiempo
// restante, con un glow suave alrededor del sector de color.
void Notifications::renderAzyre()
{
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;

    ModuleManager& mgr = ModuleManager::get();
    const auto& notifs = mgr.notifications();

    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = 20.0f;
    float y = sh - 10.0f;

    // Orden natural: la mas vieja queda en el slot inferior, la mas nueva
    // entra por encima. El slot "sube" a medida que la notificacion se asienta,
    // y las demas bajan en cascada cuando una sale.
    for (const auto& n : notifs)
    {
        char buf[128];
        if (n.hasState)
            snprintf(buf, sizeof(buf), "%s  [%s]", n.text.c_str(), n.enabled ? "ON" : "OFF");
        else
            snprintf(buf, sizeof(buf), "%s", n.text.c_str());

        ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, buf);
        float w = ts.x + 50.0f;
        if (w < 200.0f)
            w = 200.0f;
        // La altura incluye 10px de separacion con la notificacion de arriba.
        float h = ts.y + 30.0f;

        float beginX = sw - w - 10.0f;
        float endX = sw + w;
        float x = endX + (beginX - endX) * n.slide;
        y -= h * n.slide;
        if (x > sw + w && y > sh + h)
            continue;

        float percentDone = n.timeShown / n.duration;
        if (percentDone < 0.0f)
            percentDone = 0.0f;
        if (percentDone > 1.0f)
            percentDone = 1.0f;

        // Accent para ON y avisos sin estado, gris para OFF.
        ImVec4 themeCol = theme::Accent;
        if (n.hasState && !n.enabled)
            themeCol = ImVec4(150.0f / 255.0f, 155.0f / 255.0f, 160.0f / 255.0f, 1.0f);
        ImU32 accentCol = ImGui::ColorConvertFloat4ToU32(
            ImVec4(themeCol.x, themeCol.y, themeCol.z, 1.0f));

        float max = x + w;
        ImVec2 progMax(x + w * percentDone + 6.0f, y + h - 10.0f);
        if (progMax.x > max)
            progMax.x = max;
        if (progMax.x < x)
            progMax.x = x;
        ImVec2 bgMin(x + w * percentDone, y);
        ImVec2 bgMax(x + w, y + h - 10.0f);

        // Glow alrededor del sector de color, bajo los rellenos.
        toastGlow(d, ImVec2(x, y), progMax, accentCol, 50.0f, 22.0f, 5.0f);

        // Sector de color: crece con el progreso (clip evita redondear el borde
        // derecho mientras avanza).
        d->PushClipRect(ImVec2(x, y), ImVec2(x + w * percentDone, y + h - 10.0f), true);
        d->AddRectFilled(ImVec2(x, y), progMax,
                         ImGui::ColorConvertFloat4ToU32(
                             ImVec4(themeCol.x, themeCol.y, themeCol.z, 0.7f)),
                         5.0f);
        d->PopClipRect();

        // Sector negro: tiempo restante (solapa 6px sobre el color para
        // transicion suave).
        d->PushClipRect(bgMin, bgMax, true);
        d->AddRectFilled(ImVec2(x + w * percentDone - 6.0f, y), bgMax,
                         IM_COL32(0, 0, 0, 179), 5.0f);
        d->PopClipRect();

        d->AddText(font, fontSize, ImVec2(x + 10.0f, y + 10.0f),
                   IM_COL32(255, 255, 255, 255), buf);
    }
}

// =============== MODO RISE (NotificationComponent.java) ===============
// Puerto del NotificationComponent de Rise 6.9.5:
//   * una notificacion a la vez (EvictingList de 5, avanza al vencer)
//   * panel 140x30 (ancho adaptativo) en (5, 126), radius 10
//   * icono blanco 20x20 radius 6 + titulo BOLD 15 (color del tema) +
//     descripcion LIGHT 15 blanca
//   * animacion EASE_OUT_EXPO 500ms anclada al centro: reposo = 1.0,
//     salida = 1.1 (crece al 110% desvaneciendose), alpha = 1 - 10*|1 - d0|
//   * fondo negro alpha 150 (Themes.rK) + bloom negro (rz().rE) + blur
void Notifications::renderRise(float dt)
{
    ModuleManager& mgr = ModuleManager::get();
    const auto& notifs = mgr.notifications();

    // Buscar la actual; si ya no existe (consumida/borrada), tomar la
    // siguiente no mostrada (id > lastShownId_).
    const ModuleManager::Notification* cur = nullptr;
    if (curId_ >= 0)
    {
        for (const auto& n : notifs)
            if (n.id == curId_)
            {
                cur = &n;
                break;
            }
    }
    if (!cur)
    {
        for (const auto& n : notifs)
            if (n.id > lastShownId_)
            {
                cur = &n;
                break;
            }
        if (cur)
        {
            curId_ = cur->id;
            curTime_ = 0.0f;
        }
    }

    if (!cur)
    {
        animActive_ = false;
        return;
    }

    curTime_ += dt;
    bool expired = curTime_ >= cur->duration;
    float target = expired ? 1.1f : 1.0f;

    if (!animActive_ || animTarget_ != target)
    {
        animFrom_ = animVal_;
        animTarget_ = target;
        animT0_ = ImGui::GetTime();
        animActive_ = true;
    }

    float p = clampf((float)(ImGui::GetTime() - animT0_) / 0.5f, 0.0f, 1.0f);
    animVal_ = animFrom_ + (animTarget_ - animFrom_) * easeOutExpoF(p);
    bool tweenDone = (p >= 1.0f);

    // Rise: se dibuja mientras el tween no termina o no este vencida.
    if (expired && tweenDone)
    {
        lastShownId_ = curId_;
        curId_ = -1;
        animActive_ = false;
        return;
    }

    // ---- Titulo / descripcion (ModuleToggleEvent de Rise) ----
    // "Toggled" / "Toggled <modulo> on|off" para las de estado; el resto se
    // parte en "Titulo | descripcion" (o "Azyre" + texto).
    std::string title, desc;
    if (cur->hasState)
    {
        title = "Toggled";
        desc = std::string("Toggled ") + cur->text + (cur->enabled ? " on" : " off");
    }
    else
    {
        size_t sep = cur->text.find(" | ");
        if (sep != std::string::npos)
        {
            title = cur->text.substr(0, sep);
            desc = cur->text.substr(sep + 3);
        }
        else
        {
            title = "Azyre";
            desc = cur->text;
        }
    }

    float S = gui::sRise::uiScale();
    ImFont* boldF = dx11::hudFont();    // Product Sans Bold
    ImFont* lightF = dx11::riseFont();  // Product Sans regular (light)
    if (!boldF)
        boldF = ImGui::GetFont();
    if (!lightF)
        lightF = ImGui::GetFont();

    const float fontSz = 15.0f * S;
    const float iconSz = 20.0f * S;
    const float spacer = 5.0f * S;
    const float baseH = 30.0f * S;
    const float radius = 10.0f * S;

    ImVec2 tsDesc = lightF->CalcTextSizeA(fontSz, FLT_MAX, 0.0f, desc.c_str());
    ImVec2 tsTitle = boldF->CalcTextSizeA(fontSz, FLT_MAX, 0.0f, title.c_str());

    // Ancho adaptativo: max(140, anchoDesc + 3*spacer + icono + 2)
    float w = 140.0f * S;
    float needW = tsDesc.x + spacer * 3.0f + iconSz + 2.0f * S;
    if (needW > w)
        w = needW;
    float h = baseH;

    // Posicion (5, 126) como Rise, movible con el HUD editor.
    hud::Registry::get().setConfigPath(config::baseDir() + "\\hud.json");
    ImVec2 pos = hud::Registry::get().report("NotificationsRise",
                                             ImVec2(5.0f * S, 126.0f * S),
                                             ImVec2(w, h));

    // d1 = alpha de Rise: 1 - 10*|1 - d0|, clampeado.
    float d0 = animVal_;
    float d1 = 1.0f - 10.0f * fabsf(1.0f - d0);
    if (d1 < 0.0f)
        d1 = 0.0f;
    if (d1 > 1.0f)
        d1 = 1.0f;

    // Escala anclada al centro (translate + GlStateManager.scale de Rise).
    float cx = pos.x + w * 0.5f;
    float cy = pos.y + h * 0.5f;
    auto TP = [&](float x, float y) {
        return ImVec2(cx + (x - cx) * d0, cy + (y - cy) * d0);
    };
    ImVec2 rMin = TP(pos.x, pos.y);
    ImVec2 rMax = TP(pos.x + w, pos.y + h);
    float rRadius = radius * d0;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* d = ImGui::GetForegroundDrawList();

    // Blur detras del panel (Rise solo lo dibuja asentada: |d0-1| <= 0.045).
    if (fabsf(d0 - 1.0f) <= 0.045f && blur::wanted() && blur::ready() && blur::srv())
    {
        d->AddImageRounded(ImTextureRef((void*)blur::srv()), rMin, rMax,
                           ImVec2(rMin.x / io.DisplaySize.x, rMin.y / io.DisplaySize.y),
                           ImVec2(rMax.x / io.DisplaySize.x, rMax.y / io.DisplaySize.y),
                           IM_COL32(255, 255, 255, (int)(255.0f * d1)), rRadius);
    }

    // Bloom negro alrededor (rz().rE = negro alpha 190)
    for (int g = 1; g <= 3; ++g)
    {
        float e = g * 1.5f * S;
        d->AddRect(ImVec2(rMin.x - e, rMin.y - e), ImVec2(rMax.x + e, rMax.y + e),
                   IM_COL32(0, 0, 0, (int)(60.0f * d1 / (float)g)), rRadius + e, 0, 1.5f);
    }

    // Fondo (Themes.rK = negro alpha 150)
    d->AddRectFilled(rMin, rMax, IM_COL32(0, 0, 0, (int)(150.0f * d1)), rRadius);

    // Icono blanco 20x20 radius 6
    ImVec2 iMin = TP(pos.x + spacer, pos.y + spacer);
    ImVec2 iMax = TP(pos.x + spacer + iconSz, pos.y + spacer + iconSz);
    d->AddRectFilled(iMin, iMax, IM_COL32(255, 255, 255, (int)(255.0f * d1)),
                     6.0f * S * d0);

    // Titulo BOLD en color del tema (rz().rA)
    ImVec4 accent = theme::AccentRainbow ? theme::rainbowAt(0.0f, 0.0f) : theme::Accent;
    ImU32 titleCol = (ImGui::ColorConvertFloat4ToU32(
                         ImVec4(accent.x, accent.y, accent.z, 1.0f)) & 0x00FFFFFFu) |
                    ((unsigned)(int)(255.0f * d1) << 24);
    ImVec2 tPos = TP(pos.x + spacer + iconSz + spacer, pos.y + spacer + 3.0f * S);
    d->AddText(boldF, fontSz * d0, tPos, titleCol, title.c_str());

    // Descripcion LIGHT blanca debajo del titulo
    ImVec2 dPos = TP(pos.x + spacer + iconSz + spacer,
                     pos.y + spacer + 0.5f * S + spacer * 0.7f + tsTitle.y);
    d->AddText(lightF, fontSz * d0, dPos,
               IM_COL32(255, 255, 255, (int)(255.0f * d1)), desc.c_str());
}

void Notifications::drawSettings()
{
    const char* modes[] = {"Azyre", "Rise"};
    widgets::CCombo("Mode##notif", &mode_, modes, 2);
}

nlohmann::json Notifications::saveSettings() const
{
    return nlohmann::json{
        {"mode", mode_},
    };
}

void Notifications::loadSettings(const nlohmann::json& j)
{
    mode_ = j.value("mode", mode_);
    if (mode_ < 0 || mode_ > 1)
        mode_ = 0;
}

MC_REGISTER_MODULE(Notifications);

}
