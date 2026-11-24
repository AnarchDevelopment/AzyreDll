#include "ChatOpenHook.hpp"

#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"

#include <windows.h>

#include <atomic>

namespace mc::chatopen {

namespace {

// Interceptamos el punto de creación para que el ChatScreen no se apile. El
// panel reemplazo recibe teclado/mouse por el pipeline compartido de ImGui.
constexpr uintptr_t kOpenChatScreen = 0x2149C0;
constexpr uintptr_t kOpenCommandChatScreen = 0x2148F0;
constexpr uintptr_t kUiCanOpenChat = 0x04F470;

std::atomic<bool> g_armed{false};
std::atomic<bool> g_installed{false};
std::atomic<int> g_request{0};

// Firma laxa a proposito: solo el primer argumento esta confirmado. Reenviar
// rcx/rdx/r8 tal cual mantiene al original intacto cuando no interceptamos.
using OpenFn = char(__fastcall*)(void*, void*, void*);

OpenFn g_origChat = nullptr;
OpenFn g_origCommand = nullptr;

// Both native openers call this screen-stack predicate before constructing a
// ChatScreen. Preserve that gate (inventory/pause/etc. may reject chat) before
// substituting our panel. The object chain is reproduced from the opener's
// observed instructions: context+8 -> owner, owner+0x48 -> MinecraftClient.
bool nativeAllowsChat(void* context)
{
    const uintptr_t ctx = reinterpret_cast<uintptr_t>(context);
    if (!ctx || !mem::isReadable(ctx + 8, sizeof(uintptr_t)))
        return false;
    const uintptr_t owner = mem::read<uintptr_t>(ctx + 8);
    if (!owner || !mem::isReadable(owner + 0x48, sizeof(uintptr_t)))
        return false;
    const uintptr_t client = mem::read<uintptr_t>(owner + 0x48);
    void* address = reinterpret_cast<void*>(mem::resolve(kUiCanOpenChat));
    if (!client || !address || !mem::isReadable(client, sizeof(uintptr_t)) ||
        !mem::isReadable(reinterpret_cast<uintptr_t>(address), 8))
        return false;

    using GuardFn = char(__fastcall*)(void*);
    char allowed = 0;
    __try
    {
        allowed = reinterpret_cast<GuardFn>(address)(reinterpret_cast<void*>(client));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return allowed != 0;
}

char __fastcall detourChat(void* a, void* b, void* c)
{
    const bool routeToPanel = g_armed.load(std::memory_order_relaxed);
    if (routeToPanel && nativeAllowsChat(a))
    {
        g_request.store((int)Request::Chat, std::memory_order_relaxed);
        // La llamada nativa devuelve un booleano de apertura. Afirmar que la
        // solicitud fue atendida evita que el handler reintente el screen;
        // el objeto ChatScreen nunca llega a construirse ni a apilarse.
        return 1;
    }

    char result = 0;
    __try
    {
        result = g_origChat ? g_origChat(a, b, c) : (char)0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = 0;
    }
    return result;
}

char __fastcall detourCommand(void* a, void* b, void* c)
{
    const bool routeToPanel = g_armed.load(std::memory_order_relaxed);
    if (routeToPanel && nativeAllowsChat(a))
    {
        g_request.store((int)Request::Command, std::memory_order_relaxed);
        return 1;
    }

    char result = 0;
    __try
    {
        result = g_origCommand ? g_origCommand(a, b, c) : (char)0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = 0;
    }
    return result;
}

} // namespace

void setArmed(bool armed)
{
    g_armed.store(armed, std::memory_order_relaxed);
    if (!armed)
        g_request.store(0, std::memory_order_relaxed);
}

bool armed()
{
    return g_armed.load(std::memory_order_relaxed);
}

Request consume()
{
    const int v = g_request.exchange(0, std::memory_order_relaxed);
    if (v < 0 || v > 2)
        return Request::None;
    return (Request)v;
}

bool install()
{
    if (g_installed.load())
        return true;

    void* target = (void*)mem::resolve(kOpenChatScreen);
    if (!target || !mem::isReadable((uintptr_t)target, 8))
    {
        MC_LOG_ERROR("[Chat] chat screen RVA 0x%llX not resolved",
                     (unsigned long long)kOpenChatScreen);
        return false;
    }

    if (!hook::create(target, (void*)&detourChat, (void**)&g_origChat))
        return false;

    void* targetCmd = (void*)mem::resolve(kOpenCommandChatScreen);
    if (targetCmd && mem::isReadable((uintptr_t)targetCmd, 8) &&
        hook::create(targetCmd, (void*)&detourCommand, (void**)&g_origCommand))
    {
        MC_LOG("[Chat] vanilla chat/command screens intercepted");
    }
    else
    {
        MC_LOG_ERROR("[Chat] command screen RVA 0x%llX not intercepted",
                     (unsigned long long)kOpenCommandChatScreen);
    }

    g_installed.store(true);
    return true;
}

void uninstall()
{
    if (!g_installed.load())
        return;

    g_armed.store(false);
    hook::remove((void*)mem::resolve(kOpenChatScreen));
    hook::remove((void*)mem::resolve(kOpenCommandChatScreen));
    g_installed.store(false);
}

}
