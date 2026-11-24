#include "InteractPacket.hpp"

#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Entity/Actor.hpp"
#include "SDK/Network/Packets/PacketSender.hpp"

#include <cstddef>
#include <cstdint>

#include <windows.h>

namespace mc::packets {

namespace {

struct InteractAttackData
{
    uintptr_t vtable;
    uint32_t type;
    uint32_t field;
    uint8_t action;
    uint8_t padding[7];
    uint64_t targetRuntimeId;
};
static_assert(sizeof(InteractAttackData) == 0x20);
static_assert(offsetof(InteractAttackData, action) == InteractPacket::ActionOffset);
static_assert(offsetof(InteractAttackData, targetRuntimeId) ==
              InteractPacket::TargetRuntimeIdOffset);

// Cada ataque que no llega al sender deja ahora un motivo en el log. Antes
// fallaba en silencio y era imposible distinguir "no hay objetivo" de
// "el sender rechazo el paquete". Throttle de 1s para no inundar.
void logAttackReject(const char* reason, uintptr_t value)
{
    static uint64_t lastMs = 0;
    const uint64_t nowMs = GetTickCount64();
    if (nowMs - lastMs < 1000)
        return;
    lastMs = nowMs;
    MC_LOG("[Attack] rejected: %s (value=0x%llX)", reason,
           static_cast<unsigned long long>(value));
}

} // namespace

bool InteractPacket::sendAttack(const Actor& target)
{
    if (!target.valid())
    {
        logAttackReject("target not readable", 0);
        return false;
    }

    const uint64_t runtimeId = target.runtimeId();
    // -1 significa "sin runtime id asignado": el handler nativo le pide uno
    // al mundo antes de armar el paquete. Aqui no se puede resolver ese
    // camino todavia, pero al menos queda registrado en vez de fallar mudo.
    if (runtimeId == 0 || runtimeId == UINT64_MAX)
    {
        logAttackReject("target without usable runtime id",
                        static_cast<uintptr_t>(runtimeId));
        return false;
    }

    InteractAttackData packet{};
    packet.vtable = mem::resolve(VTableRva);
    if (!packet.vtable)
    {
        logAttackReject("interact vtable unresolved", VTableRva);
        return false;
    }
    packet.type = 2;
    packet.field = 1;
    packet.action = AttackAction;
    packet.targetRuntimeId = runtimeId;

    if (!PacketSender::sendGameplay(&packet))
    {
        logAttackReject("gameplay sender refused the packet",
                        static_cast<uintptr_t>(runtimeId));
        return false;
    }
    return true;
}

} // namespace mc::packets
