/*
Under an4rch Development Public Source License 1.0
*/

#include "ArrayList.hpp"
#include "../Animations/Animations.hpp"
#include "../Modules/ModuleHeader.hpp"
#include "../GUI/GUI.hpp"
#include <algorithm>
#include <cmath>
#include "../Utils/HudElement.hpp"

extern bool g_showMenu;

namespace ArrayList {
    static std::vector<ModuleInfo> g_modules;
    static bool g_initialized = false;
    HudElement* g_hud = nullptr;

    // Default settings
    bool g_enabled = true;
    ImVec4 g_bgColor = ImVec4(0.02f, 0.02f, 0.06f, 1.0f);
    float g_bgOpacity = 0.75f;
    bool g_showSideBar = true;
    ImVec4 g_sideBarColor = ImVec4(1.00f, 0.40f, 0.80f, 1.00f); // FF66CCFF (Pink)
    bool g_chromaSideBar = true;
    bool g_roundedBorders = false;
    float g_borderRadius = 4.0f;
    bool g_showSuffix = true;

    // Helper for chroma effect
    ImVec4 GetArrayListChroma(float index, float total) {
        float time = (float)GetTickCount64() / 1000.0f;
        
        // Use settings color or theme colors
        ImVec4 col1 = g_chromaSideBar ? ImVec4(1.00f, 0.40f, 0.80f, 1.00f) : g_sideBarColor; // Pink
        ImVec4 col2(0.60f, 0.50f, 1.00f, 1.00f); // Purple (9980FFFF) blend
        
        float blend = (sinf(time * 2.0f + index * 0.5f) + 1.0f) * 0.5f;
        return ImVec4(
            col1.x + (col2.x - col1.x) * blend,
            col1.y + (col2.y - col1.y) * blend,
            col1.z + (col2.z - col1.z) * blend,
            1.0f
        );
    }

    void UpdateModules() {
        // List of all available modules to track
        struct ModEntry { std::string name; std::string suffix; bool enabled; };
        
        std::vector<ModEntry> currentStates;
        currentStates.push_back({"Reach", std::to_string((int)Reach::g_reachValue) + "m", Reach::g_reachEnabled});
        currentStates.push_back({"Hitbox", std::to_string((int)(Hitbox::g_hitboxValue * 10)) + "x", Hitbox::g_hitboxEnabled});
        currentStates.push_back({"AutoSprint", "", AutoSprint::g_autoSprintEnabled});
        currentStates.push_back({"FullBright", "", FullBright::g_fullBrightEnabled});
        currentStates.push_back({"Timer", std::to_string((int)Timer::g_timerValue) + "x", Timer::g_timerEnabled});
        currentStates.push_back({"UnlockFPS", std::to_string((int)UnlockFPS::g_fpsLimit) + "fps", UnlockFPS::g_unlockFpsEnabled});
        currentStates.push_back({"MotionBlur", "", MotionBlur::g_motionBlurEnabled});
        currentStates.push_back({"Keystrokes", "", Keystrokes::g_showKeystrokes});
        currentStates.push_back({"CPSCounter", "", CPSCounter::g_showCpsCounter});
        currentStates.push_back({"FPS Overlay", "", FPSOverlay::g_showFpsOverlay});
        currentStates.push_back({"Ping Counter", std::to_string(PingCounter::g_currentPing) + "ms", PingCounter::g_showPingCounter});
        currentStates.push_back({"Render Info", "", RenderInfo::g_showRenderInfo});
        currentStates.push_back({"Watermark", "", Watermark::g_showWatermark});

        // Initialize internal state if needed
        if (!g_initialized) {
            for (const auto& m : currentStates) {
                g_modules.push_back({m.name, m.suffix, m.enabled, m.enabled ? 1.0f : 0.0f, 0.0f});
            }
            g_initialized = true;
        }

        // Sync states and suffixes
        for (size_t i = 0; i < currentStates.size(); i++) {
            g_modules[i].enabled = currentStates[i].enabled;
            g_modules[i].suffix = currentStates[i].suffix;
            
            // Animation logic
            float target = g_modules[i].enabled ? 1.0f : 0.0f;
            g_modules[i].animation += (target - g_modules[i].animation) * 0.12f;
        }
    }

    void RenderMenu() {
        GUI::RenderCustomSwitch("ArrayList", &g_enabled);
        if (GUI::BeginModuleSettings("ArrayList", &g_enabled)) {
            ImGui::ColorEdit4("Background Color", (float*)&g_bgColor, ImGuiColorEditFlags_NoInputs);
            ImGui::SliderFloat("Opacity", &g_bgOpacity, 0.0f, 1.0f, "%.2f");
            
            ImGui::Separator();
            
            GUI::RenderCustomSwitch("Show Side Bar", &g_showSideBar);
            if (g_showSideBar) {
                GUI::RenderCustomSwitch("Chroma Side Bar", &g_chromaSideBar);
                if (!g_chromaSideBar) {
                    ImGui::ColorEdit4("Side Bar Color", (float*)&g_sideBarColor, ImGuiColorEditFlags_NoInputs);
                }
            }
            
            ImGui::Separator();
            
            GUI::RenderCustomSwitch("Rounded Borders", &g_roundedBorders);
            if (g_roundedBorders) {
                ImGui::SliderFloat("Radius", &g_borderRadius, 0.0f, 12.0f, "%.0f px");
            }
            
            GUI::RenderCustomSwitch("Show Suffixes", &g_showSuffix);
            
            GUI::EndModuleSettings();
        }
    }

