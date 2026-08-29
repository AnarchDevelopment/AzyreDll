/*
Under an4rch Development Public Source License 1.0
*/

#include "MouseStrokes.hpp"
#include "../../ModuleManager.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../../GUI/GUI.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>

// Static member definitions
bool MouseStrokes::g_showMouseStrokes = false;
float MouseStrokes::g_mouseStrokesAnim = 0.0f;
ULONGLONG MouseStrokes::g_mouseStrokesEnableTime = 0;
ULONGLONG MouseStrokes::g_mouseStrokesDisableTime = 0;
HudElement* MouseStrokes::g_mouseStrokesHud = nullptr;

float MouseStrokes::g_accumulatedX = 0.0f;
float MouseStrokes::g_accumulatedY = 0.0f;
ImVec2 MouseStrokes::g_currentCursorPos = ImVec2(0.0f, 0.0f);
std::vector<MouseStrokes::CircleTrail> MouseStrokes::g_trails;
POINT MouseStrokes::g_lastMousePoint = { 0, 0 };
bool MouseStrokes::g_hasLastPoint = false;

// Default Settings
float MouseStrokes::g_uiScale = 1.0f;
float MouseStrokes::g_boxSize = 75.0f;
float MouseStrokes::g_rounding = 10.0f;
bool MouseStrokes::g_showBackground = true;
ImVec4 MouseStrokes::g_bgColor = ImVec4(0.07f, 0.07f, 0.10f, 0.65f);
bool MouseStrokes::g_showBorder = true;
ImVec4 MouseStrokes::g_borderColor = ImVec4(0.28f, 0.28f, 0.38f, 0.70f);
float MouseStrokes::g_borderWidth = 1.0f;
bool MouseStrokes::g_showShadow = true;
ImVec4 MouseStrokes::g_shadowColor = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
bool MouseStrokes::g_showGlow = false;
ImVec4 MouseStrokes::g_glowColor = ImVec4(1.0f, 1.0f, 1.0f, 0.6f);
float MouseStrokes::g_glowAmount = 15.0f;
ImVec4 MouseStrokes::g_cursorColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
float MouseStrokes::g_dotRadius = 4.0f;
float MouseStrokes::g_sensitivity = 2.0f;
float MouseStrokes::g_decayRate = 9.0f;
float MouseStrokes::g_smoothSpeed = 22.0f;
bool MouseStrokes::g_showCrosshair = false;
ImVec4 MouseStrokes::g_crosshairColor = ImVec4(1.0f, 1.0f, 1.0f, 0.15f);
bool MouseStrokes::g_clickEffect = true;
ImVec4 MouseStrokes::g_clickColor = ImVec4(0.2f, 0.85f, 1.0f, 1.0f);

void MouseStrokes::Initialize(HudElement* hud) {
    g_mouseStrokesHud = hud;
    if (g_mouseStrokesHud) {
        g_mouseStrokesHud->size = ImVec2(g_boxSize * g_uiScale, g_boxSize * g_uiScale);
        if (g_mouseStrokesHud->pos.x == 0 && g_mouseStrokesHud->pos.y == 0) {
            g_mouseStrokesHud->pos = ImVec2(10, 420);
        }
    }
}

void MouseStrokes::UpdateAnimation(ULONGLONG now) {
    if (g_showMouseStrokes) {
        if (g_mouseStrokesAnim < 1.0f) {
            g_mouseStrokesAnim += 0.05f;
            if (g_mouseStrokesAnim > 1.0f) g_mouseStrokesAnim = 1.0f;
        }
    } else {
        if (g_mouseStrokesAnim > 0.0f) {
            g_mouseStrokesAnim -= 0.05f;
            if (g_mouseStrokesAnim < 0.0f) g_mouseStrokesAnim = 0.0f;
        }
    }

    static ULONGLONG s_lastTime = now;
    float dt = (float)(now - s_lastTime) / 1000.0f;
    s_lastTime = now;
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.001f) dt = 0.001f;

    UpdateMovement(dt);
}

