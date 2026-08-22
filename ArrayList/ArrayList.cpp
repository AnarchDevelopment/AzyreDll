/*
Under an4rch Development Public Source License 1.0
*/

#include "ArrayList.hpp"
#include "../Animations/Animations.hpp"
#include "../Modules/ModuleHeader.hpp"
#include "../Modules/Visuals/ClickGUI/ClickGUI.hpp"
#include "../Modules/Globals.hpp"
#include "../GUI/GUI.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
#include "../Utils/HudElement.hpp"

extern bool g_showMenu;

namespace ArrayList {
    static std::vector<ModuleInfo> g_modules;
    static bool g_initialized = false;
    HudElement* g_hud = nullptr;

    // Default settings
    bool g_enabled = true;
    std::string g_fontName = "Default";
    ImVec4 g_bgColor = ImVec4(0.02f, 0.02f, 0.06f, 1.0f);
    float g_bgOpacity = 0.75f;
    bool g_showSideBar = true;
    ImVec4 g_sideBarColor = ImVec4(1.00f, 0.40f, 0.80f, 1.00f); // FF66CCFF (Pink)
    bool g_chromaSideBar = true;
    bool g_roundedBorders = false;
    float g_borderRadius = 4.0f;
    bool g_showSuffix = true;
    float g_size = 1.0f;

    bool g_chromaText = false;
    ImVec4 g_textColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 g_suffixColor = ImVec4(0.55f, 0.55f, 0.68f, 1.0f);
    float g_chromaSpeed = 2.0f;

    bool g_glowEnabled = false;
    float g_glowStrength = 3.0f;

    int g_animationStyle = 0;
    int g_sideMode = 0;

    int g_backgroundMode = 0;   // 0 = Normal, 1 = Mica Blur
    float g_blurRadius = 6.0f;
    float g_blurOpacity = 0.30f;

    float g_rowSpacing = 2.0f;
    float g_animationSpeed = 1.0f;
    float g_sideBarWidth = 3.0f;
    bool g_showBorder = false;
    ImVec4 g_borderColor = ImVec4(1.0f, 1.0f, 1.0f, 0.35f);
    float g_borderWidth = 1.0f;
    bool g_textShadow = true;
    float g_textShadowOffset = 1.5f;

    bool g_hasBlurRect = false;
    float g_blurRectX = 0.0f;
    float g_blurRectY = 0.0f;
    float g_blurRectW = 0.0f;
    float g_blurRectH = 0.0f;

