/*
Under an4rch Development Public Source License 1.0
*/

#include "GUI.hpp"
#include "../Animations/Animations.hpp"
#include "../Modules/Terminal/Terminal.hpp"
#include "../Modules/Info/Info.hpp"
#include "../Networking/IRChat.hpp"
#include "../Config/ConfigManager.hpp"
#include "../Networking/Client/IRCClient.hpp"
#include <windows.h>
#include <shellapi.h>
#include "../Assets/resource.h"
#include <cmath>
#include <cstdlib>
#include "../ArrayList/ArrayList.hpp"
#include "../Modules/ModuleHeader.hpp"
#include <d3d11.h>
#include "../Assets/stb/stb_image.h"
#include <winhttp.h>
#include <fstream>
#include <filesystem>
#include "../nlohmann/json.hpp"

extern ID3D11Device* pDevice;
extern HMODULE g_hModule;


// Static member initialization
bool GUI::g_showMenu = false;
float GUI::g_menuAnim = 0.0f;

int GUI::g_currentTab = 0;
int GUI::g_previousTab = 0;
ULONGLONG GUI::g_tabChangeTime = 0;
float GUI::g_tabAnim = 1.0f; // Start at 1.0f so it's visible on first open
float GUI::g_ircShiftAnim = 0.0f;
extern ULONGLONG g_notifStart;
extern bool g_showMenu;
extern bool g_firstTabOpen;
extern int g_currentTab;
extern int g_previousTab;
extern ULONGLONG g_tabChangeTime;
extern float g_tabAnim;
extern HMODULE g_hModule;

int GUI::g_currentTheme = GUI::Theme_AegleClassic;
ImVec4 GUI::g_colorBgMain = ImVec4(0.05f, 0.04f, 0.07f, 0.99f);
ImVec4 GUI::g_colorBgPanel = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
ImVec4 GUI::g_colorAccent = ImVec4(1.00f, 0.40f, 0.80f, 1.00f);
ImVec4 GUI::g_colorAccentSoft = ImVec4(0.60f, 0.50f, 1.00f, 0.40f);
ImVec4 GUI::g_colorAccentGlow = ImVec4(1.00f, 0.40f, 0.80f, 0.40f);

float GUI::g_sidebarIndicatorY = 85.0f;
float GUI::g_sidebarTargetIndicatorY = 85.0f;

std::vector<GUI::Particle> GUI::g_particles;
ImFont* GUI::g_fontDefault = nullptr;
ImFont* GUI::g_fontH1 = nullptr;
ImFont* GUI::g_fontH2 = nullptr;
ImFont* GUI::g_fontH3 = nullptr;
std::vector<GUI::LoadedFont> GUI::g_loadedFonts;

std::map<std::string, float> GUI::g_elementAnims;
void* GUI::g_tabTextures[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
void* GUI::g_likeTexture = nullptr;
void* GUI::g_downloadTexture = nullptr;

std::vector<GUI::MarketConfig> GUI::g_marketConfigs;
bool GUI::g_fetchingMarket = false;
bool GUI::g_marketFetchDone = false;
bool GUI::g_marketFetchFailed = false;

char g_notifTitle[64] = "Aegleseeker | an4rch development";
char g_notifMessage[128] = "Injected successfully.";

struct CardInfo {
    std::string key;
    ImVec2 pos;
    float anim;
};
std::vector<CardInfo> g_cardStartStack;

// Forward declarations for helper functions
void GUI::DrawShadow(ImDrawList* draw, ImVec2 pos, ImVec2 size, float rounding, float thickness, float opacity) {
    if (opacity <= 0.01f || thickness <= 0.0f) return;
    
    // Solid offset shadow (consistent with other client modules)
    float offset = thickness * 0.3f; 
    draw->AddRectFilled(
        ImVec2(pos.x + offset, pos.y + offset),
        ImVec2(pos.x + size.x + offset, pos.y + size.y + offset),
        ImColor(0, 0, 0, (int)(opacity * 180)),
        rounding
    );
}

void GUI::AddTextGlow(ImDrawList* draw, ImFont* font, float fontSize, ImVec2 pos, ImU32 col, const char* text, float thickness) {
    ImVec4 colV = ImGui::ColorConvertU32ToFloat4(col);
    for (int i = 1; i <= (int)thickness; i++) {
        float alpha = (0.25f / i);
        draw->AddText(font, fontSize, ImVec2(pos.x, pos.y), ImColor(colV.x, colV.y, colV.z, alpha), text);
        draw->AddText(font, fontSize, ImVec2(pos.x - i*0.5f, pos.y), ImColor(colV.x, colV.y, colV.z, alpha * 0.5f), text);
        draw->AddText(font, fontSize, ImVec2(pos.x + i*0.5f, pos.y), ImColor(colV.x, colV.y, colV.z, alpha * 0.5f), text);
    }
    draw->AddText(font, fontSize, pos, col, text);
}

void GUI::InitializeParticles() {
    if (!g_particles.empty()) return;
    g_particles.resize(65);
    for (int i = 0; i < 65; i++) {
        g_particles[i].pos = ImVec2((float)(rand() % 850), (float)(rand() % 580));
        g_particles[i].vel = ImVec2(((rand() % 100) - 50) / 50.0f * 15.0f, ((rand() % 100) - 50) / 50.0f * 15.0f);
        g_particles[i].size = 1.5f + (rand() % 200) / 100.0f; // 1.5 to 3.5
        g_particles[i].alpha = 0.12f + (rand() % 100) / 200.0f; // 0.12 to 0.62
        g_particles[i].speedScale = 0.4f + (rand() % 100) / 100.0f; // 0.4 to 1.4
    }
}

void GUI::RenderParticles(ImDrawList* draw, ImVec2 pos, ImVec2 size, float alpha) {
    if (!ClickGUI::g_showParticles) return;

    if (g_particles.empty()) {
        InitializeParticles();
    }

    static ULONGLONG lastTime = GetTickCount64();
    ULONGLONG now = GetTickCount64();
    float dt = (float)(now - lastTime) / 1000.0f;
    lastTime = now;
    if (dt > 0.1f) dt = 0.1f; // Clamp to avoid huge jumps on frame drops

    ImVec4 accentV = g_colorAccent;

    for (size_t i = 0; i < g_particles.size(); i++) {
        auto& p = g_particles[i];
        
        p.pos.x += p.vel.x * p.speedScale * dt;
        p.pos.y += p.vel.y * p.speedScale * dt;

        if (p.pos.x < 0) { p.pos.x = size.x; }
        else if (p.pos.x > size.x) { p.pos.x = 0; }
        if (p.pos.y < 0) { p.pos.y = size.y; }
        else if (p.pos.y > size.y) { p.pos.y = 0; }

        ImVec2 screenPos = ImVec2(pos.x + p.pos.x, pos.y + p.pos.y);
        ImU32 particleCol = ImColor(accentV.x, accentV.y, accentV.z, p.alpha * alpha);
        draw->AddCircleFilled(screenPos, p.size, particleCol);

        for (size_t j = i + 1; j < g_particles.size(); j++) {
            auto& p2 = g_particles[j];
            float dx = p.pos.x - p2.pos.x;
            float dy = p.pos.y - p2.pos.y;
            float distSq = dx * dx + dy * dy;
            float maxDist = 80.0f;
            float maxDistSq = maxDist * maxDist;

            if (distSq < maxDistSq) {
                float dist = sqrtf(distSq);
                float lineAlpha = (1.0f - (dist / maxDist)) * 0.15f * alpha;
                ImU32 lineCol = ImColor(accentV.x, accentV.y, accentV.z, lineAlpha);
                draw->AddLine(screenPos, ImVec2(pos.x + p2.pos.x, pos.y + p2.pos.y), lineCol, 1.0f);
            }
        }
    }
}

void GUI::ApplyThemePreset(int presetId) {
    if (presetId < 0 || presetId >= Theme_Max) presetId = Theme_AegleClassic;
    g_currentTheme = presetId;
    
    switch (g_currentTheme) {
        case Theme_AegleClassic:
            g_colorBgMain = ImVec4(0.05f, 0.04f, 0.07f, 0.99f);
            g_colorBgPanel = ImVec4(0.08f, 0.07f, 0.12f, 0.00f);
            g_colorAccent = ImVec4(1.00f, 0.40f, 0.80f, 1.00f); // Pink
            g_colorAccentSoft = ImVec4(0.60f, 0.50f, 1.00f, 0.40f); // Purple Soft
            g_colorAccentGlow = ImVec4(1.00f, 0.40f, 0.80f, 0.40f);
            break;
        case Theme_SakuraBlossom:
            g_colorBgMain = ImVec4(0.08f, 0.06f, 0.08f, 0.99f);
            g_colorBgPanel = ImVec4(0.12f, 0.09f, 0.12f, 0.00f);
            g_colorAccent = ImVec4(1.00f, 0.60f, 0.75f, 1.00f); // Sakura Pink
            g_colorAccentSoft = ImVec4(1.00f, 0.78f, 0.83f, 0.40f); 
            g_colorAccentGlow = ImVec4(1.00f, 0.60f, 0.75f, 0.45f);
            break;
        case Theme_Cyberpunk:
            g_colorBgMain = ImVec4(0.03f, 0.03f, 0.05f, 0.99f);
            g_colorBgPanel = ImVec4(0.07f, 0.07f, 0.10f, 0.00f);
            g_colorAccent = ImVec4(0.00f, 0.95f, 1.00f, 1.00f); // Cyan
            g_colorAccentSoft = ImVec4(0.95f, 0.90f, 0.00f, 0.40f); // Yellow
            g_colorAccentGlow = ImVec4(0.00f, 0.95f, 1.00f, 0.40f);
            break;
        case Theme_EmeraldForest:
            g_colorBgMain = ImVec4(0.04f, 0.06f, 0.05f, 0.99f);
            g_colorBgPanel = ImVec4(0.07f, 0.10f, 0.08f, 0.00f);
            g_colorAccent = ImVec4(0.20f, 0.85f, 0.55f, 1.00f); // Emerald
            g_colorAccentSoft = ImVec4(0.15f, 0.60f, 0.40f, 0.40f);
            g_colorAccentGlow = ImVec4(0.20f, 0.85f, 0.55f, 0.40f);
            break;
        case Theme_DeepSea:
            g_colorBgMain = ImVec4(0.03f, 0.05f, 0.09f, 0.99f);
            g_colorBgPanel = ImVec4(0.05f, 0.08f, 0.14f, 0.00f);
            g_colorAccent = ImVec4(0.20f, 0.60f, 1.00f, 1.00f); // Ocean Blue
            g_colorAccentSoft = ImVec4(0.40f, 0.85f, 1.00f, 0.40f); // Ice Cyan
            g_colorAccentGlow = ImVec4(0.20f, 0.60f, 1.00f, 0.40f);
            break;
    }
    
    // Apply immediately to ImGui style
    ApplyTheme();
}

void GUI::ApplyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 14.0f;
    style.ChildRounding     = 10.0f;
    style.FrameRounding     = 7.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 7.0f;

    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 0.0f;

    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding  = ImVec2(12, 8);
    style.ItemSpacing   = ImVec2(12, 10);

    ImVec4 bgMain   = g_colorBgMain;
    ImVec4 bgPanel  = g_colorBgPanel;
    ImVec4 accent   = g_colorAccent;
    ImVec4 accentSoft = g_colorAccentSoft;

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg]         = bgMain;
    colors[ImGuiCol_ChildBg]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg]          = bgPanel;
    colors[ImGuiCol_Border]           = ImVec4(bgPanel.x * 1.5f, bgPanel.y * 1.5f, bgPanel.z * 1.5f, 1.00f);
    colors[ImGuiCol_FrameBg]          = ImVec4(bgMain.x * 1.5f, bgMain.y * 1.5f, bgMain.z * 1.5f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]   = ImVec4(bgMain.x * 2.0f, bgMain.y * 2.0f, bgMain.z * 2.0f, 1.00f);
    colors[ImGuiCol_FrameBgActive]    = ImVec4(bgMain.x * 2.3f, bgMain.y * 2.3f, bgMain.z * 2.3f, 1.00f);
    colors[ImGuiCol_Button]           = ImVec4(bgMain.x * 1.8f, bgMain.y * 1.8f, bgMain.z * 1.8f, 1.00f);
    colors[ImGuiCol_ButtonHovered]    = accent;
    colors[ImGuiCol_ButtonActive]     = accentSoft;
    colors[ImGuiCol_Header]           = accentSoft;
    colors[ImGuiCol_HeaderHovered]    = accent;
    colors[ImGuiCol_HeaderActive]     = accent;
    colors[ImGuiCol_Separator]        = ImVec4(bgPanel.x * 1.5f, bgPanel.y * 1.5f, bgPanel.z * 1.5f, 1.00f);
    colors[ImGuiCol_CheckMark]        = accent;
    colors[ImGuiCol_SliderGrab]       = accent;
    colors[ImGuiCol_SliderGrabActive] = accent;
    colors[ImGuiCol_Text]             = ImVec4(0.98f, 0.98f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled]     = ImVec4(0.50f, 0.50f, 0.60f, 1.00f);
}

