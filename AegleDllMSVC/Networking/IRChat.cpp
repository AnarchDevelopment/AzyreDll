#include "IRChat.hpp"
#include "Client/IRCClient.hpp"
#include "GUI/GUI.hpp"
#include "ImGui/imgui.h"
#include "nlohmann/json.hpp"
#include "Config/ConfigManager.hpp"
#include "Animations/Animations.hpp"

#include <shlobj.h>
#include <knownfolders.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <iterator>
#include <cctype>

// Local state
static std::string s_savedUsername = "";
static std::string s_savedPassword = "";
static std::string s_savedServer = "irc.libera.chat";
static bool s_credentialsLoaded = false;
static bool s_hasCredentials = false;

static char s_serverInput[128] = "irc.libera.chat";
static char s_usernameInput[64] = "";
static char s_passwordInput[64] = "";
static char s_messageInput[512] = "";
static bool s_autoScroll = true;
static int s_lastMessageCount = 0;

static std::filesystem::path GetCredentialsPath() {
    std::filesystem::path baseDir;
    PWSTR localAppDataPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppDataPath)) && localAppDataPath != nullptr) {
        std::wstring wpath(localAppDataPath);
        CoTaskMemFree(localAppDataPath);
        baseDir = std::filesystem::path(wpath) / "Packages" / "Microsoft.MinecraftUWP_8wekyb3d8bbwe" / "RoamingState";
    } else {
        char* localAppData = std::getenv("LOCALAPPDATA");
        if (localAppData && strlen(localAppData) > 0) {
            baseDir = std::filesystem::path(localAppData) / "Packages" / "Microsoft.MinecraftUWP_8wekyb3d8bbwe" / "RoamingState";
        } else {
            baseDir = std::filesystem::current_path();
        }
    }
    return baseDir / "credentials-irc.json";
}

static bool EnsureDirExists(const std::filesystem::path& dirPath) {
    try {
        if (std::filesystem::exists(dirPath))
            return std::filesystem::is_directory(dirPath);
        return std::filesystem::create_directories(dirPath);
    } catch (...) { return false; }
}

static bool LoadCredentials(std::string& username, std::string& password) {
    try {
        auto path = GetCredentialsPath();
        if (!std::filesystem::exists(path)) return false;
        std::ifstream file(path);
        if (!file.is_open()) return false;
        nlohmann::json j;
        file >> j;
        if (j.contains("username") && j.contains("password")) {
            username = j["username"].get<std::string>();
            password = j["password"].get<std::string>();
            if (j.contains("server"))
                s_savedServer = j["server"].get<std::string>();
            return !username.empty();
        }
    } catch (...) {}
    return false;
}

static bool SaveCredentials(const std::string& username, const std::string& password) {
    try {
        auto path = GetCredentialsPath();
        auto dir = path.parent_path();
        EnsureDirExists(dir);
        nlohmann::json j;
        j["username"] = username;
        j["password"] = password;
        j["server"] = s_savedServer;
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file.is_open()) return false;
        file << j.dump(4);
        return true;
    } catch (...) {}
    return false;
}

static void DeleteCredentials() {
    try {
        auto path = GetCredentialsPath();
        if (std::filesystem::exists(path))
            std::filesystem::remove(path);
    } catch (...) {}
}

static bool SaveReceivedConfig(const std::string& filename, const std::string& jsonContent) {
    try {
        auto json = nlohmann::json::parse(jsonContent);
        std::filesystem::path configPath = std::filesystem::path(ConfigManager::GetConfigDir());
        if (!std::filesystem::exists(configPath))
            std::filesystem::create_directories(configPath);
        std::filesystem::path filepath = configPath / (filename + ".json");
        std::ofstream file(filepath, std::ios::out | std::ios::trunc);
        if (file.is_open()) {
            file << json.dump(4);
            return true;
        }
    } catch (...) {}
    return false;
}

