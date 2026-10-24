#include "ChatHook.hpp"

#include "Commands/CommandManager.hpp"
#include "Features/ChatSender.hpp"
#include "Features/PacketHelper.hpp"
#include "Features/Rotation.hpp"
#include "Features/RotationHelper.hpp"
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

// ============================================================================
// Packet tracer â€” diagnostico de colocacion.
// TRES sondas:
//   1. sendToServer (0x38B940) â€” la via del Chat/Animate/LevelEvent.
//      El place NATURAL no manda el UseItemPacket por aqui (probado).
//   2. PacketSender::send + BatchedPacketSender::send (vtable slot 2) â€”
//      LA via real de los paquetes de gameplay. El hook se resuelve de
//      las vtables en runtime. Aqui el UseItemPacket del right-click
//      aparece con sus campos + backtrace = la ruta de colocacion
//      verdadera del juego.
// Con la ventana armada todo queda en el log; relay generico de 6 slots
// con SEH â€” las firmas reales tienen <= 2-3 params (el resto se ignora).
// ============================================================================
ULONGLONG g_pktTraceUntil = 0;
unsigned g_pktTraceLogs = 0;

void logTraceFrame(int idx, void* frame)
{
    HMODULE hm = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void*)frame, &hm);
    uintptr_t a = (uintptr_t)frame;
    uintptr_t mb = (uintptr_t)hm;
    char mod[32] = "??";
    if (hm)
    {
        if (mb == (uintptr_t)GetModuleHandleW(nullptr))
        {
            snprintf(mod, sizeof(mod), "game");
        }
        else
        {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(hm, path, MAX_PATH);
            const wchar_t* slash = wcsrchr(path, L'\\');
            wcstombs(mod, slash ? slash + 1 : path, sizeof(mod) - 1);
        }
    }
    MC_LOG("[PktTrace]   #%d %p  RVA 0x%llX (%s)", idx, frame,
           (unsigned long long)(a >= mb ? a - mb : a), mod);
}

void traceOutgoingPacket(uintptr_t p, const char* srcTag)
{
    ULONGLONG now = GetTickCount64();
    if (now > g_pktTraceUntil)
        return;

    uintptr_t vt = mem::read<uintptr_t>(p);
    uintptr_t rva = (vt >= mem::base()) ? (vt - mem::base()) : vt;

    // Ruido fuera: el Animate/LevelEvent spamea cada frame y quema el
    // presupuesto antes del evento que importa (le paso al build nuevo).
    if (vt == mem::resolve(vt::AnimatePacket) ||
        vt == mem::resolve(vt::LevelEventPacket))
        return;

    // Cap POR VTABLE: 8 para el UseItemPacket (no perder ni uno para el
    // diff natural-vs-scaffold), 4 para el resto.
    static uintptr_t seenVts[16] = {};
    static unsigned seenCount[16] = {};
    int slot = -1;
    for (int i = 0; i < 16; ++i)
    {
        if (seenVts[i] == vt)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (seenVts[i] == 0)
            {
                seenVts[i] = vt;
                slot = i;
                break;
            }
        }
    }
    if (slot >= 0)
    {
        unsigned cap = (vt == mem::resolve(vt::UseItemPacket)) ? 8 : 4;
        if (seenCount[slot] >= cap)
            return;
        ++seenCount[slot];
    }

    MC_LOG("[PktTrace]%s packet vtable RVA 0x%llX",
           srcTag ? srcTag : "", (unsigned long long)rva);

    if (vt == mem::resolve(vt::MovePlayerPacket))
    {
        // Dump de los primeros floats del paquete: con el Trace armado,
        // mover la camara (mirar abajo/arriba) revela cuales son
        // pitch/yaw/headYaw — la confirmacion empirica de los offsets
        // tentativos de off::movePlayerPacket.
        float f[10] = {};
        bool readable = true;
        for (int i = 0; i < 10; ++i)
        {
            if (!mem::rawRead(&f[i], p + 0x08 + (uintptr_t)i * 4, 4))
            {
                readable = false;
                break;
            }
        }
        if (readable)
        {
            MC_LOG("[PktTrace] MovePlayer: "
                   "+08=%d(id?) +0C=%.2f +10=(%.2f,%.2f,%.2f) "
                   "+1C=(%.2f,%.2f,%.2f) +28=%.2f",
                   (int)*(int*)&f[0], f[1],
                   f[2], f[3], f[4],
                   f[5], f[6], f[7], f[8]);
        }
        return;
    }

    if (vt != mem::resolve(vt::UseItemPacket))
        return;

    // Dump campo por campo del UseItemPacket (layout confirmado por el
    // decompile del ctor — offsets corregidos):
    //   +0x08 Type=2, +0x10 X/Y/Z, +0x1C Face, +0x20 Click Vec3,
    //   +0x2C Player Vec3, +0x38 ItemStack, +0x58 Flag.
    int type = mem::read<int>(p + off::useItemPacket::Type);
    int x = mem::read<int>(p + off::useItemPacket::X);
    int y = mem::read<int>(p + off::useItemPacket::Y);
    int z = mem::read<int>(p + off::useItemPacket::Z);
    int face = mem::read<uint8_t>(p + off::useItemPacket::Face);
    float ckx = mem::read<float>(p + off::useItemPacket::ClickX);
    float cky = mem::read<float>(p + off::useItemPacket::ClickY);
    float ckz = mem::read<float>(p + off::useItemPacket::ClickZ);
    float plx = mem::read<float>(p + off::useItemPacket::PlayerX);
    float ply = mem::read<float>(p + off::useItemPacket::PlayerY);
    float plz = mem::read<float>(p + off::useItemPacket::PlayerZ);
    int flag = mem::read<int>(p + off::useItemPacket::Flag);
    MC_LOG("[PktTrace] UseItemPacket: type=%d pos=(%d,%d,%d) face=%d "
           "click=(%.2f,%.2f,%.2f) player=(%.2f,%.2f,%.2f) flag=%d",
           type, x, y, z, face, ckx, cky, ckz, plx, ply, plz, flag);

    // Backtrace: la ruta de llamadas que construyo+envio el paquete.
    void* frames[12] = {};
    USHORT n = CaptureStackBackTrace(0, 12, frames, nullptr);
    for (USHORT i = 0; i < n && i < 10; ++i)
        logTraceFrame((int)i, frames[i]);
}