void GUI::LoadFont() {
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;

    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 3;
    cfg.PixelSnapH = true;
    cfg.FontDataOwnedByAtlas = false; // Important when loading from memory if we don't want ImGui to free it

    bool defaultLoaded = false;

    // Load ProductSans from resources
    HRSRC hRes = FindResource(g_hModule, MAKEINTRESOURCE(IDR_FONT), RT_RCDATA);
    if (hRes) {
        HGLOBAL hData = LoadResource(g_hModule, hRes);
        if (hData) {
            void* pData = LockResource(hData);
            DWORD size = SizeofResource(g_hModule, hRes);
            if (pData && size > 0) {
                g_fontDefault = io.Fonts->AddFontFromMemoryTTF(pData, size, 18.0f, &cfg);
                g_fontH1 = io.Fonts->AddFontFromMemoryTTF(pData, size, 26.0f, &cfg);
                g_fontH2 = io.Fonts->AddFontFromMemoryTTF(pData, size, 22.0f, &cfg);
                g_fontH3 = io.Fonts->AddFontFromMemoryTTF(pData, size, 18.0f, &cfg);
                defaultLoaded = true;
            }
        }
    }

    // Fallback if resource fails
    if (!defaultLoaded) {
        if (io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, &cfg)) {
            g_fontDefault = io.Fonts->Fonts.back();
            g_fontH1 = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 26.0f, &cfg);
            g_fontH2 = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 22.0f, &cfg);
            g_fontH3 = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, &cfg);
        } else {
            g_fontDefault = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0f, &cfg);
            g_fontH1 = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 26.0f, &cfg);
            g_fontH2 = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 22.0f, &cfg);
            g_fontH3 = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0f, &cfg);
        }
    }

    // Add Product Sans (default) as first entry
    g_loadedFonts.clear();
    g_loadedFonts.push_back({ "Default (Product Sans)", "", g_fontDefault });

    // Helper lambda: load a font from an embedded resource ID
    auto loadFontFromResource = [&](int resourceId, const char* displayName) {
        HRSRC hR = FindResource(g_hModule, MAKEINTRESOURCE(resourceId), RT_RCDATA);
        if (!hR) return;
        HGLOBAL hG = LoadResource(g_hModule, hR);
        if (!hG) return;
        void* pD = LockResource(hG);
        DWORD sz = SizeofResource(g_hModule, hR);
        if (!pD || sz == 0) return;

        ImFontConfig fcfg;
        fcfg.OversampleH = 3;
        fcfg.OversampleV = 3;
        fcfg.PixelSnapH = true;
        fcfg.FontDataOwnedByAtlas = false;

        ImFont* f = io.Fonts->AddFontFromMemoryTTF(pD, (int)sz, 18.0f, &fcfg);
        if (f) {
            g_loadedFonts.push_back({ displayName, "", f });
        }
    };

    loadFontFromResource(IDR_FONT_GOOGLE_SANS,    "Google Sans");
    loadFontFromResource(IDR_FONT_INTER,          "Inter");
    loadFontFromResource(IDR_FONT_JETBRAINS_MONO, "JetBrains Mono");
    loadFontFromResource(IDR_FONT_MINECRAFT,      "Minecraft");
    loadFontFromResource(IDR_FONT_SF_PRO,         "SF Pro Text");
}