void IRChat::RenderMenu() {
    // Credentials handling

    if (!s_credentialsLoaded) {
        s_hasCredentials = LoadCredentials(s_savedUsername, s_savedPassword);
        s_credentialsLoaded = true;
        if (s_hasCredentials) {
            strncpy_s(s_serverInput, s_savedServer.c_str(), sizeof(s_serverInput) - 1);
            strncpy_s(s_usernameInput, s_savedUsername.c_str(), sizeof(s_usernameInput) - 1);
            strncpy_s(s_passwordInput, s_savedPassword.c_str(), sizeof(s_passwordInput) - 1);
            IRCClient::GetInstance().Connect(s_savedServer, 6667, s_savedUsername, s_savedPassword);
        }
    }

    auto& client = IRCClient::GetInstance();
    IRCStatus status = client.GetStatus();
    ImVec2 avail = ImGui::GetContentRegionAvail();

    if (!s_hasCredentials) {
        // Form dimensions
        float formWidth  = 420.0f;
        float formHeight = 300.0f;

        // Center the form in the full available area
        float startX = (avail.x - formWidth)  * 0.5f;
        float startY = (avail.y - formHeight) * 0.5f;
        if (startX < 0.0f) startX = 0.0f;
        if (startY < 0.0f) startY = 0.0f;

        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + startX, ImGui::GetCursorPosY() + startY));

        // Title
        ImGui::PushFont(GUI::g_fontH2 ? GUI::g_fontH2 : ImGui::GetFont());
        float titleWidth = ImGui::CalcTextSize("IRC Chat Registration").x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (formWidth - titleWidth) * 0.5f);
        GUI::AddTextGlow(ImGui::GetWindowDrawList(), ImGui::GetFont(), ImGui::GetFontSize(), ImGui::GetCursorScreenPos(), ImColor(GUI::g_colorAccent.x, GUI::g_colorAccent.y, GUI::g_colorAccent.z, 1.0f), "IRC Chat Registration", 3.0f);
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));
        ImGui::PopFont();
        ImGui::Spacing(); ImGui::Spacing();

        // Hint text
        float hintWidth = ImGui::CalcTextSize("Credentials will be saved locally in RoamingState.\nThis chat is in testing phase and may contain errors.").x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX + (formWidth - hintWidth) * 0.5f);
        ImGui::TextDisabled("  Credentials will be saved locally in RoamingState.\nThis chat is in testing phase and may contain errors.");
        ImGui::Spacing();

        float labelCol  = startX + 10.0f;
        float inputCol  = startX + 110.0f;
        float inputWidth = formWidth - 120.0f;

        ImGui::SetCursorPosX(labelCol);
        ImGui::Text("Username:");
        ImGui::SameLine(inputCol);
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::InputText("##irc_reg_username", s_usernameInput, sizeof(s_usernameInput));
        ImGui::Spacing();

        ImGui::SetCursorPosX(labelCol);
        ImGui::Text("Password:");
        ImGui::SameLine(inputCol);
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::InputText("##irc_reg_password", s_passwordInput, sizeof(s_passwordInput), ImGuiInputTextFlags_Password);
        ImGui::Spacing();

        ImGui::SetCursorPosX(labelCol);
        ImGui::Text("Server:");
        ImGui::SameLine(inputCol);
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::InputText("##irc_reg_server", s_serverInput, sizeof(s_serverInput));
        ImGui::SetCursorPosX(labelCol);
        ImGui::TextDisabled("e.g. irc.libera.chat, irc.rizon.net");
        ImGui::Spacing(); ImGui::Spacing();

        float btnWidth = 160.0f;
        ImGui::SetCursorPosX(startX + (formWidth - btnWidth) * 0.5f);
        if (ImGui::Button("Register & Connect", ImVec2(btnWidth, 38.0f))) {
            std::string uName(s_usernameInput);
            std::string pWord(s_passwordInput);
            std::string server(s_serverInput);
            uName.erase(std::remove_if(uName.begin(), uName.end(), ::isspace), uName.end());
            server.erase(std::remove_if(server.begin(), server.end(), ::isspace), server.end());
            if (!uName.empty() && !server.empty()) {
                s_savedUsername = uName;
                s_savedPassword = pWord;
                s_savedServer = server;
                s_hasCredentials = true;
                SaveCredentials(s_savedUsername, s_savedPassword);
                client.Connect(server, 6667, s_savedUsername, s_savedPassword);
            }
        }
        return;
    }

    // Header
    ImGui::BeginChild("IRCHeader", ImVec2(0, 45.0f), false, ImGuiWindowFlags_NoScrollbar);
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGui::SetCursorPos(ImVec2(10.0f, 12.0f));
        ImGui::PushFont(GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetFont());
        ImGui::TextColored(GUI::g_colorAccent, "#aeglegeneral");
        ImGui::PopFont();

        ImGui::SameLine(0, 20.0f);
        ImVec2 badgePos = ImGui::GetCursorScreenPos();
        badgePos.y += 2.0f;

        ImU32 dotColor = ImColor(0.5f, 0.5f, 0.5f, 1.0f);
        std::string statusText = "Disconnected";

        if (status == IRCStatus::Connecting) {
            float pulse = (sinf((float)GetTickCount64() * 0.005f) + 1.0f) * 0.5f;
            dotColor = ImColor(1.0f, 0.8f, 0.2f, 0.6f + pulse * 0.4f);
            statusText = "Connecting...";
        } else if (status == IRCStatus::Connected) {
            float pulse = (sinf((float)GetTickCount64() * 0.005f) + 1.0f) * 0.5f;
            dotColor = ImColor(0.2f, 0.9f, 0.3f, 0.6f + pulse * 0.4f);
            statusText = "Connected as " + client.GetCurrentNick();
        } else if (status == IRCStatus::ConnectionFailed) {
            dotColor = ImColor(0.9f, 0.2f, 0.2f, 1.0f);
            statusText = "Connection Failed";
        }

        draw->AddCircleFilled(ImVec2(badgePos.x + 5.0f, badgePos.y + 10.0f), 4.0f, dotColor);
        ImGui::SetCursorScreenPos(ImVec2(badgePos.x + 18.0f, badgePos.y + (20.0f - ImGui::GetFontSize()) * 0.5f));
        ImGui::TextDisabled(statusText.c_str());

        if (status == IRCStatus::ConnectionFailed) {
            ImGui::SameLine(0, 15.0f);
            ImGui::SetCursorPosY(10.0f);
            if (ImGui::Button("Retry", ImVec2(60.0f, 25.0f)))
                client.Connect(s_savedServer, 6667, s_savedUsername, s_savedPassword);
        }

        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 95.0f, 10.0f));
        if (ImGui::Button("Log Out", ImVec2(80.0f, 28.0f))) {
            client.Disconnect();
            DeleteCredentials();
            s_savedUsername = "";
            s_savedPassword = "";
            s_savedServer = "irc.libera.chat";
            s_serverInput[0] = '\0';
            s_usernameInput[0] = '\0';
            s_passwordInput[0] = '\0';
            s_hasCredentials = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Disconnect and delete stored credentials");
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Spacing();

    avail = ImGui::GetContentRegionAvail();
    float userListWidth = 150.0f;
    float chatWidth = avail.x - userListWidth - 15.0f;
    float messageWindowHeight = avail.y - 50.0f;

    // Chat messages
    ImGui::BeginChild("ChatMessages", ImVec2(chatWidth, messageWindowHeight), true);
    {
        auto messages = client.GetMessages();
        float msgIndent = 10.0f;
        std::string myNick = client.GetCurrentNick();

        for (const auto& msg : messages) {
            if (msg.type == IRCMessage::Type::System) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(GUI::g_colorAccentSoft.x, GUI::g_colorAccentSoft.y, GUI::g_colorAccentSoft.z, 0.8f));
                ImGui::TextWrapped("%s", msg.text.c_str());
                ImGui::PopStyleColor();
                continue;
            }

            if (msg.type == IRCMessage::Type::Join) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.3f, 0.8f), "--> %s %s", msg.sender.c_str(), msg.text.c_str());
                continue;
            }

            if (msg.type == IRCMessage::Type::Part || msg.type == IRCMessage::Type::Quit) {
                ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.2f, 0.8f), "<-- %s %s", msg.sender.c_str(), msg.text.c_str());
                continue;
            }

            // User message
            bool isMe = (msg.sender == myNick);
            ImVec4 nameColor = isMe ? GUI::g_colorAccent : ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
            ImGui::TextColored(nameColor, "%s", msg.sender.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled(" [%s]", msg.timeStr.c_str());

            if (msg.type == IRCMessage::Type::ConfigShare) {
                if (!msg.comment.empty()) {
                    ImGui::TextWrapped("%s", msg.comment.c_str());
                    ImGui::Spacing();
                }
                // Embed
                std::string cfgId = "cfg" + msg.configName + msg.sender + msg.timeStr;
                float embedWidth = chatWidth - 20.0f;
                ImGui::BeginChild(cfgId.c_str(), ImVec2(embedWidth, 50.0f), true, ImGuiWindowFlags_NoScrollbar);
                {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
                    ImGui::Text("Config: %s", msg.configName.c_str());
                    ImGui::SameLine(embedWidth - 60.0f);
                    if (ImGui::Button(("Download##" + cfgId).c_str(), ImVec2(50.0f, 22.0f))) {
                        if (SaveReceivedConfig(msg.configName, msg.text))
                            client.PushSystemMessage("Client", "Config saved: " + msg.configName);
                        else
                            client.PushSystemMessage("Client", "Failed to save config: " + msg.configName);
                    }
                }
                ImGui::EndChild();
            } else {
                ImGui::TextWrapped("%s", msg.text.c_str());
            }
        }

        if (s_autoScroll && (int)messages.size() > s_lastMessageCount)
            ImGui::SetScrollHereY(1.0f);
        s_lastMessageCount = (int)messages.size();
    }
    ImGui::EndChild();
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DRAG_IRC_CONFIG")) {
            std::string cfgName = (const char*)payload->Data;
            std::filesystem::path cfgPath = std::filesystem::path(ConfigManager::GetConfigDir()) / (cfgName + ".json");
            std::ifstream file(cfgPath);
            if (file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                if (client.SendConfigFile(cfgName, content, "")) {
                    client.PushSystemMessage("Client", "Config shared: " + cfgName);
                } else {
                    client.PushSystemMessage("Client", "Error sharing config.");
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();

    // User list sidebar
    ImGui::BeginChild("IRCUserList", ImVec2(userListWidth, messageWindowHeight), true);
    {
        auto users = client.GetUsers();
        ImGui::TextColored(GUI::g_colorAccent, "ONLINE (%d)", (int)users.size());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginChild("UserListScroll", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
        {
            std::string myNick = client.GetCurrentNick();
            for (const auto& user : users) {
                bool isMe = (user == myNick);
                ImVec2 bulletPos = ImGui::GetCursorScreenPos();
                bulletPos.y += ImGui::GetTextLineHeight() * 0.4f;
                bulletPos.x += 6.0f;
                ImGui::GetWindowDrawList()->AddCircleFilled(bulletPos, 3.0f, isMe ? ImColor(GUI::g_colorAccent) : ImColor(150, 150, 160));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 15.0f);
                if (isMe)
                    ImGui::TextColored(GUI::g_colorAccent, "%s", user.c_str());
                else
                    ImGui::Text("%s", user.c_str());
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // Input + Send + "+"
    ImGui::SetNextItemWidth(chatWidth - 100.0f);
    bool inputSubmitted = ImGui::InputText("##irc_chat_input", s_messageInput, sizeof(s_messageInput), ImGuiInputTextFlags_EnterReturnsTrue);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive())
        ImGui::SetKeyboardFocusHere(-1);

    ImGui::SameLine();

    bool sendClicked = ImGui::Button("Send", ImVec2(55.0f, 26.0f));

    ImGui::SameLine();

    if (ImGui::Button("+", ImVec2(30.0f, 26.0f))) {
        // Decorative / Hint button
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Drag configurations from the right panel to share them.");
    }

    if ((inputSubmitted || sendClicked) && strlen(s_messageInput) > 0) {
        std::string rawMsg(s_messageInput);
        if (client.SendChannelMessage(rawMsg))
            s_messageInput[0] = '\0';
        else
            client.PushSystemMessage("Client", "Failed to send message. Not connected?");
        ImGui::SetKeyboardFocusHere(-1);
    }
}
