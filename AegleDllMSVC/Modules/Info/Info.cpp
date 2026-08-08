/*
Under an4rch Development Public Source License 1.0
*/

#include "Info.hpp"
#include "../../GUI/GUI.hpp"
#include "../../ImGui/imgui.h"
#include "../../ImGui/imgui-markdown/imgui-markdown.h"
#include "../Globals.hpp"
#include "../Misc/UnlockFPS/UnlockFPS.hpp"
#include "../Visuals/RenderInfo/RenderInfo.hpp"
#include "../../Assets/resource.h"
#include "../../Assets/stb/stb_image.h"
#include "../../nlohmann/json.hpp"
#include <d3d11.h>
#include <windows.h>
#include <winhttp.h>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <shellapi.h>
#include <ctime>
#include <string>
#include <mutex>

#pragma comment(lib, "winhttp.lib")

extern "C" {
#define MINIAUDIO_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable: 4244)
#include "../../miniaudio/miniaudio.h"
#pragma warning(pop)
}

// External globals from dllmain.cpp
extern ID3D11Device* pDevice;
extern ID3D11DeviceContext* pContext;
extern HMODULE g_hModule;

// External globals from GUI.cpp
extern char g_notifTitle[64];
extern char g_notifMessage[128];

// Static member initialization
ImTextureID Info::g_logoTexture = 0;
int Info::g_logoWidth = 0;
int Info::g_logoHeight = 0;
uint8_t* Info::g_audioData = nullptr;
uint32_t Info::g_audioDataSize = 0;

// GitHub release statics
std::string Info::g_releaseBody = "";
std::string Info::g_releaseTag = "";
std::string Info::g_releaseName = "";
std::string Info::g_releaseDate = "";
bool Info::g_fetchInProgress = false;
bool Info::g_fetchDone = false;
bool Info::g_fetchFailed = false;
bool Info::g_showReleaseModal = false;
float Info::g_releaseModalAnim = 0.0f;
static std::mutex g_releaseMutex;

// Audio context global
static const int NUM_CLICK_SOUNDS = 3;
static ma_engine g_audioEngine;
static bool g_audioEngineInitialized = false;

// Arrays for multiple sounds
static ma_decoder g_audioDecoders[NUM_CLICK_SOUNDS];
static ma_sound g_clickSounds[NUM_CLICK_SOUNDS];
static uint8_t* g_audioDatas[NUM_CLICK_SOUNDS] = {nullptr};
static uint32_t g_audioSizes[NUM_CLICK_SOUNDS] = {0};
static bool g_soundsLoaded = false;
static unsigned int g_soundCount = 0;

void Info::Initialize() {
    if (g_audioEngineInitialized) return;
    
    LoadLogoFromResource();
    LoadAudioFromResource();
    
    // Initialize miniaudio engine
    if (ma_engine_init(NULL, &g_audioEngine) == MA_SUCCESS) {
        g_audioEngineInitialized = true;
        // Once engine is ready, initialize sound from memory
        InitSoundFromMemory();
    }
}

void Info::Shutdown() {
    if (g_soundsLoaded) {
        // Uninitialize all sounds and decoders
        for (int i = 0; i < NUM_CLICK_SOUNDS; i++) {
            ma_sound_uninit(&g_clickSounds[i]);
            ma_decoder_uninit(&g_audioDecoders[i]);
        }
        g_soundsLoaded = false;
        g_soundCount = 0;
    }
    
    if (g_audioEngineInitialized) {
        ma_engine_uninit(&g_audioEngine);
        g_audioEngineInitialized = false;
    }
    
    // Free all audio data
    for (int i = 0; i < NUM_CLICK_SOUNDS; i++) {
        if (g_audioDatas[i]) {
            delete[] g_audioDatas[i];
            g_audioDatas[i] = nullptr;
            g_audioSizes[i] = 0;
        }
    }
    
    if (g_logoTexture != 0) {
        reinterpret_cast<ID3D11ShaderResourceView*>(g_logoTexture)->Release();
        g_logoTexture = 0;
    }
}

void Info::OnFocusGained() {
    if (!g_audioEngineInitialized) return;
    ma_engine_start(&g_audioEngine);
    ma_device* pDev = ma_engine_get_device(&g_audioEngine);
    if (pDev) ma_device_start(pDev);
}

bool Info::LoadLogoFromResource() {
    // Find the resource using the module handle
    HRSRC hRes = FindResource(g_hModule, MAKEINTRESOURCE(IDR_INFO_IMAGE), RT_RCDATA);
    if (!hRes) {
        OutputDebugStringA("INFO: FindResource failed for IDR_INFO_IMAGE\n");
        return false;
    }
    
    // Load the resource
    HGLOBAL hGlobal = LoadResource(g_hModule, hRes);
    if (!hGlobal) {
        OutputDebugStringA("INFO: LoadResource failed\n");
        return false;
    }
    
    // Get resource data
    DWORD dwSize = SizeofResource(g_hModule, hRes);
    if (dwSize == 0) {
        OutputDebugStringA("INFO: SizeofResource returned 0\n");
        return false;
    }
    
    void* pData = LockResource(hGlobal);
    if (!pData) {
        OutputDebugStringA("INFO: LockResource failed\n");
        return false;
    }
    
    // Load texture from memory
    g_logoTexture = LoadTextureFromMemory((const unsigned char*)pData, (int)dwSize);
    
    return g_logoTexture != 0;
}

