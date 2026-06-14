/*
Under an4rch Development Public Source License 1.0
*/

#include "ConfigManager.hpp"
#include "Modules/ModuleHeader.hpp"
#include "Modules/Terminal/Terminal.hpp"
#include "ArrayList/ArrayList.hpp"
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
#include <ctime>

std::string ConfigManager::configDir;

static bool EnsureDirectoryExists(const std::string& directoryPath) {
    try {
        std::filesystem::path path(directoryPath);
        if (std::filesystem::exists(path)) {
            return std::filesystem::is_directory(path);
        }
        return std::filesystem::create_directories(path);
    } catch (...) {
        return false;
    }
}

void ConfigManager::Initialize() {
    try {
        std::filesystem::path baseDir;
        PWSTR localAppDataPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppDataPath)) && localAppDataPath != nullptr) {
            std::wstring wpath(localAppDataPath);
            CoTaskMemFree(localAppDataPath);
            baseDir = std::filesystem::path(wpath) /
                "Packages" /
                "Microsoft.MinecraftUWP_8wekyb3d8bbwe" /
                "LocalState" /
                "Aegle";
        } else {
            char* localAppData = std::getenv("LOCALAPPDATA");
            if (localAppData && strlen(localAppData) > 0) {
                baseDir = std::filesystem::path(localAppData) /
                    "Packages" /
                    "Microsoft.MinecraftUWP_8wekyb3d8bbwe" /
                    "LocalState" /
                    "Aegle";
            } else {
                baseDir = std::filesystem::current_path() / "Aegle";
            }
        }

        if (!EnsureDirectoryExists(baseDir.string())) {
            baseDir = std::filesystem::current_path() / "Aegle";
            EnsureDirectoryExists(baseDir.string());
        }

        configDir = baseDir.string();
        if (!configDir.empty() && configDir.back() != '\\' && configDir.back() != '/') {
            configDir += "\\";
        }
    } catch (...) {
        configDir = (std::filesystem::current_path() / "configs").string();
        EnsureDirectoryExists(configDir);
        if (!configDir.empty() && configDir.back() != '\\' && configDir.back() != '/') {
            configDir += "\\";
        }
    }
}

bool ConfigManager::SaveConfig(const std::string& name) {
    try {
        if (configDir.empty()) {
            Terminal::AddOutput("ConfigDir is empty! Cannot save config.");
            return false;
        }

        if (name.empty()) {
            Terminal::AddOutput("Config name cannot be empty");
            return false;
        }

        nlohmann::json config = CollectCurrentConfig();
        std::filesystem::path dirPath = std::filesystem::path(configDir);
        std::filesystem::path filepath = dirPath / (name + ".json");
        Terminal::AddOutput("Saving config to: " + filepath.string());

        if (!EnsureDirectoryExists(dirPath.string())) {
            Terminal::AddOutput("Failed to create config directory: " + dirPath.string());
            return false;
        }

        std::ofstream file(filepath, std::ios::out | std::ios::trunc);
        if (file.is_open()) {
            file << config.dump(4);
            Terminal::AddOutput("Config saved successfully");
            return true;
        }

        Terminal::AddOutput("Failed to open file for writing: " + filepath.string());
    } catch (const std::exception& e) {
        Terminal::AddOutput("Exception in SaveConfig: " + std::string(e.what()));
    } catch (...) {
        Terminal::AddOutput("Unknown exception in SaveConfig");
    }
    return false;
}

bool ConfigManager::LoadConfig(const std::string& name) {
    try {
        if (configDir.empty()) {
            Terminal::AddOutput("ConfigDir is empty! Cannot load config.");
            return false;
        }

        std::filesystem::path filepath = std::filesystem::path(configDir) / (name + ".json");
        Terminal::AddOutput("Loading config from: " + filepath.string());
        std::ifstream file(filepath);
        if (file.is_open()) {
            nlohmann::json config;
            file >> config;
            ApplyConfig(config);
            ReloadModulesAfterConfig();
            Terminal::AddOutput("Config loaded successfully");
            return true;
        } else {
            Terminal::AddOutput("Failed to open file for reading: " + filepath.string());
        }
    } catch (const std::exception& e) {
        Terminal::AddOutput("Exception in LoadConfig: " + std::string(e.what()));
    } catch (...) {
        Terminal::AddOutput("Unknown exception in LoadConfig");
    }
    return false;
}

bool ConfigManager::DeleteConfig(const std::string& name) {
    try {
        if (configDir.empty()) {
            Terminal::AddOutput("ConfigDir is empty! Cannot delete config.");
            return false;
        }

        std::string filepath = configDir + name + ".json";
        Terminal::AddOutput("Deleting config: " + filepath);
        std::filesystem::path path = filepath;
        if (std::filesystem::remove(path)) {
            Terminal::AddOutput("Config deleted successfully");
            return true;
        } else {
            Terminal::AddOutput("Config file not found");
        }
    } catch (const std::exception& e) {
        Terminal::AddOutput("Exception in DeleteConfig: " + std::string(e.what()));
    } catch (...) {
        Terminal::AddOutput("Unknown exception in DeleteConfig");
    }
    return false;
}

std::vector<std::string> ConfigManager::ListConfigs() {
    std::vector<std::string> configs;
    try {
        if (configDir.empty()) {
            Terminal::AddOutput("ConfigDir is empty! Cannot list configs.");
            return configs;
        }

        for (const auto& entry : std::filesystem::directory_iterator(configDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                configs.push_back(entry.path().stem().string());
            }
        }
    } catch (...) {}
    return configs;
}

