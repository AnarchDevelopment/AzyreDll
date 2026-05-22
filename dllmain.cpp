/*
Under an4rch Development Public Source License 1.0
*/

#include <windows.h>
#include <windows.ui.core.h>
#include <windows.applicationmodel.core.h>
#include <hstring.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <psapi.h>
#include <functional>


#include "minhook/MinHook.h"
#include "ImGui/imgui.h"
#include "ImGui/backend/imgui_impl_dx11.h"
#include "ImGui/backend/imgui_impl_win32.h"
#include "Modules/ModuleHeader.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/PatternScan/PatternScan.hpp"
#include "Modules/Globals.hpp"
#include "Animations/Animations.hpp"
#include "GUI/GUI.hpp"
#include "GUI/DX11/ImGuiRenderer.hpp"
#include "ArrayList/ArrayList.hpp"
#include "Hook/Hook.hpp"
#include "Input/Input.hpp"
#include "Config/ConfigManager.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "psapi.lib")

// TODO: Refactor into multiple files and classes for better organization and maintainability

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

ImGuiConfigFlags g_imguiConfigFlags = ImGuiConfigFlags_None;
WNDPROC oWndProc = NULL;
HMODULE g_hModule = NULL;

ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView = NULL;
ma_engine g_audioEngine;
HWND g_window = NULL;

bool g_showMenu = false;
bool g_RequestUnload = false;
bool g_wasInWorld = false;
float g_menuAnim = 0.0f;
ULONGLONG g_lastTime = 0, g_lastToggle = 0, g_notifStart = 0;
bool g_vsync = false;
float g_lastW = 0, g_lastH = 0;

// Tab animation
int g_currentTab = 0;
int g_previousTab = 0;
ULONGLONG g_tabChangeTime = 0;
float g_tabAnim = 0.0f;
bool g_firstTabOpen = true;
uintptr_t g_gameBase = 0;
bool IsWindowsCursorVisible() {
    CURSORINFO ci = { 0 };
    ci.cbSize = sizeof(CURSORINFO);
    if (GetCursorInfo(&ci)) {
        return (ci.flags & CURSOR_SHOWING) != 0;
    }
    return false;
}

bool IsInWorld() {
    return !IsWindowsCursorVisible();
}
HudElement g_watermarkHud = { ImVec2(10, 10), ImVec2(400, 80) };
HudElement g_renderInfoHud = { ImVec2(10, 100), ImVec2(220, 120) };
HudElement g_arrayListHud = { ImVec2(0, 10), ImVec2(300, 400) };
HudElement g_keystrokesHud = { ImVec2(30, 0), ImVec2(140, 150) };
HudElement g_cpsHud = { ImVec2(500, 400), ImVec2(80, 30) };
HudElement g_fpsOverlayHud = { ImVec2(10, 250), ImVec2(100, 35) };

LRESULT CALLBACK KeyboardBlockHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) {
        return CallNextHookEx(Input::g_keyboardHook, nCode, wParam, lParam);
    }
    
    int result = Input::KeyboardBlockHookProc(nCode, wParam, lParam, g_showMenu);
    if (result == 1) {
        return 1;  // Block
    } else {
        return CallNextHookEx(Input::g_keyboardHook, nCode, wParam, lParam);
    }
}

ImVec4 LerpImVec4(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    );
}

void ImageWithOpacity(ID3D11ShaderResourceView* srv, ImVec2 size, float opacity) {
    if (!srv || opacity <= 0.0f) {
        return;
    }

    opacity = opacity > 1.0f ? 1.0f : (opacity < 0.0f ? 0.0f : opacity);
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 pos = { 0, 0 };
    ImU32 col = IM_COL32(255, 255, 255, static_cast<int>(opacity * 255.0f));
    draw_list->AddImage((ImTextureID)srv, pos, ImVec2(pos.x + size.x, pos.y + size.y), ImVec2(0, 0), ImVec2(1, 1), col);
}
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
// Dynamic WinRT declarations for UWP Cursor Force Show
typedef HRESULT (WINAPI* PFN_WindowsCreateStringReference)(
    PCWSTR sourceString,
    UINT32 length,
    HSTRING_HEADER *hstringHeader,
    HSTRING *string
);
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
typedef HRESULT (WINAPI* PFN_RoGetActivationFactory)(
    HSTRING activatableClassId,
    REFIID  iid,
    void    **factory
);

