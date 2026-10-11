#include "ChatHook.hpp"

#include "Commands/CommandManager.hpp"
#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "Modules/ModuleManager.hpp"
#include "SDK/Game.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

#include <windows.h>

namespace mc::chathook {

namespace {

using SendToServerFn = void* (*)(void*, void*, void*);
SendToServerFn origSendToServer = nullptr;

// addMessage requiere SEH en funcion aparte: MSVC no permite __try en
// funciones con objetos que requieren unwinding (std::string).
bool sehAddMessage(void* chatCtrl, const std::string* text, const std::string* empty)
{
    using AddMessageFn = void* (*)(void*, const std::string*, const std::string*, int, char);
    __try
    {
        auto fn = reinterpret_cast<AddMessageFn>(mem::resolve(off::fn::ChatAddMessage));
        fn(chatCtrl, text, empty, 200, 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Cancelar el envio reescribiendo el mensaje como SSO " ": el paquete sigue
// su curso pero el comando no llega al servidor. Los comandos largos que ya
// vivian en heap pierden su buffer (fuga minima, aceptable).
void cancelTextMessage(uintptr_t packet)
{
    const uint8_t space = ' ';
    const uint8_t nul = 0;
    const uint64_t one = 1;
    const uint64_t ssoCap = 15;
    mem::write<uint8_t>(packet + off::textPacket::Message, space);
    mem::write<uint8_t>(packet + off::textPacket::Message + 1, nul);
    mem::write<uint64_t>(packet + off::textPacket::MessageSize, one);
    mem::write<uint64_t>(packet + off::textPacket::MessageCap, ssoCap);
}

void* hkSendToServer(void* self, void* networkId, void* packet)
{
    if (packet)
    {
        uintptr_t p = reinterpret_cast<uintptr_t>(packet);
        if (mem::read<uintptr_t>(p) == mem::resolve(vt::TextPacket))
        {
            std::string msg = mem::readString(p + off::textPacket::Message);
            if (commands::isCommand(msg))
            {
                cancelTextMessage(p);
                ModuleManager::get().queueCommand(msg);
            }
        }
    }
    return origSendToServer(self, networkId, packet);
}

} // namespace

bool install()
{
    if (origSendToServer)
        return true;

    void* target = reinterpret_cast<void*>(mem::resolve(off::fn::SendToServer));
    if (!hook::create(target, reinterpret_cast<void*>(&hkSendToServer),
                      reinterpret_cast<void**>(&origSendToServer)))
    {
        MC_LOG_ERROR("[Chat] Cannot hook sendToServer");
        return false;
    }
    MC_LOG("[Chat] sendToServer hook active");
    return true;
}

void uninstall()
{
    if (!origSendToServer)
        return;
    hook::remove(reinterpret_cast<void*>(mem::resolve(off::fn::SendToServer)));
    origSendToServer = nullptr;
    MC_LOG("[Chat] sendToServer hook removed");
}

bool display(const std::string& text)
{
    if (text.empty())
        return false;

    Game& g = Game::get();
    if (!g.clientAddr())
        return false;

    Chat chat = g.chat();
    if (!chat.valid())
        return false;

    // addMessage(ctrl, source, message, type, flag) — el PRIMER string es el
    // autor que el renderer envuelve en <>. Poner el texto como MESSAGE y
    // el autor vacio: sin <> en el render.
    std::string empty;
    if (!sehAddMessage(reinterpret_cast<void*>(chat.address()), &empty, &text))
    {
        MC_LOG_ERROR("[Chat] addMessage failed for display()");
        return false;
    }
    return true;
}

}