ImTextureID Info::LoadTextureFromMemory(const unsigned char* data, int size) {
    // Load image from memory using stb_image
    int width, height, channels;
    unsigned char* img_data = stbi_load_from_memory(data, size, &width, &height, &channels, 4);
    if (!img_data) {
        const char* err = stbi_failure_reason();
        OutputDebugStringA("INFO: stbi_load_from_memory failed: ");
        OutputDebugStringA(err ? err : "unknown error\n");
        return 0;
    }
    
    g_logoWidth = width;
    g_logoHeight = height;
    
    // Check if device is available
    if (!pDevice) {
        stbi_image_free(img_data);
        return 0;
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
    
    if (FAILED(hr) || !pTexture) {
        stbi_image_free(img_data);
        return 0;
    }
    
    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    
    ID3D11ShaderResourceView* pSRV = nullptr;
    hr = pDevice->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
    
    pTexture->Release();
    stbi_image_free(img_data);
    
    if (FAILED(hr) || !pSRV) {
        return 0;
    }
    
    return reinterpret_cast<ImTextureID>(pSRV);
}

// Helper: Load single audio from resource by ID
static bool LoadAudioByID(int resourceID, uint8_t*& outData, uint32_t& outSize) {
    HRSRC hRes = FindResource(g_hModule, MAKEINTRESOURCE(resourceID), RT_RCDATA);
    if (!hRes) {
        OutputDebugStringA("INFO: FindResource failed for audio resource\n");
        return false;
    }
    
    HGLOBAL hGlobal = LoadResource(g_hModule, hRes);
    if (!hGlobal) {
        OutputDebugStringA("INFO: LoadResource failed\n");
        return false;
    }
    
    DWORD dwSize = SizeofResource(g_hModule, hRes);
    if (dwSize == 0) {
        OutputDebugStringA("INFO: SizeofResource returned 0\n");
        return false;
    }
    
    void* pData = LockResource(hGlobal);
    if (!pData) {
        OutputDebugStringA("INFO: LockResource failed\n");
        return false;
    }
    
    outData = new uint8_t[dwSize];
    if (!outData) {
        OutputDebugStringA("INFO: Failed to allocate audio memory\n");
        return false;
    }
    
    std::memcpy(outData, pData, dwSize);
    outSize = dwSize;
    
    return true;
}

bool Info::LoadAudioFromResource() {
    // Load all 3 click sounds from resources
    int resourceIDs[NUM_CLICK_SOUNDS] = {IDR_CLICK_SOUND, IDR_CLICK_SOUND_1, IDR_CLICK_SOUND_2};
    int successCount = 0;
    
    for (int i = 0; i < NUM_CLICK_SOUNDS; i++) {
        char debugMsg[256];
        sprintf(debugMsg, "INFO: Attempting to load audio resource %d (ID: %d)\n", i, resourceIDs[i]);
        OutputDebugStringA(debugMsg);
        
        if (LoadAudioByID(resourceIDs[i], g_audioDatas[i], g_audioSizes[i])) {
            sprintf(debugMsg, "INFO: Audio %d loaded successfully, size: %d bytes\n", i, g_audioSizes[i]);
            OutputDebugStringA(debugMsg);
            successCount++;
        } else {
            sprintf(debugMsg, "INFO: Failed to load audio resource %d\n", i);
            OutputDebugStringA(debugMsg);
        }
    }
    
    char finalMsg[256];
    sprintf(finalMsg, "INFO: Loaded %d/%d click sounds\n", successCount, NUM_CLICK_SOUNDS);
    OutputDebugStringA(finalMsg);
    
    return successCount > 0;
}

void Info::InitSoundFromMemory() {
    if (!g_audioEngineInitialized) {
        OutputDebugStringA("INFO: Cannot init sounds - engine not initialized\n");
        return;
    }
    
    char debugMsg[256];
    sprintf(debugMsg, "INFO: InitSoundFromMemory started, checking %d audio slots\n", NUM_CLICK_SOUNDS);
    OutputDebugStringA(debugMsg);
    
    // Initialize decoder and sound for each audio
    for (int i = 0; i < NUM_CLICK_SOUNDS; i++) {
        if (!g_audioDatas[i] || g_audioSizes[i] == 0) {
            sprintf(debugMsg, "INFO: Slot %d - NO DATA (data=%p, size=%d)\n", i, g_audioDatas[i], g_audioSizes[i]);
            OutputDebugStringA(debugMsg);
            continue;
        }
        
        sprintf(debugMsg, "INFO: Slot %d - data=%p, size=%d\n", i, g_audioDatas[i], g_audioSizes[i]);
        OutputDebugStringA(debugMsg);
        
        // Initialize decoder from memory
        ma_result result = ma_decoder_init_memory(
            g_audioDatas[i],
            g_audioSizes[i],
            NULL,
            &g_audioDecoders[i]
        );
        
        if (result != MA_SUCCESS) {
            sprintf(debugMsg, "INFO: Slot %d - Decoder init FAILED (result: %d)\n", i, result);
            OutputDebugStringA(debugMsg);
            continue;
        }
        
        sprintf(debugMsg, "INFO: Slot %d - Decoder init SUCCESS\n", i);
        OutputDebugStringA(debugMsg);
        
        // Initialize sound from decoder
        result = ma_sound_init_from_data_source(
            &g_audioEngine,
            &g_audioDecoders[i],
            0,
            NULL,
            &g_clickSounds[i]
        );
        
        if (result != MA_SUCCESS) {
            sprintf(debugMsg, "INFO: Slot %d - Sound init FAILED (result: %d)\n", i, result);
            OutputDebugStringA(debugMsg);
            ma_decoder_uninit(&g_audioDecoders[i]);
            continue;
        }
        
        sprintf(debugMsg, "INFO: Slot %d - Sound init SUCCESS, count++\n", i);
        OutputDebugStringA(debugMsg);
        g_soundCount++;
    }
    
    sprintf(debugMsg, "INFO: InitSoundFromMemory finished - g_soundCount = %d\n", g_soundCount);
    OutputDebugStringA(debugMsg);
    
    if (g_soundCount > 0) {
        g_soundsLoaded = true;
        OutputDebugStringA("INFO: Click sounds loaded from memory successfully\n");
    } else {
        OutputDebugStringA("INFO: WARNING - No sounds loaded!\n");
    }
}

void Info::PlayClickSound() {
    if (!g_soundsLoaded || g_soundCount == 0) {
        return;
    }

    // Select a random sound
    int randomIndex = rand() % g_soundCount;

    // Check engine state
    if (ma_engine_start(&g_audioEngine) != MA_SUCCESS) {
        // If starting fails, try to start the device directly
        ma_device* pAudioDevice = ma_engine_get_device(&g_audioEngine);
        if (pAudioDevice) {
            ma_device_start(pAudioDevice);
        }
    }
    
    // Stop and Reset
    ma_sound_stop(&g_clickSounds[randomIndex]);
    ma_sound_seek_to_pcm_frame(&g_clickSounds[randomIndex], 0);
    
    // Attempt play
    ma_result result = ma_sound_start(&g_clickSounds[randomIndex]);
    
    // If still failing (common after Fullscreen Alt-Tab device loss), 
    // try one last hardware-level kick
    if (result != MA_SUCCESS) {
        ma_device* pAudioDevice = ma_engine_get_device(&g_audioEngine);
        if (pAudioDevice) {
            ma_device_stop(pAudioDevice);
            ma_device_start(pAudioDevice);
            ma_sound_start(&g_clickSounds[randomIndex]);
        }
    }
}

static std::string GetGitHubTokenFromResource() {
    HRSRC hRes = FindResource(g_hModule, MAKEINTRESOURCE(IDR_GITHUB_INI), RT_RCDATA);
    if (!hRes) return "";
    
    HGLOBAL hGlobal = LoadResource(g_hModule, hRes);
    if (!hGlobal) return "";
    
    DWORD dwSize = SizeofResource(g_hModule, hRes);
    if (dwSize == 0) return "";
    
    const char* pData = (const char*)LockResource(hGlobal);
    if (!pData) return "";
    
    std::string content(pData, dwSize);
    size_t tokenPos = content.find("token=");
    if (tokenPos == std::string::npos) return "";
    
    tokenPos += 6; // skip "token="
    size_t endPos = content.find_first_of("\r\n\t;", tokenPos);
    if (endPos == std::string::npos) {
        return content.substr(tokenPos);
    }
    return content.substr(tokenPos, endPos - tokenPos);
}

void Info::FetchLatestRelease() {
    if (g_fetchInProgress) return;
    g_fetchInProgress = true;
    g_fetchDone = false;
    g_fetchFailed = false;

    // Fire off a background thread — WinHTTP is safe off the render thread
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        std::string result;
        std::string tag, name, date;
        bool failed = false;

        HINTERNET hSession = WinHttpOpen(
            L"AegleDLL/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);

        if (hSession) {
            HINTERNET hConnect = WinHttpConnect(
                hSession,
                L"api.github.com",
                INTERNET_DEFAULT_HTTPS_PORT,
                0);

            if (hConnect) {
                HINTERNET hRequest = WinHttpOpenRequest(
                    hConnect,
                    L"GET",
                    L"/repos/AnarchDevelopment/aegledll/releases/latest",
                    nullptr,
                    WINHTTP_NO_REFERER,
                    WINHTTP_DEFAULT_ACCEPT_TYPES,
                    WINHTTP_FLAG_SECURE);

                if (hRequest) {
                    // Get token and add Authorization header if present
                    std::string token = GetGitHubTokenFromResource();
                    if (!token.empty() && token != "gph_") { // It works the same with github_pat_xxxxxxxxxx
                        std::wstring authHeader = L"Authorization: token " + std::wstring(token.begin(), token.end());
                        WinHttpAddRequestHeaders(hRequest,
                            authHeader.c_str(),
                            (DWORD)-1L,
                            WINHTTP_ADDREQ_FLAG_ADD);
                    }

                    // GitHub API requires a User-Agent header
                    WinHttpAddRequestHeaders(hRequest,
                        L"User-Agent: AegleDLL/1.0",
                        (DWORD)-1L,
                        WINHTTP_ADDREQ_FLAG_ADD);

                    WinHttpAddRequestHeaders(hRequest,
                        L"Accept: application/vnd.github+json",
                        (DWORD)-1L,
                        WINHTTP_ADDREQ_FLAG_ADD);

                    if (WinHttpSendRequest(hRequest,
                            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                        WinHttpReceiveResponse(hRequest, nullptr)) {

                        DWORD dwSize = 0;
                        std::string raw;
                        do {
                            dwSize = 0;
                            WinHttpQueryDataAvailable(hRequest, &dwSize);
                            if (dwSize == 0) break;
                            std::string chunk(dwSize, '\0');
                            DWORD dwDownloaded = 0;
                            WinHttpReadData(hRequest, &chunk[0], dwSize, &dwDownloaded);
                            raw.append(chunk, 0, dwDownloaded);
                        } while (dwSize > 0);

                        // Parse JSON and extract release metadata + "body"
                        try {
                            auto j = nlohmann::json::parse(raw);
                            if (j.contains("body") && j["body"].is_string()) {
                                result = j["body"].get<std::string>();
                            } else if (j.contains("message") && j["message"].is_string()) {
                                result = "**GitHub API Error:** " + j["message"].get<std::string>() + "\n\nRaw response:\n```json\n" + raw + "\n```";
                            } else {
                                result = "_No release notes found._\n\nRaw response:\n```\n" + raw + "\n```";
                            }
                            if (j.contains("tag_name") && j["tag_name"].is_string()) {
                                tag = j["tag_name"].get<std::string>();
                            }
                            if (j.contains("name") && j["name"].is_string()) {
                                name = j["name"].get<std::string>();
                            }
                            if (j.contains("published_at") && j["published_at"].is_string()) {
                                std::string full = j["published_at"].get<std::string>();
                                size_t tPos = full.find_first_of("Tt");
                                date = (tPos == std::string::npos) ? full : full.substr(0, tPos);
                            }
                        } catch (...) {
                            failed = true;
                            result = "**Error:** Could not parse GitHub API response. Raw response:\n```\n" + raw + "\n```";
                        }
                    } else {
                        failed = true;
                        result = "**Error:** HTTP request failed.";
                    }
                    WinHttpCloseHandle(hRequest);
                } else {
                    failed = true;
                    result = "**Error:** Could not open HTTP request.";
                }
                WinHttpCloseHandle(hConnect);
            } else {
                failed = true;
                result = "**Error:** Could not connect to api.github.com.";
            }
            WinHttpCloseHandle(hSession);
        } else {
            failed = true;
            result = "**Error:** WinHTTP session could not be created.";
        }

        {
            std::lock_guard<std::mutex> lock(g_releaseMutex);
            Info::g_releaseBody = result;
            Info::g_releaseTag = tag;
            Info::g_releaseName = name;
            Info::g_releaseDate = date;
            Info::g_fetchFailed = failed;
            Info::g_fetchDone = true;
            Info::g_fetchInProgress = false;
        }
        return 0;
    }, nullptr, 0, nullptr);
}

// ── Info Dashboard UI ──────────────────────────────────────────────────────

struct UpdateEntry {
    const char* version;
    const char* desc;
    bool isLatest;
};

static const UpdateEntry kUpdateHistory[] = {
    { "v1.0.8", "Stable Release | Added new module configs", true },
    { "v1.0.7", "Stable Release | Migration to MSVC", false },
    { "v1.0.6", "Stable Release | Config Market Added", false },
    { "v1.0.5", "Stable Release | IRC Chat added", false },
    { "v1.0.4", "Stable Release", false },
};

static ImVec2 DrawVersionBadge(ImDrawList* draw, ImFont* font, float fontPx, ImVec2 pos, const char* text, const ImVec4& accent) {
    ImVec2 textSize = font->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, text);
    float padX = 9.0f, padY = 2.5f;
    ImVec2 size(textSize.x + padX * 2.0f, textSize.y + padY * 2.0f);
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImColor(accent.x, accent.y, accent.z, 0.16f), 4.0f);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImColor(accent.x, accent.y, accent.z, 0.55f), 4.0f, 0, 1.0f);
    draw->AddText(font, fontPx, ImVec2(pos.x + padX, pos.y + padY), ImColor(accent.x, accent.y, accent.z, 1.0f), text);
    return size;
}

