#include "PacketSender.hpp"

#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Engine/Memory/FunctionRvas.hpp"

#include <excpt.h>

#include <windows.h>

namespace mc::packets {

namespace {

// Motivo throttled: un envio rechazado deja rastro en el log en vez de
// desaparecer. 1s de ventana para que un fallo por frame no lo inunde.
void logReject(const char* reason, uintptr_t sender, uintptr_t value)
{
    static uint64_t lastMs = 0;
    const uint64_t nowMs = GetTickCount64();
    if (nowMs - lastMs < 1000)
        return;
    lastMs = nowMs;
    MC_LOG("[PktSend] rejected: %s (sender=%p value=0x%llX)", reason,
           reinterpret_cast<void*>(sender), static_cast<unsigned long long>(value));
}

bool isPureCall(uintptr_t fn)
{
    const uintptr_t pureCall = mem::resolve(off::fn::PureCall);
    return pureCall != 0 && fn == pureCall;
}

} // namespace

bool PacketSender::send(uintptr_t sender, void* packet)
{
    if (!sender || !packet || !mem::isReadable(sender, sizeof(uintptr_t)))
    {
        logReject("unknown sender", sender, 0);
        return false;
    }

    const uintptr_t senderVtable = mem::read<uintptr_t>(sender);
    if (senderVtable != mem::resolve(VTableRva) &&
        senderVtable != mem::resolve(BatchedVTableRva))
    {
        logReject("unknown sender vtable", sender, senderVtable);
        return false;
    }

    const uintptr_t slotAddress = senderVtable + SendSlot * sizeof(uintptr_t);
    if (!mem::isReadable(slotAddress, sizeof(uintptr_t)))
    {
        logReject("send slot unreadable", sender, senderVtable);
        return false;
    }

    const uintptr_t sendFn = mem::read<uintptr_t>(slotAddress);
    if (!sendFn || !mem::isReadable(sendFn, 1))
    {
        logReject("send function unreadable", sender, sendFn);
        return false;
    }

    // La vtable base PacketSender (0xE699D0) es abstracta: todas sus ranuras
    // son el thunk _purecall. Despachar contra ella es un fast-fail de MSVC
    // que __try/__except no captura (mata el proceso en vez de lanzar una
    // excepcion SEH). El envio real solo puede pasar por el BatchedPacketSender.
    if (isPureCall(sendFn))
    {
        logReject("abstract PacketSender (_purecall)", sender, senderVtable);
        return false;
    }

    __try
    {
        using SendFn = void(__fastcall*)(void*, void*);
        reinterpret_cast<SendFn>(sendFn)(reinterpret_cast<void*>(sender), packet);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        logReject("send threw", sender, sendFn);
        return false;
    }
}

bool PacketSender::sendGameplay(void* packet)
{
    const uintptr_t client = Game::get().clientAddr();
    if (!client || !mem::isReadable(client, ClientConnectorOffset + sizeof(uintptr_t)))
        return false;

    const uintptr_t connector = mem::read<uintptr_t>(client + ClientConnectorOffset);
    if (!connector ||
        !mem::isReadable(connector, ConnectorSenderOffset + sizeof(uintptr_t)))
        return false;

    const uintptr_t sender = mem::read<uintptr_t>(connector + ConnectorSenderOffset);
    return send(sender, packet);
}

} // namespace mc::packets