bool ConfigManager::OpenConfigDirectory() {
    try {
        if (configDir.empty()) {
            return false;
        }

        std::filesystem::path dirPath = std::filesystem::path(configDir);
        if (!std::filesystem::exists(dirPath)) {
            if (!EnsureDirectoryExists(dirPath.string())) {
                return false;
            }
        }

        HINSTANCE result = ShellExecuteA(NULL, "open", configDir.c_str(), NULL, NULL, SW_SHOWDEFAULT);
        return reinterpret_cast<intptr_t>(result) > 32;
    } catch (...) {
        return false;
    }
}

nlohmann::json ConfigManager::CollectCurrentConfig() {
    nlohmann::json config;

    // Combat modules
    config["Combat"]["Hitbox"]["enabled"] = Hitbox::g_hitboxEnabled;
    config["Combat"]["Hitbox"]["value"] = Hitbox::g_hitboxValue;

    config["Combat"]["Reach"]["enabled"] = Reach::IsEnabled();
    config["Combat"]["Reach"]["value"] = Reach::g_reachValue;

    // Movement modules
    config["Movement"]["AutoSprint"]["enabled"] = AutoSprint::g_autoSprintEnabled;
    config["Movement"]["Timer"]["enabled"] = Timer::g_timerEnabled;
    config["Movement"]["Timer"]["value"] = Timer::g_timerValue;

    // Visuals modules
    config["Visuals"]["FullBright"]["enabled"] = FullBright::g_fullBrightEnabled;
    config["Visuals"]["FullBright"]["value"] = FullBright::g_fullBrightValue;

    config["Visuals"]["RenderInfo"]["enabled"] = RenderInfo::g_showRenderInfo;
    config["Visuals"]["RenderInfo"]["showBackground"] = RenderInfo::g_showBackground;
    config["Visuals"]["RenderInfo"]["bgOpacity"] = RenderInfo::g_bgOpacity;
    config["Visuals"]["RenderInfo"]["scale"] = RenderInfo::g_scale;
    config["Visuals"]["RenderInfo"]["colors"]["themeColor"] = 
        nlohmann::json::array({RenderInfo::g_staticColor.x, RenderInfo::g_staticColor.y, 
                               RenderInfo::g_staticColor.z, RenderInfo::g_staticColor.w});

    if (RenderInfo::g_renderInfoHud) {
        config["Visuals"]["RenderInfo"]["position"]["x"] = RenderInfo::g_renderInfoHud->pos.x;
        config["Visuals"]["RenderInfo"]["position"]["y"] = RenderInfo::g_renderInfoHud->pos.y;
    }

    config["Visuals"]["Watermark"]["enabled"] = Watermark::g_showWatermark;
    config["Visuals"]["Watermark"]["useImage"] = Watermark::g_useImage;
    config["Visuals"]["Watermark"]["customText"] = Watermark::g_customText;
    config["Visuals"]["Watermark"]["fontSize"] = Watermark::g_fontSize;
    config["Visuals"]["Watermark"]["bgOpacity"] = Watermark::g_bgOpacity;
    config["Visuals"]["Watermark"]["showBackground"] = Watermark::g_showBackground;
    config["Visuals"]["Watermark"]["showShimmer"] = Watermark::g_showShimmer;
    config["Visuals"]["Watermark"]["showGlow"] = Watermark::g_showGlow;
    config["Visuals"]["Watermark"]["chromaText"] = Watermark::g_chromaText;
    config["Visuals"]["Watermark"]["chromaSpeed"] = Watermark::g_chromaSpeed;
    config["Visuals"]["Watermark"]["chromaDirection"] = Watermark::g_chromaDirection;
    config["Visuals"]["Watermark"]["mirroredGradient"] = Watermark::g_mirroredGradient;
    config["Visuals"]["Watermark"]["edgeFade"] = Watermark::g_edgeFade;
    config["Visuals"]["Watermark"]["imageOpacity"] = Watermark::g_imageOpacity;
    config["Visuals"]["Watermark"]["imageSize"] = Watermark::g_imageSize;
    
    config["Visuals"]["Watermark"]["colors"]["staticColor"] = 
        nlohmann::json::array({Watermark::g_staticColor.x, Watermark::g_staticColor.y, 
                               Watermark::g_staticColor.z, Watermark::g_staticColor.w});
    
    for (size_t i = 0; i < Watermark::g_chromaColors.size(); i++) {
        config["Visuals"]["Watermark"]["colors"]["chromaColors"][i] = 
            nlohmann::json::array({Watermark::g_chromaColors[i].x, Watermark::g_chromaColors[i].y, 
                                   Watermark::g_chromaColors[i].z, Watermark::g_chromaColors[i].w});
    }

    if (Watermark::g_watermarkHud) {
        config["Visuals"]["Watermark"]["position"]["x"] = Watermark::g_watermarkHud->pos.x;
        config["Visuals"]["Watermark"]["position"]["y"] = Watermark::g_watermarkHud->pos.y;
    }

    // ArrayList
    config["Visuals"]["ArrayList"]["enabled"] = ArrayList::g_enabled;
    config["Visuals"]["ArrayList"]["bgOpacity"] = ArrayList::g_bgOpacity;
    config["Visuals"]["ArrayList"]["showSideBar"] = ArrayList::g_showSideBar;
    config["Visuals"]["ArrayList"]["chromaSideBar"] = ArrayList::g_chromaSideBar;
    config["Visuals"]["ArrayList"]["roundedBorders"] = ArrayList::g_roundedBorders;
    config["Visuals"]["ArrayList"]["borderRadius"] = ArrayList::g_borderRadius;
    config["Visuals"]["ArrayList"]["showSuffix"] = ArrayList::g_showSuffix;
    
    config["Visuals"]["ArrayList"]["colors"]["bgColor"] = 
        nlohmann::json::array({ArrayList::g_bgColor.x, ArrayList::g_bgColor.y, 
                               ArrayList::g_bgColor.z, ArrayList::g_bgColor.w});
    config["Visuals"]["ArrayList"]["colors"]["sideBarColor"] = 
        nlohmann::json::array({ArrayList::g_sideBarColor.x, ArrayList::g_sideBarColor.y, 
                               ArrayList::g_sideBarColor.z, ArrayList::g_sideBarColor.w});

    if (ArrayList::g_hud) {
        config["Visuals"]["ArrayList"]["position"]["x"] = ArrayList::g_hud->pos.x;
        config["Visuals"]["ArrayList"]["position"]["y"] = ArrayList::g_hud->pos.y;
    }

    config["Visuals"]["MotionBlur"]["enabled"] = MotionBlur::g_motionBlurEnabled;
    
    config["Visuals"]["Keystrokes"]["enabled"] = Keystrokes::g_showKeystrokes;
    config["Visuals"]["Keystrokes"]["scale"] = Keystrokes::g_keystrokesUIScale;
    config["Visuals"]["Keystrokes"]["blurEffect"] = Keystrokes::g_keystrokesBlurEffect;
    config["Visuals"]["Keystrokes"]["rounding"] = Keystrokes::g_keystrokesRounding;
    config["Visuals"]["Keystrokes"]["showBg"] = Keystrokes::g_keystrokesShowBg;
    config["Visuals"]["Keystrokes"]["rectShadow"] = Keystrokes::g_keystrokesRectShadow;
    config["Visuals"]["Keystrokes"]["border"] = Keystrokes::g_keystrokesBorder;
    config["Visuals"]["Keystrokes"]["glow"] = Keystrokes::g_keystrokesGlow;
    config["Visuals"]["Keystrokes"]["glowEnabled"] = Keystrokes::g_keystrokesGlowEnabled;
    config["Visuals"]["Keystrokes"]["glowSpeed"] = Keystrokes::g_keystrokesGlowSpeed;
    config["Visuals"]["Keystrokes"]["keySpacing"] = Keystrokes::g_keystrokesKeySpacing;
    config["Visuals"]["Keystrokes"]["edSpeed"] = Keystrokes::g_keystrokesEdSpeed;
    config["Visuals"]["Keystrokes"]["textShadow"] = Keystrokes::g_keystrokesTextShadow;
    config["Visuals"]["Keystrokes"]["showMouseButtons"] = Keystrokes::g_keystrokesShowMouseButtons;
    config["Visuals"]["Keystrokes"]["showSpacebar"] = Keystrokes::g_keystrokesShowSpacebar;

    if (Keystrokes::g_keystrokesHud) {
        config["Visuals"]["Keystrokes"]["position"]["x"] = Keystrokes::g_keystrokesHud->pos.x;
        config["Visuals"]["Keystrokes"]["position"]["y"] = Keystrokes::g_keystrokesHud->pos.y;
    }
    // Save Keystrokes colors
    config["Visuals"]["Keystrokes"]["colors"]["bgColor"] = 
        nlohmann::json::array({Keystrokes::g_keystrokesBgColor.x, Keystrokes::g_keystrokesBgColor.y, 
                               Keystrokes::g_keystrokesBgColor.z, Keystrokes::g_keystrokesBgColor.w});
    config["Visuals"]["Keystrokes"]["colors"]["enabledColor"] = 
        nlohmann::json::array({Keystrokes::g_keystrokesEnabledColor.x, Keystrokes::g_keystrokesEnabledColor.y, 
                               Keystrokes::g_keystrokesEnabledColor.z, Keystrokes::g_keystrokesEnabledColor.w});
    config["Visuals"]["Keystrokes"]["colors"]["textColor"] = 
        nlohmann::json::array({Keystrokes::g_keystrokesTextColor.x, Keystrokes::g_keystrokesTextColor.y, 
                               Keystrokes::g_keystrokesTextColor.z, Keystrokes::g_keystrokesTextColor.w});
    config["Visuals"]["Keystrokes"]["colors"]["textEnabledColor"] = 
        nlohmann::json::array({Keystrokes::g_keystrokesTextEnabledColor.x, Keystrokes::g_keystrokesTextEnabledColor.y, 
                               Keystrokes::g_keystrokesTextEnabledColor.z, Keystrokes::g_keystrokesTextEnabledColor.w});
    config["Visuals"]["Keystrokes"]["colors"]["borderColor"] = 
        nlohmann::json::array({Keystrokes::g_keystrokesBorderColor.x, Keystrokes::g_keystrokesBorderColor.y, 
                               Keystrokes::g_keystrokesBorderColor.z, Keystrokes::g_keystrokesBorderColor.w});
    config["Visuals"]["Keystrokes"]["colors"]["glowColor"] = 
        nlohmann::json::array({Keystrokes::g_keystrokesGlowColor.x, Keystrokes::g_keystrokesGlowColor.y, 
                               Keystrokes::g_keystrokesGlowColor.z, Keystrokes::g_keystrokesGlowColor.w});
    config["Visuals"]["Keystrokes"]["colors"]["glowEnabledColor"] = 
        nlohmann::json::array({Keystrokes::g_keystrokesGlowEnabledColor.x, Keystrokes::g_keystrokesGlowEnabledColor.y, 
                               Keystrokes::g_keystrokesGlowEnabledColor.z, Keystrokes::g_keystrokesGlowEnabledColor.w});
    config["Visuals"]["Keystrokes"]["colors"]["rectShadowColor"] = 
        nlohmann::json::array({Keystrokes::g_keystrokesRectShadowColor.x, Keystrokes::g_keystrokesRectShadowColor.y, 
                               Keystrokes::g_keystrokesRectShadowColor.z, Keystrokes::g_keystrokesRectShadowColor.w});
    config["Visuals"]["Keystrokes"]["colors"]["textShadowColor"] = 
        nlohmann::json::array({Keystrokes::g_keystrokesTextShadowColor.x, Keystrokes::g_keystrokesTextShadowColor.y, 
                               Keystrokes::g_keystrokesTextShadowColor.z, Keystrokes::g_keystrokesTextShadowColor.w});
    config["Visuals"]["Keystrokes"]["colors"]["enabledShadowColor"] = 
        nlohmann::json::array({Keystrokes::g_keystrokesEnabledShadowColor.x, Keystrokes::g_keystrokesEnabledShadowColor.y, 
                               Keystrokes::g_keystrokesEnabledShadowColor.z, Keystrokes::g_keystrokesEnabledShadowColor.w});
    config["Visuals"]["Keystrokes"]["colors"]["disabledShadowColor"] = 
        nlohmann::json::array({Keystrokes::g_keystrokesDisabledShadowColor.x, Keystrokes::g_keystrokesDisabledShadowColor.y, 
                               Keystrokes::g_keystrokesDisabledShadowColor.z, Keystrokes::g_keystrokesDisabledShadowColor.w});

    config["Visuals"]["CPSCounter"]["enabled"] = CPSCounter::g_showCpsCounter;
    if (CPSCounter::g_cpsHud) {
        config["Visuals"]["CPSCounter"]["position"]["x"] = CPSCounter::g_cpsHud->pos.x;
        config["Visuals"]["CPSCounter"]["position"]["y"] = CPSCounter::g_cpsHud->pos.y;
    }
    // Save CPSCounter colors
    config["Visuals"]["CPSCounter"]["colors"]["textColor"] = 
        nlohmann::json::array({CPSCounter::g_cpsTextColor.x, CPSCounter::g_cpsTextColor.y, 
                               CPSCounter::g_cpsTextColor.z, CPSCounter::g_cpsTextColor.w});
    config["Visuals"]["CPSCounter"]["colors"]["shadowColor"] = 
        nlohmann::json::array({CPSCounter::g_cpsCounterShadowColor.x, CPSCounter::g_cpsCounterShadowColor.y, 
                               CPSCounter::g_cpsCounterShadowColor.z, CPSCounter::g_cpsCounterShadowColor.w});

    // FPSOverlay
    config["Visuals"]["FPSOverlay"]["enabled"] = FPSOverlay::g_showFpsOverlay;
    config["Visuals"]["FPSOverlay"]["scale"] = FPSOverlay::g_fpsTextScale;
    config["Visuals"]["FPSOverlay"]["showBackground"] = FPSOverlay::g_showBackground;
    config["Visuals"]["FPSOverlay"]["bgOpacity"] = FPSOverlay::g_bgOpacity;
    config["Visuals"]["FPSOverlay"]["colors"]["textColor"] = 
        nlohmann::json::array({FPSOverlay::g_fpsTextColor.x, FPSOverlay::g_fpsTextColor.y, 
                               FPSOverlay::g_fpsTextColor.z, FPSOverlay::g_fpsTextColor.w});
    config["Visuals"]["FPSOverlay"]["colors"]["accentColor"] = 
        nlohmann::json::array({FPSOverlay::g_accentColor.x, FPSOverlay::g_accentColor.y, 
                               FPSOverlay::g_accentColor.z, FPSOverlay::g_accentColor.w});

    if (FPSOverlay::g_fpsHud) {
        config["Visuals"]["FPSOverlay"]["position"]["x"] = FPSOverlay::g_fpsHud->pos.x;
        config["Visuals"]["FPSOverlay"]["position"]["y"] = FPSOverlay::g_fpsHud->pos.y;
    }

    // PingCounter
    config["Visuals"]["PingCounter"]["enabled"] = PingCounter::g_showPingCounter;
    config["Visuals"]["PingCounter"]["textScale"] = PingCounter::g_pingTextScale;
    config["Visuals"]["PingCounter"]["showBackground"] = PingCounter::g_showBackground;
    config["Visuals"]["PingCounter"]["bgOpacity"] = PingCounter::g_bgOpacity;
    config["Visuals"]["PingCounter"]["textShadow"] = PingCounter::g_pingTextShadow;
    config["Visuals"]["PingCounter"]["minPing"] = PingCounter::g_minPing;
    config["Visuals"]["PingCounter"]["maxPing"] = PingCounter::g_maxPing;
    config["Visuals"]["PingCounter"]["updateInterval"] = PingCounter::g_pingUpdateInterval;
    config["Visuals"]["PingCounter"]["colors"]["textColor"] =
        nlohmann::json::array({PingCounter::g_pingTextColor.x, PingCounter::g_pingTextColor.y,
                               PingCounter::g_pingTextColor.z, PingCounter::g_pingTextColor.w});
    config["Visuals"]["PingCounter"]["colors"]["shadowColor"] =
        nlohmann::json::array({PingCounter::g_pingCounterShadowColor.x, PingCounter::g_pingCounterShadowColor.y,
                               PingCounter::g_pingCounterShadowColor.z, PingCounter::g_pingCounterShadowColor.w});
    if (PingCounter::g_pingHud) {
        config["Visuals"]["PingCounter"]["position"]["x"] = PingCounter::g_pingHud->pos.x;
        config["Visuals"]["PingCounter"]["position"]["y"] = PingCounter::g_pingHud->pos.y;
    }

    // Misc modules
    config["Misc"]["UnlockFPS"]["enabled"] = UnlockFPS::g_unlockFpsEnabled;
    config["Misc"]["UnlockFPS"]["fpsLimit"] = UnlockFPS::g_fpsLimit;

    config["version"] = "1.0";
    config["timestamp"] = std::time(nullptr);

    return config;
}