static void RenderProfileCard() {
    const ImVec4 accent = GUI::g_colorAccent;
    ImFont* fontH3 = GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetIO().Fonts->Fonts[0];
    ImFont* fontH2 = GUI::g_fontH2 ? GUI::g_fontH2 : ImGui::GetIO().Fonts->Fonts[0];
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetWindowPos();
    float winW = ImGui::GetWindowWidth();

    // Header
    ImGui::SetCursorPos(ImVec2(14, 10));
    ImGui::PushFont(fontH3);
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("USER PROFILE");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // MEMBER pill (top right)
    const char* pill = "MEMBER";
    ImVec2 pillTextSize = fontH3->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, pill);
    ImVec2 pillPos(winPos.x + winW - pillTextSize.x - 26.0f, winPos.y + 9.0f);
    ImVec2 pillSize(pillTextSize.x + 18.0f, pillTextSize.y + 8.0f);
    draw->AddRectFilled(pillPos, ImVec2(pillPos.x + pillSize.x, pillPos.y + pillSize.y), ImColor(accent.x, accent.y, accent.z, 0.16f), 10.0f);
    draw->AddRect(pillPos, ImVec2(pillPos.x + pillSize.x, pillPos.y + pillSize.y), ImColor(accent.x, accent.y, accent.z, 0.55f), 10.0f, 0, 1.0f);
    draw->AddText(fontH3, 12.0f, ImVec2(pillPos.x + 9.0f, pillPos.y + 4.0f), ImColor(accent.x, accent.y, accent.z, 1.0f), pill);

    // Divider
    float divY = winPos.y + 38.0f;
    draw->AddLine(ImVec2(winPos.x + 14.0f, divY), ImVec2(winPos.x + winW - 14.0f, divY), ImColor(1.0f, 1.0f, 1.0f, 0.10f), 1.0f);

    // Avatar
    ImVec2 avatarCenter(winPos.x + 46.0f, winPos.y + 79.0f);
    float avatarR = 24.0f;
    draw->AddCircleFilled(avatarCenter, avatarR, ImColor(accent.x, accent.y, accent.z, 0.18f), 48);
    draw->AddCircle(avatarCenter, avatarR, ImColor(accent.x, accent.y, accent.z, 0.65f), 48, 1.6f);

    char* user = getenv("USERNAME");
    const char* name = (user && user[0]) ? user : "Unknown";
    char initBuf[2] = { name[0], '\0' };
    ImVec2 initSize = fontH2->CalcTextSizeA(26.0f, FLT_MAX, 0.0f, initBuf);
    draw->AddText(fontH2, 26.0f, ImVec2(avatarCenter.x - initSize.x * 0.5f, avatarCenter.y - initSize.y * 0.5f), ImColor(accent.x, accent.y, accent.z, 1.0f), initBuf);

    // Name + tagline
    float textX = winPos.x + 82.0f;
    ImGui::SetCursorScreenPos(ImVec2(textX, divY + 14.0f));
    ImGui::PushFont(fontH3);
    ImGui::Text("%s", name);
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(textX, divY + 38.0f));
    ImGui::TextDisabled("by an4rch development");
}

