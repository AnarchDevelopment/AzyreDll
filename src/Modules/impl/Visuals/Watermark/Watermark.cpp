#include "Watermark.hpp"

#include "Config/Config.hpp"
#include "Features/HudRegistry.hpp"
#include "Framework/ColorUtils.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Render/DX11.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace mc {

Watermark::Watermark()
    : Module("Watermark", "Watermark with FPS and player name", Category::Visuals, 0)
{
    markHasSettings();
    setEnabled(true);
}

void Watermark::onRender()
{
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 1.0f || sh <= 1.0f)
        return;

    ImDrawList* d = ImGui::GetForegroundDrawList();

    if (style_ == 2)
        renderRise(d);
    else if (style_ == 1)
        renderSolstice(d);
    else
        renderAzyre(d, sw, sh, io);
}

// =============== MODO AZYRE (panel acrilico clasico) ===============
void Watermark::renderAzyre(ImDrawList* d, float sw, float sh, ImGuiIO& io)
{
    Game& game = Game::get();
    ULONGLONG now = GetTickCount64();

    static std::string playerName;
    static ULONGLONG lastName = 0;
    if (now - lastName > 1000)
    {
        playerName = game.localFound() ? game.localPlayer().name() : "";
        lastName = now;
    }

    char buf[160];
    if (!playerName.empty())
        snprintf(buf, sizeof(buf), "AZYRE  |  %s  |  %.0f FPS", playerName.c_str(), io.Framerate);
    else
        snprintf(buf, sizeof(buf), "AZYRE  |  %.0f FPS", io.Framerate);

    ImVec2 ts = ImGui::CalcTextSize(buf);
    float pad = 9.0f;
    ImVec2 size(ts.x + pad * 2.0f + 5.0f, ts.y + pad * 2.0f);

    hud::Registry::get().setConfigPath(config::baseDir() + "\\hud.json");
    ImVec2 p0 = hud::Registry::get().report("Watermark", ImVec2(12.0f, 12.0f), size);
    ImVec2 p1(p0.x + size.x, p0.y + size.y);

    // Panel acrilico con blur en vivo + sombra.
    widgets::AcrylicPanel(d, p0, p1, 7.0f, 1.0f, sw, sh);
    d->AddRectFilled(ImVec2(p0.x + 2.0f, p0.y), ImVec2(p0.x + 4.0f, p1.y),
                     theme::AccentU32(1.0f));
    d->AddText(ImVec2(p0.x + pad + 5.0f, p0.y + pad), IM_COL32(240, 242, 245, 255), buf);
}

// =============== MODO SOLSTICE (puerto de Watermark.cpp) ===============
// Texto por caracteres con el color del tema fluyendo (getThemedColor de
// i*100), glow circular detras de cada letra, sombra oscura a +1px y
// slide-in desde (-200, -200) con lerp dt*10.
void Watermark::renderSolstice(ImDrawList* d)
{
    float dt = ImGui::GetIO().DeltaTime;
    anim_ += ((enabled() ? 1.0f : 0.0f) - anim_) * dt * 10.0f;
    if (anim_ < 0.0f)
        anim_ = 0.0f;
    if (anim_ > 1.0f)
        anim_ = 1.0f;
    if (anim_ < 0.01f)
        return;

    ImFont* font = dx11::hudFont();
    if (!font)
        font = ImGui::GetFont();

    const char* text = "azyre";
    const float size = 45.0f;

    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);

    hud::Registry::get().setConfigPath(config::baseDir() + "\\hud.json");
    ImVec2 pos = hud::Registry::get().report("WatermarkSol", ImVec2(20.0f, 20.0f), ts);

    // Slide-in desde (-200, -200) con la animacion.
    pos.x = -200.0f + (pos.x + 200.0f) * anim_;
    pos.y = -200.0f + (pos.y + 200.0f) * anim_;

    float x = pos.x;
    for (int i = 0; text[i] != '\0'; ++i)
    {
        const char* c = text + i;
        ImVec2 cs = font->CalcTextSizeA(size, FLT_MAX, 0.0f, c, c + 1);

        ImU32 col = colors::getThemedColor((float)(i * 100), 1.0f);
        ImVec4 colV = ImGui::ColorConvertU32ToFloat4(col);
        ImVec2 center(x + cs.x * 0.5f, pos.y + cs.y * 0.5f);

        // AddShadowCircle aproximado: anillos concentricos con caida cubica
        // (radio size/3, peak anim*0.75 como el original).
        if (glow_)
        {
            float peak = anim_ * 0.75f;
            const int layers = 7;
            for (int l = 0; l < layers; ++l)
            {
                float t = (float)l / (float)(layers - 1);
                float r = 6.0f + (size / 3.0f) * t;
                float f = 1.0f - t;
                int a = (int)(peak * f * f * f * 255.0f);
                if (a <= 0)
                    continue;
                d->AddCircleFilled(center, r, (col & 0x00FFFFFFu) | ((unsigned)a << 24));
            }
        }

        // Sombria del caracter: color * 0.25 a +1px (alpha 0.925 del original).
        ImU32 shadowCol = IM_COL32((int)(colV.x * 0.25f * 255.0f),
                                   (int)(colV.y * 0.25f * 255.0f),
                                   (int)(colV.z * 0.25f * 255.0f),
                                   (int)(0.925f * 255.0f));
        d->AddText(font, size, ImVec2(x + 1.0f, pos.y + 1.0f), shadowCol, c, c + 1);

        // Caracter principal en el color del tema.
        d->AddText(font, size, ImVec2(x, pos.y), col, c, c + 1);

        x += cs.x;
    }
}

