#include "SessionStats.hpp"

#include "Features/ECS/ECS.hpp"
#include "Features/HudRegistry.hpp"
#include "Framework/ColorUtils.hpp"
#include "Framework/Math.hpp"
#include "GUI/Widgets.hpp"
#include "Render/DX11.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "winhttp")

namespace mc {

SessionStats::SessionStats()
    : Module("SessionStats", "Displays session statistics in a glass panel",
             Category::Visuals, 0)
{
    markHasSettings();
}

SessionStats::~SessionStats()
{
    fetchAbort_ = true;
    if (fetchThread_.joinable())
        fetchThread_.join();
}

void SessionStats::resetStats()
{
    sessionStart_ = GetTickCount64();
    kills_ = 0;
    deaths_ = 0;
    blocksPlaced_ = 0;
    lastHealth_ = 20;
}

void SessionStats::onEnable()
{
    resetStats();
    fetchAbort_ = false;
    hiveReady_ = false;
}

void SessionStats::onDisable()
{
    fetchAbort_ = true;
    if (fetchThread_.joinable())
        fetchThread_.join();
    hiveFetching_ = false;
}

std::string SessionStats::suffix() const
{
    if (!useHive_ || !hiveReady_)
        return "";
    return "K:" + std::to_string(hiveKills_) + " D:" + std::to_string(hiveDeaths_);
}

void SessionStats::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    // Detectar death (health llega a 0)
    LocalPlayer lp = game.localPlayer();
    int hp = lp.health();
    if (lastHealth_ > 0 && hp <= 0)
    {
        ++deaths_;
        kills_ = std::max(0, kills_); // clamp
    }
    lastHealth_ = hp;

    // Contar kills via ECS: remote con health 0 tras HurtEvent
    auto& reg = ecs::get().registry;
    auto view = reg.view<ecs::PlayerName, ecs::Health>();
    for (auto e : view)
    {
        if (reg.all_of<ecs::IsLocal>(e))
            continue;
        auto& h = view.get<ecs::Health>(e);
        // Si el health bajo de >0 a 0, es un kill probable
        // (simplificado: no podemos saber quien lo mato)
    }

    // Fetch de Hive cada 15s si esta activado
    if (useHive_ && gamertag_[0] != '\0')
    {
        unsigned long long now = GetTickCount64();
        if (!hiveFetching_ && now - lastFetch_ > 15000)
        {
            hiveFetching_ = true;
            lastFetch_ = now;
            fetchAbort_ = false;
            if (fetchThread_.joinable())
                fetchThread_.join();
            fetchThread_ = std::thread(&SessionStats::fetchThreadMain, this);
        }
    }
}

