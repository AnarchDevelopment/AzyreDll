#include "PCH.hpp"

#include "Config/Config.hpp"
#include "Framework/Log.hpp"
#include "Framework/Sound.hpp"
#include "GUI/Window.hpp"
#include "Input/InputSystem.hpp"
#include "Input/WheelHook.hpp"
#include "Modules/ModuleManager.hpp"
#include "Networking/Spotify/SpotifyService.hpp"
#include "Render/DX11.hpp"
#include "Input/MouseHook.hpp"
#include "Scripting/Scripting.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Effects.hpp"
#include "SDK/Game.hpp"

#ifdef _MSC_VER
#include <winrt/Windows.Foundation.h>
#else
#include <objbase.h> // CoInitializeEx / COINIT_MULTITHREADED (GCC/MinGW)
#endif

static void initCom()
{
#ifdef _MSC_VER
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (...)
    {
    }
#else
    // Toolchains no-MSVC (CI): inicializacion COM clasica en vez de WinRT.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
}

static DWORD WINAPI Bootstrap(LPVOID)
{
    initCom();

    MC_LOG("[BOOT] 1: installing wheel...");
    mc::wheel::install();
    MC_LOG("[BOOT] 2: crash handler...");
    mc::log::installCrashHandler();
    MC_LOG("[BOOT] 3: config init (early para el log path)...");
    mc::config::init();
    MC_LOG("[BOOT] 4: log file redirect...");
    mc::log::setFile((mc::config::baseDir() + "\\azyre_sdk.log").c_str());
    MC_LOG("[BOOT] 5: sound...");
    mc::sound::setModule(mc::dx11::moduleHandle());
    mc::sound::init();
    MC_LOG("[BOOT] 6: scripts...");
    mc::scripts::loadAll();
    MC_LOG("[BOOT] 7: config load...");
    mc::config::loadAll();
    MC_LOG("[BOOT] 7b: spotify callback server (localhost:8888)...");
    mc::spotify::startServer();
    MC_LOG("[BOOT] 8: window title...");
    mc::window::setTitle("Azyre | 1.1.0");
    MC_LOG("[BOOT] 9: dx11 install...");

    if (mc::dx11::install())
    {
        MC_LOG("[BOOT] 10: game scanner...");
        mc::Game::get().install();
        MC_LOG("[BOOT] 11: chat hook...");
        mc::chathook::install();
        MC_LOG("[BOOT] 12: effects hook...");
        mc::effects::install();
        MC_LOG("[BOOT] 13: mouse hook...");
        mc::mousehook::install();
        MC_LOG("[BOOT] 14: ALL BOOT STEPS COMPLETE");
        MC_LOG("[SUCCESS] DX11 hooks active - waiting for Present call...");
    }
    else
    {
        MC_LOG_ERROR("[ERROR] Failed to install DX11 hooks!");
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        mc::dx11::setModule(hModule);
        mc::wheel::setModule(hModule);

        initCom();

        mc::log::init("azyre_sdk.log");
        MC_LOG("=========================================");
        MC_LOG("Azyre injected!");
        MC_LOG("Process ID: %d", GetCurrentProcessId());
        MC_LOG("=========================================");

        CreateThread(nullptr, 0, Bootstrap, nullptr, 0, nullptr);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        MC_LOG("[DX11] DLL Unloading - Cleaning up...");
        mc::config::saveAll();
        mc::scripts::shutdown();
        mc::wheel::uninstall();
        mc::mousehook::uninstall();
        mc::chathook::uninstall();
        mc::effects::uninstall();
        mc::ModuleManager::get().shutdownAll();
        mc::spotify::shutdown();
        mc::Game::get().uninstall();
        mc::input::shutdown();
        mc::sound::shutdown();
        mc::dx11::shutdown();
        mc::log::shutdown();
    }
    return TRUE;
}
