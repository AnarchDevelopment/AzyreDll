/*
Under an4rch Development Public Source License 1.0
*/

#include "Terminal.hpp"
#include "Modules/ModuleHeader.hpp"
#include "Modules/Globals.hpp"
#include "Config/ConfigManager.hpp"
#include "Hook/Hook.hpp"
#include "ImGui/imgui.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <windows.h>
#include <shlobj.h>
#define INITGUID
#include <knownfolders.h>
#undef INITGUID
#ifndef KF_FLAG_NO_PACKAGE_REDIRECTION
#define KF_FLAG_NO_PACKAGE_REDIRECTION 0x00002000
#endif
#include <shellapi.h>
#include <filesystem>
#include <cstdlib>
#include <ctime>
#include "minhook/MinHook.h"
#include "ImGui/backend/imgui_impl_dx11.h"
#include "ImGui/backend/imgui_impl_win32.h"
#include "miniaudio/miniaudio.h"
#include <cmath>

// Static member initialization
std::vector<Terminal::LineEntry> Terminal::outputLines;
char Terminal::inputBuffer[256] = {0};
bool Terminal::scrollToBottom = false;
std::vector<std::string> Terminal::commandHistory;
int Terminal::historyIndex = -1;

// Unload confirmation dialog state
static bool g_showUnloadDialog = false;

bool Terminal::SaveConfig(const std::string& name) {
    return ConfigManager::SaveConfig(name);
}

bool Terminal::LoadConfig(const std::string& name) {
    return ConfigManager::LoadConfig(name);
}

bool Terminal::DeleteConfig(const std::string& name) {
    return ConfigManager::DeleteConfig(name);
}

std::vector<std::string> Terminal::ListConfigs() {
    return ConfigManager::ListConfigs();
}

bool Terminal::OpenConfigDirectory() {
    return ConfigManager::OpenConfigDirectory();
}

void Terminal::Initialize() {
    ConfigManager::Initialize();
    AddOutput("\x1B[35m[Azyre]\x1B[0m Terminal initialized. Type \x1B[33m.help\x1B[0m for commands.");
    AddOutput("\x1B[90mAll configs saved in LocalState\\Aegle\\ directory.\x1B[0m");
}

