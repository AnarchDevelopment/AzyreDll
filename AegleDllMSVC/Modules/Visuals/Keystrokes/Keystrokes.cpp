/*
Under an4rch Development Public Source License 1.0
*/

#include "Keystrokes.hpp"
#include "../../../Utils/HudElement.hpp"
#include "Helper/HelperFunctions.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cmath>
#include <cstdio>

// Static member initialization
bool Keystrokes::g_showKeystrokes = false;
float Keystrokes::g_keystrokesAnim = 0.0f;
ULONGLONG Keystrokes::g_keystrokesEnableTime = 0;
ULONGLONG Keystrokes::g_keystrokesDisableTime = 0;
HudElement* Keystrokes::g_keystrokesHud = nullptr;

float Keystrokes::g_keystrokesUIScale = 1.0f;
bool Keystrokes::g_keystrokesBlurEffect = false;
float Keystrokes::g_keystrokesRounding = 11.0f;
bool Keystrokes::g_keystrokesShowBg = false;
bool Keystrokes::g_keystrokesRectShadow = false;
float Keystrokes::g_keystrokesRectShadowOffset = 0.02f;
bool Keystrokes::g_keystrokesBorder = false;
float Keystrokes::g_keystrokesBorderWidth = 1.0f;
bool Keystrokes::g_keystrokesGlow = false;
float Keystrokes::g_keystrokesGlowAmount = 50.0f;
bool Keystrokes::g_keystrokesGlowEnabled = false;
float Keystrokes::g_keystrokesGlowEnabledAmount = 50.0f;
float Keystrokes::g_keystrokesGlowSpeed = 1.0f;
float Keystrokes::g_keystrokesKeySpacing = 1.63f;
float Keystrokes::g_keystrokesEdSpeed = 1.0f;
float Keystrokes::g_keystrokesTextScale = 1.0f;
float Keystrokes::g_keystrokesTextScale2 = 1.0f;
float Keystrokes::g_keystrokesTextXOffset = 0.5f;
float Keystrokes::g_keystrokesTextYOffset = 0.5f;
bool Keystrokes::g_keystrokesTextShadow = false;
float Keystrokes::g_keystrokesTextShadowOffset = 0.003f;
bool Keystrokes::g_keystrokesLMBRMB = true;
bool Keystrokes::g_keystrokesHideCPS = true;
std::string Keystrokes::g_keystrokesWText = "W";
std::string Keystrokes::g_keystrokesAText = "A";
std::string Keystrokes::g_keystrokesSText = "S";
std::string Keystrokes::g_keystrokesDText = "D";
std::string Keystrokes::g_keystrokesLMBText = "LMB";
std::string Keystrokes::g_keystrokesRMBText = "RMB";
std::string Keystrokes::g_keystrokesLMBCPSText = "{value} CPS";
std::string Keystrokes::g_keystrokesRMBCPSText = "{value} CPS";

ImVec4 Keystrokes::g_keystrokesBgColor = ImVec4(0.314f, 0.314f, 0.314f, 0.55f);
ImVec4 Keystrokes::g_keystrokesEnabledColor = ImVec4(0.0f, 1.0f, 0.4f, 1.0f);
ImVec4 Keystrokes::g_keystrokesTextColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
ImVec4 Keystrokes::g_keystrokesTextEnabledColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
ImVec4 Keystrokes::g_keystrokesRectShadowColor = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
ImVec4 Keystrokes::g_keystrokesTextShadowColor = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
ImVec4 Keystrokes::g_keystrokesBorderColor = ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
ImVec4 Keystrokes::g_keystrokesGlowColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
ImVec4 Keystrokes::g_keystrokesGlowEnabledColor = ImVec4(0.941f, 0.941f, 1.0f, 1.0f);
ImVec4 Keystrokes::g_keystrokesEnabledShadowColor = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
ImVec4 Keystrokes::g_keystrokesDisabledShadowColor = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);

