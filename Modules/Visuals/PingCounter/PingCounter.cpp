/*
Under an4rch Development Public Source License 1.0
*/

#include "PingCounter.hpp"
#include "../../../Utils/HudElement.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <cstdlib>
#include <cstdio>
#include <cmath>

bool PingCounter::g_showPingCounter = false;
HudElement* PingCounter::g_pingHud = nullptr;

float PingCounter::g_pingAnim = 0.0f;
ULONGLONG PingCounter::g_pingEnableTime = 0;
ULONGLONG PingCounter::g_pingDisableTime = 0;

float PingCounter::g_pingTextScale = 1.0f;
bool PingCounter::g_showBackground = true;
float PingCounter::g_bgOpacity = 0.5f;
ImVec4 PingCounter::g_pingTextColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
ImVec4 PingCounter::g_pingCounterShadowColor = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
bool PingCounter::g_pingTextShadow = true;

int PingCounter::g_minPing = 20;
int PingCounter::g_maxPing = 45;
int PingCounter::g_currentPing = 30;
ULONGLONG PingCounter::g_lastPingUpdate = 0;
int PingCounter::g_pingUpdateInterval = 1500; 

void PingCounter::Initialize(HudElement* hud) {
    g_pingHud = hud;
}

void PingCounter::UpdatePing(ULONGLONG now) {
    if (now - g_lastPingUpdate >= g_pingUpdateInterval) {
        g_lastPingUpdate = now;
        if (g_minPing > g_maxPing) {
            int temp = g_minPing;
            g_minPing = g_maxPing;
            g_maxPing = temp;
        }
        int diff = g_maxPing - g_minPing;
        if (diff > 0) {
            int randomJitter = (rand() % (diff + 1));
            g_currentPing = g_minPing + randomJitter;
        } else {
            g_currentPing = g_minPing;
        }
    }
}

void PingCounter::UpdateAnimation(ULONGLONG now) {
    if (g_showPingCounter && g_pingEnableTime == 0) {
        g_pingEnableTime = now;
        g_pingDisableTime = 0;
    }
    if (!g_showPingCounter && g_pingDisableTime == 0 && g_pingEnableTime > 0) {
        g_pingDisableTime = now;
        g_pingEnableTime = 0;
    }
    
    if (g_pingEnableTime > 0) {
        float enableElapsed = (float)(now - g_pingEnableTime) / 1000.0f;
        g_pingAnim = fminf(1.0f, enableElapsed / 0.4f);
    }
    else if (g_pingDisableTime > 0) {
        float disableElapsed = (float)(now - g_pingDisableTime) / 1000.0f;
        float disableAnim = fminf(1.0f, disableElapsed / 0.3f);
        g_pingAnim = 1.0f - disableAnim;
        if (disableAnim >= 1.0f) {
            g_pingEnableTime = 0;
            g_pingDisableTime = 0;
        }
    }
}

void PingCounter::RenderDisplay(float sw, float sh) {
    if (g_showPingCounter || g_pingAnim > 0.01f) {
        UpdatePing(GetTickCount64());

        float easedAnim = Animations::EaseOutExpo(g_pingAnim);
        
        char text[64];
        sprintf_s(text, "Ping: %d ms", g_currentPing);
        
        ImFont* font = ImGui::GetFont();
        float fontSize = 18.0f * g_pingTextScale;
        ImVec2 textSize = ImGui::CalcTextSize(text);
        textSize.x *= g_pingTextScale;
        textSize.y *= g_pingTextScale;
        
        float paddingX = 8.0f * g_pingTextScale;
        float paddingY = 4.0f * g_pingTextScale;
        
        g_pingHud->size = ImVec2(textSize.x + paddingX * 2, textSize.y + paddingY * 2);
        
        if (g_pingHud->pos.x == 0 && g_pingHud->pos.y == 0) {
            g_pingHud->pos = ImVec2(sw - g_pingHud->size.x - 10, 10);
        }
        
        extern bool g_showMenu;
        if (g_showMenu) {
            g_pingHud->HandleDrag(true);
            g_pingHud->ClampToScreen();
        }
        
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        if (draw && easedAnim > 0.0f) {
            ImVec2 pos = g_pingHud->pos;
            
            if (g_showBackground) {
                ImU32 bgCol = IM_COL32(0, 0, 0, (int)(g_bgOpacity * easedAnim * 255.0f));
                draw->AddRectFilled(pos, ImVec2(pos.x + g_pingHud->size.x, pos.y + g_pingHud->size.y), bgCol, 4.0f);
            }
            
            float textX = pos.x + paddingX;
            float textY = pos.y + paddingY;
            
            if (g_pingTextShadow) {
                float shadowOffset = 1.0f * g_pingTextScale;
                ImU32 shadowCol = ImGui::GetColorU32(ImVec4(g_pingCounterShadowColor.x, g_pingCounterShadowColor.y, g_pingCounterShadowColor.z, g_pingCounterShadowColor.w * easedAnim));
                draw->AddText(font, fontSize, ImVec2(textX + shadowOffset, textY + shadowOffset), shadowCol, text);
            }
            
            ImU32 textCol = ImGui::GetColorU32(ImVec4(g_pingTextColor.x, g_pingTextColor.y, g_pingTextColor.z, g_pingTextColor.w * easedAnim));
            draw->AddText(font, fontSize, ImVec2(textX, textY), textCol, text);
            
            if (g_showMenu) {
                draw->AddRect(pos, ImVec2(pos.x + g_pingHud->size.x, pos.y + g_pingHud->size.y), IM_COL32(0, 150, 255, 200), 0.0f, 0, 2.0f);
            }
        }
    }
}

void PingCounter::RenderMenu() {
    GUI::RenderCustomSwitch("Ping Counter", &g_showPingCounter);
    ImGui::Text("Ping Counter, but only visual xddd, without UDP receive packets,\nbest client of the era nga $$$$$$$$$, charlie kirk six seven meme tung sahur oh yea");
    if (GUI::BeginModuleSettings("Ping Counter", &g_showPingCounter)) {
        if (ImGui::BeginTabBar("PingCounterTabs")) {
            if (ImGui::BeginTabItem("General")) {
                ImGui::SliderFloat("Scale", &g_pingTextScale, 0.5f, 3.0f, "%.2f");
                GUI::RenderCustomSwitch("Show Background", &g_showBackground);
                if (g_showBackground) {
                    ImGui::SliderFloat("Background Opacity", &g_bgOpacity, 0.0f, 1.0f, "%.2f");
                }
                ImGui::Checkbox("Text Shadow", &g_pingTextShadow);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Ping Settings")) {
                ImGui::SliderInt("Min Ping", &g_minPing, 0, 300, "%d ms");
                ImGui::SliderInt("Max Ping", &g_maxPing, 0, 300, "%d ms"); // hell nah LOLOLOLOL
                ImGui::SliderInt("Update Interval", &g_pingUpdateInterval, 100, 5000, "%d ms");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Colors")) {
                ImGui::ColorEdit4("Text Color", (float*)&g_pingTextColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Shadow Color", (float*)&g_pingCounterShadowColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        GUI::EndModuleSettings();
    }
}