static ABI::Windows::UI::Core::ICoreWindow* g_uwpWindow = nullptr;
static ABI::Windows::UI::Core::ICoreCursor* g_uwpArrowCursor = nullptr;
static bool g_uwpCursorInitialized = false;
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
// Hook definitions for ICoreWindow::put_PointerCursor
typedef HRESULT (STDMETHODCALLTYPE* PFN_put_PointerCursor)(
    ABI::Windows::UI::Core::ICoreWindow* pThis,
    ABI::Windows::UI::Core::ICoreCursor* cursor
);

static PFN_put_PointerCursor oPutPointerCursor = nullptr;
static bool g_uwpCursorHooked = false;
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
HRESULT STDMETHODCALLTYPE hkPutPointerCursor(
    ABI::Windows::UI::Core::ICoreWindow* pThis,
    ABI::Windows::UI::Core::ICoreCursor* cursor
) {
    if (g_showMenu) {
        if (g_uwpArrowCursor) {
            return oPutPointerCursor(pThis, g_uwpArrowCursor);
        }
    }
    return oPutPointerCursor(pThis, cursor);
}
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
char g_qiLog[1024] = "";
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
class DispatchedHandlerImpl : public ABI::Windows::UI::Core::IDispatchedHandler {
private:
    ULONG m_ref = 1;
    std::function<void()> m_callback;

public:
    DispatchedHandlerImpl(std::function<void()> callback) : m_callback(callback) {}

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        
        static const IID iid_IAgileObject = { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };
        static const IID iid_IInspectable = { 0xaf86e2e0, 0xb12d, 0x4c6a, { 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90 } };