std::vector<ImVec4> Keystrokes::g_keystrokesStates(7, ImVec4(0.314f, 0.314f, 0.314f, 0.55f));
std::vector<ImVec4> Keystrokes::g_keystrokesTextStates(7, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
std::vector<ImVec4> Keystrokes::g_keystrokesShadowStates(7, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
float Keystrokes::g_keystrokesGlowModifier[7] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

bool Keystrokes::g_keystrokesShowSpacebar = true;
float Keystrokes::g_keystrokesSpacebarWidth = 1.0f;
bool Keystrokes::g_keystrokesShowMouseButtons = true;
bool Keystrokes::g_keystrokesShowLMBRMB = true;

float Keystrokes::g_keystrokesSpacebarHeight = 0.09f;
std::string Keystrokes::g_keystrokesLMBFormatText = "{value} CPS";
std::string Keystrokes::g_keystrokesRMBFormatText = "{value} CPS";

// CPS tracking for LMB and RMB
std::vector<ULONGLONG> Keystrokes::g_lmbClickTimes(100, 0);
std::vector<ULONGLONG> Keystrokes::g_rmbClickTimes(100, 0);
int Keystrokes::g_lmbClickIndex = 0;
int Keystrokes::g_rmbClickIndex = 0;
int Keystrokes::g_lmbCps = 0;
int Keystrokes::g_rmbCps = 0;
bool Keystrokes::g_prevLmbPressed = false;
bool Keystrokes::g_prevRmbPressed = false;
std::string Keystrokes::g_fontName = "Default";


static void DrawKeyGlow(ImDrawList* draw, const ImVec2& pos, const ImVec2& size, float rounding, const ImVec4& color, float intensity, float spread, float fade) {
    if (intensity <= 0.05f || spread <= 0.0f || fade <= 0.0f) return;

    const int layers = 12;
    for (int i = 1; i <= layers; i++) {
        float t = (float)i / (float)layers;
        float expand = spread * t;
        float alpha = color.w * intensity * fade * (1.0f - t) * (1.0f - t);
        if (alpha <= 0.003f) continue;

        ImU32 col = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, alpha));
        draw->AddRectFilled(
            ImVec2(pos.x - expand, pos.y - expand),
            ImVec2(pos.x + size.x + expand, pos.y + size.y + expand),
            col,
            rounding + expand
        );
    }
}


void Keystrokes::Initialize(HudElement* hud) {
    g_keystrokesHud = hud;
}

void Keystrokes::UpdateAnimation(ULONGLONG now) {
    // ⌨️ Keystrokes Animation - Fade in/out exponencial
    if (g_showKeystrokes && g_keystrokesEnableTime == 0) {
        g_keystrokesEnableTime = now;
        g_keystrokesDisableTime = 0;
    }
    if (!g_showKeystrokes && g_keystrokesDisableTime == 0 && g_keystrokesEnableTime > 0) {
        g_keystrokesDisableTime = now;
        g_keystrokesEnableTime = 0;
    }
    
    if (g_keystrokesEnableTime > 0) {
        float enableElapsed = (float)(now - g_keystrokesEnableTime) / 1000.0f;
        g_keystrokesAnim = fminf(1.0f, enableElapsed / 0.4f);
    }
    else if (g_keystrokesDisableTime > 0) {
        float disableElapsed = (float)(now - g_keystrokesDisableTime) / 1000.0f;
        float disableAnim = fminf(1.0f, disableElapsed / 0.3f);  // 300ms para desaparecer
        g_keystrokesAnim = 1.0f - disableAnim;
        if (disableAnim >= 1.0f) {
            g_keystrokesEnableTime = 0;
            g_keystrokesDisableTime = 0;
        }
    }
}

