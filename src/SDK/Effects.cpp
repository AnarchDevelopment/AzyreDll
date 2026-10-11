#include "SDK/Effects.hpp"

#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/Game.hpp"

#include <MinHook.h>
#include <windows.h>

namespace mc::effects {

namespace {

enum class Source
{
    Unknown,
    LocalPlayer,
    Client,
    Level,
    ScreenContext,
};

Source g_source = Source::Unknown;

constexpr int kMaxEffects = 32;
constexpr int kMaxTicks = 200000000;      // ~116 dias: cubre /effect infinitos
constexpr int kPermanentTicks = 900000;  // ~12.5h: beacons y efectos infinitos

// --- Hook del render: cachea el ScreenContext (arg 2) ---
// La firma completa de la funcion es desconocida, asi que el detour es un
// thunk en una pagina ejecutable propia:
//   mov qword ptr [rip+25], rdx   ; cachea ScreenContext en el slot local
//   mov rax, <trampoline>         ; parcheado ANTES de habilitar el hook
//   jmp rax                       ; el original sigue con TODOS sus args
// Nunca se toca la pila ni otro registro: el ret del juego vuelve a su
// caller sin enterarse del hook.
constexpr size_t kThunkSlot = 32; // cache del ctx dentro de la misma pagina
void* g_thunkPage = nullptr;
void* g_hookTarget = nullptr;

void* cachedScreenContext()
{
    if (!g_thunkPage)
        return nullptr;
    return *(void**)((uint8_t*)g_thunkPage + kThunkSlot);
}

// Valida y lee el vector de efectos de `base`.
//  - strict=true (probes del scanner): si UNA entrada esta fuera de rango,
//    el vector completo se considera invalido (evita fijar una base falsa).
//  - strict=false (camino del ScreenContext, validado por el prototipo):
//    las entradas raras se filtran por entrada, igual que phf.py
//    (if (id > 0 && duration > 0)) — el vector de Bedrock puede contener
//    slots de efectos vencidos con id 0 o duraciones negativas.
bool readVector(uintptr_t base, std::vector<Effect>& out, bool strict)
{
    out.clear();
    if (!base || !mem::isReadable(base + off::effects::VectorBegin, 16))
        return false;

    uintptr_t b = mem::read<uintptr_t>(base + off::effects::VectorBegin);
    uintptr_t e = mem::read<uintptr_t>(base + off::effects::VectorEnd);
    if (!b || e < b)
        return false;

    size_t bytes = e - b;
    if (bytes == 0)
        return true; // vector valido, sin efectos

    if (bytes % 16 != 0 || bytes > 16 * kMaxEffects)
        return false;
    if (!mem::isReadable(b, bytes))
        return false;

    for (size_t i = 0; i < bytes / 16; ++i)
    {
        int id = mem::read<int>(b + i * 16);
        int dur = mem::read<int>(b + i * 16 + 4);
        int amp = mem::read<int>(b + i * 16 + 8);
        if (strict && (id < 1 || id > 63 || dur < -1 || dur > kMaxTicks || amp < 0 || amp > 15))
            return false;
        // Mismo filtro por entrada que el prototipo; solo ids mostrables.
        if (id >= 1 && id <= 30 && dur > 0)
            out.push_back({id, dur, amp});
    }
    return true;
}

// Sondea un candidato. Solo fija la fuente si el vector es no vacio (un
// vector vacio valida trivialmente y no sirve para distinguir).
bool probe(uintptr_t base, Source s, std::vector<Effect>& out, bool strict)
{
    if (!readVector(base, out, strict))
    {
        out.clear();
        return false;
    }
    if (out.empty() && g_source == Source::Unknown)
        return false;
    if (g_source == Source::Unknown)
        MC_LOG("[Effects] fuente de efectos: %s",
               s == Source::LocalPlayer ? "LocalPlayer"
                                       : (s == Source::Client
                                              ? "Client"
                                              : (s == Source::Level ? "Level"
                                                                    : "ScreenContext")));
    g_source = s;
    return true;
}

// Dump one-shot del camino del ScreenContext para diagnosticar la cadena
// (ctx -> owner -> vector -> entradas crudas).
void debugDumpCtx()
{
    void* ctx = cachedScreenContext();
    MC_LOG("[Effects][dbg] ScreenContext cacheado: %p", ctx);
    if (!ctx)
        return;
    if (!mem::isReadable((uintptr_t)ctx + off::effects::CtxOwner, 8))
    {
        MC_LOG("[Effects][dbg] ctx+0x290 no legible");
        return;
    }
    uintptr_t owner = mem::read<uintptr_t>((uintptr_t)ctx + off::effects::CtxOwner);
    MC_LOG("[Effects][dbg] owner(+0x290): %p", (void*)owner);
    if (!owner || !mem::isReadable(owner + off::effects::VectorBegin, 16))
    {
        MC_LOG("[Effects][dbg] owner sin vector legible");
        return;
    }
    uintptr_t b = mem::read<uintptr_t>(owner + off::effects::VectorBegin);
    uintptr_t e = mem::read<uintptr_t>(owner + off::effects::VectorEnd);
    long long count = (b && e > b) ? (long long)((e - b) / 16) : 0;
    MC_LOG("[Effects][dbg] vector: begin=%p end=%p count=%lld", (void*)b, (void*)e, count);
    if (count > 0 && count <= kMaxEffects && mem::isReadable(b, (size_t)count * 16))
    {
        for (long long i = 0; i < count && i < 4; ++i)
            MC_LOG("[Effects][dbg] entry %lld: id=%d dur=%d amp=%d", i,
                   mem::read<int>(b + (size_t)i * 16),
                   mem::read<int>(b + (size_t)i * 16 + 4),
                   mem::read<int>(b + (size_t)i * 16 + 8));
    }
}

} // namespace

void install()
{
    if (g_thunkPage)
        return;
    if (!hook::initialize())
        return;

    uintptr_t target = mem::resolve(off::fn::RenderWithScreenContext);
    if (!target || !mem::isReadable(target, 16))
    {
        MC_LOG_ERROR("[Effects] render target no legible (0x%llX)",
                     (unsigned long long)off::fn::RenderWithScreenContext);
        return;
    }

    uint8_t* page = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE,
                                           PAGE_EXECUTE_READWRITE);
    if (!page)
    {
        MC_LOG_ERROR("[Effects] VirtualAlloc del thunk fallo");
        return;
    }