void GUI::RenderFontSelect(const char* label, std::string& currentFontName) {
    if (g_loadedFonts.empty()) return;

    int currentIdx = 0;
    for (size_t i = 0; i < g_loadedFonts.size(); i++) {
        if (g_loadedFonts[i].name == currentFontName) {
            currentIdx = (int)i;
            break;
        }
    }

    if (ImGui::BeginCombo(label, g_loadedFonts[currentIdx].name.c_str())) {
        for (size_t i = 0; i < g_loadedFonts.size(); i++) {
            bool isSelected = (currentIdx == (int)i);
            if (ImGui::Selectable(g_loadedFonts[i].name.c_str(), isSelected)) {
                currentFontName = g_loadedFonts[i].name;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

ImFont* GUI::GetFontByName(const std::string& fontName) {
    for (const auto& f : g_loadedFonts) {
        if (f.name == fontName) {
            return f.fontPtr;
        }
    }
    return g_fontDefault;
}


bool GUI::RenderSidebarButton(const char* label, int index) {
    bool active = (g_currentTab == index);
    
    std::string key = "sidebar_btn_" + std::to_string(index);
    if (g_elementAnims.find(key) == g_elementAnims.end()) g_elementAnims[key] = active ? 1.0f : 0.0f;
    
    float target = active ? 1.0f : (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ? 0.3f : 0.0f);
    g_elementAnims[key] += (target - g_elementAnims[key]) * 0.2f; // slightly faster response
    
    ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x - 15, 42.0f);
    ImGui::PushID(label);
    
    ImGui::SetCursorPosX(7);
    bool pressed = ImGui::Button("##btn", size);
    
    ImVec2 p_min = ImGui::GetItemRectMin();
    ImVec2 p_max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    
    float anim = g_elementAnims[key];
    
    // Update active Y position relative to sidebar window
    if (active) {
        g_sidebarTargetIndicatorY = p_min.y - ImGui::GetWindowPos().y;
    }
    
    // Draw hover background highlight (if not active, since active has the sliding indicator)
    if (!active && anim > 0.001f) {
        ImVec4 accentV = g_colorAccent;
        ImU32 col = ImColor(accentV.x, accentV.y, accentV.z, anim * 0.15f);
        draw->AddRectFilled(p_min, p_max, col, 8.0f);
    }
    
    // Draw icon if available
    void* tex = g_tabTextures[index];
    if (tex != nullptr) {
        float iconSize = 20.0f;
        ImVec2 iconPos = ImVec2(p_min.x + 15, p_min.y + (size.y - iconSize) * 0.5f);
        ImU32 iconCol;
        if (active) {
            iconCol = ImColor(255, 255, 255, 255);
        } else {
            int grayVal = (int)(130 + anim * 50);
            iconCol = ImColor(grayVal, grayVal, (int)(grayVal * 1.05f), 255);
        }
        draw->AddImage((ImTextureID)tex, iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize), ImVec2(0,0), ImVec2(1,1), iconCol);
    }

    // Text
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float textX = (tex != nullptr) ? (p_min.x + 46.0f) : (p_min.x + 20.0f);
    ImVec2 textPos = ImVec2(textX, p_min.y + (size.y - textSize.y) * 0.5f);
    
    if (active) {
        ImVec4 accentV = g_colorAccent;
        ImU32 textCol = ImColor(255, 255, 255);
        AddTextGlow(draw, ImGui::GetFont(), ImGui::GetFontSize(), textPos, textCol, label, 3.0f);
    } else {
        draw->AddText(textPos, ImColor(160, 160, 175, (int)(150 + anim * 105)), label);
    }
    
    ImGui::PopID();
    
    if (pressed && !active) {
        g_previousTab = g_currentTab;
        g_currentTab = index;
        g_tabChangeTime = GetTickCount64();
        g_tabAnim = 0.0f;
    }
    
    return pressed;
}

void GUI::RenderCustomSwitch(const char* label, bool* value) {
    ImGui::PushID(label);
    
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    
    float height = 18.0f;
    float width = 36.0f;
    float radius = height * 0.5f;
    
    std::string key = "switch_" + std::string(label);
    if (g_elementAnims.find(key) == g_elementAnims.end()) g_elementAnims[key] = *value ? 1.0f : 0.0f;
    
    float target = *value ? 1.0f : 0.0f;
    g_elementAnims[key] += (target - g_elementAnims[key]) * 0.22f;
    float anim = g_elementAnims[key];
    
    ImGui::InvisibleButton("##switch", ImVec2(width + ImGui::CalcTextSize(label).x + 15, height));
    if (ImGui::IsItemClicked()) *value = !*value;
    
    ImVec4 bgColEmpty = ImVec4(g_colorBgMain.x * 2.5f, g_colorBgMain.y * 2.5f, g_colorBgMain.z * 2.5f, 1.0f);
    ImVec4 bgColActive = g_colorAccent;
    
    ImU32 col_bg = ImColor(
        bgColEmpty.x + (bgColActive.x - bgColEmpty.x) * anim,
        bgColEmpty.y + (bgColActive.y - bgColEmpty.y) * anim,
        bgColEmpty.z + (bgColActive.z - bgColEmpty.z) * anim,
        1.0f
    );
    
    // Subtle glow behind active switch
    if (anim > 0.01f) {
        draw->AddRectFilled(ImVec2(p.x - 1, p.y - 1), ImVec2(p.x + width + 1, p.y + height + 1), ImColor(bgColActive.x, bgColActive.y, bgColActive.z, anim * 0.12f), radius + 1.0f);
    }
    
    draw->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, radius);
    
    float circle_pos = p.x + radius + anim * (width - radius * 2.0f);
    draw->AddCircleFilled(ImVec2(circle_pos, p.y + radius), radius - 3.0f, ImColor(255, 255, 255, 255));
    
    ImU32 textCol = ImColor(
        210.0f / 255.0f + (255.0f / 255.0f - 210.0f / 255.0f) * anim,
        210.0f / 255.0f + (255.0f / 255.0f - 210.0f / 255.0f) * anim,
        220.0f / 255.0f + (255.0f / 255.0f - 220.0f / 255.0f) * anim,
        1.0f
    );
    draw->AddText(ImVec2(p.x + width + 12, p.y + (height - ImGui::GetFontSize()) * 0.5f), textCol, label);
    
    ImGui::PopID();
}

bool GUI::BeginSection(const char* label, bool* open) {
    std::string key = "section_" + std::string(label);
    if (g_elementAnims.find(key) == g_elementAnims.end()) g_elementAnims[key] = *open ? 1.0f : 0.0f;
    
    float target = *open ? 1.0f : 0.0f;
    g_elementAnims[key] += (target - g_elementAnims[key]) * 0.15f;
    float anim = g_elementAnims[key];
    
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.12f, 0.12f, 0.15f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.10f, 0.10f, 0.13f, 1.00f));
    
    bool clicked = ImGui::Selectable(label, false, 0, ImVec2(0, 35));
    if (clicked) *open = !*open;
    
    ImVec2 p_min = ImGui::GetItemRectMin();
    ImVec2 p_max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    
    // Arrow animation
    float arrow_size = 6.0f;
    ImVec2 arrow_center = ImVec2(p_max.x - 20, p_min.y + 17.5f);
    if (anim > 0.5f) {
        draw->AddTriangleFilled(
            ImVec2(arrow_center.x - arrow_size, arrow_center.y - arrow_size * 0.5f),
            ImVec2(arrow_center.x + arrow_size, arrow_center.y - arrow_size * 0.5f),
            ImVec2(arrow_center.x, arrow_center.y + arrow_size * 0.5f),
            ImColor(200, 200, 200)
        );
    } else {
        draw->AddTriangleFilled(
            ImVec2(arrow_center.x - arrow_size * 0.5f, arrow_center.y - arrow_size),
            ImVec2(arrow_center.x - arrow_size * 0.5f, arrow_center.y + arrow_size),
            ImVec2(arrow_center.x + arrow_size * 0.5f, arrow_center.y),
            ImColor(150, 150, 150)
        );
    }
    
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    
    if (anim > 0.01f) {
        ImGui::Indent(15.0f);
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, anim);
        return true;
    }
    
    return false;
}

void GUI::EndSection() {
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImGui::Unindent(15.0f);
    ImGui::Spacing();
}

bool GUI::BeginModuleSettings(const char* label, bool* open) {
    std::string key = "mod_set_" + std::string(label);
    if (g_elementAnims.find(key) == g_elementAnims.end()) g_elementAnims[key] = *open ? 1.0f : 0.0f;
    
    float target = *open ? 1.0f : 0.0f;
    g_elementAnims[key] += (target - g_elementAnims[key]) * 0.15f;
    float anim = g_elementAnims[key];
    
    if (anim <= 0.01f) return false;
    
    ImGui::Spacing();
    ImVec2 startPos = ImGui::GetCursorScreenPos();
    
    ImGui::Indent(15.0f);
    ImGui::BeginGroup();
    
    g_cardStartStack.push_back({ key, startPos, anim });
    
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, anim);
    ImGui::PushID(label);
    return true;
}

void GUI::EndModuleSettings() {
    ImGui::PopID();
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImGui::Unindent(15.0f);
    
    ImVec2 endPos = ImGui::GetItemRectMax();
    
    if (!g_cardStartStack.empty()) {
        CardInfo card = g_cardStartStack.back();
        g_cardStartStack.pop_back();
        
        float anim = card.anim;
        float padding = 8.0f;
        
        ImVec2 rectMin = ImVec2(card.pos.x - 8.0f, card.pos.y);
        ImVec2 rectMax = ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 25.0f, endPos.y + padding);
        
        ImDrawList* draw = ImGui::GetWindowDrawList();
        
        // Solid/translucent clean card background respecting panel alpha setting (fully transparent)
        draw->AddRectFilled(rectMin, rectMax, ImColor(g_colorBgPanel.x, g_colorBgPanel.y, g_colorBgPanel.z, g_colorBgPanel.w * anim), 8.0f);
    }
    
    ImGui::Spacing();
}

