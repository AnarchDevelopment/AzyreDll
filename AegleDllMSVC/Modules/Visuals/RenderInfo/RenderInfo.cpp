/*
Under an4rch Development Public Source License 1.0
*/

#include "RenderInfo.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../../Modules/Misc/UnlockFPS/UnlockFPS.hpp"
#include "../../../Utils/HudElement.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cstdio>
#include <cmath>

// Static member initialization
bool RenderInfo::g_showRenderInfo = false;
float RenderInfo::g_fpsCounter = 0.0f;
float RenderInfo::g_frameTime = 0.0f;
ULONGLONG RenderInfo::g_renderInfoEnableTime = 0;
ULONGLONG RenderInfo::g_renderInfoDisableTime = 0;
float RenderInfo::g_renderInfoAnim = 0.0f;
HudElement* RenderInfo::g_renderInfoHud = nullptr;
bool RenderInfo::g_showBackground = true;
bool RenderInfo::g_showGlow = true;
float RenderInfo::g_bgOpacity = 0.6f;
ImVec4 RenderInfo::g_staticColor = ImVec4(1.00f, 0.40f, 0.80f, 1.00f); // Pink
float RenderInfo::g_scale = 1.0f;
std::string RenderInfo::g_fontName = "Default";

// Forward declarations for helper functions

void RenderInfo::Initialize(HudElement* hud) {
    g_renderInfoHud = hud;
}

void RenderInfo::UpdateAnimation(ULONGLONG now) {
    // 📊 Render Info Animation - Exponencial suave
    if (g_showRenderInfo && g_renderInfoEnableTime == 0) {
        g_renderInfoEnableTime = now;
        g_renderInfoDisableTime = 0;
    }
    if (!g_showRenderInfo && g_renderInfoDisableTime == 0 && g_renderInfoEnableTime > 0) {
        g_renderInfoDisableTime = now;
        g_renderInfoEnableTime = 0;  // Resetear enable para que se use el else if
    }
    
    if (g_renderInfoEnableTime > 0) {
        float enableElapsed = (float)(now - g_renderInfoEnableTime) / 1000.0f;
        g_renderInfoAnim = fminf(1.0f, enableElapsed / 0.4f);
    }
    else if (g_renderInfoDisableTime > 0) {
        float disableElapsed = (float)(now - g_renderInfoDisableTime) / 1000.0f;
        float disableAnim = fminf(1.0f, disableElapsed / 0.3f);  // 300ms para desaparecer
        g_renderInfoAnim = 1.0f - disableAnim;
        if (disableAnim >= 1.0f) {
            g_renderInfoEnableTime = 0;
            g_renderInfoDisableTime = 0;
        }
    }
}

