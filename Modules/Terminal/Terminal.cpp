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

// Static member initialization
std::vector<std::string> Terminal::outputLines;
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
    AddOutput("\x1B[35m[Aegleseeker]\x1B[0m Terminal initialized. Type \x1B[33m.help\x1B[0m for commands.");
    AddOutput("\x1B[90mAll configs saved in LocalState\\Aegle\\ directory.\x1B[0m");
}

void Terminal::RenderConsole() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.08f, 1.0f));
    ImGui::BeginChild("TerminalOutput", ImVec2(0, 300), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    
    for (const auto& line : outputLines) {
        RenderColoredText(line);
    }
    
    if (scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        scrollToBottom = false;
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::Spacing();
    
    // Modern Input Area
    ImGui::TextColored(ImVec4(0.6f, 0.5f, 1.0f, 1.0f), ">");
    ImGui::SameLine();
    
    ImGui::PushItemWidth(-1);
    auto callback = [](ImGuiInputTextCallbackData* data) -> int {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
            if (commandHistory.empty()) return 0;
            if (data->EventKey == ImGuiKey_UpArrow) {
                if (historyIndex == -1) historyIndex = commandHistory.size() - 1;
                else if (historyIndex > 0) historyIndex--;
            } else if (data->EventKey == ImGuiKey_DownArrow) {
                if (historyIndex != -1 && historyIndex < commandHistory.size() - 1) historyIndex++;
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
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Tip: Use Up/Down arrows for history");
    }

    // Render unload confirmation dialog if active
    RenderUnloadDialog();
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
        } else {
            AddOutput("\x1B[31mFailed to open config directory.\x1B[0m");
        }
    } else if (command == ".deattach") {
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
    outputLines.push_back(text);
    if (outputLines.size() > 1000) { // Limit output lines
        outputLines.erase(outputLines.begin());
    }
}

void Terminal::Detach() {
    // Show confirmation dialog
    g_showUnloadDialog = true;
    ImGui::OpenPopup("Confirm Unload##UnloadDialog");
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
    AddOutput("  \x1B[33m.deattach\x1B[0m                - Detach DLL safely");
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

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 180), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Confirm Unload##UnloadDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextWrapped("Are you sure you want to unload the DLL?");
        ImGui::TextWrapped("This action will disable Aegleseeker.");
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Buttons
        float buttonWidth = 120.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float totalWidth = (buttonWidth * 2) + spacing;
        float availWidth = ImGui::GetContentRegionAvail().x;
        float offsetX = (availWidth - totalWidth) / 2.0f;

        if (offsetX > 0) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        }

        if (ImGui::Button("Yes, Unload##UnloadYes", ImVec2(buttonWidth, 0))) {
            g_showUnloadDialog = false;
            ImGui::CloseCurrentPopup();
            Terminal::PerformUnload();
        }

        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();

        if (ImGui::Button("Cancel##UnloadNo", ImVec2(buttonWidth, 0))) {
            g_showUnloadDialog = false;
            ImGui::CloseCurrentPopup();
            AddOutput("Unload cancelled.");
        }

        ImGui::EndPopup();
    }
}