void GUI::RenderDashboard() {
    ImGui::BeginChild("Dashboard", ImVec2(0, -115), false, ImGuiWindowFlags_NoScrollbar);
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float cardWidth = (avail.x - 20) * 0.5f;
        
        // --- Top Row ---
        // Profile Card
        ImGui::BeginChild("ProfileCard", ImVec2(cardWidth, 120), true);
        {
            ImGui::SetCursorPos(ImVec2(15, 15));
            ImGui::TextColored(g_colorAccent, "USER PROFILE");
            ImGui::Separator();
            
            char* user = getenv("USERNAME");
            ImGui::SetCursorPos(ImVec2(15, 45));
            ImGui::Text("Username: %s", user ? user : "Unknown");
            
            ImGui::SetCursorPos(ImVec2(15, 75));
            ImGui::TextDisabled("- by an4rch development");
        }
        ImGui::EndChild();
        
        ImGui::SameLine(0, 20);
        
        // System Stats Card
        ImGui::BeginChild("StatsCard", ImVec2(cardWidth, 120), true);
        {
            ImGui::SetCursorPos(ImVec2(15, 15));
            ImGui::TextColored(g_colorAccent, "SYSTEM STATUS");
            ImGui::Separator();
            
            ImGui::SetCursorPos(ImVec2(15, 45));
            ImGui::Text("Client FPS: "); ImGui::SameLine();
            if (UnlockFPS::g_unlockFpsEnabled) {
                ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "%.0f (%.0f lim)", RenderInfo::g_fpsCounter, UnlockFPS::g_fpsLimit);
            } else {
                ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "%.0f", RenderInfo::g_fpsCounter);
            }
            
            ImGui::SetCursorPos(ImVec2(15, 65));
            ImGui::Text("Latency: "); ImGui::SameLine();
            static float ping = 18.0f;
            ping += (rand() % 3 - 1) * 0.5f;
            if (ping < 5.0f) ping = 5.0f;
            ImGui::TextColored(g_colorAccent, "%.0f ms", ping);

            ImGui::SetCursorPos(ImVec2(15, 85));
            ImGui::Text("Security: "); ImGui::SameLine();
            ImGui::TextColored(g_colorAccent, "PROTECTED");
        }
        ImGui::EndChild();
        
        ImGui::Spacing(); ImGui::Spacing();
        
        // --- Bottom Row ---
        // Changelog / Logo
        ImGui::BeginChild("LogoArea", ImVec2(0, 200), true);
        {
            ImVec2 innerAvail = ImGui::GetContentRegionAvail();
            
            // Render Changelog on the left
            ImGui::SetCursorPos(ImVec2(20, 15));
            ImGui::TextColored(g_colorAccent, "LATEST UPDATES");
            // soon api github auto update
            
            ImGui::Separator();
            
            ImGui::SetCursorPos(ImVec2(20, 45));
            ImGui::BulletText("v1.0.4 - Stable Release");
            ImGui::BulletText("v1.0.5 - Stable Release | IRC Chat added");
            ImGui::BulletText("v1.0.6 - Stable Release | Config Market Added");
            ImGui::BulletText("v1.0.7 - Pre-release | new features coming");

            
            // Render Logo on the right
            if (Info::g_logoTexture != 0) {
                float scale = fminf(innerAvail.x * 0.4f / Info::g_logoWidth, 120.0f / Info::g_logoHeight);
                ImVec2 displaySize(Info::g_logoWidth * scale, Info::g_logoHeight * scale);
                ImVec2 logoPos = ImVec2(innerAvail.x - displaySize.x - 20, (innerAvail.y - displaySize.y) * 0.5f + 20);
                
                ImGui::SetCursorPos(logoPos);
                if (ImGui::ImageButton("##InfoLogo", Info::g_logoTexture, displaySize, ImVec2(0,0), ImVec2(1,1), ImVec4(0,0,0,0))) {
                    Info::PlayClickSound();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild(); // Dashboard
}

void GUI::RenderSocialButtons() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::Spacing();
    // Center the 3 buttons: 90 + 8(gap) + 90 + 8(gap) + 110 = 306 total
    ImGui::SetCursorPosX((avail.x - 306.0f) * 0.5f);
    if (ImGui::Button("DISCORD", ImVec2(90, 35))) {
        ShellExecuteA(0, "open", "https://discord.gg/7hJjTCfyJ2", 0, 0, SW_SHOWNORMAL);
        WinExec("explorer https://discord.gg/7hJjTCfyJ2", SW_SHOWNORMAL);
        ImGui::SetClipboardText("https://discord.gg/7hJjTCfyJ2");
        strcpy(g_notifTitle, "Discord");
        strcpy(g_notifMessage, "Link copied to clipboard!");
        g_notifStart = GetTickCount64();
    }
    ImGui::SameLine(0, 8);
    if (ImGui::Button("GITHUB", ImVec2(90, 35))) {
        ShellExecuteA(0, "open", "https://github.com/iVyz3r/aegledll", 0, 0, SW_SHOWNORMAL);
        ImGui::SetClipboardText("https://github.com/iVyz3r/aegledll");
        strcpy(g_notifTitle, "Github");
        strcpy(g_notifMessage, "Link copied to clipboard!");
        g_notifStart = GetTickCount64();
    }
    ImGui::SameLine(0, 8);
    if (ImGui::Button("VERSION INFO", ImVec2(110, 35))) {
        Info::g_showReleaseModal = true;
        if (!Info::g_fetchDone && !Info::g_fetchInProgress) {
            Info::FetchLatestRelease();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Fetch latest release notes from GitHub");
    }
}

void GUI::RenderSectionHeader(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, g_colorAccent);
    ImGui::Text(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

void GUI::UpdateAnimation(ULONGLONG now, float dt) {
    // 📉 MENU ANIMATION - Enhanced Easing
    if (GUI::g_showMenu) {
        GUI::g_menuAnim += 3.5f * dt;
    } else {
        GUI::g_menuAnim -= 4.0f * dt;
    }
    if (GUI::g_menuAnim > 1.0f) GUI::g_menuAnim = 1.0f;
    if (GUI::g_menuAnim < 0.0f) GUI::g_menuAnim = 0.0f;
    
    // Tab change animation
    if (GUI::g_tabChangeTime > 0) {
        float tabChangeElapsed = (float)(now - GUI::g_tabChangeTime) / 1000.0f;
        GUI::g_tabAnim = fminf(1.0f, tabChangeElapsed / 0.35f);
    }
    
    // Smooth active sidebar indicator sliding
    g_sidebarIndicatorY = Animations::Approach(g_sidebarIndicatorY, g_sidebarTargetIndicatorY, dt, 14.0f);

    // Anim for shifting the menu left when IRC Chat tab is active
    if (GUI::g_showMenu && GUI::g_currentTab == 6) {
        GUI::g_ircShiftAnim = Animations::Approach(GUI::g_ircShiftAnim, 1.0f, dt, 10.0f);
    } else {
        GUI::g_ircShiftAnim = Animations::Approach(GUI::g_ircShiftAnim, 0.0f, dt, 10.0f);
    }
}

void GUI::RenderMenu(float screenWidth, float screenHeight) {
    if (GUI::g_menuAnim <= 0.001f) return;
    
    if (ClickGUI::g_guiStyle == 1) {
        ClickGUI::RenderSeparatedMenu(screenWidth, screenHeight);
        return;
    } else if (ClickGUI::g_guiStyle == 2) {
        ClickGUI::RenderRiseMenu(screenWidth, screenHeight);
        return;
    }
    
    float e = Animations::EaseOutQuart(GUI::g_menuAnim);
    // Dark blur background effect
        ImU32 bgCol = IM_COL32(5, 5, 10, (int)(e * 180.0f));
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(screenWidth, screenHeight), bgCol);

    if (e > 0.01f) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);
        
        // Scale animation
        float sc = 0.94f + (0.06f * e);
        ImVec2 baseSize = ImVec2(850, 580);
        ImVec2 winSize = ImVec2(baseSize.x * sc, baseSize.y * sc);
        float shiftAmt = 110.0f * sc * GUI::g_ircShiftAnim;
        ImVec2 winPos = ImVec2(screenWidth / 2 - winSize.x / 2 - shiftAmt, screenHeight / 2 - winSize.y / 2);
        
        ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
        ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);

        // Draw blurred shadow before window
        ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();
        DrawShadow(bgDraw, winPos, winSize, 14.0f * sc, 25.0f * e, 0.4f * e);

        if (ImGui::Begin("Aegleseeker", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
            
            ImVec2 wPos = ImGui::GetWindowPos();
            ImVec2 wSize = ImGui::GetWindowSize();
            
            // Render Plexus Background inside the window draw list
            RenderParticles(ImGui::GetWindowDrawList(), wPos, wSize, e);
            
            // Header Bar
            ImGui::BeginChild("HeaderBar", ImVec2(0, 55.0f * sc), false, ImGuiWindowFlags_NoScrollbar);
            {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                
                // Draw a thin separator line at the bottom of the header
                draw->AddLine(
                    ImVec2(wPos.x, wPos.y + 54.0f * sc),
                    ImVec2(wPos.x + wSize.x, wPos.y + 54.0f * sc),
                    ImColor(g_colorBgPanel.x * 1.8f, g_colorBgPanel.y * 1.8f, g_colorBgPanel.z * 1.8f, 0.5f)
                );
                
                // 1. Glowing Logo / Title on the left
                ImGui::SetCursorPos(ImVec2(20.0f * sc, 15.0f * sc));
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                ImVec2 logoPos = ImGui::GetCursorScreenPos();
                AddTextGlow(draw, ImGui::GetFont(), ImGui::GetFontSize(), logoPos, ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 1.0f), "AEGLESEEKER", 4.0f);
                ImGui::Dummy(ImVec2(ImGui::CalcTextSize("AEGLESEEKER").x, ImGui::GetFontSize()));
                ImGui::PopFont();
                
                // 2. ACTIVE Status Badge next to it
                ImGui::SameLine(0, 15.0f * sc);
                ImVec2 badgePos = ImGui::GetCursorScreenPos();
                badgePos.y += 2.0f * sc;
                // Pulse effect using time
                float pulse = (sinf((float)GetTickCount64() * 0.005f) + 1.0f) * 0.5f; // 0 to 1
                ImU32 dotColor = ImColor(0.2f, 0.9f, 0.3f, 0.6f + pulse * 0.4f);
                ImU32 badgeBorder = ImColor(0.2f, 0.9f, 0.3f, 0.4f);
                ImU32 badgeBg = ImColor(0.2f, 0.9f, 0.3f, 0.08f);
                
                // Draw badge background
                ImVec2 badgeSize = ImVec2(75.0f * sc, 20.0f * sc);
                draw->AddRectFilled(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y), badgeBg, 10.0f * sc);
                draw->AddRect(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y), badgeBorder, 10.0f * sc, 0, 1.0f);
                
                // Draw pulsing dot
                draw->AddCircleFilled(ImVec2(badgePos.x + 10.0f * sc, badgePos.y + 10.0f * sc), 3.0f * sc, dotColor);
                if (pulse > 0.1f) {
                    draw->AddCircle(ImVec2(badgePos.x + 10.0f * sc, badgePos.y + 10.0f * sc), (3.0f + pulse * 4.0f) * sc, ImColor(0.2f, 0.9f, 0.3f, (1.0f - pulse) * 0.5f), 0, 1.0f);
                }
                
                // Draw text "ACTIVE"
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.4f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                ImGui::SetCursorScreenPos(ImVec2(badgePos.x + 20.0f * sc, badgePos.y + (badgeSize.y - ImGui::GetFontSize()) * 0.5f));
                ImGui::Text("ACTIVE");
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                
                // 3. User Avatar and Close Button on the right
                // Avatar settings
                float avatarSize = 28.0f * sc;
                float rightOffset = 20.0f * sc;
                float closeBtnSize = 20.0f * sc;
                
                // Close button is at the very right
                ImVec2 closePos = ImVec2(wSize.x - rightOffset - closeBtnSize, (55.0f * sc - closeBtnSize) * 0.5f);
                ImGui::SetCursorPos(closePos);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 0.2f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.2f, 0.2f, 0.4f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button("X", ImVec2(closeBtnSize, closeBtnSize))) {
                    GUI::g_showMenu = false;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Close Menu");
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                
                // Avatar is to the left of close button
                ImVec2 avatarPos = ImVec2(wSize.x - rightOffset - closeBtnSize - 15.0f * sc - avatarSize, (55.0f * sc - avatarSize) * 0.5f);
                
                // Draw circular avatar with nice gradient
                ImVec2 avatarScreenPos = ImVec2(wPos.x + avatarPos.x, wPos.y + avatarPos.y);
                float radius = avatarSize * 0.5f;
                ImVec2 center = ImVec2(avatarScreenPos.x + radius, avatarScreenPos.y + radius);
                
                // Custom drawing: draw circular gradient or accent filled circle
                draw->AddCircleFilled(center, radius, ImColor(g_colorAccentSoft.x, g_colorAccentSoft.y, g_colorAccentSoft.z, 0.6f));
                draw->AddCircle(center, radius, ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.8f), 0, 1.5f);
                
                // Display first letter of username in the avatar
                char* user = getenv("USERNAME");
                char displayLetter = (user && strlen(user) > 0) ? toupper(user[0]) : 'U';
                char letterStr[2] = { displayLetter, '\0' };
                
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                ImVec2 letterSize = ImGui::CalcTextSize(letterStr);
                draw->AddText(ImVec2(center.x - letterSize.x * 0.5f, center.y - letterSize.y * 0.5f), ImColor(255, 255, 255, 230), letterStr);
                ImGui::PopFont();
            }
            ImGui::EndChild();
            
            // Sidebar Area
            ImGui::BeginChild("Sidebar", ImVec2(200 * sc, 0), false, ImGuiWindowFlags_NoScrollbar);
            {
                ImGui::SetCursorPosY(15.0f * sc);
                
                // Navigation buttons
                RenderSidebarButton("Combat", 0);
                RenderSidebarButton("Movement", 1);
                RenderSidebarButton("Visuals", 2);
                RenderSidebarButton("Misc", 3);
                RenderSidebarButton("Terminal", 4);
                RenderSidebarButton("Info", 5);
                RenderSidebarButton("IRC Chat", 6);
                RenderSidebarButton("Config Market", 7);
                
                // Sliding indicator pill
                ImVec2 sidebarPos = ImGui::GetWindowPos();
                ImDrawList* draw = ImGui::GetWindowDrawList();
                float indicatorHeight = 24.0f * sc;
                float indicatorWidth = 4.0f * sc;
                float startY = sidebarPos.y + g_sidebarIndicatorY + (42.0f * sc - indicatorHeight) * 0.5f;
                ImVec2 pillMin = ImVec2(sidebarPos.x + 3.0f * sc, startY);
                ImVec2 pillMax = ImVec2(sidebarPos.x + 3.0f * sc + indicatorWidth, startY + indicatorHeight);
                
                // Draw pill background glow
                draw->AddRectFilled(ImVec2(pillMin.x - 1, pillMin.y - 1), ImVec2(pillMax.x + 1, pillMax.y + 1), ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.4f), 3.0f * sc);
                // Draw the main pill
                draw->AddRectFilled(pillMin, pillMax, ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 1.0f), 2.0f * sc);

                // Footer
                ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 35 * sc);
                ImGui::SetCursorPosX(25);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.5f, 0.8f));
                ImGui::Text("v1.0.7 - pre-release");
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            
            ImGui::SameLine();
            
            // Content Area with Tab Animation
            float tab_e = Animations::EaseOutExpo(GUI::g_tabAnim);
            float slide = (1.0f - tab_e) * 30.0f;
            
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            ImGui::BeginChild("ContentAreaParent", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slide);
                
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tab_e * e);
                
                const char* tabNames[] = { "Combat", "Movement", "Visuals", "Misc", "Terminal", "Info", "IRC Chat", "Config Market" };
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                ImVec2 tabTextPos = ImGui::GetCursorScreenPos();
                AddTextGlow(ImGui::GetWindowDrawList(), ImGui::GetFont(), ImGui::GetFontSize(), tabTextPos, ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 1.0f), tabNames[GUI::g_currentTab], 5.0f);
                ImGui::Dummy(ImVec2(0, ImGui::GetFontSize())); // Placeholder for text
                ImGui::PopFont();
                ImGui::Separator();
                ImGui::Spacing(); ImGui::Spacing();

                ImGui::BeginChild("ContentScroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                {
                    static bool combatOpen = true, movementOpen = true, visualsOpen = true, miscOpen = true;

                    // Dynamic Tab Content
                    switch (GUI::g_currentTab) {
                        case 0: // Combat
                            if (BeginSection("Combat Modules", &combatOpen)) {
                                Reach::RenderMenu();
                                Hitbox::RenderMenu();
                                EndSection();
                            }
                            break;
                        case 1: // Movement
                            if (BeginSection("Movement Modules", &movementOpen)) {
                                AutoSprint::RenderMenu();
                                Timer::RenderMenu();
                                EndSection();
                            }
                            break;
                        case 2: // Visuals
                            if (BeginSection("Visual Modules", &visualsOpen)) {
                                Watermark::RenderMenu();
                                ArrayList::RenderMenu();
                                RenderInfo::RenderMenu();
                                Keystrokes::RenderMenu();
                                CPSCounter::RenderMenu();
                                FPSOverlay::RenderMenu();
                                PingCounter::RenderMenu();
                                FullBright::RenderMenu();
                                MotionBlur::RenderMenu();
                                ClickGUI::RenderMenu();
                                EndSection();
                            }
                            break;
                        case 3: // Misc
                            if (BeginSection("Misc Modules", &miscOpen)) {
                                UnlockFPS::RenderMenu();
                                AutoClicker::RenderMenu();
                                AntiAFK::RenderMenu();
                                Screenshot::RenderMenu();
                                EndSection();
                            }
                            break;
                        case 4: // Terminal
                            Terminal::RenderConsole();
                            break;
                        case 5: // Info
                            Info::RenderMenu();
                            break;
                        case 6: // IRC Chat
                            IRChat::RenderMenu();
                            break;
                        case 7: // Config Market
                            GUI::RenderConfigMarket();
                            break;
                    }
                }
                ImGui::EndChild();
                
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            
            ImGui::End();
        }

        if (GUI::g_ircShiftAnim > 0.001f) {
            float sidebarAlpha = GUI::g_ircShiftAnim * e;
            float sidebarWidth = 220.0f * sc * Animations::EaseOutQuart(GUI::g_ircShiftAnim);
            float sidebarX = winPos.x + winSize.x + 15.0f * sc;
            
            ImGui::SetNextWindowSize(ImVec2(sidebarWidth, winSize.y), ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(sidebarX, winPos.y), ImGuiCond_Always);
            
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, sidebarAlpha);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * sc);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * sc, 12.0f * sc));
            
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.09f, 0.98f));
            
            // Draw matching shadow
            DrawShadow(ImGui::GetBackgroundDrawList(), ImVec2(sidebarX, winPos.y), ImVec2(sidebarWidth, winSize.y), 12.0f * sc, 20.0f * GUI::g_ircShiftAnim, 0.35f * GUI::g_ircShiftAnim);
            
            if (ImGui::Begin("IRC Config Sidebar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar)) {
                
                ImGui::PushFont(GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetFont());
                ImGui::SetCursorPosY(15.0f * sc);
                if (sidebarWidth > 100.0f * sc) {
                    float textWidth = ImGui::CalcTextSize("IRC Configs").x;
                    ImGui::SetCursorPosX((sidebarWidth - textWidth) * 0.5f);
                    ImGui::TextColored(g_colorAccent, "IRC Configs");
                }
                ImGui::PopFont();
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                if (sidebarWidth > 150.0f * sc) {
                    if (ConfigManager::GetConfigDir().empty())
                        ConfigManager::Initialize();
                    
                    auto configs = ConfigManager::ListConfigs();
                    if (configs.empty()) {
                        ImGui::TextDisabled("No configs found.");
                    } else {
                        ImGui::TextDisabled("Drag to the chat:\n");
                        ImGui::Spacing();
                        
                        ImGui::BeginChild("SidebarConfigList", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                        for (const auto& cfg : configs) {
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.4f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.2f));
                            
                            ImGui::Selectable(cfg.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                            
                            // Drag Drop Source
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                ImGui::SetDragDropPayload("DRAG_IRC_CONFIG", cfg.c_str(), cfg.size() + 1);
                                ImGui::Text("Enviar %s.json", cfg.c_str());
                                ImGui::EndDragDropSource();
                            }
                            
                            ImGui::PopStyleColor(2);
                        }
                        ImGui::EndChild();
                    }
                }
                ImGui::End();
            }
            
            ImGui::PopStyleColor(1);
            ImGui::PopStyleVar(4);
        }

        ImGui::PopStyleVar();
    }
}

