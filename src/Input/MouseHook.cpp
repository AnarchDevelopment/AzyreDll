#include "MouseHook.hpp"

#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "GUI/Menu.hpp"
#include "Input/WheelHook.hpp"
#include "SDK/Offsets.hpp"

namespace mc::mousehook {

namespace {

using FeedFn = void(__fastcall*)(void*, char, int, short, short, short, short, uint8_t);
FeedFn g_orig = nullptr;

// Captura sin SEH (solo PODs, sin unwinding).
void feedBody(char action, int data)
{
    if (action == 4)
    {
        // Semantica del MouseHook de Solstice:
        // 0x78/0x7F = scroll arriba, 0x88/0x80 = scroll abajo.
        if (data == 0x78 || data == 0x7F)
            wheel::addGameDelta(120);
        else if (data == 0x88 || data == 0x80)
            wheel::addGameDelta(-120);
    }
}

void __fastcall hkFeed(void* self, char action, int data, short x, short y,
                       short dx, short dy, uint8_t force)
{
    __try
    {
        feedBody(action, data);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    // Con el menu abierto no reenviamos el wheel al juego: scrollear la GUI
    // no debe mover la hotbar. Todo lo demas pasa intacto.
    if (action == 4 && mc::menu::visible())
        return;

    g_orig(self, action, data, x, y, dx, dy, force);
}

} // namespace

bool install()
{
    if (g_orig)
        return true;

    if (!off::fn::MouseDeviceFeed)
    {
        MC_LOG_ERROR("[MouseHook] off::fn::MouseDeviceFeed sin configurar "
                     "(falta el RVA de MouseDevice::feed)");
        return false;
    }

    uintptr_t addr = mem::resolve(off::fn::MouseDeviceFeed);
    if (!mem::isReadable(addr, 16))
    {
        MC_LOG_ERROR("[MouseHook] MouseDevice::feed no legible @ 0x%llX",
                     (unsigned long long)off::fn::MouseDeviceFeed);
        return false;
    }

    if (!hook::create((void*)addr, (void*)&hkFeed, (void**)&g_orig))
    {
        MC_LOG_ERROR("[MouseHook] fallo hookeando MouseDevice::feed");
        return false;
    }

    MC_LOG("[MouseHook] MouseDevice::feed hookeado @ 0x%llX",
           (unsigned long long)off::fn::MouseDeviceFeed);
    return true;
}

void uninstall()
{
    if (!g_orig)
        return;
    hook::remove((void*)mem::resolve(off::fn::MouseDeviceFeed));
    g_orig = nullptr;
    MC_LOG("[MouseHook] MouseDevice::feed deshookeado");
}

}