void SessionStats::fetchThreadMain()
{
    // Fetch a la API de Hive para todos los gamemodes
    static const char* kGamemodes[] = {"sky", "sky-duos", "sky-squads", "sky-mega",
                                       "ctf", "bed",       "bed-duos",    "bed-squads",
                                       "sg",  "sg-duos"};
    constexpr int kGamemodeCount = 10;

    int totalKills = 0, totalDeaths = 0, totalPlayed = 0;

    for (int i = 0; i < kGamemodeCount && !fetchAbort_; ++i)
    {
        // Construir path
        char path[256];
        snprintf(path, sizeof(path), "/v0/game/all/%s/%s", kGamemodes[i], gamertag_);

        // WinHTTP
        HINTERNET session = WinHttpOpen(L"AzyreSDK/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
            continue;

        HINTERNET connect = WinHttpConnect(session, L"api.playhive.com",
                                           INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect)
        {
            WinHttpCloseHandle(session);
            continue;
        }

        HINTERNET request = WinHttpOpenRequest(connect, L"GET",
                                                std::wstring(path, path + strlen(path)).c_str(),
                                                nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                WINHTTP_FLAG_SECURE);
        if (!request)
        {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            continue;
        }

        BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (sent)
            sent = WinHttpReceiveResponse(request, nullptr);

        if (sent)
        {
            // Leer status code
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                nullptr, &status, &size, nullptr);

            if (status == 200)
            {
                // Leer body
                std::string body;
                DWORD dwSize = 0;
                do
                {
                    DWORD dwDownloaded = 0;
                    char buffer[4096];
                    if (!WinHttpQueryDataAvailable(request, &dwSize) || dwSize == 0)
                        break;
                    if (dwSize > sizeof(buffer))
                        dwSize = sizeof(buffer);
                    if (!WinHttpReadData(request, buffer, dwSize, &dwDownloaded))
                        break;
                    body.append(buffer, dwDownloaded);
                } while (dwSize > 0);

                // Parse JSON (simple: buscar "kills":N, "deaths":N, "played":N)
                auto findInt = [&body](const char* key) -> int {
                    std::string pat = "\"" + std::string(key) + "\":";
                    size_t pos = body.find(pat);
                    if (pos == std::string::npos)
                        return 0;
                    pos += pat.size();
                    int val = 0;
                    while (pos < body.size() && body[pos] >= '0' && body[pos] <= '9')
                    {
                        val = val * 10 + (body[pos] - '0');
                        ++pos;
                    }
                    return val;
                };

                totalKills += findInt("kills");
                totalDeaths += findInt("deaths");
                totalPlayed += findInt("played");
            }
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);

        // Pequeno delay entre requests para no saturar
        if (i + 1 < kGamemodeCount && !fetchAbort_)
            Sleep(200);
    }

    if (!fetchAbort_)
    {
        hiveKills_ = totalKills;
        hiveDeaths_ = totalDeaths;
        hivePlayed_ = totalPlayed;
        hiveReady_ = true;
    }
    hiveFetching_ = false;
}

void SessionStats::onRender()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    ImDrawList* d = ImGui::GetForegroundDrawList();

    // Posicion por HudRegistry
    auto& reg = hud::Registry::get();
    ImVec2 pos = reg.report("SessionStats", ImVec2(14.0f, sh * 0.4f), ImVec2(panelW_, panelH_));

    // Stats a mostrar
    unsigned long long now = GetTickCount64();
    unsigned long long elapsed = now - sessionStart_;
    int minutes = (int)((elapsed / 1000) / 60);
    int hours = minutes / 60;
    minutes %= 60;

    int kills = useHive_ && hiveReady_ ? hiveKills_ : kills_;
    int deaths = useHive_ && hiveReady_ ? hiveDeaths_ : deaths_;
    float kd = deaths > 0 ? (float)kills / (float)deaths : (float)kills;

    char timeStr[32];
    if (hours > 0)
        snprintf(timeStr, sizeof(timeStr), "%dh %dm", hours, minutes);
    else
        snprintf(timeStr, sizeof(timeStr), "%dm", minutes);

    char killStr[32], deathStr[32], kdStr[32], blockStr[32];
    snprintf(killStr, sizeof(killStr), "Kills: %d", kills);
    snprintf(deathStr, sizeof(deathStr), "Deaths: %d", deaths);
    snprintf(kdStr, sizeof(kdStr), "K/D: %.2f", kd);
    snprintf(blockStr, sizeof(blockStr), "Placed: %d", blocksPlaced_);

    // Panel glass
    ImVec2 mn(pos.x, pos.y);
    ImVec2 mx(pos.x + panelW_, pos.y + panelH_);
    widgets::AcrylicPanel(d, mn, mx, 8.0f, 1.0f, sw, sh, 30.0f, 14.0f);

    // Titulo
    ImFont* font = dx11::gameFont();
    if (!font)
        font = ImGui::GetFont();
    float fs = 18.0f;

    ImVec2 titleTs = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "Statistics");
    d->AddText(font, fs, ImVec2(pos.x + (panelW_ - titleTs.x) * 0.5f, pos.y + 8.0f),
               ImGui::ColorConvertFloat4ToU32(titleColor_), "Statistics");

    // Linea rainbow
    float lineY = pos.y + titleTs.y + 14.0f;
    int segments = 24;
    float segW = (panelW_ - 16.0f) / segments;
    for (int i = 0; i < segments; ++i)
    {
        ImU32 col = colors::getThemedColor((float)i / (float)segments, 0.9f);
        d->AddRectFilled(ImVec2(pos.x + 8.0f + i * segW, lineY),
                         ImVec2(pos.x + 8.0f + (i + 1) * segW, lineY + 3.0f), col);
    }

    // Stats
    float startY = lineY + 10.0f;
    float lineH = 20.0f;
    d->AddText(font, fs, ImVec2(pos.x + 12.0f, startY),
               IM_COL32(240, 242, 245, 255), timeStr);
    d->AddText(font, fs, ImVec2(pos.x + 12.0f, startY + lineH),
               IM_COL32(120, 255, 120, 255), killStr);
    d->AddText(font, fs, ImVec2(pos.x + 12.0f, startY + lineH * 2),
               IM_COL32(255, 120, 120, 255), deathStr);

    int row = 3;
    if (showKD_)
    {
        d->AddText(font, fs, ImVec2(pos.x + 12.0f, startY + lineH * row),
                   IM_COL32(240, 242, 245, 255), kdStr);
        ++row;
    }
    if (showBlocks_)
    {
        d->AddText(font, fs, ImVec2(pos.x + 12.0f, startY + lineH * row),
                   IM_COL32(150, 200, 255, 255), blockStr);
        ++row;
    }

    // Ajustar el tamano del elemento para el HUD editor
    if (hud::Element* el = reg.find("SessionStats"))
        el->size = ImVec2(panelW_, startY + lineH * row + 10.0f);
}