void GUI::RenderNotification(float screenWidth, float screenHeight) {
    extern ULONGLONG g_notifStart;
    if (g_notifStart == 0) return;

    float elapsed = (float)(GetTickCount64() - g_notifStart) / 1000.0f;
    float duration = 4.0f;
    
    if (elapsed > duration) {
        g_notifStart = 0;
        return;
    }

    float anim = 1.0f;
    if (elapsed < 0.4f) anim = Animations::EaseOutBack(elapsed / 0.4f);
    else if (elapsed > duration - 0.4f) anim = Animations::EaseInQuart((duration - elapsed) / 0.4f);

    if (anim <= 0.01f) return;

    ImVec2 size = ImVec2(320, 70);
    ImVec2 pos = ImVec2(screenWidth - (size.x + 20) * anim, screenHeight - size.y - 20);
    
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    
    // Shadow
    draw->AddRectFilled(ImVec2(pos.x + 4, pos.y + 4), ImVec2(pos.x + size.x + 4, pos.y + size.y + 4), ImColor(0, 0, 0, 50), 10.0f);
    
    // Main Background
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImColor(20, 20, 25, 240), 10.0f);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImColor(50, 50, 70, 150), 10.0f, 0, 1.5f);
    
    // Accent Side
    draw->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + 5, pos.y + size.y), ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 1.0f), 10.0f, ImDrawFlags_RoundCornersLeft);
    
    // Text and Icon (simplified icon)
    draw->AddCircleFilled(ImVec2(pos.x + 35, pos.y + size.y * 0.5f), 12.0f, ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.2f));
    draw->AddCircle(ImVec2(pos.x + 35, pos.y + size.y * 0.5f), 12.0f, ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.8f), 0, 1.5f);
    
    draw->AddText(ImVec2(pos.x + 65, pos.y + 15), ImColor(255, 255, 255), g_notifTitle);
    draw->AddText(ImVec2(pos.x + 65, pos.y + 35), ImColor(160, 160, 175), g_notifMessage);
    
    // Progress Bar
    float progress = 1.0f - (elapsed / duration);
    draw->AddRectFilled(ImVec2(pos.x + 10, pos.y + size.y - 6), ImVec2(pos.x + 10 + (size.x - 20) * progress, pos.y + size.y - 3), ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.8f), 2.0f);
}

