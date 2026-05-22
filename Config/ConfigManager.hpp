/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>

class ConfigManager {
public:
    // Configuration management
    static bool SaveConfig(const std::string& name);
    static bool LoadConfig(const std::string& name);
    static bool DeleteConfig(const std::string& name);
    static std::vector<std::string> ListConfigs();
    static bool OpenConfigDirectory();

    // Initialize config directory
    static void Initialize();

    // Get config directory
    static const std::string& GetConfigDir() { return configDir; }

    // Reload modules after config load (re-enable hooks, reinitialize, etc)
    static void ReloadModulesAfterConfig();

private:
    static std::string configDir;

    // Helper functions
    static nlohmann::json CollectCurrentConfig();
    static void ApplyConfig(const nlohmann::json& config);
};