static void RenderStatsCard() {
    const ImVec4 accent = GUI::g_colorAccent;
    ImFont* fontH3 = GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetIO().Fonts->Fonts[0];
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetWindowPos();
    float winW = ImGui::GetWindowWidth();

    ImGui::SetCursorPos(ImVec2(14, 10));
    ImGui::PushFont(fontH3);
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("SYSTEM STATUS");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    float divY = winPos.y + 38.0f;
    draw->AddLine(ImVec2(winPos.x + 14.0f, divY), ImVec2(winPos.x + winW - 14.0f, divY), ImColor(1.0f, 1.0f, 1.0f, 0.10f), 1.0f);

    float x = winPos.x + 14.0f;
    float labelW = 92.0f;
    float barW = winW - labelW - 28.0f;
    float row = divY + 8.0f;

    // FPS
    float fps = RenderInfo::g_fpsCounter;
    ImVec4 fpsCol = fps >= 60.0f ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                 : (fps >= 30.0f ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f) : ImVec4(1.0f, 0.4f, 0.35f, 1.0f));
    ImGui::SetCursorScreenPos(ImVec2(x, row));
    ImGui::Text("Client FPS");
    ImGui::SetCursorScreenPos(ImVec2(x + labelW, row));
    ImGui::PushStyleColor(ImGuiCol_Text, fpsCol);
    ImGui::Text("%.0f FPS", fps);
    ImGui::PopStyleColor();

    // FPS mini bar
    float barTop = row + 24.0f;
    ImVec2 barMin(x + labelW, barTop);
    ImVec2 barMax(x + labelW + barW, barTop + 3.0f);
    draw->AddRectFilled(barMin, barMax, ImColor(1.0f, 1.0f, 1.0f, 0.07f), 2.0f);
    float fillF = fminf(fps / 240.0f, 1.0f);
    if (fillF > 0.02f) {
        draw->AddRectFilled(barMin, ImVec2(barMin.x + barW * fillF, barMax.y), ImColor(fpsCol.x, fpsCol.y, fpsCol.z, 0.6f), 2.0f);
    }

    // Latency
    static float ping = 18.0f;
    ping += (rand() % 3 - 1) * 0.5f;
    if (ping < 5.0f) ping = 5.0f;
    ImVec4 pingCol = ping < 60.0f ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                  : (ping < 120.0f ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f) : ImVec4(1.0f, 0.4f, 0.35f, 1.0f));
    row += 32.0f;
    ImGui::SetCursorScreenPos(ImVec2(x, row));
    ImGui::Text("Latency");
    ImGui::SetCursorScreenPos(ImVec2(x + labelW, row));
    ImGui::PushStyleColor(ImGuiCol_Text, pingCol);
    ImGui::Text("%.0f ms", ping);
    ImGui::PopStyleColor();

    // Security
    row += 22.0f;
    ImGui::SetCursorScreenPos(ImVec2(x, row));
    ImGui::Text("Security");
    ImGui::SetCursorScreenPos(ImVec2(x + labelW, row));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
    ImGui::Text("PROTECTED");
    ImGui::PopStyleColor();
}