void* GUI::LoadTextureFromResource(int resourceId) {
    HRSRC hRes = FindResource(g_hModule, MAKEINTRESOURCE(resourceId), RT_RCDATA);
    if (!hRes) return nullptr;
    
    HGLOBAL hGlobal = LoadResource(g_hModule, hRes);
    if (!hGlobal) return nullptr;
    
    void* pData = LockResource(hGlobal);
    DWORD size = SizeofResource(g_hModule, hRes);
    if (!pData || size == 0) return nullptr;
    
    int width, height, channels;
    unsigned char* img_data = stbi_load_from_memory((const unsigned char*)pData, size, &width, &height, &channels, 4);
    if (!img_data) return nullptr;
    
    // Calculate average luminance of non-transparent pixels to detect if it's a dark icon
    float total_lum = 0.0f;
    int opaque_pixels = 0;
    for (int i = 0; i < width * height; i++) {
        if (img_data[i * 4 + 3] > 10) {
            unsigned char r = img_data[i * 4];
            unsigned char g = img_data[i * 4 + 1];
            unsigned char b = img_data[i * 4 + 2];
            total_lum += (0.299f * r + 0.587f * g + 0.114f * b);
            opaque_pixels++;
        }
    }
    float avg_lum = (opaque_pixels > 0) ? (total_lum / opaque_pixels) : 0.0f;
    bool is_dark = (avg_lum < 128.0f);

    // Grayscale conversion & value mapping to white so we can tint dynamically at render-time
    for (int i = 0; i < width * height; i++) {
        unsigned char r = img_data[i * 4];
        unsigned char g = img_data[i * 4 + 1];
        unsigned char b = img_data[i * 4 + 2];
        unsigned char gray = (unsigned char)(0.299f * r + 0.587f * g + 0.114f * b);
        
        if (is_dark) {
            gray = 255 - gray; // Invert to make dark lines white
        }
        
        float factor = gray / 255.0f;
        img_data[i * 4] = (unsigned char)(255.0f * factor);
        img_data[i * 4 + 1] = (unsigned char)(255.0f * factor);
        img_data[i * 4 + 2] = (unsigned char)(255.0f * factor);
    }
    
    if (!pDevice) {
        stbi_image_free(img_data);
        return nullptr;
    }
    
    // Create texture description
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    
    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = img_data;
    subResource.SysMemPitch = width * 4;
    
    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = pDevice->CreateTexture2D(&desc, &subResource, &pTexture);
    
    stbi_image_free(img_data);
    
    if (FAILED(hr) || !pTexture) {
        return nullptr;
    }
    
    // Create shader resource view
    ID3D11ShaderResourceView* pSRV = nullptr;
    hr = pDevice->CreateShaderResourceView(pTexture, nullptr, &pSRV);
    pTexture->Release();
    
    if (FAILED(hr) || !pSRV) {
        return nullptr;
    }
    
    return (void*)pSRV;
}

bool GUI::InitializeTextures() {
    g_tabTextures[0] = LoadTextureFromResource(IDR_COMBAT_ICON);
    g_tabTextures[1] = LoadTextureFromResource(IDR_MOVEMENT_ICON);
    g_tabTextures[2] = LoadTextureFromResource(IDR_VISUALS_ICON);
    g_tabTextures[3] = LoadTextureFromResource(IDR_MISC_ICON);
    g_tabTextures[4] = LoadTextureFromResource(IDR_TERMINAL_ICON);
    g_tabTextures[5] = LoadTextureFromResource(IDR_INFO_ICON);
    g_tabTextures[6] = LoadTextureFromResource(IDR_IRC_ICON);
    g_tabTextures[7] = LoadTextureFromResource(IDR_CONFIG_MARKET_ICON);
    
    g_likeTexture = LoadTextureFromResource(IDR_LIKE_ICON);
    g_downloadTexture = LoadTextureFromResource(IDR_DOWNLOAD_ICON);
    
    bool success = false;
    for (int i = 0; i < 8; i++) {
        if (g_tabTextures[i] != nullptr) {
            success = true;
        }
    }
    return success;
}

void GUI::ShutdownTextures() {
    for (int i = 0; i < 8; i++) {
        if (g_tabTextures[i] != nullptr) {
            ((ID3D11ShaderResourceView*)g_tabTextures[i])->Release();
            g_tabTextures[i] = nullptr;
        }
    }
    if (g_likeTexture != nullptr) {
        ((ID3D11ShaderResourceView*)g_likeTexture)->Release();
        g_likeTexture = nullptr;
    }
    if (g_downloadTexture != nullptr) {
        ((ID3D11ShaderResourceView*)g_downloadTexture)->Release();
        g_downloadTexture = nullptr;
    }
}

void GUI::FetchMarketConfigs() {
    if (g_fetchingMarket) return;
    g_fetchingMarket = true;
    g_marketFetchDone = false;
    g_marketFetchFailed = false;
    g_marketConfigs.clear();

    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        std::string raw;
        bool success = false;

        HINTERNET hSession = WinHttpOpen(
            L"AegleDLL/1.0",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);

        if (hSession) {
            // Set generous timeouts (Render.com free tier has cold starts)
            DWORD timeout = 30000; // 30 seconds
            WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
            WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
            WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

            HINTERNET hConnect = WinHttpConnect(
                hSession,
                L"aegle-configmp.onrender.com",
                INTERNET_DEFAULT_HTTPS_PORT,
                0);

            if (hConnect) {
                HINTERNET hRequest = WinHttpOpenRequest(
                    hConnect,
                    L"POST",
                    L"/api/list.php",
                    nullptr,
                    WINHTTP_NO_REFERER,
                    WINHTTP_DEFAULT_ACCEPT_TYPES,
                    WINHTTP_FLAG_SECURE);

                if (hRequest) {
                    // Ignore SSL errors (cert chain/name mismatches on Render.com free tier)
                    DWORD sslFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                                     SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                                     SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
                    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &sslFlags, sizeof(sslFlags));

                    WinHttpAddRequestHeaders(hRequest,
                        L"Content-Type: application/x-www-form-urlencoded",
                        (DWORD)-1L,
                        WINHTTP_ADDREQ_FLAG_ADD);
                    WinHttpAddRequestHeaders(hRequest,
                        L"User-Agent: AegleDLL/1.0",
                        (DWORD)-1L,
                        WINHTTP_ADDREQ_FLAG_ADD);

                    if (WinHttpSendRequest(hRequest,
                            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                        WinHttpReceiveResponse(hRequest, nullptr)) {

                        DWORD dwSize = 0;
                        do {
                            dwSize = 0;
                            WinHttpQueryDataAvailable(hRequest, &dwSize);
                            if (dwSize == 0) break;
                            std::string chunk(dwSize, '\0');
                            DWORD dwDownloaded = 0;
                            WinHttpReadData(hRequest, &chunk[0], dwSize, &dwDownloaded);
                            raw.append(chunk, 0, dwDownloaded);
                        } while (dwSize > 0);

                        try {
                            auto j = nlohmann::json::parse(raw);
                            if (j.is_array()) {
                                for (const auto& item : j) {
                                    MarketConfig cfg;
                                    cfg.id = 0;
                                    cfg.likes = 0;
                                    cfg.downloads = 0;
                                    cfg.status = 0;
                                    
                                    if (item.contains("id") && item["id"].is_number()) cfg.id = item["id"];
                                    if (item.contains("title") && item["title"].is_string()) cfg.title = item["title"];
                                    if (item.contains("description") && item["description"].is_string()) cfg.description = item["description"];
                                    if (item.contains("author") && item["author"].is_string()) cfg.author = item["author"];
                                    if (item.contains("likes") && item["likes"].is_number()) cfg.likes = item["likes"];
                                    if (item.contains("downloads") && item["downloads"].is_number()) cfg.downloads = item["downloads"];
                                    if (item.contains("download_url") && item["download_url"].is_string()) cfg.downloadUrl = item["download_url"];
                                    
                                    if (ConfigManager::GetConfigDir().empty()) {
                                        ConfigManager::Initialize();
                                    }
                                    std::filesystem::path cfgPath = std::filesystem::path(ConfigManager::GetConfigDir()) / (cfg.title + ".json");
                                    if (std::filesystem::exists(cfgPath)) {
                                        cfg.status = 2; // downloaded
                                    }
                                    
                                    g_marketConfigs.push_back(cfg);
                                }
                                success = true;
                            }
                        } catch (...) {
                            success = false;
                        }
                    }
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
        }

        g_marketFetchDone = true;
        g_marketFetchFailed = !success;
        g_fetchingMarket = false;
        return 0;
    }, nullptr, 0, nullptr);
}