void Terminal::RenderConsole() {
    ImGui::PushFont(GUI::g_fontMono ? GUI::g_fontMono : GUI::g_fontDefault);

    const ImVec4 accent = GUI::g_colorAccent;
    const ImVec4 frameBg = ImVec4(0.035f, 0.035f, 0.05f, 1.0f);
    const ImVec4 panelBg = ImVec4(0.075f, 0.075f, 0.105f, 0.98f);
    const ImVec4 borderCol = ImVec4(1.0f, 1.0f, 1.0f, 0.07f);
    const float rounding = 8.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.y;

    const float headerH = 32.0f;
    const float inputH = 46.0f;
    const float footerH = 20.0f;
    float outH = ImGui::GetContentRegionAvail().y - headerH - inputH - footerH - spacing * 3.0f;
    if (outH < 140.0f) outH = 140.0f;

    // ---------- Header bar (terminal titlebar) ----------
    ImGui::PushStyleColor(ImGuiCol_ChildBg, panelBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("##TermHeader", ImVec2(0, headerH), false, ImGuiWindowFlags_NoScrollbar);
    {
        ImVec2 p0 = ImGui::GetWindowPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float cy = p0.y + headerH * 0.5f;
        dl->AddCircleFilled(ImVec2(p0.x + 18.0f, cy), 4.5f, IM_COL32(255, 92, 92, 210));
        dl->AddCircleFilled(ImVec2(p0.x + 31.0f, cy), 4.5f, IM_COL32(255, 185, 60, 210));
        dl->AddCircleFilled(ImVec2(p0.x + 44.0f, cy), 4.5f, IM_COL32(80, 210, 120, 210));

        ImGui::SetCursorPos(ImVec2(60.0f, 8.0f));
        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.65f, 1.0f), "AZYRE");
        ImGui::SameLine(0, 0);
        ImGui::TextColored(accent, "TERMINAL");
        ImGui::SameLine(0, 10);
        ImGui::TextColored(ImVec4(0.35f, 0.35f, 0.45f, 1.0f), "~");

        ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
        ImGui::TextColored(ImVec4(0.40f, 0.40f, 0.52f, 1.0f), "%d lines", (int)outputLines.size());
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // ---------- Output area ----------
    ImGui::PushStyleColor(ImGuiCol_ChildBg, frameBg);
    ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::BeginChild("TerminalOutput", ImVec2(0, outH), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0.0f, 0.0f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.86f, 0.92f, 1.0f));

        bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;

        for (size_t i = 0; i < outputLines.size(); i++) {
            const auto& line = outputLines[i];
            float t = (float)(ImGui::GetTime() - line.time);
            float alpha = (t >= 0.30f) ? 1.0f : (t < 0.0f ? 0.0f : t / 0.30f);
            alpha = 1.0f - powf(1.0f - alpha, 3.0f);

            bool isLast = (i == outputLines.size() - 1);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
            if (isLast) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            RenderColoredText(line.text);
            if (isLast) ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        if (scrollToBottom || wasAtBottom) {
            ImGui::SetScrollHereY(1.0f);
        }
        scrollToBottom = false;

        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // ---------- Input row ----------
    ImGui::PushStyleColor(ImGuiCol_ChildBg, panelBg);
    ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::BeginChild("##TermInput", ImVec2(0, inputH), true, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::SetCursorPos(ImVec2(14.0f, 12.0f));

        bool blink = ((int)(ImGui::GetTime() * 2.4f) % 2 == 0);
        ImGui::TextColored(blink ? accent : ImVec4(accent.x, accent.y, accent.z, 0.12f), ">");
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.36f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.42f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
        ImGui::PushItemWidth(-14.0f);

        auto callback = [](ImGuiInputTextCallbackData* data) -> int {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
                if (commandHistory.empty()) return 0;
                if (data->EventKey == ImGuiKey_UpArrow) {
                    if (historyIndex == -1) historyIndex = (int)commandHistory.size() - 1;
                    else if (historyIndex > 0) historyIndex--;
                } else if (data->EventKey == ImGuiKey_DownArrow) {
                    if (historyIndex != -1 && historyIndex < (int)commandHistory.size() - 1) historyIndex++;
                    else historyIndex = -1;
                }

                if (historyIndex != -1) {
                    std::string cmd = commandHistory[historyIndex];
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, cmd.c_str());
                } else {
                    data->DeleteChars(0, data->BufTextLen);
                }
            }
            return 0;
        };

        if (ImGui::InputText("##TerminalInput", inputBuffer, sizeof(inputBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, callback)) {
            if (strlen(inputBuffer) > 0) {
                std::string cmd(inputBuffer);
                ExecuteCommand(cmd);
                commandHistory.push_back(cmd);
                historyIndex = -1;
                inputBuffer[0] = '\0';
                scrollToBottom = true;
                ImGui::SetKeyboardFocusHere(-1); // Keep focus
            }
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // ---------- Footer hints ----------
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.50f, 1.0f), "o");
    ImGui::SameLine(0, 5);
    ImGui::TextColored(ImVec4(0.40f, 0.40f, 0.52f, 1.0f), "ready  |  .help  |  up/down: history  |  enter: run");

    // Render unload confirmation dialog if active
    RenderUnloadDialog();

    ImGui::PopFont();
}

