#include "Hook.hpp"

#include "Memory.hpp"
#include "Log.hpp"

#include <MinHook.h>

namespace mc::hook {

static bool g_initialized = false;

bool initialize()
{
    if (g_initialized)
        return true;

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
    {
        MC_LOG_ERROR("[MinHook] MH_Initialize failed: %d", (int)status);
        return false;
    }

    g_initialized = true;
    MC_LOG("[MinHook] Initialized");
    return true;
}

bool create(void* target, void* detour, void** original)
{
    if (!g_initialized && !initialize())
        return false;
    if (!target || !detour)
        return false;

    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
    {
        MC_LOG_ERROR("[MinHook] MH_CreateHook(%p) failed: %d", target, (int)status);
        return false;
    }

    status = MH_EnableHook(target);
    if (status != MH_OK)
    {
        MC_LOG_ERROR("[MinHook] MH_EnableHook(%p) failed: %d", target, (int)status);
        return false;
    }

    MC_LOG("[MinHook] Hooked %p -> %p", target, detour);
    return true;
}

void shutdown()
{
    if (!g_initialized)
        return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_initialized = false;
    MC_LOG("[MinHook] Shutdown");
}

void* vtableFunc(uintptr_t vtableRva, int slot)
{
    uintptr_t table = mem::resolve(vtableRva);
    if (!mem::isReadable(table, (size_t)(slot + 1) * sizeof(void*)))
        return nullptr;
    return mem::read<void*>(table + (uintptr_t)slot * sizeof(void*));
}

}
