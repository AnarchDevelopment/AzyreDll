/*
Under an4rch Development Public Source License 1.0
*/

#include "GUI.hpp"
#include "../Animations/Animations.hpp"
#include "../Modules/Terminal/Terminal.hpp"
#include "../Modules/Info/Info.hpp"
#include "../Modules/Globals.hpp"
#include "../Input/Input.hpp"
#include "../Hook/Hook.hpp"
#include "../Networking/IRChat.hpp"
#include "../Config/ConfigManager.hpp"
#include "../Networking/Client/IRCClient.hpp"
#include <windows.h>
#include <shellapi.h>
#include "../Assets/resource.h"
#include <cmath>
#include <cstdlib>
#include <vector>
#include "../ArrayList/ArrayList.hpp"
#include "../Modules/ModuleHeader.hpp"
#include <d3d11.h>
#include "../Assets/stb/stb_image.h"
#include <winhttp.h>
#include <fstream>
#include <filesystem>
#include "../nlohmann/json.hpp"

#pragma comment(lib, "winhttp.lib")

#pragma warning(disable: 28159) // Consider using <variable> instead of <variable> to avoid potential issues with uninitialized variables. Reason: This warning is not relevant in this context, as we are intentionally using uninitialized variables for animation purposes.
#pragma warning(disable: 26495) // Disable uninitialized variable warnings

extern ID3D11Device* pDevice;
extern HMODULE g_hModule;


// Static member initialization
bool GUI::g_showMenu = false;
float GUI::g_menuAnim = 0.0f;
ImVec2 GUI::g_menuWinPos = ImVec2(0, 0);
ImVec2 GUI::g_menuWinSize = ImVec2(0, 0);

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
ImVec4 GUI::g_colorAccent = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
ImVec4 GUI::g_colorAccentSoft = ImVec4(0.80f, 0.80f, 0.85f, 0.40f);
ImVec4 GUI::g_colorAccentGlow = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);

float GUI::g_sidebarIndicatorY = 85.0f;
float GUI::g_sidebarTargetIndicatorY = 85.0f;

std::vector<GUI::Particle> GUI::g_particles;
ImFont* GUI::g_fontDefault = nullptr;
ImFont* GUI::g_fontH1 = nullptr;
ImFont* GUI::g_fontH2 = nullptr;
ImFont* GUI::g_fontH3 = nullptr;
ImFont* GUI::g_fontMono = nullptr;
std::vector<GUI::LoadedFont> GUI::g_loadedFonts;

std::map<std::string, float> GUI::g_elementAnims;
std::map<std::string, float> GUI::g_elementHeights;
void* GUI::g_tabTextures[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
void* GUI::g_likeTexture = nullptr;
void* GUI::g_downloadTexture = nullptr;

void* GUI::g_configIconTexture = nullptr;
std::map<std::string, void*> GUI::g_moduleTextures;

void* GUI::GetModuleTexture(const char* name) {
    auto it = g_moduleTextures.find(name);
    return (it != g_moduleTextures.end()) ? it->second : nullptr;
}

std::vector<GUI::MarketConfig> GUI::g_marketConfigs;
bool GUI::g_fetchingMarket = false;
bool GUI::g_marketFetchDone = false;
bool GUI::g_marketFetchFailed = false;

char g_notifTitle[64] = "Azyre | an4rch development";
char g_notifMessage[128] = "Injected successfully.";

struct CardInfo {
    std::string key;
    ImVec2 pos;
    float anim;
};
std::vector<CardInfo> g_cardStartStack;
std::vector<CardInfo> g_sectionStartStack;

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

// Typewriter-style text reveal: letters appear one by one (with glow + rise) as
// `progress` goes 0 -> 1, followed by a blinking caret while typing is in flight.
static void RenderTypingText(ImDrawList* draw, ImFont* font, float fontSize, ImVec2 pos, const char* text, float progress, const ImVec4& accent, float glowThickness) {
    float x = pos.x;
    int len = (int)strlen(text);

    for (int i = 0; i < len; i++) {
        char buf[2] = { text[i], '\0' };
        ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, buf);

        float letterT = Animations::Clamp01(progress * (float)len - (float)i);
        float eased = Animations::EaseOutExpo(letterT);

        if (eased > 0.01f) {
            ImVec2 letterPos = ImVec2(x, pos.y + (1.0f - eased) * -8.0f);
            float a = accent.w * eased;
            ImU32 col = ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, a));

            for (int g = 1; g <= (int)glowThickness; g++) {
                float ga = (0.22f / g) * eased;
                ImU32 glowCol = ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, ga));
                draw->AddText(font, fontSize, ImVec2(letterPos.x - g * 0.5f, letterPos.y), glowCol, buf);
                draw->AddText(font, fontSize, ImVec2(letterPos.x + g * 0.5f, letterPos.y), glowCol, buf);
                draw->AddText(font, fontSize, ImVec2(letterPos.x, letterPos.y - g * 0.5f), glowCol, buf);
            }
            draw->AddText(font, fontSize, letterPos, col, buf);
        }
        x += sz.x;
    }

    // Blinking caret while the reveal is still running
    if (progress < 1.0f) {
        float blink = (sinf((float)GetTickCount64() * 0.006f) > 0.0f) ? 0.9f : 0.15f;
        ImU32 caretCol = ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, accent.w * blink));
        draw->AddRectFilled(ImVec2(x + 2.0f, pos.y), ImVec2(x + 4.0f, pos.y + fontSize), caretCol);
    }
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
            g_colorBgMain = ImVec4(0.05f, 0.04f, 0.07f, 0.99f);        // Dark
            g_colorBgPanel = ImVec4(0.08f, 0.07f, 0.12f, 0.00f);        // Dark panel
            g_colorAccent = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);         // White accent
            g_colorAccentSoft = ImVec4(0.80f, 0.80f, 0.85f, 0.40f);     // Light gray soft
            g_colorAccentGlow = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
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
        case Theme_LegacyPink:
            g_colorBgMain = ImVec4(0.05f, 0.04f, 0.07f, 0.99f);
            g_colorBgPanel = ImVec4(0.08f, 0.07f, 0.12f, 0.00f);
            g_colorAccent = ImVec4(1.00f, 0.40f, 0.80f, 1.00f); // Pink
            g_colorAccentSoft = ImVec4(0.60f, 0.50f, 1.00f, 0.40f); // Purple Soft
            g_colorAccentGlow = ImVec4(1.00f, 0.40f, 0.80f, 0.40f);
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
    loadFontFromResource(IDR_FONT_POPPINS,        "Poppins");
    loadFontFromResource(IDR_FONT_PLAYFAIR,       "Playfair Display");

    // Dedicated smaller monospace font for the Terminal (fallback to Consolas)
    ImFontConfig mcfg;
    mcfg.OversampleH = 3;
    mcfg.OversampleV = 3;
    mcfg.PixelSnapH = true;
    mcfg.FontDataOwnedByAtlas = false;
    HRSRC hMono = FindResource(g_hModule, MAKEINTRESOURCE(IDR_FONT_JETBRAINS_MONO), RT_RCDATA);
    if (hMono) {
        HGLOBAL hMonoData = LoadResource(g_hModule, hMono);
        if (hMonoData) {
            void* pMono = LockResource(hMonoData);
            DWORD monoSize = SizeofResource(g_hModule, hMono);
            if (pMono && monoSize > 0) {
                g_fontMono = io.Fonts->AddFontFromMemoryTTF(pMono, (int)monoSize, 15.0f, &mcfg);
            }
        }
    }
    if (!g_fontMono && io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 15.0f, &cfg)) {
        g_fontMono = io.Fonts->Fonts.back();
    }
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
    
    ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x - 15, 54.0f);
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
    
    // Draw hover background highlight (if not active, since active has the sliding pill)
    if (!active && anim > 0.001f) {
        ImVec4 accentV = g_colorAccent;
        ImU32 col = ImColor(accentV.x, accentV.y, accentV.z, anim * 0.15f);
        draw->AddRectFilled(p_min, p_max, col, 8.0f);
    }
    
    // Draw icon if available
    void* tex = g_tabTextures[index];
    if (tex != nullptr) {
        float iconSize = 20.0f * (active ? (1.0f + anim * 0.08f) : 1.0f);
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
    float width = 34.0f;
    float radius = height * 0.5f;
    
    std::string key = "switch_" + std::string(label);
    if (g_elementAnims.find(key) == g_elementAnims.end()) g_elementAnims[key] = *value ? 1.0f : 0.0f;
    
    float target = *value ? 1.0f : 0.0f;
    g_elementAnims[key] = Animations::Approach(g_elementAnims[key], target, ImGui::GetIO().DeltaTime, 12.0f);
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
    // Knob keeps contrast with the fill: white when off (dark pill), dark when
    // on (white pill) so an active switch never looks like a white blob.
    ImU32 knobCol = ImColor(
        (int)((1.0f - anim) * 255.0f + bgColEmpty.x * 255.0f * anim),
        (int)((1.0f - anim) * 255.0f + bgColEmpty.y * 255.0f * anim),
        (int)((1.0f - anim) * 255.0f + bgColEmpty.z * 255.0f * anim),
        255
    );
    draw->AddCircleFilled(ImVec2(circle_pos, p.y + radius), radius - 3.0f, knobCol);
    
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
    g_elementAnims[key] = Animations::Approach(g_elementAnims[key], target, ImGui::GetIO().DeltaTime, 10.0f);
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
    
    // Arrow animation - smooth 90° rotation: points right (collapsed) -> points down (expanded)
    float arrow_size = 6.0f;
    ImVec2 arrow_center = ImVec2(p_max.x - 20, p_min.y + 17.5f);
    float arrowAngle = anim * 1.5707963f; // PI/2
    float cosA = cosf(arrowAngle), sinA = sinf(arrowAngle);
    auto rot = [&](const ImVec2& v) -> ImVec2 {
        return ImVec2(v.x * cosA - v.y * sinA, v.x * sinA + v.y * cosA);
    };
    ImVec2 rTip = rot(ImVec2(arrow_size, 0.0f));
    ImVec2 rB1 = rot(ImVec2(-arrow_size * 0.55f, -arrow_size * 0.9f));
    ImVec2 rB2 = rot(ImVec2(-arrow_size * 0.55f, arrow_size * 0.9f));
    ImVec2 p0 = ImVec2(arrow_center.x + rTip.x, arrow_center.y + rTip.y);
    ImVec2 p1 = ImVec2(arrow_center.x + rB1.x, arrow_center.y + rB1.y);
    ImVec2 p2 = ImVec2(arrow_center.x + rB2.x, arrow_center.y + rB2.y);

    // Color blends from neutral gray to accent as the section opens
    ImU32 arrowCol = ImColor(
        g_colorAccent.x * anim + 0.55f * (1.0f - anim),
        g_colorAccent.y * anim + 0.55f * (1.0f - anim),
        g_colorAccent.z * anim + 0.55f * (1.0f - anim)
    );
    draw->AddTriangleFilled(p0, p1, p2, arrowCol);

    // Accent underline that fills left-to-right while the section opens
    if (anim > 0.01f) {
        float barWidth = (p_max.x - p_min.x) * Animations::EaseOutQuart(anim);
        draw->AddRectFilled(
            ImVec2(p_min.x, p_max.y - 1.0f),
            ImVec2(p_min.x + barWidth, p_max.y),
            ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.8f * anim)
        );
    }
    
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    
    if (anim > 0.01f) {
        g_sectionStartStack.push_back({ key, p_min, anim });
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

    // Section card: faint rounded panel behind the whole section
    if (!g_sectionStartStack.empty()) {
        CardInfo sec = g_sectionStartStack.back();
        g_sectionStartStack.pop_back();

        ImVec2 endPos = ImGui::GetItemRectMax();
        ImVec2 rectMin = ImVec2(sec.pos.x - 6.0f, sec.pos.y + 2.0f);
        ImVec2 rectMax = ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 24.0f, endPos.y + 8.0f);
        if (rectMax.y > rectMin.y + 8.0f) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(rectMin, rectMax, IM_COL32(20, 20, 28, (int)(30 * sec.anim)), 10.0f);
            draw->AddRect(rectMin, rectMax,
                ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.10f * sec.anim), 10.0f, 0, 1.0f);
            draw->AddRectFilled(ImVec2(rectMin.x + 2.0f, rectMin.y + 12.0f), ImVec2(rectMin.x + 3.5f, rectMax.y - 12.0f),
                ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.45f * sec.anim), 1.0f);
        }
    }

    ImGui::Spacing();
}