    void Render() {
        if (!g_enabled) return;
        UpdateModules();
        if (!g_hud) return;

        // Filter and sort active modules
        std::vector<ModuleInfo*> activeMods;
        for (auto& m : g_modules) {
            if (m.animation > 0.001f) {
                activeMods.push_back(&m);
            }
        }

        ImDrawList* draw = ImGui::GetForegroundDrawList();
        
        if (activeMods.empty()) {
            if (g_showMenu) {
                // Show empty draggable area
                draw->AddRect(g_hud->pos, ImVec2(g_hud->pos.x + g_hud->size.x, g_hud->pos.y + 20), IM_COL32(255, 255, 255, 80));
                draw->AddText(ImVec2(g_hud->pos.x + 5, g_hud->pos.y + 2), IM_COL32(255, 255, 255, 150), "ArrayList (Empty)");
            }
            return;
        }

        // Determine alignment based on screen position
        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        bool rightAligned = (g_hud->pos.x > screenSize.x / 2.0f);

        // Sorting by width (longest first)
        std::sort(activeMods.begin(), activeMods.end(), [](ModuleInfo* a, ModuleInfo* b) {
            float wa = ImGui::CalcTextSize(a->name.c_str()).x + (a->suffix.empty() ? 0 : ImGui::CalcTextSize(a->suffix.c_str()).x + 10);
            float wb = ImGui::CalcTextSize(b->name.c_str()).x + (b->suffix.empty() ? 0 : ImGui::CalcTextSize(b->suffix.c_str()).x + 10);
            return wa > wb;
        });

        float yOffset = g_hud->pos.y;
        float baseSpacing = 22.0f;
        float maxW = 0.0f;

        for (size_t i = 0; i < activeMods.size(); i++) {
            ModuleInfo* m = activeMods[i];
            
            float anim = Animations::EaseOutExpo(m->animation);
            float currentSpacing = baseSpacing * anim;
            
            std::string fullText = m->name + (m->suffix.empty() ? "" : " [" + m->suffix + "]");
            ImVec2 textSize = ImGui::CalcTextSize(fullText.c_str());
            if (textSize.x > maxW) maxW = textSize.x;
            
            float xPos;
            if (rightAligned) {
                xPos = g_hud->pos.x + g_hud->size.x - (textSize.x + 10) * anim;
            } else {
                xPos = g_hud->pos.x + 10 * anim;
            }
            
            float yPos = yOffset;
            
            // Background
            ImU32 bgColU32 = ImGui::GetColorU32(ImVec4(g_bgColor.x, g_bgColor.y, g_bgColor.z, g_bgOpacity * anim));
            float rounding = g_roundedBorders ? g_borderRadius : 0.0f;
            
            if (rightAligned) {
                draw->AddRectFilled(ImVec2(xPos - 8, yPos), ImVec2(g_hud->pos.x + g_hud->size.x, yPos + currentSpacing), bgColU32, rounding);
            } else {
                draw->AddRectFilled(ImVec2(g_hud->pos.x, yPos), ImVec2(xPos + textSize.x + 8, yPos + currentSpacing), bgColU32, rounding);
            }
            
            // Accent Line
            if (g_showSideBar) {
                ImVec4 chromaCol = GetArrayListChroma((float)i, (float)activeMods.size());
                if (!g_chromaSideBar) chromaCol = g_sideBarColor;
                chromaCol.w = anim;
                
                if (rightAligned) {
                    draw->AddRectFilled(ImVec2(g_hud->pos.x + g_hud->size.x - 3, yPos), ImVec2(g_hud->pos.x + g_hud->size.x, yPos + currentSpacing), ImGui::GetColorU32(chromaCol), rounding);
                } else {
                    draw->AddRectFilled(ImVec2(g_hud->pos.x, yPos), ImVec2(g_hud->pos.x + 3, yPos + currentSpacing), ImGui::GetColorU32(chromaCol), rounding);
                }
            }
            
            // Render Text
            if (anim > 0.4f) {
                ImU32 textCol = g_chromaSideBar ? ImGui::GetColorU32(GetArrayListChroma((float)i, (float)activeMods.size())) : IM_COL32(255, 255, 255, (int)(anim * 255.0f));
                float textY = yPos + (currentSpacing - textSize.y) * 0.5f;
                
                draw->AddText(ImVec2(xPos, textY), textCol, m->name.c_str());
                
                if (!m->suffix.empty() && g_showSuffix) {
                    float nameWidth = ImGui::CalcTextSize(m->name.c_str()).x;
                    std::string sText = " [" + m->suffix + "]";
                    draw->AddText(ImVec2(xPos + nameWidth, textY), IM_COL32(160, 160, 180, (int)(anim * 220.0f)), sText.c_str());
                }
            }
            
            yOffset += currentSpacing;
        }

        // Update HUD size for dragging hitbox
        g_hud->size.x = maxW + 20;
        g_hud->size.y = yOffset - g_hud->pos.y;
        
        // Show draggable area border when menu is open
        if (g_showMenu) {
            draw->AddRect(g_hud->pos, ImVec2(g_hud->pos.x + g_hud->size.x, g_hud->pos.y + g_hud->size.y), IM_COL32(255, 255, 255, 80));
        }
    }
}
