#include "Keystrokes.hpp"
#include "Config/Config.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "Features/HudRegistry.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include <imgui.h>
#include <windows.h>

namespace mc {

namespace {

bool keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

ImU32 hueColor(float h, float s, float v, float a)
{
    int hi = (int)(h * 6.0f) % 6;
    float f = h * 6.0f - (float)(int)(h * 6.0f);
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    float r, g, b;
    switch (hi)
    {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)a);
}

} // namespace

Keystrokes::Keystrokes()
    : Module("Keystrokes", "Shows WASD + space + mouse keystrokes on screen", Category::Visuals, 0)
{
    markHasSettings();
}

void Keystrokes::onRender()
{
    // En modo HUD editor, el HudEditor dibuja el outline (el contenido se oculta
    // para que no se duplique). El registry necesita el path del config.
    if (menu::hudEditing())
        return;

    // Path del hud.json en cada frame (barato: solo string concat)
    hud::Registry::get().setConfigPath(config::baseDir() + "\\hud.json");

    float dt = ImGui::GetIO().DeltaTime;
    float lerp = dt * animSpeed_;
    if (lerp > 1.0f)
        lerp = 1.0f;

    bool w = keyDown('W');
    bool a = keyDown('A');
    bool s = keyDown('S');
    bool d = keyDown('D');
    bool space = keyDown(VK_SPACE);
    bool lmb = keyDown(VK_LBUTTON);
    bool rmb = keyDown(VK_RBUTTON);

    animW_     += ((w     ? 1.0f : 0.0f) - animW_) * lerp;
    animA_     += ((a     ? 1.0f : 0.0f) - animA_) * lerp;
    animS_     += ((s     ? 1.0f : 00.0f) - animS_) * lerp;
    animD_     += ((d     ? 1.0f : 0.0f) - animD_) * lerp;
    animSpace_ += ((space ? 1.0f : 0.0f) - animSpace_) * lerp;
    animLmb_   += ((lmb   ? 1.0f : 0.0f) - animLmb_) * lerp;
    animRmb_   += ((rmb   ? 1.0f : 0.0f) - animRmb_) * lerp;

    // --- Layout ---
    float ks = keySize_ * scale_;
    float gp = gap_ * scale_;
    float rnd = rounding_ * scale_;

    float wasdW = ks * 3 + gp * 2;
    float wasdH = ks * 2 + gp;

    float spaceH = showSpace_ ? ks * 0.45f + gp : 0.0f;
    float spaceW = wasdW;

    float mouseW = showMouse_ ? ks * 2 + gp : 0.0f;
    float mouseH = showMouse_ ? ks * 0.5f + gp : 0.0f;

    float totalW = wasdW;
    float totalH = wasdH + spaceH + mouseH;

    // Posicion: reportar al HudRegistry y recibir la pos (editable)
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImVec2 defaultPos(20.0f, disp.y - totalH - 20.0f);
    ImVec2 origin = hud::Registry::get().report("Keystrokes", defaultPos,
                                                ImVec2(totalW, totalH));

    // Blur disponible (mismo patron que ArrayList)
    bool hasBlur = blur::wanted() && blur::ready() && blur::srv();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float hue = ImGui::GetTime() * 0.1f;

    // Helper lambda para dibujar una tecla individual
    auto drawKey = [&](const ImVec2& pos, const ImVec2& size, const char* label, float anim,
                       float hueOff) {
        ImVec2 keyMax(pos.x + size.x, pos.y + size.y);

        // SoftShadow: glow negro en los bordes exteriores de la tecla
        widgets::SoftShadow(dl, pos, keyMax, rnd, 22.0f, 5.0f);

        // Fondo: blur del juego + scrim (o solido si no hay blur)
        if (hasBlur)
        {
            ImVec2 uv0(pos.x / disp.x, pos.y / disp.y);
            ImVec2 uv1(keyMax.x / disp.x, keyMax.y / disp.y);
            // Blur a alpha completa para que sea bien visible
            dl->AddImageRounded(ImTextureRef((void*)blur::srv()),
                                pos, keyMax, uv0, uv1,
                                IM_COL32(255, 255, 255, 255), rnd);
            // Scrim sutil para contraste (más bajo que antes)
            dl->AddRectFilled(pos, keyMax,
                              IM_COL32(16, 20, 28, (int)(60.0f * keyOpacity_)), rnd);
        }
        else
        {
            dl->AddRectFilled(pos, keyMax,
                              IM_COL32(16, 18, 22, (int)(255.0f * keyOpacity_)), rnd);
        }

        // Glow de la tecla presionada
        if (anim > 0.01f)
        {
            ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
            ImVec2 half(size.x * anim * 0.5f, size.y * anim * 0.5f);
            ImU32 col = rainbow_
                            ? hueColor(fmodf(hue + hueOff, 1.0f), 0.72f, 0.95f,
                                       255.0f * pressOpacity_ * anim)
                            : theme::AccentU32(pressOpacity_ * anim);
            dl->AddRectFilled(ImVec2(center.x - half.x, center.y - half.y),
                            ImVec2(center.x + half.x, center.y + half.y), col, rnd * anim);
        }

        // Label — tamano proporcional a la tecla (no el default 16px)
        ImFont* font = ImGui::GetFont();
        float fontSize = size.x * 0.42f;
        ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
        dl->AddText(font, fontSize,
                    ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, (int)(255.0f * textOpacity_)), label);
    };

    // --- W ---
    drawKey(ImVec2(origin.x + ks + gp, origin.y), ImVec2(ks, ks), "W", animW_, hue);

    // --- A S D ---
    float rowY = origin.y + ks + gp;
    drawKey(ImVec2(origin.x, rowY), ImVec2(ks, ks), "A", animA_, hue + 0.1f);
    drawKey(ImVec2(origin.x + ks + gp, rowY), ImVec2(ks, ks), "S", animS_, hue + 0.2f);
    drawKey(ImVec2(origin.x + ks * 2 + gp * 2, rowY), ImVec2(ks, ks), "D", animD_, hue + 0.3f);

    float yOff = rowY + ks + gp;

    // --- SPACE ---
    if (showSpace_)
    {
        ImVec2 spacePos(origin.x, yOff);
        ImVec2 spaceSize(spaceW, ks * 0.45f);
        ImVec2 spaceMax(spacePos.x + spaceSize.x, spacePos.y + spaceSize.y);

        // SoftShadow para la barra
        widgets::SoftShadow(dl, spacePos, spaceMax, rnd, 22.0f, 5.0f);

        if (hasBlur)
        {
            ImVec2 uv0(spacePos.x / disp.x, spacePos.y / disp.y);
            ImVec2 uv1(spaceMax.x / disp.x, spaceMax.y / disp.y);
            dl->AddImageRounded(ImTextureRef((void*)blur::srv()),
                                spacePos, spaceMax, uv0, uv1,
                                IM_COL32(255, 255, 255, 255), rnd);
            dl->AddRectFilled(spacePos, spaceMax,
                              IM_COL32(16, 20, 28, (int)(60.0f * keyOpacity_)), rnd);
        }
        else
        {
            dl->AddRectFilled(spacePos, spaceMax,
                              IM_COL32(16, 18, 22, (int)(255.0f * keyOpacity_)), rnd);
        }

        if (animSpace_ > 0.01f)
        {
            ImVec2 center(spacePos.x + spaceSize.x * 0.5f, spacePos.y + spaceSize.y * 0.5f);
            ImVec2 half(spaceSize.x * animSpace_ * 0.5f, spaceSize.y * animSpace_ * 0.5f);
            ImU32 col = rainbow_
                            ? hueColor(fmodf(hue + 0.4f, 1.0f), 0.72f, 0.95f,
                                       255.0f * pressOpacity_ * animSpace_)
                            : theme::AccentU32(pressOpacity_ * animSpace_);
            dl->AddRectFilled(ImVec2(center.x - half.x, center.y - half.y),
                            ImVec2(center.x + half.x, center.y + half.y), col, rnd * animSpace_);
        }

        yOff += spaceSize.y + gp;
    }

    // --- MOUSE (LMB / RMB) ---
    if (showMouse_)
    {
        float mH = ks * 0.5f;
        float mW = (spaceW - gp) * 0.5f;

        // LMB
        {
            ImVec2 pos(origin.x, yOff);
            ImVec2 posMax(pos.x + mW, pos.y + mH);

            widgets::SoftShadow(dl, pos, posMax, rnd, 22.0f, 5.0f);

            if (hasBlur)
            {
                ImVec2 uv0(pos.x / disp.x, pos.y / disp.y);
                ImVec2 uv1(posMax.x / disp.x, posMax.y / disp.y);
                dl->AddImageRounded(ImTextureRef((void*)blur::srv()),
                                    pos, posMax, uv0, uv1,
                                    IM_COL32(255, 255, 255, 255), rnd);
                dl->AddRectFilled(pos, posMax,
                                  IM_COL32(16, 20, 28, (int)(60.0f * keyOpacity_)), rnd);
            }
            else
                dl->AddRectFilled(pos, posMax,
                                  IM_COL32(16, 18, 22, (int)(255.0f * keyOpacity_)), rnd);
            if (animLmb_ > 0.01f)
            {
                ImVec2 center(pos.x + mW * 0.5f, pos.y + mH * 0.5f);
                ImVec2 half(mW * animLmb_ * 0.5f, mH * animLmb_ * 0.5f);
                ImU32 col = rainbow_
                                ? hueColor(fmodf(hue + 0.5f, 1.0f), 0.72f, 0.95f,
                                           255.0f * pressOpacity_ * animLmb_)
                                : theme::AccentU32(pressOpacity_ * animLmb_);
                dl->AddRectFilled(ImVec2(center.x - half.x, center.y - half.y),
                                ImVec2(center.x + half.x, center.y + half.y), col, rnd * animLmb_);
            }
            ImFont* font = ImGui::GetFont();
            float mFontSize = mH * 0.42f;
            ImVec2 ts = font->CalcTextSizeA(mFontSize, FLT_MAX, 0.0f, "LMB");
            dl->AddText(font, mFontSize,
                        ImVec2(pos.x + (mW - ts.x) * 0.5f, pos.y + (mH - ts.y) * 0.5f),
                        IM_COL32(255, 255, 255, (int)(255.0f * textOpacity_)), "LMB");
        }

        // RMB
        {
            ImVec2 pos(origin.x + mW + gp, yOff);
            ImVec2 posMax(pos.x + mW, pos.y + mH);

            widgets::SoftShadow(dl, pos, posMax, rnd, 22.0f, 5.0f);

            if (hasBlur)
            {
                ImVec2 uv0(pos.x / disp.x, pos.y / disp.y);
                ImVec2 uv1(posMax.x / disp.x, posMax.y / disp.y);
                dl->AddImageRounded(ImTextureRef((void*)blur::srv()),
                                    pos, posMax, uv0, uv1,
                                    IM_COL32(255, 255, 255, 255), rnd);
                dl->AddRectFilled(pos, posMax,
                                  IM_COL32(16, 20, 28, (int)(60.0f * keyOpacity_)), rnd);
            }
            else
                dl->AddRectFilled(pos, posMax,
                                  IM_COL32(16, 18, 22, (int)(255.0f * keyOpacity_)), rnd);
            if (animRmb_ > 0.01f)
            {
                ImVec2 center(pos.x + mW * 0.5f, pos.y + mH * 0.5f);
                ImVec2 half(mW * animRmb_ * 0.5f, mH * animRmb_ * 0.5f);
                ImU32 col = rainbow_
                                ? hueColor(fmodf(hue + 0.6f, 1.0f), 0.72f, 0.95f,
                                           255.0f * pressOpacity_ * animRmb_)
                                : theme::AccentU32(pressOpacity_ * animRmb_);
                dl->AddRectFilled(ImVec2(center.x - half.x, center.y - half.y),
                                ImVec2(center.x + half.x, center.y + half.y), col, rnd * animRmb_);
            }
            ImFont* font2 = ImGui::GetFont();
            float mFontSize2 = mH * 0.42f;
            ImVec2 ts2 = font2->CalcTextSizeA(mFontSize2, FLT_MAX, 0.0f, "RMB");
            dl->AddText(font2, mFontSize2,
                        ImVec2(pos.x + (mW - ts2.x) * 0.5f, pos.y + (mH - ts2.y) * 0.5f),
                        IM_COL32(255, 255, 255, (int)(255.0f * textOpacity_)), "RMB");
        }
    }
}