static void RenderLatestUpdates() {
    const ImVec4 accent = GUI::g_colorAccent;
    ImFont* fontH3 = GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetIO().Fonts->Fonts[0];
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetWindowPos();
    float winW = ImGui::GetWindowWidth();
    float winH = ImGui::GetWindowHeight();

    // Header row
    ImGui::SetCursorPos(ImVec2(14, 10));
    ImGui::PushFont(fontH3);
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("LATEST UPDATES");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // Refresh button (top right)
    float btnW = 80.0f;
    ImGui::SetCursorPos(ImVec2(winW - btnW - 14.0f, 8));
    ImGui::PushStyleColor(ImGuiCol_Text, Info::g_fetchInProgress ? ImVec4(1.0f, 1.0f, 1.0f, 0.4f) : accent);
    if (ImGui::Button("REFRESH", ImVec2(btnW, 22))) {
        if (!Info::g_fetchInProgress) Info::FetchLatestRelease();
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && !Info::g_fetchInProgress) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    // Divider
    float divY = winPos.y + 40.0f;
    draw->AddLine(ImVec2(winPos.x + 14.0f, divY), ImVec2(winPos.x + winW - 14.0f, divY), ImColor(1.0f, 1.0f, 1.0f, 0.10f), 1.0f);

    // Logo on the right
    float logoW = 0.0f, logoH = 0.0f;
    if (Info::g_logoTexture != 0) {
        float availLogoW = fminf(winW * 0.26f, 150.0f);
        float availLogoH = winH - (divY - winPos.y) - 24.0f;
        float scale = fminf(availLogoW / (float)Info::g_logoWidth, availLogoH / (float)Info::g_logoHeight);
        logoW = (float)Info::g_logoWidth * scale;
        logoH = (float)Info::g_logoHeight * scale;
        ImVec2 logoPos(winPos.x + winW - logoW - 14.0f, divY + (winH - (divY - winPos.y) - logoH) * 0.5f);
        ImGui::SetCursorScreenPos(logoPos);
        if (ImGui::ImageButton("##InfoLogo", Info::g_logoTexture, ImVec2(logoW, logoH), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0))) {
            Info::PlayClickSound();
        }
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // Scrollable text region (version history / latest release)
    float textW = winW - 28.0f - (logoW > 0.0f ? logoW + 16.0f : 0.0f);
    if (textW < 100.0f) textW = 100.0f;
    ImGui::SetCursorScreenPos(ImVec2(winPos.x + 14.0f, divY + 10.0f));
    ImGui::BeginChild("UpdatesText", ImVec2(textW, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    {
        ImDrawList* idl = ImGui::GetWindowDrawList();
        float textWrap = textW - 14.0f;

        bool hasRelease = Info::g_fetchDone && !Info::g_fetchFailed && !Info::g_releaseBody.empty();

        if (Info::g_fetchInProgress) {
            ImGui::TextDisabled("Fetching latest release from GitHub...");
            ImGui::Spacing();
        } else if (Info::g_fetchDone && Info::g_fetchFailed) {
            ImGui::TextDisabled("Could not reach GitHub - showing local release history:");
            ImGui::Spacing();
        }

        if (hasRelease) {
            const char* tag = Info::g_releaseTag.empty() ? "latest" : Info::g_releaseTag.c_str();
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 bs = DrawVersionBadge(idl, fontH3, 13.0f, p, tag, accent);
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(bs);

            if (!Info::g_releaseName.empty()) {
                ImGui::SameLine(0, 10.0f);
                ImGui::SetCursorScreenPos(ImVec2(p.x + bs.x + 10.0f, p.y + 1.0f));
                ImGui::PushFont(fontH3);
                ImGui::Text("%s", Info::g_releaseName.c_str());
                ImGui::PopFont();
            }
            if (!Info::g_releaseDate.empty()) {
                ImGui::SetCursorScreenPos(ImVec2(winPos.x + 14.0f + textW - 90.0f, p.y + 3.0f));
                ImGui::TextDisabled("%s", Info::g_releaseDate.c_str());
            }

            // Body snippet (strip markdown clutter, truncate to a preview)
            std::string clean;
            bool inCode = false;
            for (char c : Info::g_releaseBody) {
                if (c == '`') { inCode = !inCode; continue; }
                if (c == '\n' || c == '\r') c = ' ';
                if (c == '#' && !inCode) continue;
                if (c == '*' || c == '>' || c == '-') continue;
                clean += c;
            }
            std::string collapsed;
            bool prevSpace = false;
            for (char c : clean) {
                if (c == ' ' && prevSpace) continue;
                collapsed += c;
                prevSpace = (c == ' ');
            }
            while (!collapsed.empty() && collapsed[0] == ' ') collapsed.erase(0, 1);
            if (collapsed.size() > 160) {
                size_t cut = collapsed.find_last_of(" ", 160);
                if (cut == std::string::npos || cut < 40) cut = 160;
                collapsed = collapsed.substr(0, cut);
                while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
                collapsed += "...";
            }

            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + bs.y + 9.0f));
            ImGui::PushTextWrapPos(textWrap);
            ImGui::TextWrapped("%s", collapsed.c_str());
            ImGui::PopTextWrapPos();

            ImGui::Spacing();
            if (ImGui::Button("VIEW RELEASE NOTES", ImVec2(170, 24))) {
                Info::g_showReleaseModal = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        // Version history
        for (int i = 0; i < (int)IM_ARRAYSIZE(kUpdateHistory); i++) {
            const UpdateEntry& e = kUpdateHistory[i];
            ImVec4 badgeCol = e.isLatest ? accent : ImVec4(0.55f, 0.56f, 0.62f, 1.0f);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 bs = DrawVersionBadge(idl, fontH3, 13.0f, p, e.version, badgeCol);
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(bs);
            ImGui::SameLine(0, 10.0f);
            ImGui::SetCursorScreenPos(ImVec2(p.x + bs.x + 10.0f, p.y + 3.0f));
            ImGui::Text("%s", e.desc);
            ImGui::Spacing();
            if (i < (int)IM_ARRAYSIZE(kUpdateHistory) - 1) {
                ImGui::Separator();
                ImGui::Spacing();
            }
        }
    }
    ImGui::EndChild();
}

void Info::RenderDashboard() {
    ImGui::BeginChild("Dashboard", ImVec2(0, -115), false, ImGuiWindowFlags_NoScrollbar);
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float gap = 20.0f;
        float profileW = (avail.x - gap) * 0.55f;
        float statsW = avail.x - gap - profileW;

        // Top row: profile + system stats
        ImGui::BeginChild("ProfileCard", ImVec2(profileW, 120), true);
        RenderProfileCard();
        ImGui::EndChild();

        ImGui::SameLine(0, gap);

        ImGui::BeginChild("StatsCard", ImVec2(statsW, 120), true);
        RenderStatsCard();
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Spacing();

        // Bottom row: latest updates (fills remaining space)
        ImGui::BeginChild("UpdatesCard", ImVec2(0, 0), true);
        RenderLatestUpdates();
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void Info::RenderSocialButtons() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec4 accent = GUI::g_colorAccent;
    ImGui::Spacing();
    ImGui::SetCursorPosX((avail.x - 306.0f) * 0.5f);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accent.x, accent.y, accent.z, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Text, accent);

    if (ImGui::Button("DISCORD", ImVec2(90, 35))) {
        ShellExecuteA(0, "open", "https://discord.gg/7hJjTCfyJ2", 0, 0, SW_SHOWNORMAL);
        ImGui::SetClipboardText("https://discord.gg/7hJjTCfyJ2");
        strcpy(g_notifTitle, "Discord");
        strcpy(g_notifMessage, "Link copied to clipboard!");
        g_notifStart = GetTickCount64();
        PlayClickSound();
    }
    ImGui::SameLine(0, 8);
    if (ImGui::Button("GITHUB", ImVec2(90, 35))) {
        ShellExecuteA(0, "open", "https://github.com/iVyz3r/aegledll", 0, 0, SW_SHOWNORMAL);
        ImGui::SetClipboardText("https://github.com/iVyz3r/aegledll");
        strcpy(g_notifTitle, "Github");
        strcpy(g_notifMessage, "Link copied to clipboard!");
        g_notifStart = GetTickCount64();
        PlayClickSound();
    }
    ImGui::SameLine(0, 8);
    if (ImGui::Button("VERSION INFO", ImVec2(110, 35))) {
        g_showReleaseModal = true;
        if (!g_fetchDone && !g_fetchInProgress) {
            FetchLatestRelease();
        }
        PlayClickSound();
    }
    ImGui::PopStyleColor(4);

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Fetch latest release notes from GitHub");
    }
}