void MouseStrokes::OnRawMouseInput(int dx, int dy) {
    if (!g_showMouseStrokes && g_mouseStrokesAnim <= 0.001f) return;

    g_accumulatedX += (float)dx * g_sensitivity;
    g_accumulatedY += (float)dy * g_sensitivity;

    const float maxMovement = 200.0f;
    g_accumulatedX = max(-maxMovement, min(maxMovement, g_accumulatedX));
    g_accumulatedY = max(-maxMovement, min(maxMovement, g_accumulatedY));
}

void MouseStrokes::UpdateMovement(float dt) {
    // Fallback cursor delta polling when raw input is not available
    POINT pt;
    if (GetCursorPos(&pt)) {
        if (g_hasLastPoint) {
            int cdx = pt.x - g_lastMousePoint.x;
            int cdy = pt.y - g_lastMousePoint.y;
            extern bool g_showMenu;
            if (!g_showMenu && (cdx != 0 || cdy != 0)) {
                g_accumulatedX += (float)cdx * g_sensitivity;
                g_accumulatedY += (float)cdy * g_sensitivity;
                const float maxMovement = 200.0f;
                g_accumulatedX = max(-maxMovement, min(maxMovement, g_accumulatedX));
                g_accumulatedY = max(-maxMovement, min(maxMovement, g_accumulatedY));
            }
        }
        g_lastMousePoint = pt;
        g_hasLastPoint = true;
    }

    // Decay movement smoothly back to center
    float decay = expf(-g_decayRate * dt);
    g_accumulatedX *= decay;
    g_accumulatedY *= decay;

    // Target displacement inside the square box
    float boxW = g_boxSize * g_uiScale;
    float maxOffset = (boxW * 0.5f - g_dotRadius * g_uiScale - 3.0f);
    if (maxOffset < 4.0f) maxOffset = 4.0f;

    float targetX = g_accumulatedX * 0.45f * g_uiScale;
    float targetY = g_accumulatedY * 0.45f * g_uiScale;
    targetX = max(-maxOffset, min(maxOffset, targetX));
    targetY = max(-maxOffset, min(maxOffset, targetY));

    // Smooth interpolation
    float t = 1.0f - expf(-g_smoothSpeed * dt);
    g_currentCursorPos.x += (targetX - g_currentCursorPos.x) * t;
    g_currentCursorPos.y += (targetY - g_currentCursorPos.y) * t;

    // Update trail
    if (g_mouseStrokesHud) {
        ImVec2 center = ImVec2(
            g_mouseStrokesHud->pos.x + g_mouseStrokesHud->size.x * 0.5f,
            g_mouseStrokesHud->pos.y + g_mouseStrokesHud->size.y * 0.5f
        );
        ImVec2 dotPos = ImVec2(center.x + g_currentCursorPos.x, center.y + g_currentCursorPos.y);
        g_trails.insert(g_trails.begin(), { dotPos.x, dotPos.y, 1.0f });
    }

    float fadeStep = powf(0.85f, dt * 60.0f);
    for (size_t i = 0; i < g_trails.size(); ) {
        g_trails[i].alpha *= fadeStep;
        if (g_trails[i].alpha < 0.015f || g_trails.size() > 40) {
            g_trails.erase(g_trails.begin() + i);
        } else {
            ++i;
        }
    }
}