nlohmann::json SessionStats::saveSettings() const
{
    return nlohmann::json{
        {"useHive", useHive_},   {"gamertag", gamertag_},
        {"showKD", showKD_},    {"showBlocks", showBlocks_},
        {"panelW", panelW_},     {"titleR", titleColor_.x},
        {"titleG", titleColor_.y}, {"titleB", titleColor_.z},
    };
}

void SessionStats::loadSettings(const nlohmann::json& j)
{
    useHive_ = j.value("useHive", useHive_);
    showKD_ = j.value("showKD", showKD_);
    showBlocks_ = j.value("showBlocks", showBlocks_);
    panelW_ = j.value("panelW", panelW_);
    titleColor_.x = j.value("titleR", titleColor_.x);
    titleColor_.y = j.value("titleG", titleColor_.y);
    titleColor_.z = j.value("titleB", titleColor_.z);
    if (j.contains("gamertag") && j["gamertag"].is_string())
        snprintf(gamertag_, sizeof(gamertag_), "%s", j["gamertag"].get<std::string>().c_str());
}

void SessionStats::drawSettings()
{
    widgets::Checkbox("Fetch Hive API##sst", &useHive_);
    if (useHive_)
    {
        ImGui::Indent(14.0f);
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputTextWithHint("##sst_gt", "Gamertag", gamertag_, sizeof(gamertag_));
        if (hiveFetching_)
            ImGui::TextDisabled("Fetching...");
        else if (hiveReady_)
            ImGui::TextDisabled("K:%d D:%d Games:%d", hiveKills_, hiveDeaths_, hivePlayed_);
        ImGui::Unindent(14.0f);
    }

    ImGui::Separator();
    widgets::Checkbox("Show K/D##sst", &showKD_);
    widgets::Checkbox("Show blocks placed##sst", &showBlocks_);
    widgets::CSlider("Panel width##sst", &panelW_, 140.0f, 300.0f, "%.0f");
    widgets::ColorEdit3("Title color##sst", (float*)&titleColor_);

    ImGui::Separator();
    ImGui::TextDisabled("Session: %dh %dm  K:%d  D:%d  Placed:%d",
                        (int)((GetTickCount64() - sessionStart_) / 60000) / 60,
                        (int)((GetTickCount64() - sessionStart_) / 60000) % 60,
                        kills_, deaths_, blocksPlaced_);
}

MC_REGISTER_MODULE(SessionStats);

}