void RenderInfo::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    // Show Render Info module
    if (g_showRenderInfo || (g_renderInfoDisableTime > 0 && g_renderInfoAnim > 0.01f)) {
        float renderInfoAlpha = g_renderInfoAnim * 255.0f;
        float slideOffset = -60.0f + (Animations::SmoothInertia(g_renderInfoAnim) * 60.0f);
        
        if (renderInfoAlpha > 1.0f) {
            char riBuf[64];
            sprintf_s(riBuf, "Render Info");
            float wRI = ImGui::CalcTextSize(riBuf).x;
            float xPosRI = arrayListStart.x + 290.0f - wRI - 10;
            draw->AddText(ImVec2(xPosRI + slideOffset, yPos), ImGui::GetColorU32(g_staticColor), riBuf);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void RenderInfo::RenderWindow() {
    // --- 📊 RENDER INFO (TOP LEFT, BELOW WATERMARK) - DRAGGABLE ---
    if (g_showRenderInfo || g_renderInfoAnim > 0.01f) {
        if (!g_renderInfoHud) return;
        
        float easedAnim = Animations::EaseOutExpo(g_renderInfoAnim);
        float infoAlpha = 0.7f * easedAnim;
        
        // Render info dimensions
        ImVec2 renderInfoSize = ImVec2(220, 150);
        g_renderInfoHud->size = renderInfoSize;
        
        // Initialize position on first draw
        if (g_renderInfoHud->pos.x == 0 && g_renderInfoHud->pos.y == 0) {
            g_renderInfoHud->pos = ImVec2(10, 50);
        }
        
        // Handle drag and clamp when menu open
        extern bool g_showMenu;
        g_renderInfoHud->HandleDrag(GUI::IsHudEditable());
        g_renderInfoHud->ClampToScreen();
        
        // Position with smooth animation
        float animatedXOffset = -150.0f + (160.0f * easedAnim);
        ImVec2 finalPos = ImVec2(
            g_renderInfoHud->pos.x + animatedXOffset,
            g_renderInfoHud->pos.y
        );
        
        // Limitar valores para seguridad
        if (finalPos.x < -10000 || finalPos.x > 10000) finalPos.x = 10;
        if (finalPos.y < -10000 || finalPos.y > 10000) finalPos.y = 50;
        
        ImGui::SetNextWindowPos(finalPos, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f); // Fully transparent ImGui background, we draw our own
        ImGui::SetNextWindowSize(renderInfoSize, ImGuiCond_Always);
        
        // Allow drag only when menu open
        extern bool g_showMenu;
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoResize;
        if (!g_showMenu) {
            flags |= ImGuiWindowFlags_NoMove;
        }
        ImFont* font = GUI::GetFontByName(g_fontName);
        ImGui::PushFont(font);
        
        if (ImGui::Begin("##RenderInfo", nullptr, flags)) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            ImVec2 s = ImGui::GetWindowSize();

            // Render Custom Background if enabled
            if (g_showBackground) {
                GUI::DrawShadow(draw, p, s, 16.0f, 15.0f * easedAnim, 0.4f * easedAnim);
                draw->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), ImColor(15, 15, 25, (int)(g_bgOpacity * 255 * easedAnim)), 16.0f);
            }

            ImGui::SetCursorPos(ImVec2(20, 20)); // More padding to avoid clipping
            
            // Render Info with Theme
            ImU32 whiteCol = ImColor(255, 255, 255, (int)(easedAnim * 255));
            
            auto RenderItem = [&](const char* label, const char* value, ImU32 valCol) {
                ImGui::SetCursorPosX(20); // Force indentation for each item
                ImGui::TextColored(g_staticColor, label);
                ImGui::SameLine(100);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(valCol), value);
            };

            char fpsStr[64]; 
            if (UnlockFPS::g_unlockFpsEnabled) {
                sprintf_s(fpsStr, "%.0f (%.0f lim)", g_fpsCounter, UnlockFPS::g_fpsLimit);
            } else {
                sprintf_s(fpsStr, "%.0f", g_fpsCounter);
            }
            RenderItem("FPS", fpsStr, g_fpsCounter > 60 ? IM_COL32(100, 255, 100, 255) : IM_COL32(255, 100, 100, 255));
            
            RenderItem("Render", "DX11", whiteCol);
            RenderItem("System", "Windows", whiteCol);
            RenderItem("Version", "1.2.6", whiteCol);

            ImGui::End();
        }
        ImGui::PopFont();
        
        // DEBUG: show hitbox only when menu is open
        if (GUI::IsHudEditable()) {
            ImDrawList* debugDraw = ImGui::GetForegroundDrawList();
            if (debugDraw) {
                debugDraw->AddRect(
                    finalPos,
                    ImVec2(finalPos.x + renderInfoSize.x, finalPos.y + renderInfoSize.y),
                    IM_COL32(0, 255, 100, 200),
                    0.0f,
                    0,
                    2.0f
                );
            }
        }
    }
}

void RenderInfo::RenderMenu() {
    GUI::RenderCustomSwitch("Render Info", &g_showRenderInfo);
    if (GUI::BeginModuleSettings("Render Info", &g_showRenderInfo)) {
        GUI::RenderFontSelect("Font", g_fontName);
        GUI::RenderCustomSwitch("Show Background", &g_showBackground);
        ImGui::SliderFloat("Background Opacity", &g_bgOpacity, 0.0f, 1.0f, "%.2f");
        
        ImGui::Separator();
        
        ImGui::ColorEdit4("Theme Color", (float*)&g_staticColor, ImGuiColorEditFlags_NoInputs);
        ImGui::SliderFloat("Scale", &g_scale, 0.5f, 2.0f, "%.1fx");
        
        GUI::EndModuleSettings();
    }
}
void RenderInfo::UpdateFPS() {
    static float s_fps = 0;
    static int s_frames = 0;
    static ULONGLONG s_lastTime = GetTickCount64();
    ULONGLONG nowTicks = GetTickCount64();
    
    s_frames++;
    if (nowTicks - s_lastTime > 500) { 
        s_fps = s_frames * 1000.0f / (nowTicks - s_lastTime);
        s_lastTime = nowTicks;
        s_frames = 0;
    }
    g_fpsCounter = s_fps;
}