void GUI::DownloadConfig(int index) {
    if (index < 0 || index >= (int)g_marketConfigs.size()) return;
    if (g_marketConfigs[index].status == 1) return; // already downloading
    
    g_marketConfigs[index].status = 1; // downloading

    struct ThreadParams {
        int index;
        std::string title;
        std::string url;
    };
    
    ThreadParams* params = new ThreadParams{ index, g_marketConfigs[index].title, g_marketConfigs[index].downloadUrl };

    CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        ThreadParams* p = (ThreadParams*)lpParam;
        
        std::string url = p->url;
        std::string host = "aegle-configmp.onrender.com";
        std::string path = "";
        
        size_t proto_pos = url.find("://");
        std::string sub = (proto_pos == std::string::npos) ? url : url.substr(proto_pos + 3);
        size_t slash_pos = sub.find('/');
        if (slash_pos != std::string::npos) {
            host = sub.substr(0, slash_pos);
            path = sub.substr(slash_pos);
        } else {
            host = sub;
            path = "/";
        }

        std::wstring whost(host.begin(), host.end());
        std::wstring wpath(path.begin(), path.end());

        bool useHttps = (url.find("https://") == 0);
        INTERNET_PORT port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;

        std::string raw;
        bool success = false;

        HINTERNET hSession = WinHttpOpen(
            L"AegleDLL/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);

        if (hSession) {
            HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), port, 0);
            if (hConnect) {
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
                if (hRequest) {
                    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                        WinHttpReceiveResponse(hRequest, nullptr)) {

                        DWORD dwSize = 0;
                        do {
                            dwSize = 0;
                            WinHttpQueryDataAvailable(hRequest, &dwSize);
                            if (dwSize == 0) break;
                            std::string chunk(dwSize, '\0');
                            DWORD dwDownloaded = 0;
                            WinHttpReadData(hRequest, &chunk[0], dwSize, &dwDownloaded);
                            raw.append(chunk, 0, dwDownloaded);
                        } while (dwSize > 0);

                        try {
                            auto j = nlohmann::json::parse(raw);
                            if (!j.empty()) {
                                if (ConfigManager::GetConfigDir().empty()) {
                                    ConfigManager::Initialize();
                                }
                                
                                std::string filepath = ConfigManager::GetConfigDir() + p->title + ".json";
                                std::ofstream file(filepath, std::ios::out | std::ios::trunc);
                                if (file.is_open()) {
                                    file << j.dump(4);
                                    success = true;
                                }
                            }
                        } catch (...) {
                            success = false;
                        }
                    }
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
        }

        if (success) {
            g_marketConfigs[p->index].status = 2; // downloaded
            strcpy_s(g_notifTitle, "Config Market");
            sprintf_s(g_notifMessage, "Config '%s' downloaded!", p->title.c_str());
            g_notifStart = GetTickCount64();
        } else {
            g_marketConfigs[p->index].status = 3; // error
            strcpy_s(g_notifTitle, "Config Market");
            sprintf_s(g_notifMessage, "Failed to download '%s'", p->title.c_str());
            g_notifStart = GetTickCount64();
        }

        delete p;
        return 0;
    }, params, 0, nullptr);
}

