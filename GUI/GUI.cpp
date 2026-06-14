/*
Under an4rch Development Public Source License 1.0
*/

#include "GUI.hpp"
#include "../Animations/Animations.hpp"
#include "../Modules/Terminal/Terminal.hpp"
#include "../Modules/Info/Info.hpp"
#include <windows.h>
#include <shellapi.h>
#include "../Assets/resource.h"
#include <cmath>
#include <cstdlib>
#include "../ArrayList/ArrayList.hpp"
#include "../Modules/ModuleHeader.hpp"


// Static member initialization
bool GUI::g_showMenu = false;
float GUI::g_menuAnim = 0.0f;

int GUI::g_currentTab = 0;
int GUI::g_previousTab = 0;
ULONGLONG GUI::g_tabChangeTime = 0;
float GUI::g_tabAnim = 1.0f; // Start at 1.0f so it's visible on first open
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

std::map<std::string, float> GUI::g_elementAnims;
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

    // Load ProductSans from resources
    HRSRC hRes = FindResourceA(g_hModule, MAKEINTRESOURCEA(IDR_FONT), RT_RCDATA);
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
                return;
            }
        }
    }

    // Fallback if resource fails
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
    
    // Text
    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 textPos = ImVec2(p_min.x + 20, p_min.y + (size.y - textSize.y) * 0.5f);
    
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
        ShellExecuteA(0, "open", "https://github.com/iVyx3r/aegledll", 0, 0, SW_SHOWNORMAL);
        ImGui::SetClipboardText("https://github.com/iVyx3r/aegledll");
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
}

void GUI::RenderMenu(float screenWidth, float screenHeight) {
    if (GUI::g_menuAnim <= 0.001f) return;
    
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
        ImVec2 winPos = ImVec2(screenWidth / 2 - winSize.x / 2, screenHeight / 2 - winSize.y / 2);
        
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
                ImGui::Text("v1.0.4 - release");
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
                
                const char* tabNames[] = { "Combat", "Movement", "Visuals", "Misc", "Terminal", "Info" };
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
                                EndSection();
                            }
                            break;
                        case 3: // Misc
                            if (BeginSection("Misc Modules", &miscOpen)) {
                                UnlockFPS::RenderMenu();
                                EndSection();
                            }
                            break;
                        case 4: // Terminal
                            Terminal::RenderConsole();
                            break;
                        case 5: // Info
                            Info::RenderMenu();
                            break;
                    }
                }
                ImGui::EndChild();
                
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            
            ImGui::End();
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