void Keystrokes::drawSettings()
{
    ImGui::SeparatorText("Layout");
    widgets::CSlider("Scale##ks", &scale_, 0.5f, 2.5f, "%.1f");
    widgets::CSlider("Key size##ks", &keySize_, 24.0f, 64.0f, "%.0f px");
    widgets::CSlider("Gap##ks", &gap_, 0.0f, 16.0f, "%.0f px");
    widgets::CSlider("Rounding##ks", &rounding_, 0.0f, 16.0f, "%.0f px");
    widgets::CSlider("Pos X##ks", &posX_, 0.0f, 2000.0f, "%.0f");
    widgets::CSlider("Pos Y##ks", &posY_, -2000.0f, 2000.0f, "%.0f");
    if (posY_ < 0.0f)
        ImGui::TextDisabled("(negative = anchored from bottom)");

    ImGui::SeparatorText("Visual");
    widgets::Checkbox("Rainbow##ks", &rainbow_);
    widgets::CSlider("Key opacity##ks", &keyOpacity_, 0.1f, 1.0f, "%.2f");
    widgets::CSlider("Press opacity##ks", &pressOpacity_, 0.1f, 1.0f, "%.2f");
    widgets::CSlider("Text opacity##ks", &textOpacity_, 0.1f, 1.0f, "%.2f");
    widgets::CSlider("Anim speed##ks", &animSpeed_, 4.0f, 24.0f, "%.0f");

    ImGui::SeparatorText("Keys");
    widgets::Checkbox("Show space bar##ks", &showSpace_);
    widgets::Checkbox("Show mouse buttons##ks", &showMouse_);
}

MC_REGISTER_MODULE(Keystrokes);

}