    // mov qword ptr [rip+25], rdx  -> cache en page+32
    page[0] = 0x48;
    page[1] = 0x89;
    page[2] = 0x15;
    int32_t disp = (int32_t)(kThunkSlot - 7);
    memcpy(page + 3, &disp, 4);
    // mov rax, imm64 (placeholder: se parchea antes de habilitar)
    page[7] = 0x48;
    page[8] = 0xB8;
    memset(page + 9, 0, 8);
    // jmp rax
    page[17] = 0xFF;
    page[18] = 0xE0;

    void* trampoline = nullptr;
    MH_STATUS st = MH_CreateHook((void*)target, page, &trampoline);
    if (st != MH_OK)
    {
        MC_LOG_ERROR("[Effects] MH_CreateHook(render) fallo: %d", (int)st);
        VirtualFree(page, 0, MEM_RELEASE);
        return;
    }

    // Parchar el salto ANTES de habilitar: sin ventana de carrera.
    uintptr_t tramp = (uintptr_t)trampoline;
    memcpy(page + 9, &tramp, 8);

    st = MH_EnableHook((void*)target);
    if (st != MH_OK)
    {
        MC_LOG_ERROR("[Effects] MH_EnableHook(render) fallo: %d", (int)st);
        MH_RemoveHook((void*)target);
        VirtualFree(page, 0, MEM_RELEASE);
        return;
    }

    g_thunkPage = page;
    g_hookTarget = (void*)target;
    MC_LOG("[Effects] hook del render activo (thunk @ %p)", page);
}

void uninstall()
{
    if (g_hookTarget)
        hook::remove(g_hookTarget);
    if (g_thunkPage)
        VirtualFree(g_thunkPage, 0, MEM_RELEASE);
    g_thunkPage = nullptr;
    g_hookTarget = nullptr;
}

std::vector<Effect> active()
{
    std::vector<Effect> out;
    Game& g = Game::get();

    // Dueno del vector via ScreenContext (cacheado por el hook del render).
    void* ctx = cachedScreenContext();
    uintptr_t ctxOwner = 0;
    if (ctx)
        ctxOwner = mem::read<uintptr_t>((uintptr_t)ctx + off::effects::CtxOwner);

    // Dump one-shot: primera vez que hay ctx cacheado con jugador en juego.
    static bool dumped = false;
    if (!dumped && ctx && g.localFound())
    {
        dumped = true;
        debugDumpCtx();
    }

    // 1. Fuente ya fijada; si se corrompio (respawn/rejoin) se re-sondea.
    switch (g_source)
    {
    case Source::ScreenContext:
        if (ctxOwner && probe(ctxOwner, Source::ScreenContext, out, false))
            return out;
        break;
    case Source::LocalPlayer:
        if (probe(g.localAddr(), Source::LocalPlayer, out, false))
            return out;
        break;
    case Source::Client:
        if (probe(g.clientAddr(), Source::Client, out, false))
            return out;
        break;
    case Source::Level:
        if (probe(g.levelAddr(), Source::Level, out, false))
            return out;
        break;
    default:
        break;
    }
    if (g_source != Source::Unknown && out.empty())
        g_source = Source::Unknown;

    // 2. Sondeo: ScreenContext primero (camino validado por el prototipo,
    //    lectura leniente como phf.py), luego las bases del scanner con
    //    validacion estricta.
    if (ctxOwner && probe(ctxOwner, Source::ScreenContext, out, false))
        return out;
    if (probe(g.localAddr(), Source::LocalPlayer, out, true))
        return out;
    if (probe(g.clientAddr(), Source::Client, out, true))
        return out;
    if (probe(g.levelAddr(), Source::Level, out, true))
        return out;

    out.clear();
    return out;
}

bool sourceLocked()
{
    return g_source != Source::Unknown;
}

}