void Keystrokes::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    // Keystrokes module in array list
    if (g_showKeystrokes || (g_keystrokesDisableTime > 0 && g_keystrokesAnim > 0.01f)) {
        float keystrokesAlpha = g_keystrokesAnim * 255.0f;
        float slideOffset = -60.0f + (Animations::SmoothInertia(g_keystrokesAnim) * 60.0f);
        
        if (keystrokesAlpha > 1.0f) {
            char kBuf[64];
            sprintf_s(kBuf, "Keystrokes");
            float wK = ImGui::CalcTextSize(kBuf).x;
            float xPosK = arrayListStart.x + 300.0f - wK - 10;  // Adjusted from hardcoded 290
            draw->AddText(ImVec2(xPosK + slideOffset - 1, yPos + 1), IM_COL32(0, 0, 0, 220), kBuf); // Sombra
            draw->AddText(ImVec2(xPosK + slideOffset, yPos), IM_COL32(100, 200, 255, (int)keystrokesAlpha), kBuf);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void Keystrokes::RenderDisplay(float sw, float sh) {
    if (g_showKeystrokes || g_keystrokesAnim > 0.01f) {
        // Update color lerping based on key states
        bool handlerRes[7] = {
            (GetAsyncKeyState('W') & 0x8000) != 0,
            (GetAsyncKeyState('A') & 0x8000) != 0,
            (GetAsyncKeyState('S') & 0x8000) != 0,
            (GetAsyncKeyState('D') & 0x8000) != 0,
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0,
            (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0
        };

        // Update glow modifier based on key state
        for (int i = 0; i < 7; i++) {
            if (handlerRes[i]) {
                g_keystrokesStates[i] = LerpImVec4(g_keystrokesStates[i], g_keystrokesEnabledColor, 0.15f * g_keystrokesEdSpeed);
                g_keystrokesTextStates[i] = LerpImVec4(g_keystrokesTextStates[i], g_keystrokesTextEnabledColor, 0.15f * g_keystrokesEdSpeed);
                if (g_keystrokesTextShadow) {
                    g_keystrokesShadowStates[i] = LerpImVec4(g_keystrokesShadowStates[i], g_keystrokesEnabledShadowColor, 0.15f * g_keystrokesEdSpeed);
                }
                if (g_keystrokesGlowEnabled) {
                    g_keystrokesGlowModifier[i] = fminf(1.0f, g_keystrokesGlowModifier[i] + 0.2f * g_keystrokesGlowSpeed);
                }
            } else {
                g_keystrokesStates[i] = LerpImVec4(g_keystrokesStates[i], g_keystrokesBgColor, 0.15f * g_keystrokesEdSpeed);
                g_keystrokesTextStates[i] = LerpImVec4(g_keystrokesTextStates[i], g_keystrokesTextColor, 0.15f * g_keystrokesEdSpeed);
                if (g_keystrokesTextShadow) {
                    g_keystrokesShadowStates[i] = LerpImVec4(g_keystrokesShadowStates[i], g_keystrokesDisabledShadowColor, 0.15f * g_keystrokesEdSpeed);
                }
                if (g_keystrokesGlowEnabled) {
                    g_keystrokesGlowModifier[i] = fmaxf(0.0f, g_keystrokesGlowModifier[i] - 0.1f * g_keystrokesGlowSpeed);
                }
            }
        }

        float easedAnim = Animations::EaseOutExpo(g_keystrokesAnim);
        float keystrokesAlpha = easedAnim * 255.0f;
        
        // Keystrokes layout size calculation
        float scaledSize = 130.0f * g_keystrokesUIScale;
        float keySpacing = g_keystrokesKeySpacing * g_keystrokesUIScale;
        float keyHeight = 38.0f * g_keystrokesUIScale;
        float padding = 6.0f * g_keystrokesUIScale;
        
        float calculatedHeight = padding + keyHeight + keySpacing + keyHeight; // W + ASD
        if (g_keystrokesShowMouseButtons) {
            calculatedHeight += keySpacing + keyHeight;
        }
        if (g_keystrokesShowSpacebar) {
            calculatedHeight += keySpacing + (keyHeight * 0.7f); // Spacebar height is 0.7 * keyHeight
        }
        calculatedHeight += padding; // Bottom padding

        ImVec2 keystrokesSize = ImVec2(scaledSize, calculatedHeight);
        g_keystrokesHud->size = keystrokesSize;
        
        // Initialize position on first draw
        if (g_keystrokesHud->pos.y == 0) {
            g_keystrokesHud->pos = ImVec2(30, sh - 250);
        }
        
        // Handle drag and clamp when menu open
        extern bool g_showMenu;
        if (GUI::IsHudEditable()) {
            g_keystrokesHud->HandleDrag(true);
            g_keystrokesHud->ClampToScreen();
        }
        
        // Position with smooth animation
        float animatedYOffset = 150.0f - (150.0f * easedAnim);
        ImVec2 finalPos = ImVec2(
            g_keystrokesHud->pos.x,
            g_keystrokesHud->pos.y + animatedYOffset
        );
        
        if (finalPos.x < -10000 || finalPos.x > 10000) finalPos.x = 30;
        if (finalPos.y < -10000 || finalPos.y > 10000) finalPos.y = sh - 250;
        
        ImDrawList* keystrokesDraw = ImGui::GetForegroundDrawList();
        if (keystrokesDraw && keystrokesAlpha > 1.0f) {
            float baseX = finalPos.x;
            float baseY = finalPos.y;
            float containerWidth = scaledSize;
            float padding = 6.0f * g_keystrokesUIScale;
            float rounding = g_keystrokesRounding * g_keystrokesUIScale;
            float textShadowOffset = g_keystrokesTextShadowOffset * g_keystrokesUIScale * 10.0f;
            
            // Apply blur effect to background if enabled
            if (g_keystrokesShowBg && g_keystrokesBlurEffect) {
                keystrokesDraw->AddRectFilled(finalPos, ImVec2(finalPos.x + keystrokesSize.x, finalPos.y + keystrokesSize.y), IM_COL32(0, 0, 0, 20), rounding);
            }
            
            // Draw background shadow if enabled
            if (g_keystrokesShowBg && g_keystrokesRectShadow) {
                float shadowOffset = g_keystrokesRectShadowOffset * g_keystrokesUIScale * 10.0f;
                keystrokesDraw->AddRectFilled(
                    ImVec2(finalPos.x + shadowOffset, finalPos.y + shadowOffset),
                    ImVec2(finalPos.x + keystrokesSize.x + shadowOffset, finalPos.y + keystrokesSize.y + shadowOffset),
                    ImGui::GetColorU32(g_keystrokesRectShadowColor),
                    rounding
                );
            }
            
            // Draw main background if enabled
            if (g_keystrokesShowBg) {
                keystrokesDraw->AddRectFilled(finalPos, ImVec2(finalPos.x + keystrokesSize.x, finalPos.y + keystrokesSize.y), ImGui::GetColorU32(g_keystrokesBgColor), rounding);
            }
            
            bool wPressed = handlerRes[0];
            bool aPressed = handlerRes[1];
            bool sPressed = handlerRes[2];
            bool dPressed = handlerRes[3];
            bool lmbPressed = handlerRes[4];
            bool rmbPressed = handlerRes[5];
            bool spacePressed = handlerRes[6];
            
            ImFont* keystrokesFont = GUI::GetFontByName(g_fontName);
            if (keystrokesFont) {
                float asdWidth = (containerWidth - (2 * keySpacing)) / 3.0f;
                float wWidth = asdWidth;
                float wX = baseX + (containerWidth - wWidth) / 2.0f;
                ImVec2 wPos = ImVec2(wX, baseY + padding);

                // W Key - with lerped colors and glow
                ImU32 wBgCol = ImGui::GetColorU32(g_keystrokesStates[0]);
                
                // Glow for disabled state
                if (g_keystrokesGlow && g_keystrokesGlowModifier[0] > 0.05f) {
                    DrawKeyGlow(keystrokesDraw, wPos, ImVec2(wWidth, keyHeight), rounding, g_keystrokesGlowColor, g_keystrokesGlowModifier[0], g_keystrokesGlowAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                }
                
                // Glow for enabled state
                if (g_keystrokesGlowEnabled && wPressed && g_keystrokesGlowModifier[0] > 0.05f) {
                    DrawKeyGlow(keystrokesDraw, wPos, ImVec2(wWidth, keyHeight), rounding, g_keystrokesGlowEnabledColor, g_keystrokesGlowModifier[0], g_keystrokesGlowEnabledAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                }
                
                keystrokesDraw->AddRectFilled(wPos, ImVec2(wPos.x + wWidth, wPos.y + keyHeight), wBgCol, rounding);
                if (g_keystrokesBorder) {
                    keystrokesDraw->AddRect(wPos, ImVec2(wPos.x + wWidth, wPos.y + keyHeight), ImGui::GetColorU32(g_keystrokesBorderColor), rounding, 0, g_keystrokesBorderWidth);
                }
                
                float textPosX = wPos.x + (12.0f * g_keystrokesUIScale) + (g_keystrokesTextXOffset * g_keystrokesUIScale);
                float textPosY = wPos.y + (8.0f * g_keystrokesUIScale) + (g_keystrokesTextYOffset * g_keystrokesUIScale);
                float wasdTextScale = 16.0f * g_keystrokesUIScale * g_keystrokesTextScale;
                
                if (g_keystrokesTextShadow) {
                    keystrokesDraw->AddText(keystrokesFont, wasdTextScale, ImVec2(textPosX + textShadowOffset, textPosY + textShadowOffset), ImGui::GetColorU32(g_keystrokesShadowStates[0]), g_keystrokesWText.c_str());
                }
                keystrokesDraw->AddText(keystrokesFont, wasdTextScale, ImVec2(textPosX, textPosY), ImGui::GetColorU32(g_keystrokesTextStates[0]), g_keystrokesWText.c_str());
                
                float row2Y = baseY + padding + keyHeight + keySpacing;
                
                // A Key
                ImVec2 aPos = ImVec2(baseX, row2Y);
                ImU32 aBgCol = ImGui::GetColorU32(g_keystrokesStates[1]);
                
                if (g_keystrokesGlow && g_keystrokesGlowModifier[1] > 0.05f) {
                    DrawKeyGlow(keystrokesDraw, aPos, ImVec2(asdWidth, keyHeight), rounding, g_keystrokesGlowColor, g_keystrokesGlowModifier[1], g_keystrokesGlowAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                }
                
                if (g_keystrokesGlowEnabled && aPressed && g_keystrokesGlowModifier[1] > 0.05f) {
                    DrawKeyGlow(keystrokesDraw, aPos, ImVec2(asdWidth, keyHeight), rounding, g_keystrokesGlowEnabledColor, g_keystrokesGlowModifier[1], g_keystrokesGlowEnabledAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                }
                
                keystrokesDraw->AddRectFilled(aPos, ImVec2(aPos.x + asdWidth, aPos.y + keyHeight), aBgCol, rounding);
                if (g_keystrokesBorder) {
                    keystrokesDraw->AddRect(aPos, ImVec2(aPos.x + asdWidth, aPos.y + keyHeight), ImGui::GetColorU32(g_keystrokesBorderColor), rounding, 0, g_keystrokesBorderWidth);
                }
                
                float aTextPosX = aPos.x + (10.0f * g_keystrokesUIScale) + (g_keystrokesTextXOffset * g_keystrokesUIScale);
                float aTextPosY = aPos.y + (6.0f * g_keystrokesUIScale) + (g_keystrokesTextYOffset * g_keystrokesUIScale);
                float asdTextScale = 18.0f * g_keystrokesUIScale * g_keystrokesTextScale;
                
                if (g_keystrokesTextShadow) {
                    keystrokesDraw->AddText(keystrokesFont, asdTextScale, ImVec2(aTextPosX + textShadowOffset, aTextPosY + textShadowOffset), ImGui::GetColorU32(g_keystrokesShadowStates[1]), g_keystrokesAText.c_str());
                }
                keystrokesDraw->AddText(keystrokesFont, asdTextScale, ImVec2(aTextPosX, aTextPosY), ImGui::GetColorU32(g_keystrokesTextStates[1]), g_keystrokesAText.c_str());
                
                // S Key
                ImVec2 sPos = ImVec2(baseX + asdWidth + keySpacing, row2Y);
                ImU32 sBgCol = ImGui::GetColorU32(g_keystrokesStates[2]);
                
                if (g_keystrokesGlow && g_keystrokesGlowModifier[2] > 0.05f) {
                    DrawKeyGlow(keystrokesDraw, sPos, ImVec2(asdWidth, keyHeight), rounding, g_keystrokesGlowColor, g_keystrokesGlowModifier[2], g_keystrokesGlowAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                }
                
                if (g_keystrokesGlowEnabled && sPressed && g_keystrokesGlowModifier[2] > 0.05f) {
                    DrawKeyGlow(keystrokesDraw, sPos, ImVec2(asdWidth, keyHeight), rounding, g_keystrokesGlowEnabledColor, g_keystrokesGlowModifier[2], g_keystrokesGlowEnabledAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                }
                
                keystrokesDraw->AddRectFilled(sPos, ImVec2(sPos.x + asdWidth, sPos.y + keyHeight), sBgCol, rounding);
                if (g_keystrokesBorder) {
                    keystrokesDraw->AddRect(sPos, ImVec2(sPos.x + asdWidth, sPos.y + keyHeight), ImGui::GetColorU32(g_keystrokesBorderColor), rounding, 0, g_keystrokesBorderWidth);
                }
                
                float sTextPosX = sPos.x + (10.0f * g_keystrokesUIScale) + (g_keystrokesTextXOffset * g_keystrokesUIScale);
                float sTextPosY = sPos.y + (6.0f * g_keystrokesUIScale) + (g_keystrokesTextYOffset * g_keystrokesUIScale);
                
                if (g_keystrokesTextShadow) {
                    keystrokesDraw->AddText(keystrokesFont, asdTextScale, ImVec2(sTextPosX + textShadowOffset, sTextPosY + textShadowOffset), ImGui::GetColorU32(g_keystrokesShadowStates[2]), g_keystrokesSText.c_str());
                }
                keystrokesDraw->AddText(keystrokesFont, asdTextScale, ImVec2(sTextPosX, sTextPosY), ImGui::GetColorU32(g_keystrokesTextStates[2]), g_keystrokesSText.c_str());
                
                // D Key
                ImVec2 dPos = ImVec2(baseX + (asdWidth + keySpacing) * 2, row2Y);
                ImU32 dBgCol = ImGui::GetColorU32(g_keystrokesStates[3]);
                
                if (g_keystrokesGlow && g_keystrokesGlowModifier[3] > 0.05f) {
                    DrawKeyGlow(keystrokesDraw, dPos, ImVec2(asdWidth, keyHeight), rounding, g_keystrokesGlowColor, g_keystrokesGlowModifier[3], g_keystrokesGlowAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                }
                
                if (g_keystrokesGlowEnabled && dPressed && g_keystrokesGlowModifier[3] > 0.05f) {
                    DrawKeyGlow(keystrokesDraw, dPos, ImVec2(asdWidth, keyHeight), rounding, g_keystrokesGlowEnabledColor, g_keystrokesGlowModifier[3], g_keystrokesGlowEnabledAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                }
                
                keystrokesDraw->AddRectFilled(dPos, ImVec2(dPos.x + asdWidth, dPos.y + keyHeight), dBgCol, rounding);
                if (g_keystrokesBorder) {
                    keystrokesDraw->AddRect(dPos, ImVec2(dPos.x + asdWidth, dPos.y + keyHeight), ImGui::GetColorU32(g_keystrokesBorderColor), rounding, 0, g_keystrokesBorderWidth);
                }
                
                float dTextPosX = dPos.x + (10.0f * g_keystrokesUIScale) + (g_keystrokesTextXOffset * g_keystrokesUIScale);
                float dTextPosY = dPos.y + (6.0f * g_keystrokesUIScale) + (g_keystrokesTextYOffset * g_keystrokesUIScale);
                
                if (g_keystrokesTextShadow) {
                    keystrokesDraw->AddText(keystrokesFont, asdTextScale, ImVec2(dTextPosX + textShadowOffset, dTextPosY + textShadowOffset), ImGui::GetColorU32(g_keystrokesShadowStates[3]), g_keystrokesDText.c_str());
                }
                keystrokesDraw->AddText(keystrokesFont, asdTextScale, ImVec2(dTextPosX, dTextPosY), ImGui::GetColorU32(g_keystrokesTextStates[3]), g_keystrokesDText.c_str());
                
                // Row 3: LMB, RMB / Spacebar  
                float row3Y = row2Y + keyHeight + keySpacing;
                
                // Mouse button handling
                bool lmbPressed = handlerRes[4];
                bool rmbPressed = handlerRes[5];
                bool spacePressed = handlerRes[6];
                
                // Track CPS for LMB
                if (lmbPressed && !g_prevLmbPressed) {
                    g_lmbClickTimes[g_lmbClickIndex] = GetTickCount64();
                    g_lmbClickIndex = (g_lmbClickIndex + 1) % 100;
                }
                g_prevLmbPressed = lmbPressed;
                
                // Track CPS for RMB
                if (rmbPressed && !g_prevRmbPressed) {
                    g_rmbClickTimes[g_rmbClickIndex] = GetTickCount64();
                    g_rmbClickIndex = (g_rmbClickIndex + 1) % 100;
                }
                g_prevRmbPressed = rmbPressed;

                // Recalculate CPS every frame for smooth decay to 0
                int lmbCount = 0;
                int rmbCount = 0;
                ULONGLONG now = GetTickCount64();
                for (int i = 0; i < 100; i++) {
                    if (g_lmbClickTimes[i] > 0) {
                        if (now - g_lmbClickTimes[i] < 1000) lmbCount++;
                        else g_lmbClickTimes[i] = 0;
                    }
                    if (g_rmbClickTimes[i] > 0) {
                        if (now - g_rmbClickTimes[i] < 1000) rmbCount++;
                        else g_rmbClickTimes[i] = 0;
                    }
                }
                g_lmbCps = lmbCount;
                g_rmbCps = rmbCount;
                
                if (g_keystrokesShowMouseButtons) {
                    float lmbRmbWidth = (containerWidth - keySpacing) / 2.0f;
                    
                    // LMB
                    ImVec2 lmbPos = ImVec2(baseX, row3Y);
                    ImU32 lmbBgCol = ImGui::GetColorU32(g_keystrokesStates[4]);
                    
                    if (g_keystrokesGlow && g_keystrokesGlowModifier[4] > 0.05f) {
                        DrawKeyGlow(keystrokesDraw, lmbPos, ImVec2(lmbRmbWidth, keyHeight), rounding, g_keystrokesGlowColor, g_keystrokesGlowModifier[4], g_keystrokesGlowAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                    }
                    
                    if (g_keystrokesGlowEnabled && lmbPressed && g_keystrokesGlowModifier[4] > 0.05f) {
                        DrawKeyGlow(keystrokesDraw, lmbPos, ImVec2(lmbRmbWidth, keyHeight), rounding, g_keystrokesGlowEnabledColor, g_keystrokesGlowModifier[4], g_keystrokesGlowEnabledAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                    }
                    
                    keystrokesDraw->AddRectFilled(lmbPos, ImVec2(lmbPos.x + lmbRmbWidth, lmbPos.y + keyHeight), lmbBgCol, rounding);
                    if (g_keystrokesBorder) {
                        keystrokesDraw->AddRect(lmbPos, ImVec2(lmbPos.x + lmbRmbWidth, lmbPos.y + keyHeight), ImGui::GetColorU32(g_keystrokesBorderColor), rounding, 0, g_keystrokesBorderWidth);
                    }
                    
                    float lmbTextPosX = lmbPos.x + (15.0f * g_keystrokesUIScale) + (g_keystrokesTextXOffset * g_keystrokesUIScale);
                    float lmbTextPosY = lmbPos.y + (3.0f * g_keystrokesUIScale) + (g_keystrokesTextYOffset * g_keystrokesUIScale);
                    float mouseTextScale = 14.0f * g_keystrokesUIScale * g_keystrokesTextScale2;
                    
                    if (g_keystrokesTextShadow) {
                        keystrokesDraw->AddText(keystrokesFont, mouseTextScale, ImVec2(lmbTextPosX + textShadowOffset, lmbTextPosY + textShadowOffset), ImGui::GetColorU32(g_keystrokesShadowStates[4]), g_keystrokesLMBText.c_str());
                    }
                    keystrokesDraw->AddText(keystrokesFont, mouseTextScale, ImVec2(lmbTextPosX, lmbTextPosY), ImGui::GetColorU32(g_keystrokesTextStates[4]), g_keystrokesLMBText.c_str());
                    
                    // LMB CPS text
                    std::string lmbCpsStr = std::to_string(g_lmbCps) + " CPS";
                    float cpsMiniScale = 11.0f * g_keystrokesUIScale * g_keystrokesTextScale2;
                    if (g_keystrokesTextShadow) {
                        keystrokesDraw->AddText(keystrokesFont, cpsMiniScale, ImVec2(lmbTextPosX + textShadowOffset, lmbTextPosY + 19.0f * g_keystrokesUIScale + textShadowOffset), ImGui::GetColorU32(g_keystrokesShadowStates[4]), lmbCpsStr.c_str());
                    }
                    keystrokesDraw->AddText(keystrokesFont, cpsMiniScale, ImVec2(lmbTextPosX, lmbTextPosY + 19.0f * g_keystrokesUIScale), ImGui::GetColorU32(g_keystrokesTextStates[4]), lmbCpsStr.c_str());
                    
                    // RMB
                    if (g_keystrokesShowLMBRMB) {
                        ImVec2 rmbPos = ImVec2(baseX + lmbRmbWidth + keySpacing, row3Y);
                        ImU32 rmbBgCol = ImGui::GetColorU32(g_keystrokesStates[5]);
                        
                        if (g_keystrokesGlow && g_keystrokesGlowModifier[5] > 0.05f) {
                            DrawKeyGlow(keystrokesDraw, rmbPos, ImVec2(lmbRmbWidth, keyHeight), rounding, g_keystrokesGlowColor, g_keystrokesGlowModifier[5], g_keystrokesGlowAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                        }
                        
                        if (g_keystrokesGlowEnabled && rmbPressed && g_keystrokesGlowModifier[5] > 0.05f) {
                            DrawKeyGlow(keystrokesDraw, rmbPos, ImVec2(lmbRmbWidth, keyHeight), rounding, g_keystrokesGlowEnabledColor, g_keystrokesGlowModifier[5], g_keystrokesGlowEnabledAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                        }
                        
                        keystrokesDraw->AddRectFilled(rmbPos, ImVec2(rmbPos.x + lmbRmbWidth, rmbPos.y + keyHeight), rmbBgCol, rounding);
                        if (g_keystrokesBorder) {
                            keystrokesDraw->AddRect(rmbPos, ImVec2(rmbPos.x + lmbRmbWidth, rmbPos.y + keyHeight), ImGui::GetColorU32(g_keystrokesBorderColor), rounding, 0, g_keystrokesBorderWidth);
                        }
                        
                        float rmbTextPosX = rmbPos.x + (15.0f * g_keystrokesUIScale) + (g_keystrokesTextXOffset * g_keystrokesUIScale);
                        float rmbTextPosY = rmbPos.y + (3.0f * g_keystrokesUIScale) + (g_keystrokesTextYOffset * g_keystrokesUIScale);
                        
                        if (g_keystrokesTextShadow) {
                            keystrokesDraw->AddText(keystrokesFont, mouseTextScale, ImVec2(rmbTextPosX + textShadowOffset, rmbTextPosY + textShadowOffset), ImGui::GetColorU32(g_keystrokesShadowStates[5]), g_keystrokesRMBText.c_str());
                        }
                        keystrokesDraw->AddText(keystrokesFont, mouseTextScale, ImVec2(rmbTextPosX, rmbTextPosY), ImGui::GetColorU32(g_keystrokesTextStates[5]), g_keystrokesRMBText.c_str());
                        
                        // RMB CPS text
                        std::string rmbCpsStr = std::to_string(g_rmbCps) + " CPS";
                        if (g_keystrokesTextShadow) {
                            keystrokesDraw->AddText(keystrokesFont, cpsMiniScale, ImVec2(rmbTextPosX + textShadowOffset, rmbTextPosY + 19.0f * g_keystrokesUIScale + textShadowOffset), ImGui::GetColorU32(g_keystrokesShadowStates[5]), rmbCpsStr.c_str());
                        }
                        keystrokesDraw->AddText(keystrokesFont, cpsMiniScale, ImVec2(rmbTextPosX, rmbTextPosY + 19.0f * g_keystrokesUIScale), ImGui::GetColorU32(g_keystrokesTextStates[5]), rmbCpsStr.c_str());
                    }
                }
                
                // Row 4: Spacebar
                if (g_keystrokesShowSpacebar) {
                    float row4Y = row3Y + keyHeight + keySpacing;
                    float spacebarWidth = containerWidth;
                    float spacebarHeight = keyHeight * 0.7f;
                    
                    ImVec2 spacebarPos = ImVec2(baseX, row4Y);
                    ImU32 spacebarBgCol = ImGui::GetColorU32(g_keystrokesStates[6]);
                    
                    if (g_keystrokesGlow && g_keystrokesGlowModifier[6] > 0.05f) {
                        DrawKeyGlow(keystrokesDraw, spacebarPos, ImVec2(spacebarWidth, spacebarHeight), rounding, g_keystrokesGlowColor, g_keystrokesGlowModifier[6], g_keystrokesGlowAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                    }
                    
                    if (g_keystrokesGlowEnabled && spacePressed && g_keystrokesGlowModifier[6] > 0.05f) {
                        DrawKeyGlow(keystrokesDraw, spacebarPos, ImVec2(spacebarWidth, spacebarHeight), rounding, g_keystrokesGlowEnabledColor, g_keystrokesGlowModifier[6], g_keystrokesGlowEnabledAmount * 0.25f * g_keystrokesUIScale, easedAnim);
                    }
                    
                    keystrokesDraw->AddRectFilled(spacebarPos, ImVec2(spacebarPos.x + spacebarWidth, spacebarPos.y + spacebarHeight), spacebarBgCol, rounding);
                    if (g_keystrokesBorder) {
                        keystrokesDraw->AddRect(spacebarPos, ImVec2(spacebarPos.x + spacebarWidth, spacebarPos.y + spacebarHeight), ImGui::GetColorU32(g_keystrokesBorderColor), rounding, 0, g_keystrokesBorderWidth);
                    }
                    
                    // Spacebar text (centered)
                    float spaceTextPosX = spacebarPos.x + (spacebarWidth / 2.0f) - (25.0f * g_keystrokesUIScale / 2.0f) + (g_keystrokesTextXOffset * g_keystrokesUIScale);
                    float spaceTextPosY = spacebarPos.y + (spacebarHeight / 2.0f) - (8.0f * g_keystrokesUIScale / 2.0f) + (g_keystrokesTextYOffset * g_keystrokesUIScale);
                    
                    if (g_keystrokesTextShadow) {
                        keystrokesDraw->AddText(keystrokesFont, 16.0f * g_keystrokesUIScale, ImVec2(spaceTextPosX + textShadowOffset, spaceTextPosY + textShadowOffset), ImGui::GetColorU32(g_keystrokesShadowStates[6]), "___");
                    }
                    keystrokesDraw->AddText(keystrokesFont, 16.0f * g_keystrokesUIScale, ImVec2(spaceTextPosX, spaceTextPosY), ImGui::GetColorU32(g_keystrokesTextStates[6]), "___");
                }
            }
        }
        
        // DEBUG: show hitbox only when menu is open
        if (GUI::IsHudEditable()) {
            ImDrawList* debugDraw = ImGui::GetForegroundDrawList();
            if (debugDraw) {
                debugDraw->AddRect(
                    finalPos,
                    ImVec2(finalPos.x + keystrokesSize.x, finalPos.y + keystrokesSize.y),
                    IM_COL32(0, 150, 255, 200),
                    0.0f,
                    0,
                    2.0f
                );
            }
        }
    }
}

void Keystrokes::RenderMenu() {
    GUI::RenderCustomSwitch("Keystrokes", &g_showKeystrokes);
    
    if (GUI::BeginModuleSettings("Keystrokes", &g_showKeystrokes)) {
        if (ImGui::BeginTabBar("KeystrokesTabs")) {
            if (ImGui::BeginTabItem("General")) {
                GUI::RenderFontSelect("Font", g_fontName);
                ImGui::SliderFloat("Scale", &g_keystrokesUIScale, 0.5f, 2.0f, "%.2f");
                ImGui::SliderFloat("Rounding", &g_keystrokesRounding, 0.0f, 20.0f, "%.1f");
                GUI::RenderCustomSwitch("Blur Effect", &g_keystrokesBlurEffect);
                GUI::RenderCustomSwitch("Show Background", &g_keystrokesShowBg);
                ImGui::Checkbox("Background Shadow", &g_keystrokesRectShadow);
                ImGui::Checkbox("Text Shadow", &g_keystrokesTextShadow);
                ImGui::Checkbox("Border", &g_keystrokesBorder);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Layout")) {
                ImGui::Checkbox("Mouse Buttons", &g_keystrokesShowMouseButtons);
                ImGui::Checkbox("Spacebar", &g_keystrokesShowSpacebar);
                ImGui::SliderFloat("Key Spacing", &g_keystrokesKeySpacing, 0.5f, 5.0f, "%.2f");
                ImGui::SliderFloat("Highlight Speed", &g_keystrokesEdSpeed, 0.1f, 10.0f, "%.1f");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Glow")) {
                ImGui::Checkbox("Disabled Glow", &g_keystrokesGlow);
                ImGui::Checkbox("Enabled Glow", &g_keystrokesGlowEnabled);
                ImGui::SliderFloat("Glow Amount", &g_keystrokesGlowAmount, 0.0f, 100.0f, "%.0f");
                ImGui::SliderFloat("Enabled Glow Amount", &g_keystrokesGlowEnabledAmount, 0.0f, 100.0f, "%.0f");
                ImGui::SliderFloat("Glow Speed", &g_keystrokesGlowSpeed, 0.1f, 10.0f, "%.1f");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Colors")) {
                ImGui::ColorEdit4("Background Color", (float*)&g_keystrokesBgColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Enabled Color", (float*)&g_keystrokesEnabledColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Text Color", (float*)&g_keystrokesTextColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Text Enabled Color", (float*)&g_keystrokesTextEnabledColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Border Color", (float*)&g_keystrokesBorderColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Glow Color", (float*)&g_keystrokesGlowColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Glow Enabled Color", (float*)&g_keystrokesGlowEnabledColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Rect Shadow Color", (float*)&g_keystrokesRectShadowColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Text Shadow Color", (float*)&g_keystrokesTextShadowColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Enabled Shadow", (float*)&g_keystrokesEnabledShadowColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Disabled Shadow", (float*)&g_keystrokesDisabledShadowColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        GUI::EndModuleSettings();
    }
}

std::string Keystrokes::ProcessKeystrokesFormat(const std::string& format, int value) {
    std::string result = format;
    std::string formatUpper = format;
    for (char& c : formatUpper) c = std::toupper(c);
    
    size_t pos = formatUpper.find("{VALUE}");
    if (pos != std::string::npos) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", value);
        result.replace(pos, 7, buffer);
    }
    
    return result;
}
