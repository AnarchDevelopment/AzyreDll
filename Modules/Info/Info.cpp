/*
Under an4rch Development Public Source License 1.0
*/

#include "Info.hpp"
#include "../../GUI/GUI.hpp"
#include "../../ImGui/imgui.h"
#include "../Globals.hpp"
#include "../../Assets/resource.h"
#include "../../Assets/stb/stb_image.h"
#include <d3d11.h>
#include <windows.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>

extern "C" {
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio/miniaudio.h"
}

// External globals from dllmain.cpp
extern ID3D11Device* pDevice;
extern ID3D11DeviceContext* pContext;
extern HMODULE g_hModule;

// Static member initialization
ImTextureID Info::g_logoTexture = 0;
int Info::g_logoWidth = 0;
int Info::g_logoHeight = 0;
uint8_t* Info::g_audioData = nullptr;
uint32_t Info::g_audioDataSize = 0;

// Audio context global
static const int NUM_CLICK_SOUNDS = 3;
// extern ma_engine g_audioEngine; // now in Globals.hpp
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

void Info::RenderMenu() {
    GUI::RenderDashboard();
    
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