bool GUI::BeginModuleSettings(const char* label, bool* open) {
    std::string key = "mod_set_" + std::string(label);
    if (g_elementAnims.find(key) == g_elementAnims.end()) g_elementAnims[key] = *open ? 1.0f : 0.0f;
    
    float target = *open ? 1.0f : 0.0f;
    g_elementAnims[key] = Animations::Approach(g_elementAnims[key], target, ImGui::GetIO().DeltaTime, 10.0f);
    float anim = g_elementAnims[key];
    
    if (anim <= 0.01f) return false;
    
    ImGui::Spacing();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1.0f - anim) * 6.0f);
    ImVec2 startPos = ImGui::GetCursorScreenPos();

    ImGui::Indent(12.0f);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.8f);
    ImGui::BeginGroup();

    g_cardStartStack.push_back({ key, startPos, anim });

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
    ImGui::PushID(label);
    return true;
}

void GUI::EndModuleSettings() {
    ImGui::EndGroup();
    ImVec2 endPos = ImGui::GetItemRectMax();

    if (!g_cardStartStack.empty()) {
        CardInfo card = g_cardStartStack.back();
        g_cardStartStack.pop_back();
    }

    ImGui::PopID();
    ImGui::PopStyleVar(); // ItemSpacing
}

// Helper: returns pointer into label past the '##' for display, or the full label if none
static const char* DisplayLabel(const char* label) {
    static char s_buf[128];
    const char* sep = strstr(label, "##");
    if (!sep) return label;
    int len = (int)(sep - label);
    if (len >= 128) len = 127;
    memcpy(s_buf, label, len);
    s_buf[len] = '\0';
    return s_buf;
}