void MouseStrokes::RenderDisplay(float screenWidth, float screenHeight) {
    if (g_mouseStrokesAnim <= 0.001f || !g_mouseStrokesHud) return;

    float totalSize = g_boxSize * g_uiScale;
    g_mouseStrokesHud->size = ImVec2(totalSize, totalSize);

    if (GUI::IsHudEditable()) {
        g_mouseStrokesHud->HandleDrag(true);
        g_mouseStrokesHud->ClampToScreen();

        ImDrawList* debugDraw = ImGui::GetForegroundDrawList();
        if (debugDraw) {
            ImVec2 p1 = g_mouseStrokesHud->pos;
            ImVec2 p2 = ImVec2(p1.x + g_mouseStrokesHud->size.x, p1.y + g_mouseStrokesHud->size.y);
            debugDraw->AddRect(p1, p2, ImColor(0, 255, 255, 200), g_rounding * g_uiScale, 0, 2.0f);
        }
    }

    float easedAnim = Animations::EaseOutExpo(g_mouseStrokesAnim);
    ImGui::SetNextWindowPos(g_mouseStrokesHud->pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(g_mouseStrokesHud->size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##MouseStrokes", nullptr, flags)) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetWindowPos();
        ImVec2 s = ImGui::GetWindowSize();
        float roundVal = g_rounding * g_uiScale;

        // Shadow
        if (g_showShadow) {
            GUI::DrawShadow(draw, p, s, roundVal, 15.0f * easedAnim, 0.45f * easedAnim);
        }

        // Background
        if (g_showBackground) {
            draw->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y),
                ImColor(g_bgColor.x, g_bgColor.y, g_bgColor.z, g_bgColor.w * easedAnim), roundVal);
        }

        // Glow
        if (g_showGlow) {
            ImVec4 glw = g_glowColor;
            draw->AddRect(p, ImVec2(p.x + s.x, p.y + s.y),
                ImColor(glw.x, glw.y, glw.z, glw.w * 0.4f * easedAnim), roundVal, 0, 2.5f);
        }

        // Border
        if (g_showBorder) {
            draw->AddRect(p, ImVec2(p.x + s.x, p.y + s.y),
                ImColor(g_borderColor.x, g_borderColor.y, g_borderColor.z, g_borderColor.w * easedAnim),
                roundVal, 0, g_borderWidth);
        }

        // Crosshair / Grid lines
        ImVec2 center = ImVec2(p.x + s.x * 0.5f, p.y + s.y * 0.5f);
        if (g_showCrosshair) {
            ImU32 chCol = ImColor(g_crosshairColor.x, g_crosshairColor.y, g_crosshairColor.z, g_crosshairColor.w * easedAnim);
            float crossLen = 6.0f * g_uiScale;
            draw->AddLine(ImVec2(center.x - crossLen, center.y), ImVec2(center.x + crossLen, center.y), chCol, 1.0f);
            draw->AddLine(ImVec2(center.x, center.y - crossLen), ImVec2(center.x, center.y + crossLen), chCol, 1.0f);
        }

        // Fading circle trails
        for (int i = (int)g_trails.size() - 1; i >= 0; --i) {
            const auto& tr = g_trails[i];
            float alphaVal = tr.alpha * g_cursorColor.w * easedAnim;
            if (alphaVal <= 0.005f) continue;
            ImU32 trCol = ImColor(g_cursorColor.x, g_cursorColor.y, g_cursorColor.z, alphaVal * 0.55f);
            float rad = g_dotRadius * g_uiScale * (0.35f + 0.65f * tr.alpha);
            draw->AddCircleFilled(ImVec2(tr.x, tr.y), rad, trCol);
        }

        // Current cursor dot
        bool clicking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        ImVec4 dotColor = (clicking && g_clickEffect) ? g_clickColor : g_cursorColor;
        ImVec2 dotPos = ImVec2(center.x + g_currentCursorPos.x, center.y + g_currentCursorPos.y);

        // Outer glow on main dot
        draw->AddCircleFilled(dotPos, (g_dotRadius + 1.5f) * g_uiScale,
            ImColor(dotColor.x, dotColor.y, dotColor.z, 0.35f * easedAnim));
        // Main dot
        draw->AddCircleFilled(dotPos, g_dotRadius * g_uiScale,
            ImColor(dotColor.x, dotColor.y, dotColor.z, dotColor.w * easedAnim));
        // Highlight ring
        draw->AddCircle(dotPos, g_dotRadius * g_uiScale,
            ImColor(1.0f, 1.0f, 1.0f, 0.45f * easedAnim), 0, 1.0f);
    }
    ImGui::End();
}