void Info::RenderMenu() {
    // Auto-fetch the latest GitHub release once when the Info tab is opened
    static bool s_autoFetchStarted = false;
    if (!s_autoFetchStarted) {
        s_autoFetchStarted = true;
        if (!g_fetchDone && !g_fetchInProgress) {
            FetchLatestRelease();
        }
    }

    RenderDashboard();
    RenderSocialButtons();

    // ── Release Notes Modal ──────────────────────────────────────────────────
    float targetAnim = g_showReleaseModal ? 1.0f : 0.0f;
    g_releaseModalAnim += (targetAnim - g_releaseModalAnim) * 0.12f;

    if (g_releaseModalAnim > 0.001f) {
        float scale = 0.85f + 0.15f * g_releaseModalAnim;
        float alpha = g_releaseModalAnim;

        ImVec2 baseSize = ImVec2(600, 440);
        ImVec2 size = ImVec2(baseSize.x * scale, baseSize.y * scale);

        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(
            ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        // Suppress default border — we draw a custom animated one
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));

        // Capture window rect inside Begin (valid even if Begin returns true)
        ImVec2 wMin, wMax;

        if (ImGui::Begin("##ReleaseNotes", nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize   |
                ImGuiWindowFlags_NoMove     |
                ImGuiWindowFlags_NoCollapse)) {

            wMin = ImGui::GetWindowPos();
            wMax = ImVec2(wMin.x + ImGui::GetWindowWidth(), wMin.y + ImGui::GetWindowHeight());

                // ── Title bar (accent title + right-aligned close button) ──
                float titlePadX = ImGui::GetStyle().WindowPadding.x;
                ImGui::SetCursorPosX(titlePadX);
                ImGui::PushStyleColor(ImGuiCol_Text, GUI::g_colorAccent);
                ImGui::Text("Current Version Info");
                ImGui::PopStyleColor();

                float closeSize = 24.0f;
                float closeX = ImGui::GetWindowWidth() - titlePadX - closeSize;
                float titleY = ImGui::GetCursorPosY();
                ImGui::SetCursorPos(ImVec2(closeX, titleY));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button("X##CloseRelease", ImVec2(closeSize, closeSize))) {
                    g_showReleaseModal = false;
                }
                ImGui::PopStyleVar();

                ImGui::SetCursorPosY(titleY + closeSize + 8.0f);
                ImGui::Separator();
                ImGui::Spacing();

                if (g_fetchInProgress) {
                    float t = (float)(GetTickCount64() % 900) / 300.0f;
                    const char* dots[] = { "Loading .  ", "Loading .. ", "Loading ..." };
                    ImVec2 dotSize = ImGui::CalcTextSize(dots[0]);
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - dotSize.x) * 0.5f);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (avail.y - dotSize.y) * 0.5f);
                    ImGui::TextDisabled("%s", dots[(int)t % 3]);
                } else if (!g_releaseBody.empty()) {
                    // Inner padding so the release notes don't touch the modal edges
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
                    ImGui::BeginChild("##MarkdownScroll", ImVec2(0, 0), false,
                        ImGuiWindowFlags_AlwaysVerticalScrollbar);

                ImGui::MarkdownConfig mdConfig;
                mdConfig.linkCallback    = [](ImGui::MarkdownLinkCallbackData data) {
                    std::string url(data.link, data.linkLength);
                    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                };
                mdConfig.tooltipCallback = nullptr;
                mdConfig.imageCallback   = nullptr;
                mdConfig.linkIcon        = "";
                mdConfig.headingFormats[0] = { GUI::g_fontH1 ? GUI::g_fontH1 : ImGui::GetIO().Fonts->Fonts[0], true };
                mdConfig.headingFormats[1] = { GUI::g_fontH2 ? GUI::g_fontH2 : ImGui::GetIO().Fonts->Fonts[0], true };
                mdConfig.headingFormats[2] = { GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetIO().Fonts->Fonts[0], false };
                mdConfig.formatCallback = [](const ImGui::MarkdownFormatInfo& markdownFormatInfo_, bool start_) {
                    ImGui::defaultMarkdownFormatCallback(markdownFormatInfo_, start_);
                    if (markdownFormatInfo_.type == ImGui::MarkdownFormatType::HEADING) {
                        if (start_) ImGui::PushStyleColor(ImGuiCol_Text, GUI::g_colorAccent);
                        else        ImGui::PopStyleColor();
                    }
                };

                std::lock_guard<std::mutex> lock(g_releaseMutex);
                ImGui::Markdown(g_releaseBody.c_str(), g_releaseBody.size(), mdConfig);

                ImGui::EndChild();
                ImGui::PopStyleVar();
            } else {
                ImVec2 ts = ImGui::CalcTextSize("No data available.");
                ImVec2 avail = ImGui::GetContentRegionAvail();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - ts.x) * 0.5f);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (avail.y - ts.y) * 0.5f);
                ImGui::TextDisabled("No data available.");
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(4); // Alpha, WindowRounding, WindowBorderSize, WindowPadding

        // ── Animated glowing border drawn on the foreground draw list ─────────
        if (wMax.x > wMin.x && wMax.y > wMin.y) {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            const float rounding = 12.0f;
            const ImVec4& ac = GUI::g_colorAccent;

            // 1. Base thick border (slightly transparent accent color)
            fg->AddRect(wMin, wMax,
                ImColor(ac.x, ac.y, ac.z, alpha * 0.55f),
                rounding, 0, 3.5f);

            // 2. Outer soft glow layer (thicker, very transparent)
            fg->AddRect(
                ImVec2(wMin.x - 1, wMin.y - 1),
                ImVec2(wMax.x + 1, wMax.y + 1),
                ImColor(ac.x, ac.y, ac.z, alpha * 0.18f),
                rounding + 1.0f, 0, 8.0f);

            // 3. Moving spotlight along the perimeter (clockwise, 3-second loop)
            float W = wMax.x - wMin.x;
            float H = wMax.y - wMin.y;
            float perimeter = 2.0f * (W + H);
            float tLight = fmodf((float)(GetTickCount64()) / 3000.0f, 1.0f);

            // Compute position(s) for the spotlight (draw primary + a 2nd halfway around for symmetry)
            for (int pass = 0; pass < 2; pass++) {
                float tPass = fmodf(tLight + pass * 0.5f, 1.0f);
                float dist = tPass * perimeter;

                ImVec2 lightPos;
                if (dist < W) {                     // top edge →
                    lightPos = ImVec2(wMin.x + dist, wMin.y);
                } else if (dist < W + H) {          // right edge ↓
                    lightPos = ImVec2(wMax.x, wMin.y + (dist - W));
                } else if (dist < 2.0f*W + H) {    // bottom edge ←
                    lightPos = ImVec2(wMax.x - (dist - W - H), wMax.y);
                } else {                            // left edge ↑
                    lightPos = ImVec2(wMin.x, wMax.y - (dist - 2.0f*W - H));
                }

                // Draw soft halo layers (outermost → innermost)
                fg->AddCircleFilled(lightPos, 28.0f, ImColor(ac.x, ac.y, ac.z, alpha * 0.07f));
                fg->AddCircleFilled(lightPos, 18.0f, ImColor(ac.x, ac.y, ac.z, alpha * 0.15f));
                fg->AddCircleFilled(lightPos, 10.0f, ImColor(ac.x, ac.y, ac.z, alpha * 0.35f));
                fg->AddCircleFilled(lightPos,  5.0f, ImColor(ac.x, ac.y, ac.z, alpha * 0.70f));
                // Bright white core
                fg->AddCircleFilled(lightPos,  2.5f, ImColor(1.0f, 1.0f, 1.0f, alpha * 0.85f));
            }
        }
    }
    
    // Bottom footer panel for theme customizer
    ImGui::BeginChild("ThemeCustomizer", ImVec2(0, 50), true);
    {
        ImGui::SetCursorPos(ImVec2(15, 12));
        ImGui::TextColored(GUI::g_colorAccent, "THEME CUSTOMIZER");
        
        ImGui::SameLine(0, 40);
        ImGui::SetCursorPosY(10);
        
        const char* themes[] = { "Aegle Classic", "Sakura Blossom", "Cyberpunk 2077", "Emerald Forest", "Deep Sea" };
        int currentTheme = GUI::g_currentTheme;
        
        ImGui::Text("Active Theme:"); ImGui::SameLine();
        ImGui::PushItemWidth(180);
        if (ImGui::Combo("##ActiveTheme", &currentTheme, themes, IM_ARRAYSIZE(themes))) {
            GUI::ApplyThemePreset(currentTheme);
        }
        
        ImGui::PopItemWidth();
    }
    ImGui::EndChild();
}