// ── Custom Slider ──────────────────────────────────────────────────────────────
bool GUI::RenderSlider(const char* label, float* value, float min, float max, const char* format) {
    ImGui::PushID(label);

    const char* displayText = DisplayLabel(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    float lineH = ImGui::GetFrameHeight();
    float labelW = ImGui::CalcTextSize(displayText).x;
    float availW = ImGui::GetContentRegionAvail().x;
    float sliderW = availW - labelW - 16.0f;
    if (sliderW < 80.0f) sliderW = availW * 0.55f;

    float trackH = 6.0f;
    float trackY = p.y + (lineH - trackH) * 0.5f;

    // Interaction area
    ImGui::InvisibleButton("##slider", ImVec2(availW, lineH));
    bool changed = false;
    if (ImGui::IsItemActive()) {
        float mouseX = ImGui::GetIO().MousePos.x;
        float t = (mouseX - (p.x + labelW + 10.0f)) / sliderW;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        float newVal = min + t * (max - min);
        if (newVal != *value) { *value = newVal; changed = true; }
    }

    float t = (*value - min) / (max - min);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    ImVec4 accV = g_colorAccent;
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    // Animated hover glow
    std::string key = "sl_" + std::string(label);
    if (g_elementAnims.find(key) == g_elementAnims.end()) g_elementAnims[key] = 0.0f;
    float hTarget = active ? 1.0f : (hovered ? 0.6f : 0.0f);
    g_elementAnims[key] = Animations::Approach(g_elementAnims[key], hTarget, ImGui::GetIO().DeltaTime, 12.0f);
    float hAnim = g_elementAnims[key];

    // Label
    draw->AddText(ImVec2(p.x, p.y + (lineH - ImGui::GetFontSize()) * 0.5f),
        IM_COL32(210, 210, 220, 220), displayText);

    // Track
    float trackX = p.x + labelW + 10.0f;
    float trackEnd = trackX + sliderW;

    // Track glow on hover
    if (hAnim > 0.01f) {
        draw->AddRectFilled(ImVec2(trackX - 2, trackY - 2), ImVec2(trackEnd + 2, trackY + trackH + 2),
            IM_COL32((int)(accV.x * 255), (int)(accV.y * 255), (int)(accV.z * 255), (int)(12 * hAnim)), 5.0f);
    }
    draw->AddRectFilled(ImVec2(trackX, trackY), ImVec2(trackEnd, trackY + trackH),
        IM_COL32(22, 22, 30, 200), 3.0f);

    // Filled portion with animated glow
    if (sliderW * t > 2.0f) {
        float fillEnd = trackX + sliderW * t;
        draw->AddRectFilledMultiColor(
            ImVec2(trackX, trackY), ImVec2(fillEnd, trackY + trackH),
            IM_COL32((int)(accV.x * 180), (int)(accV.y * 140), (int)(accV.z * 220), 220),
            IM_COL32((int)(accV.x * 255), (int)(accV.y * 255), (int)(accV.z * 255), 240),
            IM_COL32((int)(accV.x * 255), (int)(accV.y * 255), (int)(accV.z * 255), 240),
            IM_COL32((int)(accV.x * 180), (int)(accV.y * 140), (int)(accV.z * 220), 220));
        draw->AddRectFilled(
            ImVec2(trackX, trackY), ImVec2(fillEnd, trackY + trackH * 0.4f),
            IM_COL32(255, 255, 255, 25));
    }

    // Knob with animated glow
    float knobX = trackX + sliderW * t;
    float knobR = 7.0f + hAnim * 2.0f;
#pragma warning(disable : 4244)
    float glowPulse = (sinf(ImGui::GetTime() * 4.0f) + 1.0f) * 0.5f;
    float glowA = 20.0f + glowPulse * 12.0f * hAnim;

    draw->AddCircleFilled(ImVec2(knobX, trackY + trackH * 0.5f), knobR + 5.0f,
        IM_COL32((int)(accV.x * 255), (int)(accV.y * 255), (int)(accV.z * 255), (int)(glowA)));
    draw->AddCircleFilled(ImVec2(knobX, trackY + trackH * 0.5f), knobR,
        IM_COL32(240, 240, 248, 255));
    draw->AddCircle(ImVec2(knobX, trackY + trackH * 0.5f), knobR,
        IM_COL32((int)(accV.x * 255), (int)(accV.y * 255), (int)(accV.z * 255), (int)(100 + 80 * hAnim)), 0, 1.5f);

    // Value text
    char valBuf[64];
    snprintf(valBuf, sizeof(valBuf), format, *value);
    ImVec2 valSize = ImGui::CalcTextSize(valBuf);
    draw->AddText(ImVec2(trackEnd - valSize.x, p.y + (lineH - valSize.y) * 0.5f),
        IM_COL32((int)(accV.x * 255), (int)(accV.y * 255), (int)(accV.z * 255), 210), valBuf);

    ImGui::PopID();
    return changed;
}

// ── Custom Combo / Dropdown ────────────────────────────────────────────────────
bool GUI::RenderCombo(const char* label, int* current_item, const char* const* items, int items_count) {
    ImGui::PushID(label);

    const char* displayText = DisplayLabel(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    float lineH = ImGui::GetFrameHeight();
    float labelW = ImGui::CalcTextSize(displayText).x;
    float availW = ImGui::GetContentRegionAvail().x;
    float comboW = availW - labelW - 16.0f;
    if (comboW < 100.0f) comboW = availW * 0.6f;

    ImVec4 accV = g_colorAccent;

    // Label
    draw->AddText(ImVec2(p.x, p.y + (lineH - ImGui::GetFontSize()) * 0.5f),
        ImColor(210, 210, 220, 220), displayText);

    // Combo box background
    float comboX = p.x + labelW + 10.0f;
    ImVec2 comboMin(comboX, p.y);
    ImVec2 comboMax(comboX + comboW, p.y + lineH);

    bool hovered = ImGui::IsItemHovered();

    // Invisible button for interaction
    ImGui::SetCursorScreenPos(comboMin);
    ImGui::InvisibleButton("##combo", ImVec2(comboW, lineH));
    bool clicked = ImGui::IsItemClicked();
    bool comboHovered = ImGui::IsItemHovered();

    // Background
    ImU32 bgCol = comboHovered ? IM_COL32(30, 30, 40, 220) : IM_COL32(20, 20, 28, 200);
    draw->AddRectFilled(comboMin, comboMax, bgCol, 6.0f);
    draw->AddRect(comboMin, comboMax,
        IM_COL32((int)(accV.x * 60), (int)(accV.y * 60), (int)(accV.z * 60), 80), 6.0f, 0, 1.0f);

    // Current item text
    const char* currentText = (*current_item >= 0 && *current_item < items_count) ? items[*current_item] : "---";
    draw->AddText(ImVec2(comboMin.x + 10.0f, comboMin.y + (lineH - ImGui::GetFontSize()) * 0.5f),
        ImColor(230, 230, 238, 230), currentText);

    // Chevron arrow
    float arrowX = comboMax.x - 16.0f;
    float arrowY = comboMin.y + lineH * 0.5f;
    float arrowS = 4.0f;
    draw->AddLine(ImVec2(arrowX - arrowS, arrowY - arrowS * 0.6f), ImVec2(arrowX, arrowY + arrowS * 0.3f),
        ImColor(140, 140, 160, 180), 1.5f);
    draw->AddLine(ImVec2(arrowX, arrowY + arrowS * 0.3f), ImVec2(arrowX + arrowS, arrowY - arrowS * 0.6f),
        ImColor(140, 140, 160, 180), 1.5f);

    // Open popup on click
    if (clicked) ImGui::OpenPopup("##combo_popup");

    // Custom styled popup
    ImGui::SetNextWindowPos(ImVec2(comboMin.x, comboMax.y + 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.07f, 0.07f, 0.11f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(accV.x * 0.3f, accV.y * 0.3f, accV.z * 0.3f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(accV.x * 0.15f, accV.y * 0.15f, accV.z * 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(accV.x * 0.2f, accV.y * 0.2f, accV.z * 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(accV.x * 0.3f, accV.y * 0.3f, accV.z * 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.90f, 0.95f, 1.0f));

    bool valueChanged = false;
    if (ImGui::BeginPopup("##combo_popup", ImGuiWindowFlags_NoMove)) {
        for (int i = 0; i < items_count; i++) {
            bool isSelected = (*current_item == i);

            // Custom selectable with accent highlight
            ImVec2 itemPos = ImGui::GetCursorScreenPos();
            ImVec2 itemSize(comboW, ImGui::GetFrameHeight());

            // Highlight bar for selected item
            if (isSelected) {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    itemPos, ImVec2(itemPos.x + comboW, itemPos.y + itemSize.y),
                    IM_COL32((int)(accV.x * 255), (int)(accV.y * 255), (int)(accV.z * 255), 20), 4.0f);
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(itemPos.x, itemPos.y + 4.0f), ImVec2(itemPos.x + 2.5f, itemPos.y + itemSize.y - 4.0f),
                    IM_COL32((int)(accV.x * 255), (int)(accV.y * 255), (int)(accV.z * 255), 200), 1.0f);
            }

            if (ImGui::Selectable(items[i], isSelected)) {
                *current_item = i;
                valueChanged = true;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(3);

    ImGui::PopID();
    return valueChanged;
}

// ── Custom Slider Int ─────────────────────────────────────────────────────────
bool GUI::RenderSliderInt(const char* label, int* value, int min, int max, const char* format) {
    float fval = (float)*value;
    bool changed = RenderSlider(label, &fval, (float)min, (float)max, format);
    if (changed) *value = (int)fval;
    return changed;
}

// ── Custom Checkbox ───────────────────────────────────────────────────────────
bool GUI::RenderCheckbox(const char* label, bool* value) {
    ImGui::PushID(label);

    const char* displayText = DisplayLabel(label);
    float lineH = ImGui::GetFrameHeight();
    if (lineH < 20.0f) lineH = 20.0f;
    float boxS = 16.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float textW = ImGui::CalcTextSize(displayText).x;

    std::string key = "chk_" + std::string(label);
    if (g_elementAnims.find(key) == g_elementAnims.end()) g_elementAnims[key] = *value ? 1.0f : 0.0f;

    float dt = ImGui::GetIO().DeltaTime;
    float target = *value ? 1.0f : 0.0f;
    g_elementAnims[key] = Animations::Approach(g_elementAnims[key], target, dt, 14.0f);
    float anim = g_elementAnims[key];

    ImGui::InvisibleButton("##chk", ImVec2(boxS + 10.0f + textW, lineH));
    bool changed = false;
    if (ImGui::IsItemClicked()) { *value = !*value; changed = true; }

    bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec4 accV = g_colorAccent;

    ImVec2 boxMin(p.x, p.y + (lineH - boxS) * 0.5f);
    ImVec2 boxMax(p.x + boxS, boxMin.y + boxS);
    float rounding = 3.0f;

    if (hovered) {
        draw->AddRectFilled(ImVec2(boxMin.x - 3, boxMin.y - 3), ImVec2(boxMax.x + 3, boxMax.y + 3),
            ImColor(accV.x, accV.y, accV.z, 0.08f), rounding + 2.0f);
    }

    float uncheckedR = 28.0f, uncheckedG = 28.0f, uncheckedB = 36.0f;
    float bgR = uncheckedR + (accV.x * 255.0f - uncheckedR) * anim;
    float bgG = uncheckedG + (accV.y * 255.0f - uncheckedG) * anim;
    float bgB = uncheckedB + (accV.z * 255.0f - uncheckedB) * anim;
    draw->AddRectFilled(boxMin, boxMax, IM_COL32((int)bgR, (int)bgG, (int)bgB, (int)(220 + anim * 35)), rounding);

    float borderAlpha = 40.0f + 80.0f * anim;
    draw->AddRect(boxMin, boxMax,
        IM_COL32((int)(accV.x * 200), (int)(accV.y * 200), (int)(accV.z * 200), (int)borderAlpha), rounding, 0, 1.0f);

    if (anim > 0.01f) {
        float s = Animations::EaseOutBack(anim);
        float cx = boxMin.x + boxS * 0.5f;
        float cy = boxMin.y + boxS * 0.5f;
        float len = 4.0f * s;
        draw->AddLine(ImVec2(cx - len, cy), ImVec2(cx - len * 0.15f, cy + len * 0.75f),
            IM_COL32(255, 255, 255, (int)(240 * anim)), 2.0f);
        draw->AddLine(ImVec2(cx - len * 0.15f, cy + len * 0.75f), ImVec2(cx + len, cy - len * 0.65f),
            IM_COL32(255, 255, 255, (int)(240 * anim)), 2.0f);
    }

    draw->AddText(ImVec2(boxMax.x + 8.0f, p.y + (lineH - ImGui::GetFontSize()) * 0.5f),
        IM_COL32(210, 210, 220, 220), displayText);

    ImGui::PopID();
    return changed;
}

// ── Custom Button ─────────────────────────────────────────────────────────────
bool GUI::RenderButton(const char* label, const ImVec2& size) {
    ImGui::PushID(label);

    const char* displayText = DisplayLabel(label);

    ImVec2 btnSize = size;
    if (btnSize.x <= 0.0f) btnSize.x = ImGui::GetContentRegionAvail().x;
    if (btnSize.y <= 0.0f) btnSize.y = ImGui::GetFrameHeight() + 4.0f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec4 accV = g_colorAccent;

    std::string key = "btn_" + std::string(label);
    if (g_elementAnims.find(key) == g_elementAnims.end()) g_elementAnims[key] = 0.0f;

    ImGui::InvisibleButton("##btn", btnSize);
    bool clicked = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    float target = active ? 1.0f : (hovered ? 0.5f : 0.0f);
    g_elementAnims[key] = Animations::Approach(g_elementAnims[key], target, ImGui::GetIO().DeltaTime, 10.0f);
    float anim = g_elementAnims[key];

    ImVec2 bMin = p;
    ImVec2 bMax = ImVec2(p.x + btnSize.x, p.y + btnSize.y);

    // Background
    ImU32 bgCol;
    if (active) {
        bgCol = IM_COL32((int)(accV.x * 200), (int)(accV.y * 200), (int)(accV.z * 200), 200);
    } else if (hovered) {
        bgCol = IM_COL32(
            (int)(22 + (accV.x * 255 - 22) * anim * 0.6f),
            (int)(22 + (accV.y * 255 - 22) * anim * 0.6f),
            (int)(30 + (accV.z * 255 - 30) * anim * 0.6f), 200);
    } else {
        bgCol = IM_COL32(22, 22, 30, 180);
    }
    draw->AddRectFilled(bMin, bMax, bgCol, 6.0f);

    // Border
    draw->AddRect(bMin, bMax,
        IM_COL32((int)(accV.x * 255), (int)(accV.y * 255), (int)(accV.z * 255), (int)(30 + 60 * anim)), 6.0f, 0, 1.0f);

    // Text
    ImVec2 ts = ImGui::CalcTextSize(displayText);
    ImVec2 textPos(bMin.x + (btnSize.x - ts.x) * 0.5f, bMin.y + (btnSize.y - ts.y) * 0.5f);
    ImU32 textCol = hovered ? IM_COL32(255, 255, 255, 240) : IM_COL32(190, 190, 205, 200);
    draw->AddText(textPos, textCol, displayText);

    ImGui::PopID();
    return clicked;
}

void GUI::RenderSectionHeader(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, g_colorAccent);
    ImGui::Text(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

bool GUI::IsHudEditable() {
    return g_showMenu && GUI::g_menuAnim > 0.01f;
}

void GUI::UpdateAnimation(ULONGLONG now, float dt) {
    dt = Animations::Clamp01(dt * 10.0f) * 0.1f;
    if (GUI::g_showMenu) {
        GUI::g_menuAnim = Animations::Approach(GUI::g_menuAnim, 1.0f, dt, 3.0f);
    } else {
        GUI::g_menuAnim = Animations::Approach(GUI::g_menuAnim, 0.0f, dt, 18.0f);
    }
    GUI::g_menuAnim = Animations::Clamp01(GUI::g_menuAnim);
    if (!GUI::g_showMenu && GUI::g_menuAnim < 0.02f) {
        GUI::g_menuAnim = 0.0f;
    }
    
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

static void RestoreGameCursorLock(bool wasInWorld) {
    if (!wasInWorld || !g_window || !IsWindow(g_window)) return;

    RECT windowRect{};
    if (GetWindowRect(g_window, &windowRect)) {
        if (Hook::oClipCursor) {
            Hook::oClipCursor(&windowRect);
        } else {
            ClipCursor(&windowRect);
        }
    }
}

void GUI::HandleMenuToggle() {
    if (!(GetAsyncKeyState(VK_INSERT) & 0x8000) || (GetTickCount64() - g_lastToggle) <= 400)
        return;

    // NOTE: unqualified `g_showMenu` inside this member function resolves to
    // GUI::g_showMenu, so the global from Globals.hpp (used by Present/Input/
    // hooks) must be addressed explicitly with `::`. Keeping both in sync here
    // is what drives the cursor logic in-world.
    ::g_showMenu = !::g_showMenu;
    GUI::g_showMenu = ::g_showMenu;
    ClickGUI::g_enabled = ::g_showMenu;
    g_lastToggle = GetTickCount64();

    if (::g_showMenu) {
        Input::BlockGameInput();
        Hook::oClipCursor(NULL);
        g_wasInWorld = Input::IsInWorld();
        g_tabChangeTime = GetTickCount64();
        g_tabAnim = 0.0f;
        Input::DebugLogCursorState("toggle-open");
    } else {
        Input::UnblockGameInput();
        if (g_wasInWorld) {
            RestoreGameCursorLock(g_wasInWorld);
            Input::DebugLogCursorState("toggle-close");
        }
    }
}

void GUI::SyncMenuState() {
    // Sync menu state if closed via the GUI Close ("X") button
    if (::g_showMenu && !GUI::g_showMenu) {
        ::g_showMenu = false;
        ClickGUI::g_enabled = false;
        Input::UnblockGameInput();
        RestoreGameCursorLock(g_wasInWorld);
    }

    // Sync menu state if ClickGUI is disabled from settings
    if (::g_showMenu && !ClickGUI::g_enabled) {
        ::g_showMenu = false;
        GUI::g_showMenu = false;
        Input::UnblockGameInput();
        RestoreGameCursorLock(g_wasInWorld);
    }
}

void GUI::RenderBackdrop(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, IDXGISwapChain* pSwapChain, float screenWidth, float screenHeight) {
    float menuOpacity = GUI::g_showMenu
        ? Animations::EaseOutExpo(GUI::g_menuAnim)
        : Animations::EaseInOutQuad(GUI::g_menuAnim);

    if (GUI::g_menuAnim > 0.001f && ClickGUI::g_bgStyle == 1) {
        ClickGUI::RenderBlurBackground(pDevice, pContext, pSwapChain, screenWidth, screenHeight, menuOpacity);
    } else {
        // Region-scoped Mica blur behind the ArrayList HUD (skipped when the
        // full-screen menu blur already frosted the scene this frame).
        ArrayList::RenderBlur(pDevice, pContext, pSwapChain);
    }

    // DX11/DXGI soft blur shadow behind the ClickGUI window (replaces the old
    // flat DrawShadow). Renders a dark rounded-rect, gaussian-blurs it and
    // composites it before ImGui draws the window on top.
    if (GUI::g_menuAnim > 0.001f && g_menuWinSize.x > 1.0f && g_menuWinSize.y > 1.0f) {
        ClickGUI::RenderBlurShadow(pDevice, pContext, pSwapChain, screenWidth, screenHeight,
                                   g_menuWinPos.x, g_menuWinPos.y,
                                   g_menuWinSize.x, g_menuWinSize.y, menuOpacity);
    }

    // Rise Background shader pass (DX11 offscreen texture for Regular mode)
    if (GUI::g_menuAnim > 0.001f && ClickGUI::g_guiStyle == 0 && ClickGUI::g_showRiseBackground &&
        g_menuWinSize.x > 1.0f && g_menuWinSize.y > 1.0f) {
        static ULONGLONG s_riseStartTime = GetTickCount64();
        float timeSec = (float)(GetTickCount64() - s_riseStartTime) / 1000.0f;
        ClickGUI::RenderRiseBackground(pDevice, pContext, g_menuWinSize.x, g_menuWinSize.y, timeSec, menuOpacity);
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
    } else if (ClickGUI::g_guiStyle == 3) {
        ClickGUI::RenderLunarMenu(screenWidth, screenHeight);
        return;
    } else if (ClickGUI::g_guiStyle == 4) {
        ClickGUI::RenderFigmaMenu(screenWidth, screenHeight);
        return;
    } else if (ClickGUI::g_guiStyle == 5) {
        ClickGUI::RenderAuroraMenu(screenWidth, screenHeight);
        return;
    } else if (ClickGUI::g_guiStyle == 6) {
        ClickGUI::RenderFlarialMenu(screenWidth, screenHeight);
        return;
    } else if (ClickGUI::g_guiStyle == 7) {
        ClickGUI::RenderNixonMenu(screenWidth, screenHeight);
        return;
    }
    
    float positionProgress = GUI::g_showMenu
        ? Animations::EaseOutExpo(GUI::g_menuAnim)
        : GUI::g_menuAnim;
    float e = GUI::g_showMenu
        ? positionProgress
        : Animations::EaseInOutQuad(GUI::g_menuAnim);
    // Background: dark vignette behind the menu
    {
        ImDrawList* bd = ImGui::GetBackgroundDrawList();
        int baseA = (ClickGUI::g_bgStyle == 0) ? 175 : 25;
        bd->AddRectFilled(ImVec2(0, 0), ImVec2(screenWidth, screenHeight), IM_COL32(3, 4, 8, (int)(e * baseA)));
    }

    if (e > 0.01f) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);

        // Slide the window vertically without changing its size.
        float sc = 1.0f;
        ImVec2 baseSize = ImVec2(1060, 680);
        ImVec2 winSize = ImVec2(baseSize.x * sc, baseSize.y * sc);
        float shiftAmt = 110.0f * sc * GUI::g_ircShiftAnim;
        float verticalDirection = GUI::g_showMenu ? 1.0f : -1.0f;
        float verticalDistance = GUI::g_showMenu ? 180.0f : 320.0f;
        float verticalOffset = verticalDirection * verticalDistance * (1.0f - positionProgress);
        ImVec2 winPos = ImVec2(screenWidth / 2 - winSize.x / 2 - shiftAmt,
                       screenHeight / 2 - winSize.y / 2 + verticalOffset);

        // Publish the window rect so RenderBackdrop can regenerate the DX11 blur
        // shadow texture each frame (the old flat DrawShadow was replaced).
        g_menuWinPos  = winPos;
        g_menuWinSize = winSize;

        // Composite the DX11-blurred shadow as a premultiplied-alpha image over the
        // vignette but under the window. The texture is updated in RenderBackdrop
        // (which runs right before ImGui::Render), so it's current by draw time.
        if (ID3D11ShaderResourceView* shadowSRV = ClickGUI::GetShadowSRV()) {
            const float shadowMargin = ClickGUI::kShadowMargin;
            ImVec2 shadowPos = ImVec2(winPos.x - shadowMargin, winPos.y - shadowMargin);
            ImGui::GetBackgroundDrawList()->AddImage(
                (ImTextureID)shadowSRV,
                shadowPos, ImVec2(shadowPos.x + (float)ClickGUI::GetShadowTexW(),
                                  shadowPos.y + (float)ClickGUI::GetShadowTexH()),
                ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
        }

        ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
        ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * sc);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,     ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(6.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2(10.0f, 5.0f));
        ImGui::PushStyleColor(ImGuiCol_Border,
            ImVec4(g_colorAccent.x * 0.22f, g_colorAccent.y * 0.22f, g_colorAccent.z * 0.22f, 0.65f));

        if (ImGui::Begin("Azyre", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
            
            ImVec2 wPos = ImGui::GetWindowPos();
            ImVec2 wSize = ImGui::GetWindowSize();
            
            // Render Rise Background inside the window draw list
            if (ClickGUI::g_showRiseBackground) {
                if (ID3D11ShaderResourceView* riseSRV = ClickGUI::GetRiseSRV()) {
                    ImGui::GetWindowDrawList()->AddImageRounded(
                        (ImTextureID)riseSRV,
                        wPos,
                        ImVec2(wPos.x + wSize.x, wPos.y + wSize.y),
                        ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32(255, 255, 255, (int)(e * 255.0f)),
                        16.0f * sc
                    );
                }
            }
            
            // ── Header Bar ──
            ImGui::BeginChild("HeaderBar", ImVec2(0, 64.0f * sc), false, ImGuiWindowFlags_NoScrollbar);
            {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                float headerH = 64.0f * sc;
                ImVec4 accV = g_colorAccent;
                ImU32 accCol = ImColor(accV.x, accV.y, accV.z, 1.0f);

                // Header background tint
                draw->AddRectFilled(ImVec2(wPos.x, wPos.y), ImVec2(wPos.x + wSize.x, wPos.y + headerH),
                    ImColor(g_colorBgPanel.x + 0.02f, g_colorBgPanel.y + 0.02f, g_colorBgPanel.z + 0.02f, 0.35f));

                // Bottom separator + animated accent sweep
                float sepY = wPos.y + headerH - 1.0f;
                draw->AddLine(ImVec2(wPos.x, sepY), ImVec2(wPos.x + wSize.x, sepY),
                    ImColor(0.55f, 0.55f, 0.60f, 0.25f), 1.0f);
                float sweepT = Animations::EaseInOutQuad(GUI::g_tabAnim);
                if (sweepT > 0.002f && sweepT < 0.998f) {
                    float sweepX = wPos.x + sweepT * wSize.x;
                    float segLen = 110.0f * sc;
                    draw->AddLine(ImVec2(sweepX - segLen * 0.4f, sepY), ImVec2(sweepX + segLen, sepY),
                        ImColor(accV.x, accV.y, accV.z, 0.25f), 1.5f);
                    draw->AddLine(ImVec2(sweepX, sepY), ImVec2(sweepX + segLen * 0.55f, sepY),
                        ImColor(accV.x, accV.y, accV.z, 0.95f), 1.5f);
                }

                // Logo mark (accent diamond)
                float mk = 12.0f * sc;
                ImVec2 mc = ImVec2(wPos.x + 36.0f * sc, wPos.y + headerH * 0.5f);
                ImVec2 d1 = ImVec2(mc.x, mc.y - mk * 1.4f);
                ImVec2 d2 = ImVec2(mc.x + mk * 1.4f, mc.y);
                ImVec2 d3 = ImVec2(mc.x, mc.y + mk * 1.4f);
                ImVec2 d4 = ImVec2(mc.x - mk * 1.4f, mc.y);
                draw->AddQuadFilled(d1, d2, d3, d4, ImColor(accV.x, accV.y, accV.z, 0.95f));
                draw->AddQuad(d1, d2, d3, d4, ImColor(255, 255, 255, 90), 1.5f);

                // Title
                ImGui::SetCursorPos(ImVec2(58.0f * sc, 15.0f * sc));
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                ImVec2 logoPos = ImGui::GetCursorScreenPos();
                AddTextGlow(draw, ImGui::GetFont(), ImGui::GetFontSize(), logoPos, accCol, "AZYRE", 4.0f);
                ImGui::Dummy(ImVec2(ImGui::CalcTextSize("AZYRE").x, ImGui::GetFontSize()));
                ImGui::PopFont();

                // Version tag
                ImGui::SameLine(0, 10.0f * sc);
                ImGui::SetCursorPosY(20.0f * sc);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.52f, 0.9f));
                ImGui::Text("v1.1.0");
                ImGui::PopStyleColor();

                // ACTIVE status pill
                ImGui::SameLine(0, 18.0f * sc);
                ImVec2 badgePos = ImGui::GetCursorScreenPos();
                badgePos.y += 13.0f * sc;
                float pulse = (sinf((float)GetTickCount64() * 0.005f) + 1.0f) * 0.5f;
                ImVec2 badgeSize = ImVec2(82.0f * sc, 22.0f * sc);
                draw->AddRectFilled(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y),
                    ImColor(0.2f, 0.9f, 0.3f, 0.08f), 11.0f * sc);
                draw->AddRect(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y),
                    ImColor(0.2f, 0.9f, 0.3f, 0.35f), 11.0f * sc, 0, 1.0f);
                ImU32 dotColor = ImColor(0.2f, 0.9f, 0.3f, 0.6f + pulse * 0.4f);
                draw->AddCircleFilled(ImVec2(badgePos.x + 12.0f * sc, badgePos.y + badgeSize.y * 0.5f), 3.5f * sc, dotColor);
                if (pulse > 0.1f)
                    draw->AddCircle(ImVec2(badgePos.x + 12.0f * sc, badgePos.y + badgeSize.y * 0.5f), (3.5f + pulse * 4.0f) * sc,
                        ImColor(0.2f, 0.9f, 0.3f, (1.0f - pulse) * 0.5f), 0, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.4f, 1.0f));
                ImGui::SetCursorScreenPos(ImVec2(badgePos.x + 24.0f * sc, badgePos.y + (badgeSize.y - ImGui::GetFontSize()) * 0.5f));
                ImGui::Text("ACTIVE");
                ImGui::PopStyleColor();

                // Right controls: avatar + close
                char* user = getenv("USERNAME");
                float closeSz = 26.0f * sc;
                float avatarSize = 32.0f * sc;
                float rightOff = 18.0f * sc;
                float gap = 14.0f * sc;

                // Close button (custom drawn X)
                ImVec2 closePos = ImVec2(wSize.x - rightOff - closeSz, (headerH - closeSz) * 0.5f);
                ImGui::SetCursorPos(closePos);
                ImGui::PushID("hdr_close");
                ImGui::InvisibleButton("##hdr_close", ImVec2(closeSz, closeSz));
                bool cHov = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked()) {
                    g_showMenu = false;
                    GUI::g_showMenu = false;
                    ClickGUI::g_enabled = false;
                }
                ImVec2 cC = ImVec2(wPos.x + closePos.x + closeSz * 0.5f, wPos.y + closePos.y + closeSz * 0.5f);
                if (cHov) {
                    draw->AddCircleFilled(cC, closeSz * 0.5f, ImColor(1.0f, 0.2f, 0.2f, 0.15f));
                    draw->AddCircle(cC, closeSz * 0.5f, ImColor(1.0f, 0.2f, 0.2f, 0.4f), 0, 1.5f);
                }
                ImU32 xCol = cHov ? ImColor(255, 110, 110, 255) : ImColor(255, 255, 255, 130);
                float xhw = 7.0f * sc;
                draw->AddLine(ImVec2(cC.x - xhw, cC.y - xhw), ImVec2(cC.x + xhw, cC.y + xhw), xCol, 1.8f);
                draw->AddLine(ImVec2(cC.x + xhw, cC.y - xhw), ImVec2(cC.x - xhw, cC.y + xhw), xCol, 1.8f);
                if (cHov) ImGui::SetTooltip("Close Menu");
                ImGui::PopID();

                // Avatar (to the left of the close button)
                ImVec2 avatarPos = ImVec2(closePos.x - gap - avatarSize, (headerH - avatarSize) * 0.5f);
                ImVec2 avatarScreenPos = ImVec2(wPos.x + avatarPos.x, wPos.y + avatarPos.y);
                float radius = avatarSize * 0.5f;
                ImVec2 center = ImVec2(avatarScreenPos.x + radius, avatarScreenPos.y + radius);
                draw->AddCircleFilled(center, radius, ImColor(g_colorAccentSoft.x, g_colorAccentSoft.y, g_colorAccentSoft.z, 0.6f));
                draw->AddCircle(center, radius, ImColor(accV.x, accV.y, accV.z, 0.8f), 0, 1.5f);
                char displayLetter = (user && strlen(user) > 0) ? toupper(user[0]) : 'U';
                char letterStr[2] = { displayLetter, '\0' };
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                ImVec2 letterSize = ImGui::CalcTextSize(letterStr);
                draw->AddText(ImVec2(center.x - letterSize.x * 0.5f, center.y - letterSize.y * 0.5f), ImColor(255, 255, 255, 230), letterStr);
                ImGui::PopFont();
            }
            ImGui::EndChild();
            
            // ── Sidebar Area ──
            ImGui::BeginChild("Sidebar", ImVec2(224.0f * sc, 0), false, ImGuiWindowFlags_NoScrollbar);
            {
                ImDrawList* sDraw = ImGui::GetWindowDrawList();
                ImVec2 sbPos = ImGui::GetWindowPos();
                ImVec4 accV = g_colorAccent;
                float sbW = ImGui::GetWindowWidth();

                // Vertical divider on the right edge
                sDraw->AddLine(ImVec2(sbPos.x + sbW - 1.0f, sbPos.y + 8.0f),
                    ImVec2(sbPos.x + sbW - 1.0f, sbPos.y + ImGui::GetWindowHeight() - 8.0f),
                    ImColor(0.5f, 0.5f, 0.55f, 0.18f), 1.0f);

                // Section label
                ImGui::SetCursorPos(ImVec2(20.0f * sc, 18.0f * sc));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.52f, 0.9f));
                ImGui::Text("NAVIGATION");
                ImGui::PopStyleColor();
                ImGui::Spacing();

                // Sliding selection pill (drawn behind the buttons)
                float pillX = sbPos.x + 8.0f;
                float pillW = sbW - 18.0f;
                float pillH = 54.0f;
                float pillY = sbPos.y + g_sidebarIndicatorY;
                if (pillY > sbPos.y + 4.0f) {
                    ImVec2 pMin = ImVec2(pillX, pillY);
                    ImVec2 pMax = ImVec2(pillX + pillW, pillY + pillH);
                    sDraw->AddRectFilled(ImVec2(pMin.x - 2.0f, pMin.y - 2.0f), ImVec2(pMax.x + 2.0f, pMax.y + 2.0f),
                        ImColor(accV.x, accV.y, accV.z, 0.10f), 11.0f);
                    sDraw->AddRectFilled(pMin, pMax, ImColor(accV.x, accV.y, accV.z, 0.16f), 9.0f);
                    sDraw->AddRect(pMin, pMax, ImColor(accV.x, accV.y, accV.z, 0.40f), 9.0f, 0, 1.0f);
                    sDraw->AddRectFilled(ImVec2(pMin.x + 3.0f, pMin.y + 7.0f), ImVec2(pMin.x + 5.0f, pMax.y - 7.0f),
                        ImColor(accV.x, accV.y, accV.z, 0.95f), 1.5f);
                }

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

                // Navigation buttons
                RenderSidebarButton("Combat", 0);
                RenderSidebarButton("Movement", 1);
                RenderSidebarButton("Visuals", 2);
                RenderSidebarButton("Misc", 3);
                RenderSidebarButton("Terminal", 4);
                RenderSidebarButton("Info", 5);
                RenderSidebarButton("IRC Chat", 6);
                RenderSidebarButton("Config Market", 7);

                // Bottom profile card
                float profileH = 56.0f * sc;
                ImVec2 cardMin = ImVec2(sbPos.x + 8.0f, sbPos.y + ImGui::GetWindowHeight() - profileH - 10.0f);
                ImVec2 cardMax = ImVec2(cardMin.x + pillW, sbPos.y + ImGui::GetWindowHeight() - 8.0f);
                sDraw->AddRectFilled(cardMin, cardMax, ImColor(0.10f, 0.10f, 0.14f, 0.60f), 10.0f);
                sDraw->AddRect(cardMin, cardMax, ImColor(accV.x, accV.y, accV.z, 0.25f), 10.0f, 0, 1.0f);

                char* user = getenv("USERNAME");
                char disp = (user && strlen(user) > 0) ? toupper(user[0]) : 'U';
                char dStr[2] = { disp, '\0' };

                // Mini avatar
                ImVec2 pc = ImVec2(cardMin.x + 22.0f, cardMin.y + profileH * 0.5f);
                sDraw->AddCircleFilled(pc, 13.0f, ImColor(accV.x, accV.y, accV.z, 0.5f));
                sDraw->AddCircle(pc, 13.0f, ImColor(accV.x, accV.y, accV.z, 0.9f), 0, 1.5f);
                ImGui::PushFont(GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetFont());
                ImVec2 ds = ImGui::CalcTextSize(dStr);
                sDraw->AddText(ImVec2(pc.x - ds.x * 0.5f, pc.y - ds.y * 0.5f), ImColor(255, 255, 255, 220), dStr);
                ImGui::PopFont();

                // Username + status
                sDraw->AddText(ImVec2(cardMin.x + 44.0f, cardMin.y + 12.0f), ImColor(215, 215, 225, 220), user ? user : "User");
                sDraw->AddText(ImVec2(cardMin.x + 44.0f, cardMin.y + 33.0f), IM_COL32(102, 229, 128, 200), "Online");
            }
            ImGui::EndChild();
            
            ImGui::SameLine();
            
            // Content Area with Tab Animation
            float tab_e = Animations::EaseOutExpo(GUI::g_tabAnim);
            float slide = (1.0f - tab_e) * 30.0f;
            
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            ImGui::BeginChild("ContentAreaParent", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slide);
                
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tab_e * e);
                
                const char* tabNames[] = { "Combat", "Movement", "Visuals", "Misc", "Terminal", "Info", "IRC Chat", "Config Market" };
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                ImVec2 tabTextPos = ImGui::GetCursorScreenPos();
                RenderTypingText(ImGui::GetWindowDrawList(), ImGui::GetFont(), ImGui::GetFontSize(), tabTextPos, tabNames[GUI::g_currentTab], tab_e, g_colorAccent, 5.0f);
                ImVec2 titleSz = ImGui::CalcTextSize(tabNames[GUI::g_currentTab]);
                ImVec2 titleEnd = ImVec2(tabTextPos.x + titleSz.x, tabTextPos.y + titleSz.y + 10.0f);
                ImGui::Dummy(ImVec2(titleSz.x, ImGui::GetFontSize() + 10.0f));
                ImGui::PopFont();

                // Animated accent underline that grows from the title to the edge
                ImDrawList* cDraw = ImGui::GetWindowDrawList();
                float rightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowWidth();
                cDraw->AddLine(ImVec2(titleEnd.x, titleEnd.y), ImVec2(rightEdge, titleEnd.y),
                    ImColor(0.45f, 0.45f, 0.50f, 0.25f), 1.0f);
                if (tab_e > 0.001f) {
                    float growW = (rightEdge - titleEnd.x) * Animations::EaseOutQuart(tab_e);
                    cDraw->AddLine(ImVec2(titleEnd.x, titleEnd.y), ImVec2(titleEnd.x + growW, titleEnd.y),
                        ImColor(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.75f), 2.0f);
                }
                ImGui::Spacing();

                // Styled scrollbar for the content area
                ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.30f, 0.30f, 0.36f, 0.55f));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.8f));
                ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 3.0f);
                ImGui::BeginChild("ContentScroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                {
                    static char g_regularSearch[64] = "";

                    auto PassesSearch = [&](const char* name) -> bool {
                        if (!g_regularSearch[0]) return true;
                        char sLow[64] = { 0 };
                        char nLow[64] = { 0 };
                        for (int i = 0; g_regularSearch[i] && i < 63; i++) sLow[i] = (char)tolower((unsigned char)g_regularSearch[i]);
                        for (int i = 0; name[i] && i < 63; i++) nLow[i] = (char)tolower((unsigned char)name[i]);
                        return strstr(nLow, sLow) != nullptr;
                    };

                    static int s_selectedModule = -1;

                    // Grid card layout for module tabs (0-3)
                    if (GUI::g_currentTab >= 0 && GUI::g_currentTab <= 3) {
                        struct ModCard { const char* name; const char* icon; bool* enabled; void (*callback)(); void (*settingsFn)(); };

                        auto GetModules = []() -> std::vector<ModCard> {
                            switch (GUI::g_currentTab) {
                                case 0: return {
                                    { "Reach", "\xE2\x9A\x94", &Reach::g_reachEnabled, [](){ if (Reach::g_reachEnabled) Reach::SetEnabled(true); else Reach::SetEnabled(false); }, &Reach::RenderMenu },
                                    { "Hitbox", "\xF0\x9F\x94\xB1", &Hitbox::g_hitboxEnabled, [](){ if (Hitbox::g_hitboxEnabled) Hitbox::Enable(); else Hitbox::Disable(); }, &Hitbox::RenderMenu },
                                    { "Rapid Hit", "\xE2\x9A\xA1", &RapidHit::g_rapidHitEnabled, [](){ if (RapidHit::g_rapidHitEnabled) RapidHit::Enable(); else RapidHit::Disable(); }, &RapidHit::RenderMenu },
                                };
                                case 1: return {
                                    { "Auto Sprint", "\xF0\x9F\x8F\x83", &AutoSprint::g_autoSprintEnabled, [](){ if (AutoSprint::g_autoSprintEnabled) AutoSprint::Enable(); else AutoSprint::Disable(); }, &AutoSprint::RenderMenu },
                                    { "Glide", "\xF0\x9F\xAB\x9F", &Glide::g_glideEnabled, [](){ if (Glide::g_glideEnabled) Glide::Enable(); else Glide::Disable(); }, &Glide::RenderMenu },
                                    { "Fly", "\xE2\x9C\x88", &Fly::g_flyEnabled, [](){ if (Fly::g_flyEnabled) Fly::Enable(); else Fly::Disable(); }, &Fly::RenderMenu },
                                    { "Timer", "\xE2\x8F\xB1", &Timer::g_timerEnabled, [](){ if (Timer::g_timerEnabled) Timer::Enable(); else Timer::Disable(); }, &Timer::RenderMenu },
                                    { "High Jump", "\xF0\x9F\x94\xBC", &HighJump::g_enabled, [](){ if (HighJump::g_enabled) HighJump::Enable(); else HighJump::Disable(); }, &HighJump::RenderMenu },
                                };
                                case 2: return {
                                    { "Watermark", "\xF0\x9F\x92\xAC", &Watermark::g_showWatermark, nullptr, &Watermark::RenderMenu },
                                    { "ArrayList", "\xE2\x9E\x85", &ArrayList::g_enabled, nullptr, &ArrayList::RenderMenu },
                                    { "Render Info", "\xF0\x9F\x96\xA5", &RenderInfo::g_showRenderInfo, nullptr, &RenderInfo::RenderMenu },
                                    { "Keystrokes", "\xF0\x9F\x96\xB1", &Keystrokes::g_showKeystrokes, nullptr, &Keystrokes::RenderMenu },
                                    { "CPS Counter", "\xF0\x9F\x96\xB1", &CPSCounter::g_showCpsCounter, nullptr, &CPSCounter::RenderMenu },
                                    { "FPS Overlay", "\xE2\x9C\x8E", &FPSOverlay::g_showFpsOverlay, [](){ if (FPSOverlay::g_showFpsOverlay) { FPSOverlay::g_fpsOverlayEnableTime = GetTickCount64(); FPSOverlay::g_fpsOverlayDisableTime = 0; } else { FPSOverlay::g_fpsOverlayDisableTime = GetTickCount64(); FPSOverlay::g_fpsOverlayEnableTime = 0; } }, &FPSOverlay::RenderMenu },
                                    { "Ping Counter", "\xF0\x9F\x93\xB1", &PingCounter::g_showPingCounter, nullptr, &PingCounter::RenderMenu },
                                    { "Player Info", "\xF0\x9F\x91\xA4", &PlayerInfo::g_showPlayerInfo, nullptr, &PlayerInfo::RenderMenu },
                                    { "Mouse Strokes", "\xF0\x9F\x96\xB1", &MouseStrokes::g_showMouseStrokes, [](){ if (MouseStrokes::g_showMouseStrokes) { MouseStrokes::g_mouseStrokesEnableTime = GetTickCount64(); MouseStrokes::g_mouseStrokesDisableTime = 0; } else { MouseStrokes::g_mouseStrokesDisableTime = GetTickCount64(); MouseStrokes::g_mouseStrokesEnableTime = 0; } }, &MouseStrokes::RenderMenu },
                                    { "FullBright", "\xE2\x9C\xA8", &FullBright::g_fullBrightEnabled, [](){ if (FullBright::g_fullBrightEnabled) FullBright::Enable(); else FullBright::Disable(); }, &FullBright::RenderMenu },
                                    { "Motion Blur", "\xF0\x9F\xAA\x9C", &MotionBlur::g_motionBlurEnabled, nullptr, &MotionBlur::RenderMenu },
                                    { "Click GUI", "\xE2\x9A\x99", &ClickGUI::g_enabled, [](){ g_showMenu = ClickGUI::g_enabled; GUI::g_showMenu = g_showMenu; }, &ClickGUI::RenderMenu },
                                    { "NoHurtCam", "\xF0\x9F\x94\x84", &NoHurtCam::g_noHurtCamEnabled, [](){ if (NoHurtCam::g_noHurtCamEnabled) NoHurtCam::Enable(); else NoHurtCam::Disable(); }, &NoHurtCam::RenderMenu },
                                };
                                case 3: return {
                                    { "Unlock FPS", "\xE2\x8F\xB1", &UnlockFPS::g_unlockFpsEnabled, nullptr, &UnlockFPS::RenderMenu },
                                    { "Auto Clicker", "\xF0\x9F\x96\xB1", &AutoClicker::g_enabled, nullptr, &AutoClicker::RenderMenu },
                                    { "Anti-AFK", "\xF0\x9F\x8F\x9E", &AntiAFK::g_enabled, nullptr, &AntiAFK::RenderMenu },
                                    { "Screenshot", "\xF0\x9F\x93\xB7", &Screenshot::g_enabled, nullptr, &Screenshot::RenderMenu },
                                };
                                default: return {};
                            }
                        };

                        auto modules = GetModules();
                        ImVec4 accV = g_colorAccent;

                        if (s_selectedModule >= (int)modules.size()) s_selectedModule = -1;

                        // ── SETTINGS VIEW ──
                        if (s_selectedModule >= 0 && s_selectedModule < (int)modules.size()) {
                            ModCard& sel = modules[s_selectedModule];
                            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 8.0f, ImGui::GetCursorPosY() + 4.0f));
                            ImVec2 backPos = ImGui::GetCursorScreenPos();
                            float backSz = 28.0f;
                            ImDrawList* draw = ImGui::GetWindowDrawList();
                            draw->AddRectFilled(backPos, ImVec2(backPos.x + backSz, backPos.y + backSz),
                                ImColor(accV.x, accV.y, accV.z, 0.85f * e), 6.0f);
                            float cx = backPos.x + backSz * 0.5f;
                            float cy = backPos.y + backSz * 0.5f;
                            draw->AddLine(ImVec2(cx + 5.0f, cy - 5.0f), ImVec2(cx - 3.0f, cy), ImColor(1.0f, 1.0f, 1.0f, 0.95f * e), 2.0f);
                            draw->AddLine(ImVec2(cx - 3.0f, cy), ImVec2(cx + 5.0f, cy + 5.0f), ImColor(1.0f, 1.0f, 1.0f, 0.95f * e), 2.0f);

                            ImGui::SetCursorScreenPos(backPos);
                            ImGui::PushID("nx_back");
                            if (ImGui::InvisibleButton("##back", ImVec2(backSz, backSz))) {
                                s_selectedModule = -1;
                            }
                            ImGui::PopID();

                            ImGui::SameLine(0.0f, 12.0f);
                            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                            std::string title = std::string(sel.name) + " SETTINGS";
                            for (auto& c : title) c = (char)toupper((unsigned char)c);
                            ImGui::TextColored(ImVec4(accV.x, accV.y, accV.z, 0.95f * e), "%s", title.c_str());
                            ImGui::PopFont();

                            ImGui::Spacing();
                            ImVec2 sepPos = ImGui::GetCursorScreenPos();
                            draw->AddLine(sepPos, ImVec2(sepPos.x + ImGui::GetContentRegionAvail().x, sepPos.y),
                                ImColor(accV.x, accV.y, accV.z, 0.25f * e), 1.0f);
                            ImGui::Spacing(); ImGui::Spacing();

                            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);
                            if (sel.settingsFn) sel.settingsFn();
                            ImGui::PopStyleVar();

                        } else {
                            // ── GRID VIEW ──
                            float availW = ImGui::GetContentRegionAvail().x;
                            int cols = 3;
                            float cardGap = 10.0f;
                            float cardW = (availW - (float)(cols - 1) * cardGap) / (float)cols;
                            float cardH = 54.0f;
                            float dt = ImGui::GetIO().DeltaTime;
                            ImDrawList* draw = ImGui::GetWindowDrawList();

                            for (int i = 0; i < (int)modules.size(); i++) {
                                ModCard& m = modules[i];
                                if (!PassesSearch(m.name)) continue;
                                int col = i % cols;

                                float delay = (float)i * 0.03f;
                                float raw = (tab_e * e - delay) / (1.0f - delay > 0.01f ? 1.0f - delay : 0.01f);
                                float prog = raw < 0.0f ? 0.0f : (raw > 1.0f ? 1.0f : raw);
                                float alpha = Animations::EaseOutQuart(prog) * e;
                                if (alpha <= 0.001f) continue;

                                if (col == 0 && i > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + cardGap);
                                if (col > 0) ImGui::SameLine(0.0f, cardGap);

                                ImVec2 cardPos = ImGui::GetCursorScreenPos();
                                bool isEnabled = *m.enabled;
                                bool isHov = ImGui::IsMouseHoveringRect(cardPos, ImVec2(cardPos.x + cardW, cardPos.y + cardH));

                                std::string hKey = "cardh_" + std::string(m.name);
                                float& hAnim = g_elementAnims[hKey];
                                hAnim = Animations::Approach(hAnim, isHov ? 1.0f : 0.0f, dt, 14.0f);
                                float h = hAnim;

                                float bgBright = h * 12.0f;
                                ImU32 bgCol = isEnabled
                                    ? IM_COL32((int)(accV.x * 45 + bgBright), (int)(accV.y * 45 + bgBright), (int)(accV.z * 45 + bgBright), (int)((200 + h * 30) * alpha))
                                    : IM_COL32((int)(22 + h * 8), (int)(22 + h * 8), (int)(30 + h * 8), (int)((210 + h * 30) * alpha));
                                draw->AddRectFilled(cardPos, ImVec2(cardPos.x + cardW, cardPos.y + cardH), bgCol, 8.0f);

                                draw->AddRectFilled(
                                    ImVec2(cardPos.x, cardPos.y + 8.0f),
                                    ImVec2(cardPos.x + 3.0f, cardPos.y + cardH - 8.0f),
                                    ImColor(accV.x, accV.y, accV.z, (isEnabled ? 0.85f : 0.15f + h * 0.25f) * alpha), 1.5f);

                                float borderA = h * 0.35f * alpha;
                                if (borderA > 0.001f)
                                    draw->AddRect(cardPos, ImVec2(cardPos.x + cardW, cardPos.y + cardH),
                                        ImColor(accV.x, accV.y, accV.z, borderA), 8.0f, 0, 1.0f + h * 0.5f);

                                // Module icon (texture or fallback)
                                void* modTex = GUI::GetModuleTexture(m.name);
                                if (modTex) {
                                    float iconSz = 22.0f;
                                    ImVec2 iconPos(cardPos.x + 10.0f, cardPos.y + (cardH - iconSz) * 0.5f);
                                    draw->AddImage((ImTextureID)modTex, iconPos, ImVec2(iconPos.x + iconSz, iconPos.y + iconSz));
                                } else {
                                    ImGui::PushFont(GUI::g_fontH2 ? GUI::g_fontH2 : ImGui::GetFont());
                                    ImVec2 iconSz = ImGui::CalcTextSize(m.icon);
                                    draw->AddText(ImVec2(cardPos.x + 14.0f, cardPos.y + (cardH - iconSz.y) * 0.5f),
                                        ImColor(1.0f, 1.0f, 1.0f, (0.90f + h * 0.10f) * alpha), m.icon);
                                    ImGui::PopFont();
                                }

                                ImGui::PushFont(GUI::g_fontDefault);
                                ImVec2 nameSz = ImGui::CalcTextSize(m.name);
                                draw->AddText(ImVec2(cardPos.x + 42.0f, cardPos.y + (cardH - nameSz.y) * 0.5f),
                                    ImColor(0.92f + h * 0.08f, 0.92f + h * 0.08f, 0.95f, 0.95f * alpha), m.name);
                                ImGui::PopFont();

                                float swW = 36.0f, swH = 18.0f;
                                float swX = cardPos.x + cardW - swW - 12.0f;
                                float swY = cardPos.y + (cardH - swH) * 0.5f;
                                float swR = swH * 0.5f;
                                ImU32 trackCol = isEnabled
                                    ? IM_COL32((int)(accV.x * 255), (int)(accV.y * 255), (int)(accV.z * 255), (int)((230 + h * 25) * alpha))
                                    : IM_COL32((int)(50 + h * 15), (int)(50 + h * 15), (int)(60 + h * 15), (int)((180 + h * 40) * alpha));
                                draw->AddRectFilled(ImVec2(swX, swY), ImVec2(swX + swW, swY + swH), trackCol, swR);
                                float knobOff = isEnabled ? (swW - swR - 2.0f) : 2.0f;
                                draw->AddCircleFilled(ImVec2(swX + knobOff + swR - 2.0f, swY + swH * 0.5f),
                                    swR - 3.0f, IM_COL32(255, 255, 255, (int)(255 * alpha)));

                                float gearX = swX - 22.0f;
                                float gearY = cardPos.y + (cardH - 16.0f) * 0.5f;
                                bool gearHov = ImGui::IsMouseHoveringRect(ImVec2(gearX - 2.0f, gearY - 2.0f), ImVec2(gearX + 18.0f, gearY + 18.0f));
                                std::string gKey = "gearh_" + std::string(m.name);
                                float& gAnim = g_elementAnims[gKey];
                                gAnim = Animations::Approach(gAnim, gearHov ? 1.0f : 0.0f, dt, 16.0f);
                                float g = gAnim;

                                if (GUI::g_configIconTexture) {
                                    float iconScale = 1.0f + g * 0.12f;
                                    float iconSize = 16.0f * iconScale;
                                    float iconOff = (iconSize - 16.0f) * 0.5f;
                                    draw->AddImage((ImTextureID)GUI::g_configIconTexture,
                                        ImVec2(gearX - iconOff, gearY - iconOff),
                                        ImVec2(gearX + iconSize - iconOff, gearY + iconSize - iconOff),
                                        ImVec2(0, 0), ImVec2(1, 1),
                                        ImColor(1.0f, 1.0f, 1.0f, (0.50f + g * 0.50f) * alpha));
                                    if (g > 0.01f)
                                        draw->AddCircleFilled(ImVec2(gearX + 8.0f, gearY + 8.0f), 12.0f,
                                            ImColor(accV.x, accV.y, accV.z, 0.12f * g * alpha));
                                }

                                // Single card button — decide action based on mouse position
                                ImGui::SetCursorScreenPos(cardPos);
                                ImGui::PushID(("grid_" + std::string(m.name)).c_str());
                                if (ImGui::InvisibleButton("##card", ImVec2(cardW, cardH))) {
                                    ImVec2 mp = ImGui::GetIO().MousePos;
                                    float gearClickZoneX = gearX - 4.0f;
                                    if (mp.x >= gearClickZoneX) {
                                        // Click on gear icon area → open settings
                                        s_selectedModule = i;
                                    } else {
                                        // Click elsewhere → toggle module
                                        *m.enabled = !*m.enabled;
                                        if (m.callback) m.callback();
                                    }
                                }
                                ImGui::PopID();
                            }
                        }

                    } else {
                        // Non-module tabs
                        switch (GUI::g_currentTab) {
                            case 4: Terminal::RenderConsole(); break;
                            case 5: Info::RenderMenu(); break;
                            case 6: IRChat::RenderMenu(); break;
                            case 7: GUI::RenderConfigMarket(); break;
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(4);
                
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            
        }

        // ImGui requires a matching End() for every Begin() even when Begin()
        // returns false (collapsed/clipped window), otherwise "Missing End()".
        ImGui::End();

        ImGui::PopStyleVar(3); // Regular-mode layout overrides (ItemSpacing / ItemInnerSpacing / FramePadding)
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

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
            }

            // Always pair Begin() with End(), even when Begin() returned false
            ImGui::End();
            
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
    // Allocate the full mip chain so small UI icons are sampled from a
    // prefiltered version rather than minified directly from the source.
    desc.MipLevels = 0;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    
    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = pDevice->CreateTexture2D(&desc, nullptr, &pTexture);
    
    if (FAILED(hr) || !pTexture) {
        stbi_image_free(img_data);
        return nullptr;
    }

    pContext->UpdateSubresource(pTexture, 0, nullptr, img_data, width * 4, 0);
    stbi_image_free(img_data);
    
    // Create shader resource view
    ID3D11ShaderResourceView* pSRV = nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = UINT(-1);
    hr = pDevice->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
    pTexture->Release();
    
    if (FAILED(hr) || !pSRV) {
        return nullptr;
    }
    
    pContext->GenerateMips(pSRV);
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
    g_configIconTexture = LoadTextureFromResource(IDR_CONFIG_ICON);

    // Module icons
    g_moduleTextures["Reach"]       = LoadTextureFromResource(IDR_MOD_REACH);
    g_moduleTextures["Hitbox"]      = LoadTextureFromResource(IDR_MOD_HITBOX);
    g_moduleTextures["Rapid Hit"]   = LoadTextureFromResource(IDR_MOD_RAPIDHIT);
    g_moduleTextures["Auto Sprint"] = LoadTextureFromResource(IDR_MOD_AUTOSPRINT);
    g_moduleTextures["Fly"]         = LoadTextureFromResource(IDR_MOD_FLY);
    g_moduleTextures["Glide"]       = LoadTextureFromResource(IDR_MOD_GLIDE);
    g_moduleTextures["Timer"]       = LoadTextureFromResource(IDR_MOD_TIMER);
    g_moduleTextures["High Jump"]   = LoadTextureFromResource(IDR_MOD_HIGHJUMP);
    g_moduleTextures["Watermark"]   = LoadTextureFromResource(IDR_MOD_WATERMARK);
    g_moduleTextures["ArrayList"]   = LoadTextureFromResource(IDR_MOD_ARRAYLIST);
    g_moduleTextures["Render Info"] = LoadTextureFromResource(IDR_MOD_RENDERINFO);
    g_moduleTextures["Keystrokes"]  = LoadTextureFromResource(IDR_MOD_KEYSTROKES);
    g_moduleTextures["CPS Counter"] = LoadTextureFromResource(IDR_MOD_CPSCOUNTER);
    g_moduleTextures["FPS Overlay"] = LoadTextureFromResource(IDR_MOD_FPSOVERLAY);
    g_moduleTextures["Ping Counter"]= LoadTextureFromResource(IDR_MOD_PINGCOUNTER);
    g_moduleTextures["Player Info"] = LoadTextureFromResource(IDR_MOD_PLAYERINFO);
    g_moduleTextures["Mouse Strokes"]= LoadTextureFromResource(IDR_MOD_MOUSESTROKES);
    g_moduleTextures["FullBright"]  = LoadTextureFromResource(IDR_MOD_FULLBRIGHT);
    g_moduleTextures["Motion Blur"] = LoadTextureFromResource(IDR_MOD_MOTIONBLUR);
    g_moduleTextures["NoHurtCam"]   = LoadTextureFromResource(IDR_MOD_NOHURTCAM);
    g_moduleTextures["Click GUI"]   = LoadTextureFromResource(IDR_MOD_CLICKGUI);
    g_moduleTextures["Unlock FPS"]  = LoadTextureFromResource(IDR_MOD_UNLOCKFPS);
    g_moduleTextures["Auto Clicker"]= LoadTextureFromResource(IDR_MOD_AUTOCLICKER);
    g_moduleTextures["Anti-AFK"]    = LoadTextureFromResource(IDR_MOD_ANTIAFK);
    g_moduleTextures["Screenshot"]  = LoadTextureFromResource(IDR_MOD_SCREENSHOT);
    
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
    for (auto& kv : g_moduleTextures) {
        if (kv.second != nullptr) {
            ((ID3D11ShaderResourceView*)kv.second)->Release();
            kv.second = nullptr;
        }
    }
    g_moduleTextures.clear();
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