    // Helper for chroma effect
    ImVec4 GetArrayListChroma(float index, float total) {
        float time = (float)GetTickCount64() / 1000.0f;
        
        // Use settings color or theme colors
        ImVec4 col1 = g_chromaSideBar ? ImVec4(1.00f, 0.40f, 0.80f, 1.00f) : g_sideBarColor; // Pink
        ImVec4 col2(0.60f, 0.50f, 1.00f, 1.00f); // Purple (9980FFFF) blend
        
        float blend = (sinf(time * g_chromaSpeed + index * 0.5f) + 1.0f) * 0.5f;
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
        currentStates.push_back({"Glide", "", Glide::g_glideEnabled});
        currentStates.push_back({"Fly", "", Fly::g_flyEnabled});
        currentStates.push_back({"FullBright", "", FullBright::g_fullBrightEnabled});
        currentStates.push_back({"Timer", std::to_string((int)Timer::g_timerValue) + "x", Timer::g_timerEnabled});
        currentStates.push_back({"UnlockFPS", std::to_string((int)UnlockFPS::g_fpsLimit) + "fps", UnlockFPS::g_unlockFpsEnabled});
        currentStates.push_back({"AutoClicker", std::to_string((int)AutoClicker::g_cps) + "cps", AutoClicker::g_enabled});
        currentStates.push_back({"AntiAFK", "", AntiAFK::g_enabled});
        currentStates.push_back({"Screenshot", "", Screenshot::g_enabled});
        currentStates.push_back({"MotionBlur", "", MotionBlur::g_motionBlurEnabled});
        currentStates.push_back({"Keystrokes", "", Keystrokes::g_showKeystrokes});
        currentStates.push_back({"CPSCounter", "", CPSCounter::g_showCpsCounter});
        currentStates.push_back({"FPS Overlay", "", FPSOverlay::g_showFpsOverlay});
        currentStates.push_back({"Ping Counter", (PingCounter::g_currentPing >= 0) ? std::to_string(PingCounter::g_currentPing) + "ms" : "--", PingCounter::g_showPingCounter});
        currentStates.push_back({"Player Info", "", PlayerInfo::g_showPlayerInfo});
        currentStates.push_back({"Render Info", "", RenderInfo::g_showRenderInfo});
        currentStates.push_back({"Watermark", "", Watermark::g_showWatermark});

        // Initialize internal state if needed. Rebuilds safely if the module set
        // ever changes (preserving existing animation state by name).
        if (!g_initialized || g_modules.size() != currentStates.size()) {
            std::vector<ModuleInfo> rebuilt;
            rebuilt.reserve(currentStates.size());
            for (const auto& m : currentStates) {
                float anim = m.enabled ? 1.0f : 0.0f;
                float width = 0.0f;
                for (const auto& old : g_modules) {
                    if (old.name == m.name) { anim = old.animation; width = old.width; break; }
                }
                rebuilt.push_back({ m.name, m.suffix, m.enabled, anim, width });
            }
            g_modules = std::move(rebuilt);
            g_initialized = true;
        }

        // Sync states and suffixes (frame-rate independent animation)
        float dt = ImGui::GetIO().DeltaTime;
        if (dt <= 0.0f) dt = 0.016f;
        float speed = 8.0f * g_animationSpeed;
        for (size_t i = 0; i < currentStates.size(); i++) {
            g_modules[i].enabled = currentStates[i].enabled;
            g_modules[i].suffix = currentStates[i].suffix;

            // Animation logic
            float target = g_modules[i].enabled ? 1.0f : 0.0f;
            g_modules[i].animation = Animations::Approach(g_modules[i].animation, target, dt, speed);
        }
    }

