/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>

class Terminal {
public:
    static void Initialize();
    static void RenderConsole();

    // Command handling
    static void ExecuteCommand(const std::string& command);
    static void AddOutput(const std::string& text);

    // Config management
    static bool SaveConfig(const std::string& name);
    static bool LoadConfig(const std::string& name);
    static bool DeleteConfig(const std::string& name);
    static std::vector<std::string> ListConfigs();
    static bool OpenConfigDirectory();

    // Detach functionality
    static void Detach();

private:
    struct LineEntry {
        std::string text;
        double time;
    };
    static std::vector<LineEntry> outputLines;
    static char inputBuffer[256];
    static bool scrollToBottom;
    static std::string configDir;

    // Command History
    static std::vector<std::string> commandHistory;
    static int historyIndex;

    // Helper functions
    static void ShowHelp();
    static void RenderColoredText(const std::string& text);
    static nlohmann::json CollectCurrentConfig();
    static void ApplyConfig(const nlohmann::json& config);
    static void RenderUnloadDialog();
    static void PerformUnload();
};