// ---- Hooks de PacketSender::send / BatchedPacketSender::send ----
// La via REAL: el place natural manda el UseItemPacket por aca (probado:
// sendToServer solo vio Animate + LevelEvent). El backtrace desde aqui
// nombra la ruta de colocacion del juego.
struct SenderHook
{
    void* target = nullptr;
    void* original = nullptr;
    bool installed = false;
};

SenderHook g_senderHooks[2];

long long __fastcall senderDetour0(void* a1, void* a2, void* a3, void* a4,
                                   void* a5, void* a6)
{
    if (a2)
        traceOutgoingPacket((uintptr_t)a2, "[S0]");
    auto o = reinterpret_cast<long long(__fastcall*)(void*, void*, void*, void*,
                                                     void*, void*)>(
        g_senderHooks[0].original);
    long long r = 0;
    __try
    {
        r = o(a1, a2, a3, a4, a5, a6);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return r;
}

long long __fastcall senderDetour1(void* a1, void* a2, void* a3, void* a4,
                                   void* a5, void* a6)
{
    if (a2)
        traceOutgoingPacket((uintptr_t)a2, "[S1]");
    auto o = reinterpret_cast<long long(__fastcall*)(void*, void*, void*, void*,
                                                     void*, void*)>(
        g_senderHooks[1].original);
    long long r = 0;
    __try
    {
        r = o(a1, a2, a3, a4, a5, a6);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return r;
}

void installSenderTraceHooks()
{
    if (!hook::initialize())
        return;

    struct VtSource
    {
        uintptr_t rva;
        void* detour;
    };
    const VtSource sources[2] = {
        {vt::PacketSender, (void*)&senderDetour0},
        {vt::BatchedPacketSender, (void*)&senderDetour1},
    };

    for (int i = 0; i < 2; ++i)
    {
        if (g_senderHooks[i].installed)
            continue;
        uintptr_t vt = mem::resolve(sources[i].rva);
        if (!vt || !mem::isReadable(vt + 16, 8))
            continue;
        // slot 2 = send(Packet&) â€” resuelto de la vtable EN RUNTIME.
        void* target = (void*)mem::read<uintptr_t>(vt + 16);
        if (!target)
            continue;
        if (hook::create(target, sources[i].detour, &g_senderHooks[i].original))
        {
            g_senderHooks[i].target = target;
            g_senderHooks[i].installed = true;
            MC_LOG("[PktTrace] sender hook %d activo (fn %p)", i, target);
        }
    }
}

// Handles capturados de los envios del propio juego: con estos podemos
// inyectar paquetes propios por la misma via (sendRawPacket).
void* g_capSelf = nullptr;
void* g_capNetId = nullptr;

// ============================================================================
// Silent rotations GLOBALES (Features/Rotation.hpp) — el equivalente del
// onPacketOutEvent de Solstice para este build pre-1.16: la rotacion del
// cliente viaja en el MovePlayerPacket (vtable 0xE644F8). Cuando hay una
// request vigente (rot::request de cualquier modulo), sobrescribimos
// pitch/yaw/headYaw del paquete ANTES de que origSendToServer lo
// serialice — el server ve el place "mirando" al objetivo, la camara
// del cliente nunca se mueve.
// ============================================================================
void applyRotation(uintptr_t p)
{
    // GATE: los offsets del paquete no estan confirmados — escribir
    // tentativos puede corromper la POSICION del paquete y desincronizar
    // al player con el server. Inerte hasta confirmar con [MPPDump].
    if (!off::movePlayerPacket::OffsetsConfirmed)
        return;

    if (mem::read<uintptr_t>(p) != mem::resolve(vt::MovePlayerPacket))
        return;

    // Solo tocar el paquete si los campos son legibles; mem::write es
    // SEH-guardado (un offset mal no tumba el proceso).
    if (!mem::isReadable(p + off::movePlayerPacket::Pitch, 12))
        return;

    float pitch = 0.0f, yaw = 0.0f, headYaw = 0.0f;
    if (rot::current(pitch, yaw, &headYaw))
    {
        mem::write<float>(p + off::movePlayerPacket::Pitch, pitch);
        mem::write<float>(p + off::movePlayerPacket::Yaw, yaw);
        mem::write<float>(p + off::movePlayerPacket::HeadYaw, headYaw);
    }
    else if (rotation_helper::outgoingHeadYaw(yaw))
    {
        // The visual helper temporarily drives Actor+0x218, which the packet
        // constructor also copies into headYaw. Restore the real camera yaw
        // in the outgoing packet so this option remains client-only.
        mem::write<float>(p + off::movePlayerPacket::HeadYaw, yaw);
    }
}

// ============================================================================
// Auto-dump del MovePlayerPacket: los primeros 4 paquetes de movimiento
// de la sesion quedan en el log ([MPPDump]) junto con la rotacion REAL
// del actor como referencia. Comparando que floats del paquete coinciden
// con el actor se confirman los offsets de pitch/yaw/headYaw — sin
// tracer y sin tocar nada.
// ============================================================================
void mppAutoDump(uintptr_t p)
{
    // Cazar hasta 6 paquetes CON el actor ya resuelto y EN MOVIMIENTO
    // (la render pos solo se distingue de la logica cuando el actor
    // avanza: la logica salta por tick, la render interpola por frame).
    static int s_withActor = 0;
    static int s_tries = 0;
    static Vec3 s_lastDumpPos{};
    static bool s_haveLast = false;
    if (s_withActor >= 6 || s_tries >= 400)
        return;
    if (mem::read<uintptr_t>(p) != mem::resolve(vt::MovePlayerPacket))
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return; // sin actor: ni contar el intento
    LocalPlayer lp = game.localPlayer();
    if (!lp.valid())
        return;

    ++s_tries;

    // Solo dumpear en movimiento: la pos del actor debe haber cambiado
    // desde el ultimo dump (los del spawn quieto no sirven).
    Vec3 actorPos = lp.posExact();
    if (s_haveLast)
    {
        float ddx = actorPos.x - s_lastDumpPos.x;
        float ddy = actorPos.y - s_lastDumpPos.y;
        float ddz = actorPos.z - s_lastDumpPos.z;
        if (ddx * ddx + ddy * ddy + ddz * ddz < 0.0004f) // < 0.02 bloques
            return;
    }
    s_lastDumpPos = actorPos;
    s_haveLast = true;

    float f[12] = {};
    for (int i = 0; i < 12; ++i)
    {
        if (!mem::rawRead(&f[i], p + 0x08 + (uintptr_t)i * 4, 4))
            return;
    }

    float a[18] = {}; // dump del ACTOR +0x38..+0x7C (zona de rotaciones)
    for (int i = 0; i < 18; ++i)
        mem::rawRead(&a[i], lp.address() + 0x38 + (uintptr_t)i * 4, 4);

    ++s_withActor;
    MC_LOG("[MPPDump] actor(p=%.2f y=%.2f) | pk+1C=%.2f +20=%.2f +24(pitch)=%.2f",
           lp.pitch(), lp.yaw(), f[5], f[6], f[7]);

    // Zona baja del actor (+0x00..+0x34): buscamos la RENDER POS — la
    // copia interpolada de mPosition (+0x0C) que el motor usa para
    // dibujar el modelo (la analogia de los +0x44/0x48 de la rotacion).
    // Mientras caminas: la pos logica (+0x0C) SALTA por tick; la render
    // pos cambia cada frame con deltas pequenos. Si existe, los ESP la
    // leen directa: cero lag, cero paro, calza 1:1 con el modelo.
    {
        float a0[14] = {};
        bool ok0 = true;
        for (int i = 0; i < 14; ++i)
        {
            if (!mem::rawRead(&a0[i], lp.address() + (uintptr_t)i * 4, 4))
            {
                ok0 = false;
                break;
            }
        }
        if (ok0)
        {
            char buf[512];
            int n = snprintf(buf, sizeof(buf), "[MPPDump] actor+00:");
            for (int i = 0; i < 14 && n > 0 && n < (int)sizeof(buf) - 10; ++i)
            {
                uintptr_t off = (uintptr_t)i * 4;
                n += snprintf(buf + n, sizeof(buf) - n, " %llx=%.3f",
                              (unsigned long long)off, a0[i]);
            }
            MC_LOG("%s", buf);
        }
    }

    // Zona de rotacion del actor: los floats que SIGAN al yaw/pitch del
    // paquete son los campos de rotacion del actor usados para el
    // render del modelo (headYaw/bodyYaw) — escribir SOLO esos gira la
    // cabeza sin mover la camara.
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "[MPPDump] actor+38:");
    for (int i = 0; i < 18 && n > 0 && n < (int)sizeof(buf) - 12; ++i)
    {
        uintptr_t off = 0x38 + (uintptr_t)i * 4;
        n += snprintf(buf + n, sizeof(buf) - n, " %llx=%.2f",
                      (unsigned long long)off, a[i]);
    }
    MC_LOG("%s", buf);
}

void* hkSendToServer(void* self, void* networkId, void* packet)
{
    g_capSelf = self;
    g_capNetId = networkId;
    if (packet)
    {
        uintptr_t p = reinterpret_cast<uintptr_t>(packet);
        if (blink::onOutgoingPacket(p))
            return nullptr; // capturado por el Blink: no sale al server
        mppAutoDump(p);
        applyRotation(p);
        traceOutgoingPacket(p, "[SS]");
        if (mem::read<uintptr_t>(p) == mem::resolve(vt::TextPacket))
        {
            // Plantilla para Spammer/Killsults: clona un chat real saliente
            // (xuid/platform id incluidos) para reenvios propios.
            chatsend::onOutgoingTextPacket(p);

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

    // addMessage(ctrl, source, message, type, flag) â€” el PRIMER string es el
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

void armPacketTrace(unsigned seconds)
{
    // El hook de sendToServer debe estar vivo para ver los paquetes.
    install();
    // Y los del sender REAL (donde viaja el UseItemPacket del place).
    installSenderTraceHooks();
    g_pktTraceUntil = GetTickCount64() + (ULONGLONG)seconds * 1000ull;
    g_pktTraceLogs = 0;
    MC_LOG("[PktTrace] ARMADO %us â€” sendToServer + PacketSender::send; "
           "coloca un bloque con RIGHT-CLICK natural",
           seconds);
}

bool sendRawPacket(void* packet)
{
    if (!origSendToServer || !g_capSelf || !g_capNetId || !packet)
        return false;

    // SEH aparte: el paquete construido a mano puede tener campos
    // imperfectos; una falta no debe propagarse al hilo del juego.
    bool ok = false;
    __try
    {
        origSendToServer(g_capSelf, g_capNetId, packet);
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    return ok;
}

}
