/*
Under an4rch Development Public Source License 1.0
*/

#include "Screenshot.hpp"
#include "Animations/Animations.hpp"
#include "ImGui/imgui.h"
#include "GUI/GUI.hpp"
#include "Assets/stb/stb_image_write.h"
#include <windows.h>
#include <shlobj.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <cstdio>
#include <cmath>
#include <ctime>

// Notification globals from dllmain.cpp / GUI.cpp
extern ULONGLONG g_notifStart;
extern char g_notifTitle[64];
extern char g_notifMessage[128];

// ─── Static member initialization ───────────────────────────────────────────
bool          Screenshot::g_enabled         = false;
int           Screenshot::g_hotkey          = VK_F12;
bool          Screenshot::g_showHud         = true;
bool          Screenshot::g_includeHud      = true;
bool          Screenshot::g_notifyOnCapture = true;
ULONGLONG     Screenshot::g_enableTime      = 0;
ULONGLONG     Screenshot::g_disableTime     = 0;
std::wstring  Screenshot::g_lastPath        = L"";
bool          Screenshot::g_pendingCapture  = false;

// Internal
static bool      s_wasHotkeyDown = false;
static ULONGLONG s_lastHudTime   = 0;

// Generate timestamped filename in the AppContainer-accessible LocalAppData folder.
// GetTempPathW() resolves to %TEMP% which UWP AppContainers cannot access.
// SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL) returns the package-
// redirected local app data folder that IS accessible from within the container.
static std::wstring MakeScreenshotPath() {
    std::wstring folder;

    // Try SHGetKnownFolderPath first (works inside AppContainer)
    PWSTR pPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &pPath))) {
        folder = std::wstring(pPath) + L"\\aegle_screenshots";
        CoTaskMemFree(pPath);
    } else {
        // Absolute fallback: use GetEnvironmentVariableW which is also redirected
        wchar_t buf[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) > 0) {
            folder = std::wstring(buf) + L"\\aegle_screenshots";
        } else {
            // Last resort: Documents folder
            wchar_t docs[MAX_PATH] = {};
            SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, docs);
            folder = std::wstring(docs) + L"\\aegle_screenshots";
        }
    }

    CreateDirectoryW(folder.c_str(), NULL);

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf_s(name, 64, L"\\aegle_%04d%02d%02d_%02d%02d%02d.png",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    return folder + name;
}

void Screenshot::Initialize() {
    // Nothing to pre-allocate
}

void Screenshot::TryCaptureFrame(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, IDXGISwapChain* pSwapChain) {
    if (!g_enabled || !pDevice || !pContext || !pSwapChain) return;

    // Hotkey polling
    bool isDown = (GetAsyncKeyState(g_hotkey) & 0x8000) != 0;
    if (isDown && !s_wasHotkeyDown) {
        g_pendingCapture = true;
    }
    s_wasHotkeyDown = isDown;

    if (!g_pendingCapture) return;
    g_pendingCapture = false;

    // ── 1. Obtener el backbuffer ──────────────────────────────────────────
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (FAILED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer))) return;

    D3D11_TEXTURE2D_DESC bbDesc = {};
    pBackBuffer->GetDesc(&bbDesc);

    // ── 2. Crear staging texture (CPU readable) ───────────────────────────
    D3D11_TEXTURE2D_DESC stagingDesc = bbDesc;
    stagingDesc.Usage          = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags      = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags      = 0;
    stagingDesc.MipLevels      = 1;
    stagingDesc.ArraySize      = 1;
    stagingDesc.SampleDesc     = { 1, 0 };
    // Use BGRA if the backbuffer is BGRA, otherwise try RGBA
    if (stagingDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
        stagingDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        stagingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    ID3D11Texture2D* pStaging = nullptr;
    if (FAILED(pDevice->CreateTexture2D(&stagingDesc, nullptr, &pStaging))) {
        pBackBuffer->Release();
        return;
    }

    // ── 3. Copy backbuffer → staging ─────────────────────────────────────
    pContext->CopyResource(pStaging, pBackBuffer);
    pBackBuffer->Release();

    // ── 4. Map staging texture ────────────────────────────────────────────
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(pContext->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped))) {
        pStaging->Release();
        return;
    }

    int width  = (int)bbDesc.Width;
    int height = (int)bbDesc.Height;
    int rowPitch = (int)mapped.RowPitch;

    // ── 5. Convert BGRA → RGBA if needed + pack into contiguous buffer ────
    BYTE* src    = (BYTE*)mapped.pData;
    BYTE* pixels = new BYTE[width * height * 4];

    bool isBGRA = (bbDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                   bbDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);

    for (int y = 0; y < height; y++) {
        BYTE* srcRow = src + (size_t)y * rowPitch;
        BYTE* dstRow = pixels + (size_t)y * width * 4;
        if (isBGRA) {
            for (int x = 0; x < width; x++) {
                dstRow[x*4+0] = srcRow[x*4+2]; // R ← B
                dstRow[x*4+1] = srcRow[x*4+1]; // G
                dstRow[x*4+2] = srcRow[x*4+0]; // B ← R
                dstRow[x*4+3] = 255;
            }
        } else {
            for (int x = 0; x < width; x++) {
                dstRow[x*4+0] = srcRow[x*4+0];
                dstRow[x*4+1] = srcRow[x*4+1];
                dstRow[x*4+2] = srcRow[x*4+2];
                dstRow[x*4+3] = 255;
            }
        }
    }

    pContext->Unmap(pStaging, 0);
    pStaging->Release();

    // ── 6. Generar path y guardar PNG con stb_image_write ─────────────────
    std::wstring wpath = MakeScreenshotPath();

    // Convertir a UTF-8 para stb_image_write
    char pathUtf8[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, pathUtf8, sizeof(pathUtf8), NULL, NULL);

    int ok = stbi_write_png(pathUtf8, width, height, 4, pixels, width * 4);
    delete[] pixels;

    if (ok) {
        g_lastPath    = wpath;
        s_lastHudTime = GetTickCount64();
        if (g_notifyOnCapture) {
            strcpy_s(g_notifTitle,   "Screenshot");
            strcpy_s(g_notifMessage, "Guardado en %TEMP%\\aegle_screenshots");
            g_notifStart = GetTickCount64();
        }
    }
}