void ConfigManager::ApplyConfig(const nlohmann::json& config) {
    // Combat modules
    if (config.contains("Combat")) {
        if (config["Combat"].contains("Hitbox")) {
            if (config["Combat"]["Hitbox"].contains("enabled")) {
                Hitbox::g_hitboxEnabled = config["Combat"]["Hitbox"]["enabled"];
            }
            if (config["Combat"]["Hitbox"].contains("value")) {
                Hitbox::g_hitboxValue = config["Combat"]["Hitbox"]["value"];
            }
        }
        if (config["Combat"].contains("Reach")) {
            if (config["Combat"]["Reach"].contains("enabled")) {
                Reach::SetEnabled(config["Combat"]["Reach"]["enabled"]);
            }
            if (config["Combat"]["Reach"].contains("value")) {
                if (Reach::IsEnabled()) {
                    Reach::UpdateValue(config["Combat"]["Reach"]["value"]);
                } else {
                    Reach::g_reachValue = 3.0f;
                }
            }
        }
    }

    // Movement modules
    if (config.contains("Movement")) {
        if (config["Movement"].contains("AutoSprint")) {
            if (config["Movement"]["AutoSprint"].contains("enabled")) {
                AutoSprint::g_autoSprintEnabled = config["Movement"]["AutoSprint"]["enabled"];
            }
        }
        if (config["Movement"].contains("Timer")) {
            if (config["Movement"]["Timer"].contains("enabled")) {
                Timer::g_timerEnabled = config["Movement"]["Timer"]["enabled"];
            }
            if (config["Movement"]["Timer"].contains("value")) {
                Timer::g_timerValue = config["Movement"]["Timer"]["value"];
            }
        }
    }

    // Visuals modules
    if (config.contains("Visuals")) {
        auto& visuals = config["Visuals"];
        if (visuals.contains("FullBright")) {
            if (visuals["FullBright"].contains("enabled")) {
                FullBright::g_fullBrightEnabled = visuals["FullBright"]["enabled"];
            }
            if (visuals["FullBright"].contains("value")) {
                FullBright::g_fullBrightValue = visuals["FullBright"]["value"];
            }
        }
        if (visuals.contains("RenderInfo")) {
            if (visuals["RenderInfo"].contains("enabled")) {
                RenderInfo::g_showRenderInfo = visuals["RenderInfo"]["enabled"];
            }
            if (visuals["RenderInfo"].contains("showBackground")) RenderInfo::g_showBackground = visuals["RenderInfo"]["showBackground"];
            if (visuals["RenderInfo"].contains("bgOpacity")) RenderInfo::g_bgOpacity = visuals["RenderInfo"]["bgOpacity"];
            if (visuals["RenderInfo"].contains("scale")) RenderInfo::g_scale = visuals["RenderInfo"]["scale"];
            
            if (visuals["RenderInfo"].contains("colors")) {
                auto& c = visuals["RenderInfo"]["colors"]["themeColor"];
                if (c.is_array() && c.size() == 4) {
                    RenderInfo::g_staticColor = ImVec4(c[0], c[1], c[2], c[3]);
                }
            }

            if (visuals["RenderInfo"].contains("position") && RenderInfo::g_renderInfoHud) {
                RenderInfo::g_renderInfoHud->pos.x = visuals["RenderInfo"]["position"]["x"];
                RenderInfo::g_renderInfoHud->pos.y = visuals["RenderInfo"]["position"]["y"];
            }
        }
        if (visuals.contains("Watermark")) {
            auto& wm = visuals["Watermark"];
            if (wm.contains("enabled")) Watermark::g_showWatermark = wm["enabled"];
            if (wm.contains("useImage")) Watermark::g_useImage = wm["useImage"];
            if (wm.contains("customText")) strcpy_s(Watermark::g_customText, std::string(wm["customText"]).c_str());
            if (wm.contains("fontSize")) Watermark::g_fontSize = wm["fontSize"];
            if (wm.contains("bgOpacity")) Watermark::g_bgOpacity = wm["bgOpacity"];
            if (wm.contains("showBackground")) Watermark::g_showBackground = wm["showBackground"];
            if (wm.contains("showShimmer")) Watermark::g_showShimmer = wm["showShimmer"];
            if (wm.contains("showGlow")) Watermark::g_showGlow = wm["showGlow"];
            if (wm.contains("chromaText")) Watermark::g_chromaText = wm["chromaText"];
            if (wm.contains("chromaSpeed")) Watermark::g_chromaSpeed = wm["chromaSpeed"];
            if (wm.contains("chromaDirection")) Watermark::g_chromaDirection = wm["chromaDirection"];
            if (wm.contains("mirroredGradient")) Watermark::g_mirroredGradient = wm["mirroredGradient"];
            if (wm.contains("edgeFade")) Watermark::g_edgeFade = wm["edgeFade"];
            if (wm.contains("imageOpacity")) Watermark::g_imageOpacity = wm["imageOpacity"];
            if (wm.contains("imageSize")) Watermark::g_imageSize = wm["imageSize"];
            
            if (wm.contains("colors")) {
                if (wm["colors"].contains("staticColor") && wm["colors"]["staticColor"].size() == 4) {
                    Watermark::g_staticColor = ImVec4(wm["colors"]["staticColor"][0], wm["colors"]["staticColor"][1], 
                                                      wm["colors"]["staticColor"][2], wm["colors"]["staticColor"][3]);
                }
                if (wm["colors"].contains("chromaColors") && wm["colors"]["chromaColors"].is_array()) {
                    Watermark::g_chromaColors.clear();
                    for (size_t i = 0; i < wm["colors"]["chromaColors"].size(); i++) {
                        if (wm["colors"]["chromaColors"][i].size() == 4) {
                            Watermark::g_chromaColors.push_back(ImVec4(
                                wm["colors"]["chromaColors"][i][0], 
                                wm["colors"]["chromaColors"][i][1], 
                                wm["colors"]["chromaColors"][i][2], 
                                wm["colors"]["chromaColors"][i][3]
                            ));
                        }
                    }
                }
            }

            if (wm.contains("position") && Watermark::g_watermarkHud) {
                Watermark::g_watermarkHud->pos.x = wm["position"]["x"];
                Watermark::g_watermarkHud->pos.y = wm["position"]["y"];
            }
        }
        
        if (visuals.contains("ArrayList")) {
            auto& al = visuals["ArrayList"];
            if (al.contains("enabled")) ArrayList::g_enabled = al["enabled"];
            if (al.contains("bgOpacity")) ArrayList::g_bgOpacity = al["bgOpacity"];
            if (al.contains("showSideBar")) ArrayList::g_showSideBar = al["showSideBar"];
            if (al.contains("chromaSideBar")) ArrayList::g_chromaSideBar = al["chromaSideBar"];
            if (al.contains("roundedBorders")) ArrayList::g_roundedBorders = al["roundedBorders"];
            if (al.contains("borderRadius")) ArrayList::g_borderRadius = al["borderRadius"];
            if (al.contains("showSuffix")) ArrayList::g_showSuffix = al["showSuffix"];
            
            if (al.contains("colors")) {
                if (al["colors"].contains("bgColor") && al["colors"]["bgColor"].size() == 4) {
                    ArrayList::g_bgColor = ImVec4(al["colors"]["bgColor"][0], al["colors"]["bgColor"][1], 
                                                  al["colors"]["bgColor"][2], al["colors"]["bgColor"][3]);
                }
                if (al["colors"].contains("sideBarColor") && al["colors"]["sideBarColor"].size() == 4) {
                    ArrayList::g_sideBarColor = ImVec4(al["colors"]["sideBarColor"][0], al["colors"]["sideBarColor"][1], 
                                                       al["colors"]["sideBarColor"][2], al["colors"]["sideBarColor"][3]);
                }
            }

            if (al.contains("position") && ArrayList::g_hud) {
                ArrayList::g_hud->pos.x = al["position"]["x"];
                ArrayList::g_hud->pos.y = al["position"]["y"];
            }
        }
        if (visuals.contains("MotionBlur")) {
            if (visuals["MotionBlur"].contains("enabled")) {
                MotionBlur::g_motionBlurEnabled = visuals["MotionBlur"]["enabled"];
            }
        }
        if (visuals.contains("Keystrokes")) {
            if (visuals["Keystrokes"].contains("enabled")) {
                Keystrokes::g_showKeystrokes = visuals["Keystrokes"]["enabled"];
            }
            if (visuals["Keystrokes"].contains("scale")) Keystrokes::g_keystrokesUIScale = visuals["Keystrokes"]["scale"];
            if (visuals["Keystrokes"].contains("blurEffect")) Keystrokes::g_keystrokesBlurEffect = visuals["Keystrokes"]["blurEffect"];
            if (visuals["Keystrokes"].contains("rounding")) Keystrokes::g_keystrokesRounding = visuals["Keystrokes"]["rounding"];
            if (visuals["Keystrokes"].contains("showBg")) Keystrokes::g_keystrokesShowBg = visuals["Keystrokes"]["showBg"];
            if (visuals["Keystrokes"].contains("rectShadow")) Keystrokes::g_keystrokesRectShadow = visuals["Keystrokes"]["rectShadow"];
            if (visuals["Keystrokes"].contains("border")) Keystrokes::g_keystrokesBorder = visuals["Keystrokes"]["border"];
            if (visuals["Keystrokes"].contains("glow")) Keystrokes::g_keystrokesGlow = visuals["Keystrokes"]["glow"];
            if (visuals["Keystrokes"].contains("glowEnabled")) Keystrokes::g_keystrokesGlowEnabled = visuals["Keystrokes"]["glowEnabled"];
            if (visuals["Keystrokes"].contains("glowSpeed")) Keystrokes::g_keystrokesGlowSpeed = visuals["Keystrokes"]["glowSpeed"];
            if (visuals["Keystrokes"].contains("keySpacing")) Keystrokes::g_keystrokesKeySpacing = visuals["Keystrokes"]["keySpacing"];
            if (visuals["Keystrokes"].contains("edSpeed")) Keystrokes::g_keystrokesEdSpeed = visuals["Keystrokes"]["edSpeed"];
            if (visuals["Keystrokes"].contains("textShadow")) Keystrokes::g_keystrokesTextShadow = visuals["Keystrokes"]["textShadow"];
            if (visuals["Keystrokes"].contains("showMouseButtons")) Keystrokes::g_keystrokesShowMouseButtons = visuals["Keystrokes"]["showMouseButtons"];
            if (visuals["Keystrokes"].contains("showSpacebar")) Keystrokes::g_keystrokesShowSpacebar = visuals["Keystrokes"]["showSpacebar"];

            if (visuals["Keystrokes"].contains("position") && Keystrokes::g_keystrokesHud) {
                Keystrokes::g_keystrokesHud->pos.x = visuals["Keystrokes"]["position"]["x"];
                Keystrokes::g_keystrokesHud->pos.y = visuals["Keystrokes"]["position"]["y"];
            }
            // Load Keystrokes colors
            if (visuals["Keystrokes"].contains("colors")) {
                auto& colors = visuals["Keystrokes"]["colors"];
                if (colors.contains("bgColor") && colors["bgColor"].size() == 4) {
                    Keystrokes::g_keystrokesBgColor = ImVec4(colors["bgColor"][0], colors["bgColor"][1], 
                                                               colors["bgColor"][2], colors["bgColor"][3]);
                }
                if (colors.contains("enabledColor") && colors["enabledColor"].size() == 4) {
                    Keystrokes::g_keystrokesEnabledColor = ImVec4(colors["enabledColor"][0], colors["enabledColor"][1], 
                                                                    colors["enabledColor"][2], colors["enabledColor"][3]);
                }
                if (colors.contains("textColor") && colors["textColor"].size() == 4) {
                    Keystrokes::g_keystrokesTextColor = ImVec4(colors["textColor"][0], colors["textColor"][1], 
                                                                 colors["textColor"][2], colors["textColor"][3]);
                }
                if (colors.contains("textEnabledColor") && colors["textEnabledColor"].size() == 4) {
                    Keystrokes::g_keystrokesTextEnabledColor = ImVec4(colors["textEnabledColor"][0], colors["textEnabledColor"][1], 
                                                                        colors["textEnabledColor"][2], colors["textEnabledColor"][3]);
                }
                if (colors.contains("borderColor") && colors["borderColor"].size() == 4) {
                    Keystrokes::g_keystrokesBorderColor = ImVec4(colors["borderColor"][0], colors["borderColor"][1], 
                                                                        colors["borderColor"][2], colors["borderColor"][3]);
                }
                if (colors.contains("glowColor") && colors["glowColor"].size() == 4) {
                    Keystrokes::g_keystrokesGlowColor = ImVec4(colors["glowColor"][0], colors["glowColor"][1], 
                                                                        colors["glowColor"][2], colors["glowColor"][3]);
                }
                if (colors.contains("glowEnabledColor") && colors["glowEnabledColor"].size() == 4) {
                    Keystrokes::g_keystrokesGlowEnabledColor = ImVec4(colors["glowEnabledColor"][0], colors["glowEnabledColor"][1], 
                                                                        colors["glowEnabledColor"][2], colors["glowEnabledColor"][3]);
                }
                if (colors.contains("rectShadowColor") && colors["rectShadowColor"].size() == 4) {
                    Keystrokes::g_keystrokesRectShadowColor = ImVec4(colors["rectShadowColor"][0], colors["rectShadowColor"][1], 
                                                                        colors["rectShadowColor"][2], colors["rectShadowColor"][3]);
                }
                if (colors.contains("textShadowColor") && colors["textShadowColor"].size() == 4) {
                    Keystrokes::g_keystrokesTextShadowColor = ImVec4(colors["textShadowColor"][0], colors["textShadowColor"][1], 
                                                                        colors["textShadowColor"][2], colors["textShadowColor"][3]);
                }
                if (colors.contains("enabledShadowColor") && colors["enabledShadowColor"].size() == 4) {
                    Keystrokes::g_keystrokesEnabledShadowColor = ImVec4(colors["enabledShadowColor"][0], colors["enabledShadowColor"][1], 
                                                                        colors["enabledShadowColor"][2], colors["enabledShadowColor"][3]);
                }
                if (colors.contains("disabledShadowColor") && colors["disabledShadowColor"].size() == 4) {
                    Keystrokes::g_keystrokesDisabledShadowColor = ImVec4(colors["disabledShadowColor"][0], colors["disabledShadowColor"][1], 
                                                                        colors["disabledShadowColor"][2], colors["disabledShadowColor"][3]);
                }
            }
        }
        if (visuals.contains("CPSCounter")) {
            if (visuals["CPSCounter"].contains("enabled")) {
                CPSCounter::g_showCpsCounter = visuals["CPSCounter"]["enabled"];
            }
            if (visuals.contains("CPSCounter") && visuals["CPSCounter"].contains("position")) {
                if (CPSCounter::g_cpsHud) {
                    CPSCounter::g_cpsHud->pos.x = visuals["CPSCounter"]["position"]["x"];
                    CPSCounter::g_cpsHud->pos.y = visuals["CPSCounter"]["position"]["y"];
                }
            }

            if (visuals.contains("FPSOverlay")) {
                auto& fps = visuals["FPSOverlay"];
                if (fps.contains("enabled")) FPSOverlay::g_showFpsOverlay = fps["enabled"];
                if (fps.contains("scale")) FPSOverlay::g_fpsTextScale = fps["scale"];
                if (fps.contains("showBackground")) FPSOverlay::g_showBackground = fps["showBackground"];
                if (fps.contains("bgOpacity")) FPSOverlay::g_bgOpacity = fps["bgOpacity"];
                
                if (fps.contains("colors")) {
                    auto& c = fps["colors"];
                    if (c.contains("textColor")) {
                        FPSOverlay::g_fpsTextColor = ImVec4(c["textColor"][0], c["textColor"][1], c["textColor"][2], c["textColor"][3]);
                    }
                    if (c.contains("accentColor")) {
                        FPSOverlay::g_accentColor = ImVec4(c["accentColor"][0], c["accentColor"][1], c["accentColor"][2], c["accentColor"][3]);
                    }
                }

                if (fps.contains("position") && FPSOverlay::g_fpsHud) {
                    FPSOverlay::g_fpsHud->pos.x = fps["position"]["x"];
                    FPSOverlay::g_fpsHud->pos.y = fps["position"]["y"];
                }
            }
            // Load CPSCounter colors
            if (visuals["CPSCounter"].contains("colors")) {
                auto& colors = visuals["CPSCounter"]["colors"];
                if (colors.contains("textColor") && colors["textColor"].size() == 4) {
                    CPSCounter::g_cpsTextColor = ImVec4(colors["textColor"][0], colors["textColor"][1], 
                                                         colors["textColor"][2], colors["textColor"][3]);
                }
                if (colors.contains("shadowColor") && colors["shadowColor"].size() == 4) {
                    CPSCounter::g_cpsCounterShadowColor = ImVec4(colors["shadowColor"][0], colors["shadowColor"][1], 
                                                                   colors["shadowColor"][2], colors["shadowColor"][3]);
                }
            }
        }
        // PingCounter
        if (visuals.contains("PingCounter")) {
            auto& pc = visuals["PingCounter"];
            if (pc.contains("enabled")) PingCounter::g_showPingCounter = pc["enabled"];
            if (pc.contains("textScale")) PingCounter::g_pingTextScale = pc["textScale"];
            if (pc.contains("showBackground")) PingCounter::g_showBackground = pc["showBackground"];
            if (pc.contains("bgOpacity")) PingCounter::g_bgOpacity = pc["bgOpacity"];
            if (pc.contains("textShadow")) PingCounter::g_pingTextShadow = pc["textShadow"];
            if (pc.contains("minPing")) PingCounter::g_minPing = pc["minPing"];
            if (pc.contains("maxPing")) PingCounter::g_maxPing = pc["maxPing"];
            if (pc.contains("updateInterval")) PingCounter::g_pingUpdateInterval = pc["updateInterval"];
            if (pc.contains("colors")) {
                auto& c = pc["colors"];
                if (c.contains("textColor") && c["textColor"].size() == 4)
                    PingCounter::g_pingTextColor = ImVec4(c["textColor"][0], c["textColor"][1], c["textColor"][2], c["textColor"][3]);
                if (c.contains("shadowColor") && c["shadowColor"].size() == 4)
                    PingCounter::g_pingCounterShadowColor = ImVec4(c["shadowColor"][0], c["shadowColor"][1], c["shadowColor"][2], c["shadowColor"][3]);
            }
            if (pc.contains("position") && PingCounter::g_pingHud) {
                PingCounter::g_pingHud->pos.x = pc["position"]["x"];
                PingCounter::g_pingHud->pos.y = pc["position"]["y"];
            }
        }
    }

    // Misc modules
    if (config.contains("Misc")) {
        if (config["Misc"].contains("UnlockFPS")) {
            if (config["Misc"]["UnlockFPS"].contains("enabled")) {
                UnlockFPS::g_unlockFpsEnabled = config["Misc"]["UnlockFPS"]["enabled"];
            }
            if (config["Misc"]["UnlockFPS"].contains("fpsLimit")) {
                UnlockFPS::g_fpsLimit = config["Misc"]["UnlockFPS"]["fpsLimit"];
            }
        }
    }

    Terminal::AddOutput("Configuration applied successfully.");
}

void ConfigManager::ReloadModulesAfterConfig() {
    // Re-enable or disable modules based on their saved state
    if (Hitbox::g_hitboxEnabled) {
        Hitbox::Enable();
    } else {
        Hitbox::Disable();
    }
    
    if (AutoSprint::g_autoSprintEnabled) {
        AutoSprint::Enable();
    } else {
        AutoSprint::Disable();
    }

    if (FullBright::g_fullBrightEnabled) {
        FullBright::Enable();
    } else {
        FullBright::Disable();
    }

    if (Reach::IsEnabled()) {
        Reach::SetEnabled(true);
    } else {
        Reach::SetEnabled(false);
    }

    if (Timer::g_timerEnabled) {
        Timer::Enable();
    } else {
        Timer::Disable();
    }

    Terminal::AddOutput("Modules reloaded after config load.");
}