    void RenderMenu() {
        GUI::RenderCustomSwitch("ArrayList", &g_enabled);
        if (GUI::BeginModuleSettings("ArrayList", &g_enabled)) {
            GUI::RenderFontSelect("Font", g_fontName);
            GUI::RenderSlider("Size", &g_size, 0.5f, 2.0f, "%.2fx");

            ImGui::Separator();
            ImGui::TextDisabled("Text");
            GUI::RenderCustomSwitch("Chroma Text", &g_chromaText);
            if (!g_chromaText) {
                ImGui::ColorEdit4("Text Color", (float*)&g_textColor, ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Suffix Color", (float*)&g_suffixColor, ImGuiColorEditFlags_NoInputs);
            }
            if (g_chromaText || g_chromaSideBar) {
                GUI::RenderSlider("Chroma Speed", &g_chromaSpeed, 0.5f, 6.0f, "%.1fx");
            }
            GUI::RenderCustomSwitch("Text Glow", &g_glowEnabled);
            if (g_glowEnabled) {
                GUI::RenderSlider("Glow Strength", &g_glowStrength, 1.0f, 8.0f, "%.0f");
            }

            ImGui::Separator();
            ImGui::TextDisabled("Behavior");
            const char* animStyles[] = { "Slide", "Fade", "Stagger" };
            ImGui::SetNextItemWidth(-1.0f);
            GUI::RenderCombo("##al_anim_style", &g_animationStyle, animStyles, IM_ARRAYSIZE(animStyles));
            const char* sideModes[] = { "Auto", "Left", "Right" };
            ImGui::SetNextItemWidth(-1.0f);
            GUI::RenderCombo("##al_side_mode", &g_sideMode, sideModes, IM_ARRAYSIZE(sideModes));

            ImGui::Separator();

            ImGui::TextDisabled("Background");
            const char* bgModes[] = { "Normal", "Mica Blur" };
            ImGui::SetNextItemWidth(-1.0f);
            GUI::RenderCombo("##al_bg_mode", &g_backgroundMode, bgModes, IM_ARRAYSIZE(bgModes));

            if (g_backgroundMode == 1) {
                GUI::RenderSlider("Blur Radius", &g_blurRadius, 1.0f, 20.0f, "%.1f");
                GUI::RenderSlider("Blur Opacity", &g_blurOpacity, 0.0f, 1.0f, "%.2f");
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.8f, 1.0f), "Mica Blur background can may contain errors");
                ImGui::TextDisabled("Mica Blur frosted the scene behind the list.\nLower the opacity below for a clearer effect.");
            } else {
                ImGui::ColorEdit4("Background Color", (float*)&g_bgColor, ImGuiColorEditFlags_NoInputs);
                GUI::RenderSlider("Opacity", &g_bgOpacity, 0.0f, 1.0f, "%.2f");
            }

            ImGui::Separator();

            GUI::RenderCustomSwitch("Show Side Bar", &g_showSideBar);
            if (g_showSideBar) {
                GUI::RenderSlider("Side Bar Width", &g_sideBarWidth, 1.0f, 8.0f, "%.0f px");
                GUI::RenderCustomSwitch("Chroma Side Bar", &g_chromaSideBar);
                if (!g_chromaSideBar) {
                    ImGui::ColorEdit4("Side Bar Color", (float*)&g_sideBarColor, ImGuiColorEditFlags_NoInputs);
                }
            }

            ImGui::Separator();

            GUI::RenderSlider("Row Spacing", &g_rowSpacing, 0.0f, 12.0f, "%.1f px");
            GUI::RenderSlider("Animation Speed", &g_animationSpeed, 0.2f, 3.0f, "%.1fx");

            GUI::RenderCustomSwitch("Rounded Borders", &g_roundedBorders);
            if (g_roundedBorders) {
                GUI::RenderSlider("Radius", &g_borderRadius, 0.0f, 12.0f, "%.0f px");
            }

            GUI::RenderCustomSwitch("Border", &g_showBorder);
            if (g_showBorder) {
                ImGui::ColorEdit4("Border Color", (float*)&g_borderColor, ImGuiColorEditFlags_NoInputs);
                GUI::RenderSlider("Border Width", &g_borderWidth, 0.5f, 3.0f, "%.1f px");
            }

            GUI::RenderCustomSwitch("Text Shadow", &g_textShadow);
            if (g_textShadow) {
                GUI::RenderSlider("Shadow Offset", &g_textShadowOffset, 0.5f, 4.0f, "%.1f px");
            }

            GUI::RenderCustomSwitch("Show Suffixes", &g_showSuffix);

            GUI::EndModuleSettings();
        }
    }

    void Render() {
        if (!g_enabled || !g_hud) {
            g_hasBlurRect = false;
            return;
        }
        UpdateModules();

        ImFont* font = GUI::GetFontByName(g_fontName);
        ImGui::PushFont(font);

        const float s = g_size;
        const float fontPx = ImGui::GetFontSize() * s;
        auto ts = [&](const std::string& t) {
            return font->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, t.c_str());
        };

        // Filter and sort active modules
        std::vector<ModuleInfo*> activeMods;
        for (auto& m : g_modules) {
            if (m.animation > 0.001f) {
                activeMods.push_back(&m);
            }
        }

        ImDrawList* draw = ImGui::GetForegroundDrawList();

        if (activeMods.empty()) {
            g_hud->size.x = 140.0f * s;
            g_hud->size.y = 20.0f * s;
            g_hasBlurRect = false;
            if (GUI::IsHudEditable()) {
                // Show empty draggable area
                draw->AddRect(g_hud->pos, ImVec2(g_hud->pos.x + g_hud->size.x, g_hud->pos.y + g_hud->size.y), IM_COL32(255, 255, 255, 80));
                draw->AddText(ImVec2(g_hud->pos.x + 5, g_hud->pos.y + 2), IM_COL32(255, 255, 255, 150), "ArrayList (Empty)");
            }
            ImGui::PopFont();
            return;
        }

        // Effective row width (respects the suffix toggle)
        auto rowWidth = [&](const ModuleInfo* m) {
            float w = ts(m->name).x;
            if (g_showSuffix && !m->suffix.empty())
                w += ts(" [" + m->suffix + "]").x;
            return w;
        };

        // Determine alignment based on screen position (or manual override)
        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        bool rightAligned = (g_sideMode == 2) || (g_sideMode == 0 && g_hud->pos.x > screenSize.x / 2.0f);

        // Sorting by width (longest first)
        std::sort(activeMods.begin(), activeMods.end(), [&rowWidth](ModuleInfo* a, ModuleInfo* b) {
            return rowWidth(a) > rowWidth(b);
        });

        float yOffset = g_hud->pos.y;
        float baseSpacing = (20.0f + g_rowSpacing) * s;
        float maxW = 0.0f;
        float rowH = 0.0f;
        const float padX = 8.0f * s;
        const float textPad = 10.0f * s;

        for (size_t i = 0; i < activeMods.size(); i++) {
            ModuleInfo* m = activeMods[i];

            float anim = Animations::EaseOutExpo(m->animation);
            float currentSpacing = baseSpacing * anim;
            rowH = (rowH > currentSpacing) ? rowH : currentSpacing;

            std::string fullText = m->name + (m->suffix.empty() || !g_showSuffix ? "" : " [" + m->suffix + "]");
            ImVec2 textSize = ts(fullText);
            if (textSize.x > maxW) maxW = textSize.x;

            // Movement/alpha factor (stagger delays each row slightly)
            float moveT = anim;
            if (g_animationStyle == 2) {
                float lag = m->animation - (float)i * 0.04f;
                moveT = Animations::EaseOutExpo(lag < 0.0f ? 0.0f : lag);
            }

            float xPos;
            if (rightAligned) {
                xPos = (g_animationStyle == 1)
                    ? g_hud->pos.x + g_hud->size.x - (textSize.x + textPad)
                    : g_hud->pos.x + g_hud->size.x - (textSize.x + textPad) * moveT;
            } else {
                xPos = (g_animationStyle == 1)
                    ? g_hud->pos.x + textPad
                    : g_hud->pos.x + textPad * moveT;
            }

            float yPos = yOffset;

            // Background (rows are translucent in Mica mode so the frosted layer shows through)
            float bgAlpha = g_bgOpacity;
            if (g_backgroundMode == 1) bgAlpha = (g_bgOpacity < 0.55f) ? g_bgOpacity : 0.55f;
            ImU32 bgColU32 = ImGui::GetColorU32(ImVec4(g_bgColor.x, g_bgColor.y, g_bgColor.z, bgAlpha * moveT));
            float rounding = (g_roundedBorders ? g_borderRadius : 0.0f) * s;

            float rectMinX, rectMaxX;
            if (rightAligned) {
                rectMinX = xPos - padX;
                rectMaxX = g_hud->pos.x + g_hud->size.x;
            } else {
                rectMinX = g_hud->pos.x;
                rectMaxX = xPos + textSize.x + padX;
            }
            draw->AddRectFilled(ImVec2(rectMinX, yPos), ImVec2(rectMaxX, yPos + currentSpacing), bgColU32, rounding);

            // Accent Line
            if (g_showSideBar && g_sideBarWidth > 0.0f) {
                ImVec4 chromaCol = GetArrayListChroma((float)i, (float)activeMods.size());
                if (!g_chromaSideBar) chromaCol = g_sideBarColor;
                chromaCol.w = moveT;
                float sbW = g_sideBarWidth * s;

                if (rightAligned) {
                    draw->AddRectFilled(ImVec2(g_hud->pos.x + g_hud->size.x - sbW, yPos), ImVec2(g_hud->pos.x + g_hud->size.x, yPos + currentSpacing), ImGui::GetColorU32(chromaCol), rounding);
                } else {
                    draw->AddRectFilled(ImVec2(g_hud->pos.x, yPos), ImVec2(g_hud->pos.x + sbW, yPos + currentSpacing), ImGui::GetColorU32(chromaCol), rounding);
                }
            }

            // Border
            if (g_showBorder) {
                ImVec4 bc = g_borderColor;
                bc.w *= moveT;
                draw->AddRect(ImVec2(rectMinX, yPos), ImVec2(rectMaxX, yPos + currentSpacing), ImGui::GetColorU32(bc), rounding, 0, g_borderWidth * s);
            }

            // Render Text
            if (anim > 0.4f) {
                ImVec4 tc = g_chromaText ? GetArrayListChroma((float)i, (float)activeMods.size()) : g_textColor;
                tc.w = moveT;
                ImU32 textCol = ImGui::GetColorU32(tc);
                float textY = yPos + (currentSpacing - textSize.y) * 0.5f;

                if (g_textShadow) {
                    ImU32 shadowCol = IM_COL32(0, 0, 0, (int)(moveT * 200.0f));
                    float shOff = g_textShadowOffset * s;
                    draw->AddText(font, fontPx, ImVec2(xPos + shOff, textY + shOff), shadowCol, m->name.c_str());
                    if (!m->suffix.empty() && g_showSuffix) {
                        float nameWidth = ts(m->name).x;
                        draw->AddText(font, fontPx, ImVec2(xPos + nameWidth + shOff, textY + shOff), shadowCol, (" [" + m->suffix + "]").c_str());
                    }
                }

                if (g_glowEnabled) {
                    ImU32 glowCol = ImGui::GetColorU32(ImVec4(tc.x, tc.y, tc.z, moveT * 0.60f));
                    GUI::AddTextGlow(draw, font, fontPx, ImVec2(xPos, textY), glowCol, m->name.c_str(), g_glowStrength);
                }
                draw->AddText(font, fontPx, ImVec2(xPos, textY), textCol, m->name.c_str());

                if (!m->suffix.empty() && g_showSuffix) {
                    float nameWidth = ts(m->name).x;
                    ImVec4 suffixCol = g_chromaText ? GetArrayListChroma((float)i, (float)activeMods.size()) : g_suffixColor;
                    suffixCol.w = moveT;
                    draw->AddText(font, fontPx, ImVec2(xPos + nameWidth, textY), ImGui::GetColorU32(suffixCol), (" [" + m->suffix + "]").c_str());
                }
            }

            yOffset += currentSpacing;
        }

        // Update HUD size for dragging hitbox
        g_hud->size.x = maxW + 20.0f * s;
        g_hud->size.y = yOffset - g_hud->pos.y;
        if (g_hud->size.y < rowH) g_hud->size.y = rowH;

        // Store bounds for the Mica blur region
        g_hasBlurRect = (g_backgroundMode == 1);
        g_blurRectX = g_hud->pos.x;
        g_blurRectY = g_hud->pos.y;
        g_blurRectW = g_hud->size.x;
        g_blurRectH = g_hud->size.y;

        // Show draggable area border when menu is open
        if (GUI::IsHudEditable()) {
            draw->AddRect(g_hud->pos, ImVec2(g_hud->pos.x + g_hud->size.x, g_hud->pos.y + g_hud->size.y), IM_COL32(255, 255, 255, 80));
        }
        ImGui::PopFont();
    }

    void RenderBlur(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, IDXGISwapChain* pSwapChain) {
        if (!g_enabled || g_backgroundMode != 1) {
            g_hasBlurRect = false;
            return;
        }
        if (!g_hasBlurRect || g_blurRectW <= 0.0f || g_blurRectH <= 0.0f) return;

        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        ClickGUI::RenderBlurRegion(pDevice, pContext, pSwapChain,
                                   screenSize.x, screenSize.y,
                                   g_blurRectX, g_blurRectY, g_blurRectW, g_blurRectH,
                                   g_blurRadius * 0.5f, g_blurOpacity);
    }

    void HandleHudDrag(float screenWidth, bool menuOpen) {
        g_hud = &g_arrayListHud;

        g_arrayListHud.HandleDrag(menuOpen);
        g_arrayListHud.ClampToScreen();
        if (g_arrayListHud.pos.x == 0 && g_arrayListHud.pos.y == 10) {
            g_arrayListHud.pos.x = screenWidth - 250;
        }
    }
}
