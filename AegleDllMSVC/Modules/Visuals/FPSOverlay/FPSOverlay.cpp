/*
Under an4rch Development Public Source License 1.0
*/

#include "FPSOverlay.hpp"
#include "../../ModuleManager.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../../GUI/GUI.hpp"
#include "../../Misc/UnlockFPS/UnlockFPS.hpp"
#include "../RenderInfo/RenderInfo.hpp"
#include <cstdio>

// Static member initialization
bool FPSOverlay::g_showFpsOverlay = false;
float FPSOverlay::g_fpsTextScale = 1.0f;
ImVec4 FPSOverlay::g_fpsTextColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
bool FPSOverlay::g_showBackground = false;
float FPSOverlay::g_bgOpacity = 0.5f;
bool FPSOverlay::g_showShadow = false;
float FPSOverlay::g_shadowSpread = 10.0f;
float FPSOverlay::g_shadowBlur = 10.0f;
bool FPSOverlay::g_showTextShadow = true;
float FPSOverlay::g_textShadowOffset = 1.0f;
ImVec4 FPSOverlay::g_accentColor = ImVec4(1.0f, 0.4f, 0.8f, 1.0f); // Pink
std::string FPSOverlay::g_fontName = "Default";

float FPSOverlay::g_fpsOverlayAnim = 0.0f;
ULONGLONG FPSOverlay::g_fpsOverlayEnableTime = 0;
ULONGLONG FPSOverlay::g_fpsOverlayDisableTime = 0;
HudElement* FPSOverlay::g_fpsHud = nullptr;

void FPSOverlay::Initialize(HudElement* hudElement) {
    g_fpsHud = hudElement;
    if (g_fpsHud) {
        g_fpsHud->size = ImVec2(100, 35);
        if (g_fpsHud->pos.x == 0 && g_fpsHud->pos.y == 0) {
            g_fpsHud->pos = ImVec2(10, 250);
        }
    }
}

void FPSOverlay::UpdateAnimation(ULONGLONG now) {
    if (g_showFpsOverlay) {
        if (g_fpsOverlayAnim < 1.0f) {
            g_fpsOverlayAnim += 0.05f;
            if (g_fpsOverlayAnim > 1.0f) g_fpsOverlayAnim = 1.0f;
        }
    } else {
        if (g_fpsOverlayAnim > 0.0f) {
            g_fpsOverlayAnim -= 0.05f;
            if (g_fpsOverlayAnim < 0.0f) g_fpsOverlayAnim = 0.0f;
        }
    }
}

