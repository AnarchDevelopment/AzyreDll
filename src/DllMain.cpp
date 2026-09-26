#include "PCH.hpp"

#include "Config/Config.hpp"
#include "Framework/Log.hpp"
#include "Framework/Sound.hpp"
#include "GUI/Window.hpp"
#include "Input/InputSystem.hpp"
#include "Input/WheelHook.hpp"
#include "Modules/ModuleManager.hpp"
#include "Render/DX11.hpp"
#include "Scripting/Scripting.hpp"
#include "SDK/Game.hpp"

#include <winrt/Windows.Foundation.h>

static DWORD WINAPI Bootstrap(LPVOID)
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (...)
    {
    }

    mc::wheel::install();
    mc::log::installCrashHandler();
    mc::sound::setModule(mc::dx11::moduleHandle());
    mc::sound::init();
    // Precalcular rutas (WinRT solo en este hilo MTA) y cargar scripts antes
    // de la config para que sus estados persistidos se apliquen.
    mc::config::init();
    mc::log::setFile((mc::config::baseDir() + "\\azyre_sdk.log").c_str());
    mc::scripts::loadAll();
    mc::config::loadAll();
    mc::window::setTitle("Azyre | 1.1.0");

    if (mc::dx11::install())
    {
        mc::Game::get().install();
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

        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }
        catch (...)
        {
        }

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
        mc::ModuleManager::get().shutdownAll();
        mc::Game::get().uninstall();
        mc::input::shutdown();
        mc::sound::shutdown();
        mc::dx11::shutdown();
        mc::log::shutdown();
    }
    return TRUE;
}