void Screenshot::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    if (!g_enabled && g_disableTime == 0) return;

    ULONGLONG now = GetTickCount64();
    float timeSinceEnable  = (float)(now - g_enableTime)  / 1000.0f;
    float timeSinceDisable = (float)(now - g_disableTime) / 1000.0f;
    const float FADE = 0.3f;

    float alpha = 255.0f;
    float slide = 0.0f;
    if (g_enabled) {
        alpha = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / FADE)) * 255.0f;
        slide = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / 0.4f)) * 60.0f - 60.0f;
    } else if (timeSinceDisable < FADE) {
        alpha = Animations::SmoothInertia(1.0f - timeSinceDisable / FADE) * 255.0f;
    } else {
        g_disableTime = 0;
        return;
    }

    if (alpha > 1.0f && draw) {
        char buf[64];
        // Mostrar tecla hotkey
        sprintf_s(buf, sizeof(buf), "Screenshot [F%d]", g_hotkey - VK_F1 + 1);
        ImVec2 textSize = ImGui::CalcTextSize(buf);
        float x = arrayListStart.x + 300.0f - textSize.x - 10.0f;
        draw->AddText(ImVec2(x + slide - 1, yPos + 1), IM_COL32(0,0,0,200), buf);
        draw->AddText(ImVec2(x + slide,     yPos),     IM_COL32(200,255,130,(int)alpha), buf);
        yPos += 18.0f;
        arrayListEnd.y = yPos;
    }
}

void Screenshot::RenderMenu() {
    bool prev = g_enabled;
    GUI::RenderCustomSwitch("Screenshot", &g_enabled);
    if (prev != g_enabled) {
        if (g_enabled) { g_enableTime  = GetTickCount64(); g_disableTime = 0; }
        else            { g_disableTime = GetTickCount64(); g_enableTime  = 0; }
    }

    if (GUI::BeginModuleSettings("Screenshot", &g_enabled)) {
        static const char* fkeys[] = {
            "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"
        };
        int fkIdx = g_hotkey - VK_F1;
        if (fkIdx < 0)  fkIdx = 0;
        if (fkIdx > 11) fkIdx = 11;
        if (ImGui::Combo("Hotkey##SS", &fkIdx, fkeys, 12))
            g_hotkey = VK_F1 + fkIdx;

        GUI::RenderCustomSwitch("Notificar##SS",    &g_notifyOnCapture);
        GUI::RenderCustomSwitch("Mostrar en HUD##SS", &g_showHud);

        if (!g_lastPath.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Ultimo guardado:");
            // Mostrar solo el nombre de archivo (no la ruta completa)
            size_t slash = g_lastPath.find_last_of(L'\\');
            std::wstring fname = (slash != std::wstring::npos)
                ? g_lastPath.substr(slash + 1)
                : g_lastPath;
            char fnameA[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, fname.c_str(), -1, fnameA, sizeof(fnameA), NULL, NULL);
            ImGui::TextColored(ImVec4(0.6f,1.0f,0.5f,1.0f), "%s", fnameA);
        }

        GUI::EndModuleSettings();
    }
}

// Stub — not used since we use stb_image_write directly
bool Screenshot::SaveToPNG(const wchar_t*, BYTE*, int, int) { return false; }
