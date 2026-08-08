/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <d3d11.h>
#include <string>

/// @brief Screenshot module - Captures the current DX11 frame and saves it as PNG to the temp folder
class Screenshot {
public:
    static bool  g_enabled;
    static int   g_hotkey;           // virtual-key code para captura (F12 por defecto)
    static bool  g_showHud;          // mostrar ruta del ultimo screenshot en HUD
    static bool  g_includeHud;       // capturar el overlay tambien (siempre true si no hay separacion de backbuffer)
    static bool  g_notifyOnCapture;  // mostrar notificacion de GUI al capturar

    static ULONGLONG g_enableTime;
    static ULONGLONG g_disableTime;
    static std::wstring g_lastPath;  // ruta del ultimo screenshot guardado
    static bool  g_pendingCapture;   // solicitud de captura para el siguiente frame

    static void Initialize();

    /// @brief Debe llamarse en el render loop DESPUES de ImGui::Render() pero ANTES del swap chain Present.
    ///        pDevice y pSwapChain deben ser validos.
    static void TryCaptureFrame(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, IDXGISwapChain* pSwapChain);

    static void RenderArrayList(struct ImDrawList* draw, struct ImVec2 arrayListStart, float& yPos, struct ImVec2& arrayListEnd);
    static void RenderMenu();

private:
    static bool SaveToPNG(const wchar_t* path, BYTE* pixels, int width, int height);
};