        char temp[128];
        sprintf(temp, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X} ",
               riid.Data1, riid.Data2, riid.Data3,
               riid.Data4[0], riid.Data4[1], riid.Data4[2], riid.Data4[3],
               riid.Data4[4], riid.Data4[5], riid.Data4[6], riid.Data4[7]);
        
        if (strlen(g_qiLog) + strlen(temp) < sizeof(g_qiLog) - 1) {
            strcat(g_qiLog, temp);
        }

        if (riid == IID_IUnknown || 
            riid == __uuidof(ABI::Windows::UI::Core::IDispatchedHandler) ||
            riid == iid_IAgileObject ||
            riid == iid_IInspectable) {
            *ppvObject = static_cast<ABI::Windows::UI::Core::IDispatchedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
    virtual ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_ref);
    }
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
    virtual ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0) {
            delete this;
        }
        return ref;
    }
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
    virtual HRESULT STDMETHODCALLTYPE Invoke() override {
        if (m_callback) {
            m_callback();
        }
        return S_OK;
    }
};
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
bool InitializeUwpCursor() {
    if (g_uwpCursorInitialized) return true;

    HMODULE hCombase = GetModuleHandleA("combase.dll");
    if (!hCombase) {
        hCombase = LoadLibraryA("combase.dll");
    }
    if (!hCombase) return false;

    PFN_WindowsCreateStringReference pfnCreateString = 
        (PFN_WindowsCreateStringReference)GetProcAddress(hCombase, "WindowsCreateStringReference");
    PFN_RoGetActivationFactory pfnGetFactory = 
        (PFN_RoGetActivationFactory)GetProcAddress(hCombase, "RoGetActivationFactory");

    if (!pfnCreateString || !pfnGetFactory) return false;

    // 1. Get CoreApplication static interface (ICoreImmersiveApplication)
    HSTRING_HEADER classHeader;
    HSTRING classStr;
    HRESULT hr = pfnCreateString(L"Windows.ApplicationModel.Core.CoreApplication", 45, &classHeader, &classStr);
    if (FAILED(hr)) return false;

    ABI::Windows::ApplicationModel::Core::ICoreImmersiveApplication* coreImmersiveApp = nullptr;
    hr = pfnGetFactory(classStr, __uuidof(ABI::Windows::ApplicationModel::Core::ICoreImmersiveApplication), (void**)&coreImmersiveApp);
    if (FAILED(hr) || !coreImmersiveApp) return false;

    // 2. Get MainView
    ABI::Windows::ApplicationModel::Core::ICoreApplicationView* mainView = nullptr;
    hr = coreImmersiveApp->get_MainView(&mainView);
    coreImmersiveApp->Release();
    if (FAILED(hr) || !mainView) return false;

    // 3. Get CoreWindow from MainView
    hr = mainView->get_CoreWindow(&g_uwpWindow);
    mainView->Release();
    if (FAILED(hr) || !g_uwpWindow) return false;

    // 4. Create the Arrow Cursor
    HSTRING_HEADER cursorHeader;
    HSTRING cursorStr;
    hr = pfnCreateString(L"Windows.UI.Core.CoreCursor", 26, &cursorHeader, &cursorStr);
    if (FAILED(hr)) {
        g_uwpWindow->Release();
        g_uwpWindow = nullptr;
        return false;
    }

    ABI::Windows::UI::Core::ICoreCursorFactory* cursorFactory = nullptr;
    hr = pfnGetFactory(cursorStr, __uuidof(ABI::Windows::UI::Core::ICoreCursorFactory), (void**)&cursorFactory);
    if (FAILED(hr) || !cursorFactory) {
        g_uwpWindow->Release();
        g_uwpWindow = nullptr;
        return false;
    }

    hr = cursorFactory->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_Arrow, 0, &g_uwpArrowCursor);
    cursorFactory->Release();
    if (FAILED(hr) || !g_uwpArrowCursor) {
        g_uwpWindow->Release();
        g_uwpWindow = nullptr;
        return false;
    }

    g_uwpCursorInitialized = true;
    return true;
}
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
void HookUwpCursor() {
    if (g_uwpCursorHooked) return;
    if (!g_uwpWindow) return;

    void** vtable = *(void***)g_uwpWindow;
    void* pTarget = vtable[15]; // put_PointerCursor is at vtable index 15

    if (MH_CreateHook(pTarget, (LPVOID)&hkPutPointerCursor, (LPVOID*)&oPutPointerCursor) == MH_OK) {
        if (MH_EnableHook(pTarget) == MH_OK) {
            g_uwpCursorHooked = true;
        }
    }
}