void Terminal::ExecuteCommand(const std::string& command) {
    AddOutput("\x1B[90m> " + command + "\x1B[0m");
    
    if (command == ".help") {
        ShowHelp();
    } else if (command.substr(0, 13) == ".config save ") {
        std::string name = command.substr(13);
        if (SaveConfig(name)) {
            AddOutput("\x1B[32mConfig saved:\x1B[0m " + name);
        } else {
            AddOutput("\x1B[31mFailed to save config:\x1B[0m " + name);
        }
    } else if (command.substr(0, 13) == ".config load ") {
        std::string name = command.substr(13);
        if (LoadConfig(name)) {
            AddOutput("\x1B[32mConfig loaded:\x1B[0m " + name);
        } else {
            AddOutput("\x1B[31mFailed to load config:\x1B[0m " + name);
        }
    } else if (command.substr(0, 15) == ".config delete ") {
        std::string name = command.substr(15);
        if (DeleteConfig(name)) {
            AddOutput("\x1B[33mConfig deleted:\x1B[0m " + name);
        } else {
            AddOutput("\x1B[31mFailed to delete config:\x1B[0m " + name);
        }
    } else if (command == ".config list") {
        auto configs = ListConfigs();
        if (configs.empty()) {
            AddOutput("\x1B[33mNo configs found.\x1B[0m");
        } else {
            AddOutput("\x1B[36mAvailable configs:\x1B[0m");
            for (const auto& config : configs) {
                AddOutput("  \x1B[90m-\x1B[0m " + config);
            }
        }
    } else if (command == ".config opendirectory") {
        if (OpenConfigDirectory()) {
            AddOutput("\x1B[32mOpened config directory.\x1B[0m");
            // open minecraft folder
            std::string minecraftPath = "C:\\Users\\" + std::string(getenv("USERNAME")) + "\\AppData\\Local\\Packages\\Microsoft.MinecraftUWP_8wekyb3d8bbwe";
            ShellExecuteA(NULL, "open", minecraftPath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
        }
         else {
            AddOutput("\x1B[31mFailed to open config directory.\x1B[0m");
        }
    } else if (command == ".detach") {
        AddOutput("\x1B[35mDetaching DLL...\x1B[0m");
        Detach();
    } else if (command == ".clear") {
        outputLines.clear();
        AddOutput("\x1B[36mConsole cleared.\x1B[0m");
    } else {
        AddOutput("\x1B[31mUnknown command.\x1B[0m Type \x1B[33m.help\x1B[0m for available commands.");
    }
}

void Terminal::AddOutput(const std::string& text) {
    outputLines.push_back({ text, ImGui::GetTime() });
    if (outputLines.size() > 1000) { // Limit output lines
        outputLines.erase(outputLines.begin());
    }
}

void Terminal::Detach() {
    // Show confirmation dialog
    g_showUnloadDialog = true;
}

void Terminal::PerformUnload() {
    AddOutput("Unloading DLL...");
    
    // Signal to render thread to do cleanup
    extern bool g_RequestUnload;
    g_RequestUnload = true;
}

void Terminal::ShowHelp() {
    AddOutput("\x1B[36mAvailable commands:\x1B[0m");
    AddOutput("  \x1B[33m.help\x1B[0m                    - Show this help");
    AddOutput("  \x1B[33m.config save <name>\x1B[0m      - Save current config");
    AddOutput("  \x1B[33m.config load <name>\x1B[0m      - Load config");
    AddOutput("  \x1B[33m.config delete <name>\x1B[0m    - Delete config");
    AddOutput("  \x1B[33m.config list\x1B[0m             - List available configs");
    AddOutput("  \x1B[33m.config opendirectory\x1B[0m    - Open the config directory");
    AddOutput("  \x1B[33m.clear\x1B[0m                   - Clear terminal");
    AddOutput("  \x1B[33m.detach\x1B[0m                   - Detach DLL safely");
}

void Terminal::RenderColoredText(const std::string& text) {
    ImVec4 color = ImGui::GetStyle().Colors[ImGuiCol_Text];
    size_t lastPos = 0;
    size_t pos = text.find("\x1B[", 0);

    while (pos != std::string::npos) {
        if (pos > lastPos) {
            ImGui::TextColored(color, "%s", text.substr(lastPos, pos - lastPos).c_str());
            ImGui::SameLine(0, 0);
        }

        size_t endPos = text.find("m", pos);
        if (endPos != std::string::npos) {
            std::string code = text.substr(pos + 2, endPos - (pos + 2));
            int colorCode = atoi(code.c_str());

            switch (colorCode) {
                case 0:  color = ImGui::GetStyle().Colors[ImGuiCol_Text]; break; // Reset
                case 31: color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break; // Red
                case 32: color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); break; // Green
                case 33: color = ImVec4(1.0f, 1.0f, 0.4f, 1.0f); break; // Yellow
                case 34: color = ImVec4(0.4f, 0.6f, 1.0f, 1.0f); break; // Blue
                case 35: color = ImVec4(1.0f, 0.5f, 1.0f, 1.0f); break; // Magenta (Aegle)
                case 36: color = ImVec4(0.4f, 1.0f, 1.0f, 1.0f); break; // Cyan
                case 37: color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break; // White
                case 90: color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break; // Gray
            }
            lastPos = endPos + 1;
        } else {
            lastPos = pos + 2;
        }
        pos = text.find("\x1B[", lastPos);
    }

    if (lastPos < text.length()) {
        ImGui::TextColored(color, "%s", text.substr(lastPos).c_str());
    } else {
        ImGui::NewLine(); // Finish the line if last chunk was colored
    }
}