// =============== MODO RISE (watermark de ModernInterface.java) ===============
// Rise dibuja el nombre del cliente a 36px MEDIUM en (6, 6) con gradiente POR
// CARACTER (ColorUtil.a: lerp(rA, rB, sin(i*0.095)*0.5+0.5)) y sombra; despues,
// en la misma Y, el nombre de usuario a 18px en blanco alpha 170 con sombra
// (o el Custom Client Name en color rB, sin sombra, si esta configurado).
void Watermark::renderRise(ImDrawList* d)
{
    ImFont* font = dx11::riseFont();
    if (!font)
        font = ImGui::GetFont();

    // rA/rB del tema. Para el tema funcion (Rainbow) se calculan en runtime
    // como Themes.rA()/rB() = getAccentColor((0,0)) / ((0,50)).
    auto rA = []() { return theme::AccentRainbow ? theme::rainbowAt(0.0f, 0.0f) : theme::Accent; };
    auto rB = []() { return theme::AccentRainbow ? theme::rainbowAt(0.0f, 50.0f) : theme::Accent2; };

    // Nombre del jugador (cacheado 1s, como el modo Azyre).
    Game& game = Game::get();
    ULONGLONG now = GetTickCount64();
    static std::string playerName;
    static ULONGLONG lastName = 0;
    if (now - lastName > 1000)
    {
        playerName = game.localFound() ? game.localPlayer().name() : "";
        lastName = now;
    }

    // Texto secundario: Custom Client Name (rB, sin sombra) o el nombre del
    // jugador (blanco alpha 170, con sombra).
    bool custom = (customName_[0] != '\0');
    const char* second = custom ? customName_ : playerName.c_str();

    const char* logo = "Azyre";
    const float logoSz = 36.0f;
    const float nameSz = 18.0f;

    // Ancho del logo sumando caracter a caracter + 0.5 (como ColorUtil.a).
    float logoW = 0.0f;
    ImVec2 logoTs(0.0f, logoSz);
    for (int i = 0; logo[i] != '\0'; ++i)
    {
        ImVec2 cs = font->CalcTextSizeA(logoSz, FLT_MAX, 0.0f, logo + i, logo + i + 1);
        logoW += cs.x + 0.5f;
        if (cs.y > logoTs.y)
            logoTs.y = cs.y;
    }

    float secondW = 0.0f;
    if (second && second[0] != '\0')
        secondW = font->CalcTextSizeA(nameSz, FLT_MAX, 0.0f, second).x;

    // (6, 6) como Rise; movable con el HUD editor.
    ImVec2 size(logoW + 2.0f + secondW, logoTs.y);
    hud::Registry::get().setConfigPath(config::baseDir() + "\\hud.json");
    ImVec2 pos = hud::Registry::get().report("WatermarkRise", ImVec2(6.0f, 6.0f), size);

    // Logo: gradiente por caracter con sombra (Font.b = drawStringWithShadow).
    float x = pos.x;
    for (int i = 0; logo[i] != '\0'; ++i)
    {
        const char* c = logo + i;
        ImVec2 cs = font->CalcTextSizeA(logoSz, FLT_MAX, 0.0f, c, c + 1);

        float f = sinf((float)i * 0.095f) * 0.5f + 0.5f;
        ImVec4 a = rA();
        ImVec4 b = rB();
        ImVec4 colV(a.x + (b.x - a.x) * f,
                    a.y + (b.y - a.y) * f,
                    a.z + (b.z - a.z) * f, 1.0f);

        ImU32 shadowCol = IM_COL32((int)(colV.x * 0.25f * 255.0f),
                                   (int)(colV.y * 0.25f * 255.0f),
                                   (int)(colV.z * 0.25f * 255.0f),
                                   (int)(0.925f * 255.0f));
        d->AddText(font, logoSz, ImVec2(x + 1.0f, pos.y + 1.0f), shadowCol, c, c + 1);
        d->AddText(font, logoSz, ImVec2(x, pos.y),
                   ImGui::ColorConvertFloat4ToU32(colV), c, c + 1);

        x += cs.x + 0.5f;
    }

    // Texto secundario en 6 + anchoLogo + 2, misma Y (Font.b para el usuario,
    // Font.a sin sombra para el Custom Client Name).
    if (secondW > 0.0f)
    {
        ImVec2 secondPos(pos.x + logoW + 2.0f, pos.y);
        if (custom)
        {
            d->AddText(font, nameSz, secondPos,
                       ImGui::ColorConvertFloat4ToU32(rB()), second);
        }
        else
        {
            d->AddText(font, nameSz, ImVec2(secondPos.x + 1.0f, secondPos.y + 1.0f),
                       IM_COL32(64, 64, 64, 170), second);
            d->AddText(font, nameSz, secondPos,
                       IM_COL32(255, 255, 255, 170), second);
        }
    }
}

void Watermark::drawSettings()
{
    const char* styles[] = {"Azyre", "Solstice", "Rise"};
    widgets::CCombo("Style##wm", &style_, styles, 3);
    if (style_ == 1)
        widgets::Checkbox("Glow##wm", &glow_);
    if (style_ == 2)
        widgets::InputText("Custom Name##wm", customName_, sizeof(customName_));
}

nlohmann::json Watermark::saveSettings() const
{
    return nlohmann::json{
        {"style", style_},
        {"glow", glow_},
        {"customName", customName_},
    };
}

void Watermark::loadSettings(const nlohmann::json& j)
{
    style_ = j.value("style", style_);
    if (style_ < 0 || style_ > 2)
        style_ = 0;
    glow_ = j.value("glow", glow_);
    std::string cn = j.value("customName", std::string());
    snprintf(customName_, sizeof(customName_), "%s", cn.c_str());
}

MC_REGISTER_MODULE(Watermark);

}