void GUI::RenderConfigMarket() {
    // Trigger fetch on first open
    if (!g_marketFetchDone && !g_fetchingMarket) {
        FetchMarketConfigs();
    }

    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;

    // --- Refresh button (top-right, always visible unless loading) ---
    if (!g_fetchingMarket) {
        float btnW = 80.0f, btnH = 29.0f;
        ImGui::SetCursorPos(ImVec2(avail_w - btnW, 0.0f));

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.15f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.50f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 1.0f));

        if (ImGui::Button("  Refresh", ImVec2(btnW, btnH))) {
            g_marketFetchDone  = false;
            g_marketFetchFailed = false;
            g_marketConfigs.clear();
            FetchMarketConfigs();
        }

        ImGui::TextDisabled("Upload your configs at https://aegle-configmp.onrender.com/index.php ");

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();

        ImGui::Spacing();
    }

    // Recalc after button
    avail_w = ImGui::GetContentRegionAvail().x;
    avail_h = ImGui::GetContentRegionAvail().y;

    // Loading state
    if (g_fetchingMarket) {
        float t = (float)(GetTickCount64() % 900) / 300.0f;
        const char* dots[] = { "Loading Config Market .", "Loading Config Market ..", "Loading Config Market ..." };
        const char* msg = dots[(int)t % 3];
        ImVec2 textSize = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPos(ImVec2((avail_w - textSize.x) * 0.5f, avail_h * 0.4f));
        ImGui::TextDisabled("%s", msg);
        ImGui::Spacing();
        ImGui::TextDisabled("Upload your configs in https://aegle-configmp.onrender.com/index.php");
        return;
    }

    // Error state
    if (g_marketFetchFailed) {
        const char* errMsg = "Failed to connect to marketplace.";
        ImVec2 errSize = ImGui::CalcTextSize(errMsg);
        ImGui::SetCursorPos(ImVec2((avail_w - errSize.x) * 0.5f, avail_h * 0.38f));
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", errMsg);

        float btnW2 = 140.0f, btnH2 = 30.0f;
        ImGui::SetCursorPos(ImVec2((avail_w - btnW2) * 0.5f, ImGui::GetCursorPosY() + 10.0f));
        if (ImGui::Button("Retry Connection", ImVec2(btnW2, btnH2))) {
            g_marketFetchDone  = false;
            g_marketFetchFailed = false;
            FetchMarketConfigs();
        }
        return;
    }

    // Empty state (fetch done but no results)
    if (g_marketFetchDone && g_marketConfigs.empty()) {
        const char* emptyMsg = "No configs found in the marketplace.";
        ImVec2 emptySize = ImGui::CalcTextSize(emptyMsg);
        ImGui::SetCursorPos(ImVec2((avail_w - emptySize.x) * 0.5f, avail_h * 0.4f));
        ImGui::TextDisabled("%s", emptyMsg);
        return;
    }

    // Pagination logic
    static int g_marketPage = 0;
    static int g_lastMarketPage = 0;
    static float g_marketPageAnim = 1.0f;
    static bool g_marketFirstOpen = true;

    int totalConfigs = (int)g_marketConfigs.size();
    const int configsPerPage = 8;
    int totalPages = (totalConfigs + configsPerPage - 1) / configsPerPage;
    if (totalPages < 1) totalPages = 1;

    if (g_marketPage < 0) g_marketPage = 0;
    if (g_marketPage >= totalPages) g_marketPage = totalPages - 1;

    if (g_marketFirstOpen && g_marketFetchDone) {
        g_marketPageAnim = 0.0f;
        g_marketFirstOpen = false;
    }
    if (g_marketPage != g_lastMarketPage) {
        g_marketPageAnim = 0.0f;
        g_lastMarketPage = g_marketPage;
    }
    g_marketPageAnim += (1.0f - g_marketPageAnim) * 0.12f;

    int startIdx = g_marketPage * configsPerPage;
    int endIdx = (std::min)(startIdx + configsPerPage, totalConfigs);

    ImGui::BeginChild("MarketScrollList", ImVec2(0, avail_h - 70.0f), false, ImGuiWindowFlags_None);
    {
        float gap = 12.0f;
        float cardWidth = (ImGui::GetContentRegionAvail().x - 15.0f - gap) / 2.0f; // 15px is for vertical scrollbar
        float cardHeight = 120.0f;
        
        // Apply page fade animation (alpha only, no slide to prevent text shifting)
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_marketPageAnim);
        
        for (int i = startIdx; i < endIdx; i++) {
            const auto& cfg = g_marketConfigs[i];
            
            if (i > startIdx && (i - startIdx) % 2 != 0) {
                ImGui::SameLine(0.0f, gap);
            }
            
            std::string childId = "MarketCard_" + std::to_string(cfg.id);
            std::string animKey = "MarketCardHover_" + std::to_string(cfg.id);
            
            if (g_elementAnims.find(animKey) == g_elementAnims.end()) {
                g_elementAnims[animKey] = 0.0f;
            }
            float animVal = g_elementAnims[animKey];
            
            ImVec4 borderCol = ImVec4(
                g_colorAccentSoft.x + (g_colorAccent.x - g_colorAccentSoft.x) * animVal,
                g_colorAccentSoft.y + (g_colorAccent.y - g_colorAccentSoft.y) * animVal,
                g_colorAccentSoft.z + (g_colorAccent.z - g_colorAccentSoft.z) * animVal,
                0.2f + 0.4f * animVal
            );
            
            ImVec4 bgCol = ImVec4(
                g_colorBgPanel.x + (g_colorBgPanel.x * 1.3f - g_colorBgPanel.x) * animVal,
                g_colorBgPanel.y + (g_colorBgPanel.y * 1.3f - g_colorBgPanel.y) * animVal,
                g_colorBgPanel.z + (g_colorBgPanel.z * 1.3f - g_colorBgPanel.z) * animVal,
                0.15f + 0.08f * animVal
            );
            
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
            
            ImGui::PushStyleColor(ImGuiCol_ChildBg, bgCol);
            ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
            
            ImGui::BeginChild(childId.c_str(), ImVec2(cardWidth, cardHeight), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 cursor = ImGui::GetCursorPos();
                ImVec2 screenPos = ImGui::GetCursorScreenPos();
                ImDrawList* draw = ImGui::GetWindowDrawList();
                
                // Icon Box - size 36x36, padding 12px, so pos is (12, 12)
                float boxSize = 36.0f;
                // Calculate absolute box position
                ImVec2 boxMin = ImVec2(screenPos.x, screenPos.y + 2.0f);
                ImVec2 boxMax = ImVec2(boxMin.x + boxSize, boxMin.y + boxSize);
                
                draw->AddRectFilled(boxMin, boxMax, ImColor(g_colorBgMain.x * 2.0f, g_colorBgMain.y * 2.0f, g_colorBgMain.z * 2.0f, 0.5f), 6.0f);
                draw->AddRect(boxMin, boxMax, ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.4f), 6.0f, 0, 1.0f);
                
                ImGui::PushFont(g_fontH3 ? g_fontH3 : ImGui::GetFont());
                ImVec2 braceSize = ImGui::CalcTextSize("{}");
                ImVec2 bracePos = ImVec2(boxMin.x + (boxSize - braceSize.x) * 0.5f, boxMin.y + (boxSize - braceSize.y) * 0.5f);
                draw->AddText(bracePos, ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.8f), "{}");
                ImGui::PopFont();
                
                float textStartX = cursor.x + boxSize + 10.0f;
                
                // Title
                ImGui::SetCursorPos(ImVec2(textStartX, cursor.y));
                ImGui::PushFont(g_fontH3 ? g_fontH3 : ImGui::GetFont());
                ImGui::Text("%s", cfg.title.c_str());
                ImGui::PopFont();
                
                // Author
                ImGui::SameLine();
                ImGui::SetCursorPosY(cursor.y + 1.0f);
                ImGui::TextDisabled("by %s", cfg.author.c_str());
                
                // Description (with wrap)
                ImGui::SetCursorPos(ImVec2(textStartX, cursor.y + 22.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.75f, 0.9f));
                ImGui::PushTextWrapPos(cardWidth - 12.0f);
                ImGui::Text("%s", cfg.description.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                
                // Likes and Downloads (Bottom Left)
                float statsY = cursor.y + 72.0f;
                ImGui::SetCursorPos(ImVec2(cursor.x, statsY));
                
                if (g_likeTexture) {
                    ImGui::Image(ImTextureRef(g_likeTexture), ImVec2(14, 14), ImVec2(0,0), ImVec2(1,1));
                    ImGui::SameLine(0, 3);
                }
                ImGui::SetCursorPosY(statsY - 1.0f);
                ImGui::Text("%d", cfg.likes);
                
                ImGui::SameLine(0, 10);
                ImGui::SetCursorPosY(statsY);
                if (g_downloadTexture) {
                    ImGui::Image(ImTextureRef(g_downloadTexture), ImVec2(14, 14), ImVec2(0,0), ImVec2(1,1));
                    ImGui::SameLine(0, 3);
                }
                ImGui::SetCursorPosY(statsY - 1.0f);
                ImGui::Text("%d", cfg.downloads);
                
                // Action Buttons (at bottom-right y = 72.0f)
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                
                if (cfg.status == 0 || cfg.status == 3) {
                    float downloadBtnW = 75.0f;
                    float loadBtnW = 55.0f;
                    float buttonsTotalW = downloadBtnW + 5.0f + loadBtnW;
                    
                    ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - buttonsTotalW, cursor.y + 72.0f));
                    
                    if (cfg.status == 0) {
                        if (ImGui::Button("Download", ImVec2(downloadBtnW, 24.0f))) {
                            DownloadConfig(i);
                        }
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.6f));
                        if (ImGui::Button("Retry", ImVec2(downloadBtnW, 24.0f))) {
                            DownloadConfig(i);
                        }
                        ImGui::PopStyleColor();
                    }
                    
                    ImGui::SameLine(0, 5);
                    ImGui::BeginDisabled(true);
                    ImGui::Button("Load", ImVec2(loadBtnW, 24.0f));
                    ImGui::EndDisabled();
                }
                else if (cfg.status == 1) {
                    float dlBtnW = 95.0f;
                    float loadBtnW = 55.0f;
                    float buttonsTotalW = dlBtnW + 5.0f + loadBtnW;
                    
                    ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - buttonsTotalW, cursor.y + 72.0f));
                    
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.4f));
                    ImGui::BeginDisabled(true);
                    ImGui::Button("Downloading...", ImVec2(dlBtnW, 24.0f));
                    ImGui::EndDisabled();
                    ImGui::PopStyleColor();
                    
                    ImGui::SameLine(0, 5);
                    ImGui::BeginDisabled(true);
                    ImGui::Button("Load", ImVec2(loadBtnW, 24.0f));
                    ImGui::EndDisabled();
                }
                else if (cfg.status == 2) {
                    float loadBtnW = 55.0f;
                    float reinstallBtnW = 75.0f;
                    float deleteBtnW = 60.0f;
                    float buttonsTotalW = loadBtnW + 5.0f + reinstallBtnW + 5.0f + deleteBtnW;
                    
                    ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - buttonsTotalW, cursor.y + 72.0f));
                    
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.8f, 0.3f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
                    if (ImGui::Button("Load", ImVec2(loadBtnW, 24.0f))) {
                        if (ConfigManager::LoadConfig(cfg.title)) {
                            strcpy_s(g_notifTitle, "Config Market");
                            sprintf_s(g_notifMessage, "Loaded '%s'!", cfg.title.c_str());
                            g_notifStart = GetTickCount64();
                        } else {
                            strcpy_s(g_notifTitle, "Config Market");
                            sprintf_s(g_notifMessage, "Failed to load '%s'", cfg.title.c_str());
                            g_notifStart = GetTickCount64();
                        }
                    }
                    ImGui::PopStyleColor(3);
                    
                    ImGui::SameLine(0, 5);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.25f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.45f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.65f));
                    if (ImGui::Button("Reinstall", ImVec2(reinstallBtnW, 24.0f))) {
                        DownloadConfig(i);
                    }
                    ImGui::PopStyleColor(3);
                    
                    ImGui::SameLine(0, 5);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.4f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.2f, 0.2f, 0.8f));
                    if (ImGui::Button("Delete", ImVec2(deleteBtnW, 24.0f))) {
                        if (ConfigManager::DeleteConfig(cfg.title)) {
                            g_marketConfigs[i].status = 0;
                            strcpy_s(g_notifTitle, "Config Market");
                            sprintf_s(g_notifMessage, "Deleted '%s'", cfg.title.c_str());
                            g_notifStart = GetTickCount64();
                        } else {
                            strcpy_s(g_notifTitle, "Config Market");
                            sprintf_s(g_notifMessage, "Failed to delete '%s'", cfg.title.c_str());
                            g_notifStart = GetTickCount64();
                        }
                    }
                    ImGui::PopStyleColor(3);
                }
                
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            
            bool childHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
            float targetVal = childHovered ? 1.0f : 0.0f;
            g_elementAnims[animKey] += (targetVal - g_elementAnims[animKey]) * 0.15f;
            
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            
            if ((i - startIdx) % 2 != 0 || i == endIdx - 1) {
                ImGui::Spacing();
            }
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    char pageText[32];
    sprintf_s(pageText, "Page %d of %d", g_marketPage + 1, totalPages);
    float textW = ImGui::CalcTextSize(pageText).x;
    float pagButtonsW = 60.0f * 2 + 10.0f * 2 + textW;

    ImGui::SetCursorPosX((avail_w - pagButtonsW) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    bool prevDisabled = (g_marketPage == 0);
    if (prevDisabled) ImGui::BeginDisabled(true);
    if (ImGui::Button("< Prev", ImVec2(60, 24))) {
        g_marketPage--;
    }
    if (prevDisabled) ImGui::EndDisabled();

    ImGui::SameLine(0, 10);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
    ImGui::TextDisabled("%s", pageText);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.0f);
    ImGui::SameLine(0, 10);

    bool nextDisabled = (g_marketPage >= totalPages - 1);
    if (nextDisabled) ImGui::BeginDisabled(true);
    if (ImGui::Button("Next >", ImVec2(60, 24))) {
        g_marketPage++;
    }
    if (nextDisabled) ImGui::EndDisabled();

    ImGui::PopStyleVar();
}