void MouseStrokes::RenderMenu() {
    bool before = g_showMouseStrokes;
    GUI::RenderCustomSwitch("MouseStrokes Module", &g_showMouseStrokes);
    if (g_showMouseStrokes != before) {
        if (g_showMouseStrokes) {
            g_mouseStrokesEnableTime = GetTickCount64();
            g_mouseStrokesDisableTime = 0;
            g_accumulatedX = 0.0f;
            g_accumulatedY = 0.0f;
            g_currentCursorPos = ImVec2(0.0f, 0.0f);
            g_trails.clear();
        } else {
            g_mouseStrokesDisableTime = GetTickCount64();
            g_mouseStrokesEnableTime = 0;
        }
    }

    if (GUI::BeginModuleSettings("MouseStrokes", &g_showMouseStrokes)) {
        GUI::RenderSlider("Scale##MS", &g_uiScale, 0.5f, 2.5f, "%.2f");
        GUI::RenderSlider("Box Size##MS", &g_boxSize, 40.0f, 160.0f, "%.0f px");
        GUI::RenderSlider("Dot Radius##MS", &g_dotRadius, 2.0f, 10.0f, "%.1f");
        GUI::RenderSlider("Sensitivity##MS", &g_sensitivity, 0.5f, 8.0f, "%.2f");
        GUI::RenderSlider("Decay Speed##MS", &g_decayRate, 2.0f, 25.0f, "%.1f");
        GUI::RenderSlider("Smoothness##MS", &g_smoothSpeed, 5.0f, 50.0f, "%.1f");
        GUI::RenderSlider("Rounding##MS", &g_rounding, 0.0f, 30.0f, "%.0f px");

        ImGui::ColorEdit4("Cursor Color##MS", (float*)&g_cursorColor, ImGuiColorEditFlags_NoInputs);

        GUI::RenderCustomSwitch("Show Background##MS", &g_showBackground);
        if (g_showBackground) {
            ImGui::ColorEdit4("Background Color##MS", (float*)&g_bgColor, ImGuiColorEditFlags_NoInputs);
        }

        GUI::RenderCustomSwitch("Show Border##MS", &g_showBorder);
        if (g_showBorder) {
            GUI::RenderSlider("Border Width##MS", &g_borderWidth, 0.5f, 4.0f, "%.1f");
            ImGui::ColorEdit4("Border Color##MS", (float*)&g_borderColor, ImGuiColorEditFlags_NoInputs);
        }

        GUI::RenderCustomSwitch("Show Shadow##MS", &g_showShadow);
        if (g_showShadow) {
            ImGui::ColorEdit4("Shadow Color##MS", (float*)&g_shadowColor, ImGuiColorEditFlags_NoInputs);
        }

        GUI::RenderCustomSwitch("Show Glow##MS", &g_showGlow);
        if (g_showGlow) {
            ImGui::ColorEdit4("Glow Color##MS", (float*)&g_glowColor, ImGuiColorEditFlags_NoInputs);
        }

        GUI::RenderCustomSwitch("Crosshair##MS", &g_showCrosshair);
        if (g_showCrosshair) {
            ImGui::ColorEdit4("Crosshair Color##MS", (float*)&g_crosshairColor, ImGuiColorEditFlags_NoInputs);
        }

        GUI::RenderCustomSwitch("Click Reaction##MS", &g_clickEffect);
        if (g_clickEffect) {
            ImGui::ColorEdit4("Click Color##MS", (float*)&g_clickColor, ImGuiColorEditFlags_NoInputs);
        }

        ImGui::TextDisabled("Drag the MouseStrokes box to reposition it.");

        GUI::EndModuleSettings();
    }
}