void Terminal::RenderUnloadDialog() {
    if (!g_showUnloadDialog) {
        return;
    }

    // Open the popup here, at the same ID-stack level as BeginPopupModal().
    // Opening it from the InputText child scope (Detach) hashed a different
    // popup ID, so the modal never opened.
    ImGui::OpenPopup("Confirm Unload##UnloadDialog");

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(400.0f, 0.0f), ImVec2(400.0f, FLT_MAX));

    // Use the interface font for a cleaner look (the console pushes the mono font)
    ImGui::PushFont(GUI::g_fontDefault);

    const ImVec4 accent    = GUI::g_colorAccent;
    const ImVec4 bgPanel   = ImVec4(GUI::g_colorBgPanel.x, GUI::g_colorBgPanel.y, GUI::g_colorBgPanel.z, 0.96f);
    const ImVec4 danger    = ImVec4(0.85f, 0.24f, 0.24f, 1.0f);
    const ImVec4 dangerHov = ImVec4(0.95f, 0.30f, 0.30f, 1.0f);
    const ImVec4 dangerAct = ImVec4(1.00f, 0.38f, 0.38f, 1.0f);
    const ImVec4 amber     = ImVec4(1.00f, 0.72f, 0.30f, 1.0f);
    const ImVec4 titleText = ImVec4(0.95f, 0.95f, 1.00f, 1.0f);
    const ImVec4 bodyText  = ImVec4(0.62f, 0.62f, 0.72f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, bgPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(accent.x, accent.y, accent.z, 0.40f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);

    if (ImGui::BeginPopupModal("Confirm Unload##UnloadDialog", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Accent glow bar along the top edge of the panel
        const ImVec2 wPos = ImGui::GetWindowPos();
        const float wW = ImGui::GetWindowWidth();
        dl->AddRectFilledMultiColor(
            ImVec2(wPos.x + 12.0f, wPos.y + 1.0f),
            ImVec2(wPos.x + wW - 12.0f, wPos.y + 3.0f),
            IM_COL32(0, 0, 0, 0),
            ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.10f)),
            ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.85f)),
            IM_COL32(0, 0, 0, 0));

        // ---------- Header: warning badge + title ----------
        const float badgeR = 17.0f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(p.x + badgeR, p.y + badgeR), badgeR, ImColor(danger), 40);
        dl->AddCircle(ImVec2(p.x + badgeR, p.y + badgeR), badgeR, ImColor(1.0f, 1.0f, 1.0f, 0.25f), 40, 1.0f);

        ImGui::SetCursorScreenPos(ImVec2(p.x + badgeR - 7.0f, p.y + badgeR - 10.0f));
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "!");
        ImGui::SameLine(0, 14.0f);
        ImGui::SetCursorScreenPos(ImVec2(p.x + badgeR * 2.0f + 14.0f, p.y + badgeR - ImGui::GetTextLineHeight() * 0.5f));
        ImGui::TextColored(accent, "CONFIRM UNLOAD");

        ImGui::Spacing();
        ImGui::Spacing();

        // ---------- Body ----------
        ImGui::PushTextWrapPos(352.0f);
        ImGui::TextColored(titleText, "Are you sure you want to unload Azyre?");
        ImGui::Spacing();
        ImGui::TextColored(bodyText, "This will disable every module, hotkey and override while the DLL is unloaded from the game.");
        ImGui::Spacing();
        ImGui::TextColored(amber, "Configurations are saved and will not be lost.");
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---------- Actions ----------
        const float btnW = 132.0f;
        const float btnH = 34.0f;
        const float btnGap = 10.0f;
        const float availW = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - btnW * 2.0f - btnGap) * 0.5f);

        // Cancel (default focus, safe)
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.13f, 0.13f, 0.17f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.21f, 0.21f, 0.27f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.28f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.92f, 1.0f));
        if (GUI::RenderButton("Cancel##UnloadNo", ImVec2(btnW, btnH))) {
            g_showUnloadDialog = false;
            ImGui::CloseCurrentPopup();
            AddOutput("Unload cancelled.");
        }
        ImGui::SetItemDefaultFocus();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();

        ImGui::SameLine(0, btnGap);

        // Yes, Unload (danger)
        ImGui::PushStyleColor(ImGuiCol_Button,        danger);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, dangerHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  dangerAct);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (GUI::RenderButton("Yes, Unload##UnloadYes", ImVec2(btnW, btnH))) {
            g_showUnloadDialog = false;
            ImGui::CloseCurrentPopup();
            Terminal::PerformUnload();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();

        // Escape also closes the modal via ImGui; keep the flag in sync so the
        // per-frame OpenPopup() above does not immediately reopen it.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            g_showUnloadDialog = false;
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}