void FPSOverlay::RenderDisplay(int screenWidth, int screenHeight) {
    if (g_fpsOverlayAnim <= 0.01f) return;
    if (!g_fpsHud) return;
 
    ImFont* fpsFont = GUI::GetFontByName(g_fontName);
    ImGui::PushFont(fpsFont);
 
    char fpsText[32];
    sprintf_s(fpsText, "FPS: %.0f", RenderInfo::g_fpsCounter);
    
    // Calculate required size based on text and scale
    float fontSize = ImGui::GetFontSize() * g_fpsTextScale;
    ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fpsText);
    float padding = 20.0f * g_fpsTextScale;
    g_fpsHud->size = ImVec2(textSize.x + padding, textSize.y + padding);

    extern bool g_showMenu;
    if (GUI::IsHudEditable()) {
        g_fpsHud->HandleDrag(true);
        g_fpsHud->ClampToScreen();
        
        // Draw collision border (cyan) like other modules
        ImDrawList* debugDraw = ImGui::GetForegroundDrawList();
        if (debugDraw) {
            ImVec2 p1 = g_fpsHud->pos;
            ImVec2 p2 = ImVec2(g_fpsHud->pos.x + g_fpsHud->size.x, g_fpsHud->pos.y + g_fpsHud->size.y);
            debugDraw->AddRect(p1, p2, ImColor(0, 255, 255, 200), 10.0f, 0, 2.0f);
        }
    }

    float easedAnim = Animations::EaseOutExpo(g_fpsOverlayAnim);
    ImGui::SetNextWindowPos(g_fpsHud->pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(g_fpsHud->size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##FPSOverlay", nullptr, flags)) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetWindowPos();
        ImVec2 s = ImGui::GetWindowSize();

        if (g_showShadow) {
            GUI::DrawShadow(draw, p, s, g_shadowSpread, g_shadowBlur * easedAnim, 0.4f * easedAnim);
        }

        if (g_showBackground) {
            draw->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), ImColor(15, 15, 25, (int)(g_bgOpacity * 255 * easedAnim)), 10.0f);
        }

        // Center text perfectly
        ImVec2 textPos = ImVec2(p.x + (s.x - textSize.x) * 0.5f, p.y + (s.y - textSize.y) * 0.5f);

        ImU32 textCol = ImGui::ColorConvertFloat4ToU32(ImVec4(g_fpsTextColor.x, g_fpsTextColor.y, g_fpsTextColor.z, g_fpsTextColor.w * easedAnim));
        
        // Text Shadow (CPSCounter Style)
        if (g_showTextShadow) {
            ImU32 shadowCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0.8f * easedAnim));
            draw->AddText(ImGui::GetFont(), fontSize, ImVec2(textPos.x + g_textShadowOffset, textPos.y + g_textShadowOffset), shadowCol, fpsText);
        }

        // Active Text Glow (Premium)
        GUI::AddTextGlow(draw, ImGui::GetFont(), fontSize, textPos, textCol, fpsText, 2.0f);
        
        ImGui::End();
    }
    ImGui::PopFont();
}

void FPSOverlay::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    if (g_showFpsOverlay || g_fpsOverlayAnim > 0.01f) {
        float alpha = g_fpsOverlayAnim * 255.0f;
        if (alpha > 1.0f && draw) {
            const char* label = "FPS Overlay";
            ImVec2 textSize = ImGui::CalcTextSize(label);
            float xPos = arrayListStart.x + 300.0f - textSize.x - 10.0f;
            
            draw->AddText(ImVec2(xPos - 1, yPos + 1), IM_COL32(0, 0, 0, (int)(alpha * 0.8f)), label);
            draw->AddText(ImVec2(xPos, yPos), IM_COL32(255, 102, 204, (int)alpha), label);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void FPSOverlay::RenderMenu() {
    bool prev = g_showFpsOverlay;
    GUI::RenderCustomSwitch("FPS Overlay", &g_showFpsOverlay);
    if (prev != g_showFpsOverlay) {
        if (g_showFpsOverlay) {
            g_fpsOverlayEnableTime = GetTickCount64();
            g_fpsOverlayDisableTime = 0;
        } else {
            g_fpsOverlayDisableTime = GetTickCount64();
            g_fpsOverlayEnableTime = 0;
        }
    }

    if (GUI::BeginModuleSettings("FPS Overlay", &g_showFpsOverlay)) {
        GUI::RenderFontSelect("Font", g_fontName);
        ImGui::SliderFloat("Scale", &g_fpsTextScale, 0.5f, 2.0f, "%.1f");
        ImGui::ColorEdit4("Text Color", (float*)&g_fpsTextColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        GUI::RenderCustomSwitch("Show Background", &g_showBackground);
        if (g_showBackground) {
            ImGui::SliderFloat("Background Opacity", &g_bgOpacity, 0.0f, 1.0f, "%.2f");
        }
        
        GUI::RenderCustomSwitch("Show Shadow", &g_showShadow);
        if (g_showShadow) {
            ImGui::SliderFloat("Shadow Offset", &g_shadowSpread, 0.0f, 20.0f, "%.1f");
        }
        
        GUI::RenderCustomSwitch("Text Shadow", &g_showTextShadow);
        if (g_showTextShadow) {
            ImGui::SliderFloat("Text Shadow Offset", &g_textShadowOffset, 0.0f, 5.0f, "%.1f");
        }

        ImGui::ColorEdit4("Accent Color", (float*)&g_accentColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        GUI::EndModuleSettings();
    }
}