extern char g_notifTitle[64];
extern char g_notifMessage[128];
extern ULONGLONG g_notifStart;
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
void ShowDebugNotif(const char* title, const char* msg) {
    strncpy(g_notifTitle, title, sizeof(g_notifTitle) - 1);
    g_notifTitle[sizeof(g_notifTitle) - 1] = '\0';
    strncpy(g_notifMessage, msg, sizeof(g_notifMessage) - 1);
    g_notifMessage[sizeof(g_notifMessage) - 1] = '\0';
    g_notifStart = GetTickCount64();
}
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
void UpdateUwpCursorState() {
    if (!g_uwpCursorInitialized) {
        bool success = InitializeUwpCursor();
        if (success) {
            HookUwpCursor();
            ShowDebugNotif("UWP Cursor Hook", "Cursor initialized and hooked successfully!");
        } else {
            static ULONGLONG lastFailTime = 0;
            if (GetTickCount64() - lastFailTime > 5000) {
                lastFailTime = GetTickCount64();
                ShowDebugNotif("UWP Cursor Hook Error", "Failed to initialize UWP cursor.");
            }
        }
    }
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
    if (g_uwpCursorInitialized && g_uwpWindow) {
        static bool lastShowMenu = false;
        if (g_showMenu != lastShowMenu) {
            lastShowMenu = g_showMenu;
            
            ABI::Windows::UI::Core::ICoreDispatcher* dispatcher = nullptr;
            HRESULT hr = g_uwpWindow->get_Dispatcher(&dispatcher);
            if (SUCCEEDED(hr) && dispatcher) {
                auto handler = new DispatchedHandlerImpl([]() {
                    HRESULT hrInner;
                    if (g_showMenu) {
                        hrInner = g_uwpWindow->put_PointerCursor(g_uwpArrowCursor);
                    } else {
                        hrInner = g_uwpWindow->put_PointerCursor(nullptr);
                    }
                    if (FAILED(hrInner)) {
                        char buf[64];
                        sprintf(buf, "put_PointerCursor failed: 0x%08X", (unsigned int)hrInner);
                        ShowDebugNotif("UWP Cursor Error", buf);
                    }
                });
                g_qiLog[0] = '\0';
                ABI::Windows::Foundation::IAsyncAction* action = nullptr;
                hr = dispatcher->RunAsync(ABI::Windows::UI::Core::CoreDispatcherPriority_Normal, handler, &action);
                if (SUCCEEDED(hr) && action) {
                    action->Release();
                }
                handler->Release();
                dispatcher->Release();
                if (FAILED(hr)) {
                    char buf[1280];
                    sprintf(buf, "RunAsync failed: 0x%08X\nQueries: %s", (unsigned int)hr, g_qiLog);
                    ShowDebugNotif("UWP Dispatcher Error", buf);
                }
            } else {
                char buf[64];
                sprintf(buf, "get_Dispatcher failed: 0x%08X", (unsigned int)hr);
                ShowDebugNotif("UWP Dispatcher Error", buf);
            }
        }
    }
}
 
/*
    TODO: This UWP function is deprecated, we delete it in the future
*/
 
void CleanupUwpCursor() {
    if (g_uwpCursorHooked) {
        if (g_uwpWindow) {
            void** vtable = *(void***)g_uwpWindow;
            if (vtable) {
                void* pTarget = vtable[15];
                MH_DisableHook(pTarget);
                MH_RemoveHook(pTarget);
            }
        }
        g_uwpCursorHooked = false;
        oPutPointerCursor = nullptr;
    }
    if (g_uwpWindow) {
        // Dispatch setting pointer cursor to null on cleanup if possible, otherwise do it here
        ABI::Windows::UI::Core::ICoreDispatcher* dispatcher = nullptr;
        HRESULT hr = g_uwpWindow->get_Dispatcher(&dispatcher);
        if (SUCCEEDED(hr) && dispatcher) {
            auto handler = new DispatchedHandlerImpl([]() {
                g_uwpWindow->put_PointerCursor(nullptr);
            });
            ABI::Windows::Foundation::IAsyncAction* action = nullptr;
            hr = dispatcher->RunAsync(ABI::Windows::UI::Core::CoreDispatcherPriority_Normal, handler, &action);
            if (SUCCEEDED(hr) && action) {
                action->Release();
            }
            handler->Release();
            dispatcher->Release();
        } else {
            g_uwpWindow->put_PointerCursor(nullptr);
        }
        g_uwpWindow->Release();
        g_uwpWindow = nullptr;
    }
    if (g_uwpArrowCursor) {
        g_uwpArrowCursor->Release();
        g_uwpArrowCursor = nullptr;
    }
    g_uwpCursorInitialized = false;
}

// -----------------------------------------------------------------------------------------------------

LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    if (uMsg == WM_ACTIVATEAPP && wParam == TRUE) {
        // Force audio engine start when game regains focus
        ma_engine_start(&g_audioEngine);
        ma_device* pDev = ma_engine_get_device(&g_audioEngine);
        if (pDev) ma_device_start(pDev);
    }

    if (g_showMenu) {
        switch (uMsg) {
            case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN:
            case WM_MBUTTONUP: case WM_MOUSEWHEEL: case WM_INPUT: 
                return 1;
        }
    }
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void CleanupRenderTarget() {
    if (mainRenderTargetView) { 
        mainRenderTargetView->Release(); 
        mainRenderTargetView = NULL; 
    }
}
HRESULT STDMETHODCALLTYPE hkPresent_Impl(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (UnlockFPS::g_unlockFpsEnabled) {
        SyncInterval = 0;
        Flags |= 0x0200; // DXGI_PRESENT_ALLOW_TEARING
    } else if (!g_vsync) {
        SyncInterval = 0;
    }
        
    if (!pDevice) {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice))) {
            pDevice->GetImmediateContext(&pContext);
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            g_window = sd.OutputWindow;
            if (!g_window) g_window = GetForegroundWindow();

            ImGui::CreateContext();
            ImGui_ImplWin32_Init(g_window);
            ImGui_ImplDX11_Init(pDevice, pContext);
            GUI::LoadFont();
            ImGui_ImplDX11_CreateDeviceObjects();
            GUI::ApplyTheme();
            
            oWndProc = (WNDPROC)SetWindowLongPtr(g_window, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
            g_gameBase = (uintptr_t)GetModuleHandleA(NULL);
            
            // Texture/Resource Initialization (Must be on render thread)
            Watermark::InitializeTextures();
            
            // Centralized Module Initialization
            Module::Initialize(g_gameBase, &g_renderInfoHud, &g_watermarkHud, &g_keystrokesHud, &g_cpsHud, &g_fpsOverlayHud);
            
            Watermark::g_watermarkEnableTime = GetTickCount64();
            Watermark::g_watermarkAnim = 1.0f;
            
            HMODULE hModule = GetModuleHandleA(NULL);
            MODULEINFO mi;
            GetModuleInformation(GetCurrentProcess(), hModule, &mi, sizeof(mi));
            if (!AutoSprint::g_autoSprintAddr) {
                BYTE pattern[] = {0x0F, 0xB6, 0x41, 0x63, 0x48, 0x8D, 0x2D, 0x39, 0xE0, 0xC3, 0x00};
                AutoSprint::g_autoSprintAddr = PatternScan::Scan(g_gameBase, mi.SizeOfImage, pattern, sizeof(pattern));
            }
            if (!FullBright::g_fullBrightAddr) {
                BYTE pattern[] = {0xF3, 0x0F, 0x10, 0x80, 0xA0, 0x01, 0x00, 0x00};
                FullBright::g_fullBrightAddr = PatternScan::Scan(g_gameBase, mi.SizeOfImage, pattern, sizeof(pattern));
            }

            g_notifStart = GetTickCount64();
            g_lastTime = GetTickCount64();
        }
    }

    ID3D11Texture2D* pBackBuffer = NULL;
    float sw = 0, sh = 0;
    if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer))) {
        D3D11_TEXTURE2D_DESC desc;
        pBackBuffer->GetDesc(&desc);
        sw = (float)desc.Width; 
        sh = (float)desc.Height;
        if (!mainRenderTargetView) pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
        if (MotionBlur::g_motionBlurEnabled) {
            int maxFrames = 1;
            if (MotionBlur::g_blurType == "Time Aware Blur") {
                maxFrames = (int)round(MotionBlur::g_maxHistoryFrames);
            } else if (MotionBlur::g_blurType == "Real Motion Blur") {
                maxFrames = 8;
            } else {
                maxFrames = (int)round(MotionBlur::g_blurIntensity);
            }
            
            if (maxFrames <= 0) maxFrames = 4;
            if (maxFrames > 16) maxFrames = 16;
            
            MotionBlur::InitializeBackbufferStorage(maxFrames);
            ID3D11ShaderResourceView* srv = MotionBlur::CopyBackbufferToSRV(pDevice, pContext, pSwapChain);
            if (srv) {
                if ((int)MotionBlur::g_previousFrames.size() >= maxFrames) {
                    if (MotionBlur::g_previousFrames[0]) MotionBlur::g_previousFrames[0]->Release();
                    MotionBlur::g_previousFrames.erase(MotionBlur::g_previousFrames.begin());
                    MotionBlur::g_frameTimestamps.erase(MotionBlur::g_frameTimestamps.begin());
                }
                
                MotionBlur::g_previousFrames.push_back(srv);
                MotionBlur::g_frameTimestamps.push_back((float)GetTickCount64() / 1000.0f);
            }
        }
        
        pBackBuffer->Release();
    }

    if (sw <= 0) return Hook::oPresent(pSwapChain, 0, Flags);
    if ((GetAsyncKeyState(VK_INSERT) & 0x8000) && (GetTickCount64() - g_lastToggle) > 400) {
        g_showMenu = !g_showMenu;
        GUI::g_showMenu = g_showMenu;
        g_lastToggle = GetTickCount64();
        
        if (g_showMenu) {
            Input::BlockGameInput();
            Hook::oClipCursor(NULL);
            g_wasInWorld = IsInWorld();
            while (ShowCursor(TRUE) < 0);
        } else {
            Input::UnblockGameInput();
            if (g_wasInWorld) {
                while (ShowCursor(FALSE) >= 0);
            }
        }
    }

    // Sync menu state if closed via the GUI Close ("X") button
    if (g_showMenu && !GUI::g_showMenu) {
        g_showMenu = false;
        Input::UnblockGameInput();
        if (g_wasInWorld) {
            while (ShowCursor(FALSE) >= 0);
        }
    }
    static bool wasMotionBlurEnabled = false;
    if (!MotionBlur::g_motionBlurEnabled && wasMotionBlurEnabled) {
        MotionBlur::CleanupBackbufferStorage();
    }
    wasMotionBlurEnabled = MotionBlur::g_motionBlurEnabled;
    ULONGLONG now = GetTickCount64();
    float dt = (float)(now - g_lastTime) / 1000.0f;
    g_lastTime = now;
    GUI::UpdateAnimation(now, dt);
    float easedMenuAnim = Animations::SmoothInertia(GUI::g_menuAnim);
    Module::UpdateAnimation(now);

    ImGui_ImplDX11_NewFrame();
    // Set ImGui software cursor based on Windows cursor visibility

    Input::Update(g_window, sw, sh, g_showMenu, g_showMenu && !IsWindowsCursorVisible());

    CPSCounter::UpdateCPS(now, Input::IsLMBPressed(), Input::IsRMBPressed(), Input::WasLMBPressed(), Input::WasRMBPressed());
    CPSCounter::UpdateAnimation(now);

    ImGui::NewFrame();

    if (MotionBlur::g_motionBlurEnabled && !g_showMenu && MotionBlur::g_previousFrames.size() > 0 && MotionBlur::g_motionBlurAnim > 0.01f) {
        float currentTime = (float)GetTickCount64() / 1000.0f;
        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        ImDrawList* blurDraw = ImGui::GetBackgroundDrawList();
    
        if (MotionBlur::g_blurType == "Average Pixel Blur") {
            float alpha = 0.25f;
            float bleedFactor = 0.95f;
            for (const auto& frame : MotionBlur::g_previousFrames) {
                if (frame) {
                    ImU32 col = IM_COL32(255, 255, 255, (int)(alpha * MotionBlur::g_motionBlurAnim * 255.0f));
                    blurDraw->AddImage((ImTextureID)frame, ImVec2(0, 0), screenSize, ImVec2(0, 0), ImVec2(1, 1), col);
                    alpha *= bleedFactor;
                }
            }
        } 
        else if (MotionBlur::g_blurType == "Ghost Frames") {
            float alpha = 0.30f;
            float bleedFactor = 0.80f;
            for (const auto& frame : MotionBlur::g_previousFrames) {
                if (frame) {
                    ImU32 col = IM_COL32(255, 255, 255, (int)(alpha * MotionBlur::g_motionBlurAnim * 255.0f));
                    blurDraw->AddImage((ImTextureID)frame, ImVec2(0, 0), screenSize, ImVec2(0, 0), ImVec2(1, 1), col);
                    alpha *= bleedFactor;
                }
            }
        }
        else if (MotionBlur::g_blurType == "Time Aware Blur") {
            float T = MotionBlur::g_blurTimeConstant;
            std::vector<float> weights;
            float totalWeight = 0.0f;
            
            for (size_t i = 0; i < MotionBlur::g_previousFrames.size(); i++) {
                float age = currentTime - MotionBlur::g_frameTimestamps[i];
                float weight = expf(-age / T);
                weights.push_back(weight);
                totalWeight += weight;
            }
            
            if (totalWeight > 0.0f) {
                for (float& w : weights) {
                    w /= totalWeight;
                }
            }
            
            for (size_t i = 0; i < MotionBlur::g_previousFrames.size(); i++) {
                if (MotionBlur::g_previousFrames[i] && weights[i] > 0.001f) {
                    ImU32 col = IM_COL32(255, 255, 255, (int)(weights[i] * MotionBlur::g_motionBlurAnim * 255.0f));
                    blurDraw->AddImage((ImTextureID)MotionBlur::g_previousFrames[i], ImVec2(0, 0), screenSize, ImVec2(0, 0), ImVec2(1, 1), col);
                }
            }
        }
        else if (MotionBlur::g_blurType == "Real Motion Blur") {
            float alpha = 0.35f;
            float bleedFactor = 0.85f;
            for (const auto& frame : MotionBlur::g_previousFrames) {
                if (frame) {
                    ImU32 col = IM_COL32(255, 255, 255, (int)(alpha * MotionBlur::g_motionBlurAnim * 255.0f));
                    blurDraw->AddImage((ImTextureID)frame, ImVec2(0, 0), screenSize, ImVec2(0, 0), ImVec2(1, 1), col);
                    alpha *= bleedFactor;
                }
            }
        }
        else if (MotionBlur::g_blurType == "V4") {
            float alpha = 0.35f;
            float bleedFactor = 0.85f;
            for (const auto& frame : MotionBlur::g_previousFrames) {
                if (frame) {
                    ImU32 col = IM_COL32(255, 255, 255, (int)(alpha * MotionBlur::g_motionBlurAnim * 255.0f));
                    blurDraw->AddImage((ImTextureID)frame, ImVec2(0, 0), screenSize, ImVec2(0, 0), ImVec2(1, 1), col);
                    alpha *= bleedFactor;
                }
            }
        }
    }

    g_arrayListHud.HandleDrag(g_showMenu);
    g_arrayListHud.ClampToScreen();
    if (g_arrayListHud.pos.x == 0 && g_arrayListHud.pos.y == 10) {
        g_arrayListHud.pos.x = sw - 250;
    }
    ArrayList::g_hud = &g_arrayListHud;

    GUI::RenderNotification(sw, sh);
    if (GUI::g_menuAnim > 0.001f) {
        GUI::RenderMenu(sw, sh);
    }
    
    Module::RenderDisplay(sw, sh);

    ImGui::Render();
    
    pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    UnlockFPS::UpdateFPS();
    
    HRESULT hr = Hook::oPresent(pSwapChain, SyncInterval, Flags);
    if (FAILED(hr) && (Flags & 0x0200)) {
        // Fallback: Try without ALLOW_TEARING if it failed
        Flags &= ~0x0200;
        hr = Hook::oPresent(pSwapChain, SyncInterval, Flags);
    }
    return hr;
}

DWORD WINAPI MainThread(LPVOID lpReserved) {
    UnlockFPS::Initialize();
    UnlockFPS::SetFPS(60.0f);
    Hook::Initialize();
    return 0;
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hModule = hMod;
        DisableThreadLibraryCalls(hMod);
        CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
