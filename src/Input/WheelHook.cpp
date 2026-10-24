#include "WheelHook.hpp"

#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "GUI/Menu.hpp"

#include <MinHook.h>
#include <atomic>

namespace mc::wheel {

static HMODULE g_module = nullptr;
static HHOOK g_hook = nullptr;
static HANDLE g_thread = nullptr;
static DWORD g_threadId = 0;
static std::atomic<int> g_units{0};
static std::atomic<int> g_lastSource{-1};
static std::atomic<unsigned long long> g_lastSourceTime{0};
static bool g_loggedWinrt = false;
static bool g_loggedLL = false;
static bool g_loggedMsg = false;
static bool g_loggedGame = false;
static float g_lastValue = 0.0f;

// Varias rutas pueden capturar el MISMO giro (raw input, WH_MOUSE_LL, WinRT
// CoreWindow, mensajes): wheelFeed deduplica por fuente para que un mismo
// gesto no se cuente dos veces.
enum WheelSource
{
    SRC_GAME = 0,
    SRC_RAW = 1,
    SRC_WINRT = 2,
    SRC_WINRT_DIRECT = 3,
    SRC_MSG = 4,
    SRC_LL = 5
};

static void wheelFeed(int delta, int source)
{
    if (delta == 0)
        return;
    unsigned long long now = GetTickCount64();
    int last = g_lastSource.load(std::memory_order_relaxed);
    if (last != -1 && last != source &&
        (now - g_lastSourceTime.load(std::memory_order_relaxed)) < 25ull)
        return; // mismo gesto capturado por otra ruta: ignorar
    g_lastSource.store(source, std::memory_order_relaxed);
    g_lastSourceTime.store(now, std::memory_order_relaxed);
    g_units.fetch_add(delta, std::memory_order_relaxed);
}

static bool g_msgHooked = false;
static LPVOID g_peekTarget = nullptr;
static LPVOID g_getTarget = nullptr;
static decltype(&PeekMessageW) oPeek = nullptr;
static decltype(&GetMessageW) oGet = nullptr;

// --- Raw input: la via del juego ACTIVO ---
// El juego registra Raw Input para el mouse (por eso WH_MOUSE_LL no ve
// nada con la ventana activa) y llama GetRawInputData/GetRawInputBuffer
// para procesar sus WM_INPUT. Hookeamos las DOS y observamos el RAWINPUT
// que el propio juego trae: el wheel (RI_MOUSE_WHEEL) viaja ahi aunque el
// juego internamente use el evento WinRT de MouseDevice.
//
// Nota: WIN32_LEAN_AND_MEAN del PCH excluye la seccion de raw input de
// winuser.h, asi que las definiciones minimas viven aqui (layout estable
// del contrato RAWINPUT de x64).
namespace rawx {

constexpr UINT kRidInput = 0x10000003;
constexpr DWORD kRimTypeMouse = 0;
constexpr DWORD kRimTypeKeyboard = 1;
constexpr USHORT kRiMouseWheel = 0x0400;

#pragma pack(push, 8)
struct Header
{
    DWORD dwType;
    DWORD dwSize;
    HANDLE hDevice;
    WPARAM wParam;
};
struct Mouse
{
    USHORT usFlags;
    union
    {
        ULONG ulButtons;
        struct
        {
            USHORT usButtonFlags;
            USHORT usButtonData;
        };
    };
    ULONG ulRawButtons;
    LONG lLastX;
    LONG lLastY;
    ULONG ulExtraInformation;
};
struct Keyboard
{
    USHORT UnitId;
    USHORT MakeCode;
    USHORT Flags;
    USHORT Reserved;
    USHORT VKey;
    USHORT Message;
    USHORT ExtraInformation;
};
struct RawInput
{
    Header header;
    union
    {
        Mouse mouse;
        Keyboard keyboard;
    } data;
};
#pragma pack(pop)

inline const RawInput* nextBlock(const RawInput* cur)
{
    ULONG_PTR p = (ULONG_PTR)cur + cur->header.dwSize;
    return (const RawInput*)((p + 7) & ~(ULONG_PTR)7);
}

} // namespace rawx

static LPVOID g_rawTarget = nullptr;
static LPVOID g_rawBufTarget = nullptr;
using GetRawInputDataFn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using GetRawInputBufferFn = UINT(WINAPI*)(LPVOID, PUINT);
static GetRawInputDataFn oGetRawInputData = nullptr;
static GetRawInputBufferFn oGetRawInputBuffer = nullptr;
static std::atomic<bool> g_loggedRaw{false};
static std::atomic<bool> g_loggedRawKbd{false};

// Syscalls de win32u.dll (NTSTATUS = LONG, __stdcall en x64 == __fastcall ABI).
using NtGetRawInputDataFn = LONG(__stdcall*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using NtGetRawInputBufferFn = LONG(__stdcall*)(LPVOID, PUINT, UINT);

// Tamanio minimo de un bloque RAWINPUT valido (header + mouse) y maximo de
// blocks por buffer (defensa contra dwSize basura -> walk infinito/crash,
// p.ej. al abrir el inventario cuando el buffer trae datos parciales).
static constexpr UINT kMinRawBlockSize = 40;
static constexpr LONG kMaxRawBlocks = 256;

static void feedWheelRaw(int delta)
{
    if (delta == 0)
        return;
    wheelFeed(delta, SRC_RAW);
    bool expected = false;
    if (g_loggedRaw.compare_exchange_strong(expected, true))
        MC_LOG("[Wheel] raw input (GetRawInputData) source active");
}

static void parseRawMouse(const rawx::RawInput* raw)
{
    if (!raw || raw->header.dwType != rawx::kRimTypeMouse)
    {
        // Diagnostics: is keyboard raw input flowing through here at all?
        if (raw && raw->header.dwType == rawx::kRimTypeKeyboard)
        {
            bool expected = false;
            if (g_loggedRawKbd.compare_exchange_strong(expected, true))
                MC_LOG("[Wheel] raw KEYBOARD input seen (game uses raw input for keys)");
        }
        return;
    }
    if (raw->data.mouse.usButtonFlags & rawx::kRiMouseWheel)
    {
        short delta = (short)raw->data.mouse.usButtonData;
        feedWheelRaw(delta);
    }
}

// Input gate: while the menu (or the HUD editor) is open, neutralize the
// raw input the game is about to read so clicks/movement/keys never reach
// the gameplay — the GUI must not attack, place or walk the player. Our
// own ImGui polls GetAsyncKeyState, so the menu itself is unaffected. The
// wheel accumulator is fed by parseRawMouse BEFORE this runs, so menu
// scrolling keeps working while the game gets nothing.
static void gateRawInput(rawx::RawInput* raw)
{
    if (!raw)
        return;
    if (!mc::menu::visible() && !mc::menu::hudEditing())
        return;

    if (raw->header.dwType == rawx::kRimTypeMouse)
    {
        raw->data.mouse.usButtonFlags = 0;
        raw->data.mouse.usButtonData = 0;
        raw->data.mouse.ulRawButtons = 0;
        raw->data.mouse.lLastX = 0;
        raw->data.mouse.lLastY = 0;
    }
    else if (raw->header.dwType == rawx::kRimTypeKeyboard)
    {
        // Inert key event: valid structure, no key. The game reads a
        // keypress with VKey 0 and ignores it.
        raw->data.keyboard.MakeCode = 0;
        raw->data.keyboard.VKey = 0;
        raw->data.keyboard.Message = 0;
    }
}

// Walk validado del buffer RAWINPUT: cada bloque debe caber entero dentro
// del buffer y con dwSize plausible. Con la UI/inventario abierta el buffer
// puede traer datos parciales/sin inicializar -> walk a ciegas = crash.
static void handleRawBuffer(LPVOID data, UINT size, LONG blocks)
{
    if (!data || size == 0)
        return;
    rawx::RawInput* raw = (rawx::RawInput*)data;
    const BYTE* end = (const BYTE*)data + size;

    LONG limit = (blocks > 0 && blocks < kMaxRawBlocks) ? blocks : kMaxRawBlocks;
    for (LONG i = 0; i < limit && (const BYTE*)raw < end; ++i)
    {
        // El header debe caber en el buffer antes de leer dwSize.
        if ((const BYTE*)raw + sizeof(rawx::Header) > end)
            break;

        DWORD sz = raw->header.dwSize;
        if (sz < kMinRawBlockSize ||
            sz > size - (UINT)((const BYTE*)raw - (const BYTE*)data))
            break;

        parseRawMouse(raw);
        gateRawInput(raw);
        raw = (rawx::RawInput*)rawx::nextBlock(raw);
    }
}

static UINT WINAPI hkGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData,
                                     PUINT pcbSize, UINT cbSizeHeader)
{
    UINT r = oGetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
    // r = bytes copiados: validar que el bloque completo cabe antes de leer.
    if (r != (UINT)-1 && r >= 40 && pData && uiCommand == rawx::kRidInput)
    {
        parseRawMouse((const rawx::RawInput*)pData);
        gateRawInput((rawx::RawInput*)pData);
    }
    return r;
}

static UINT WINAPI hkGetRawInputBuffer(LPVOID pData, PUINT pcbSize)
{
    UINT r = oGetRawInputBuffer(pData, pcbSize);
    // r = numero de estructuras escritas. El walk de cada bloque queda
    // delimitado por r y validado por handleRawBuffer (dwSize sane).
    if (r != (UINT)-1 && r > 0 && r < 256 && pData && pcbSize && *pcbSize > 0)
    {
        bool expected = false;
        if (g_loggedRaw.compare_exchange_strong(expected, true))
            MC_LOG("[Wheel] raw input (GetRawInputData) source active");
        handleRawBuffer(pData, *pcbSize, (LONG)r);
    }
    return r;
}

// --- Capa de syscall win32u.dll (NtUserGetRawInputData/Buffer) ---
static NtGetRawInputDataFn oNtGetRawInputData = nullptr;
static NtGetRawInputBufferFn oNtGetRawInputBuffer = nullptr;
static LPVOID g_ntRawTarget = nullptr;
static LPVOID g_ntRawBufTarget = nullptr;
static std::atomic<bool> g_loggedNtRaw{false};

static LONG __stdcall hkNtGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData,
                                          PUINT pcbSize, UINT cbSizeHeader)
{
    LONG r = oNtGetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
    if (r >= (LONG)kMinRawBlockSize && pData && uiCommand == rawx::kRidInput)
    {
        bool expected = false;
        if (g_loggedNtRaw.compare_exchange_strong(expected, true))
            MC_LOG("[Wheel] NtUserGetRawInputData source active");
        parseRawMouse((const rawx::RawInput*)pData);
        gateRawInput((rawx::RawInput*)pData);
    }
    return r;
}

static LONG __stdcall hkNtGetRawInputBuffer(LPVOID pData, PUINT pcbSize, UINT cbSizeHeader)
{
    LONG r = oNtGetRawInputBuffer(pData, pcbSize, cbSizeHeader);
    // r = numero de estructuras escritas (semantica GetRawInputBuffer). Solo
    // procesamos si r es plausible; el walk valida cada bloque.
    if (r > 0 && r < kMaxRawBlocks && pData && pcbSize && *pcbSize > 0)
    {
        bool expected = false;
        if (g_loggedNtRaw.compare_exchange_strong(expected, true))
            MC_LOG("[Wheel] NtUserGetRawInputBuffer source active");
        handleRawBuffer(pData, *pcbSize, r);
    }
    return r;
}

static bool installRawInputHooks()
{
    if (g_rawTarget)
        return true;
    if (!hook::initialize())
    {
        MC_LOG_ERROR("[Wheel] MinHook init failed (raw input)");
        return false;
    }

    MH_STATUS s1 = MH_CreateHookApiEx(L"user32.dll", "GetRawInputData",
                                      reinterpret_cast<LPVOID>(&hkGetRawInputData),
                                      reinterpret_cast<LPVOID*>(&oGetRawInputData), &g_rawTarget);
    MH_STATUS s2 = MH_CreateHookApiEx(L"user32.dll", "GetRawInputBuffer",
                                      reinterpret_cast<LPVOID>(&hkGetRawInputBuffer),
                                      reinterpret_cast<LPVOID*>(&oGetRawInputBuffer), &g_rawBufTarget);
    if (s1 == MH_OK && g_rawTarget)
        MH_EnableHook(g_rawTarget);
    if (s2 == MH_OK && g_rawBufTarget)
        MH_EnableHook(g_rawBufTarget);

    // Capa de syscall (win32u.dll): el stack de input de UWP/CoreWindow lee el
    // RAWINPUT llamando directamente a NtUserGetRawInputData/Buffer en vez de
    // los wrappers de user32. Es la via que funciona con la ventana ACTIVA.
    MH_STATUS s3 = MH_CreateHookApiEx(L"win32u.dll", "NtUserGetRawInputData",
                                      reinterpret_cast<LPVOID>(&hkNtGetRawInputData),
                                      reinterpret_cast<LPVOID*>(&oNtGetRawInputData), &g_ntRawTarget);
    MH_STATUS s4 = MH_CreateHookApiEx(L"win32u.dll", "NtUserGetRawInputBuffer",
                                      reinterpret_cast<LPVOID>(&hkNtGetRawInputBuffer),
                                      reinterpret_cast<LPVOID*>(&oNtGetRawInputBuffer), &g_ntRawBufTarget);
    if (s3 == MH_OK && g_ntRawTarget)
        MH_EnableHook(g_ntRawTarget);
    if (s4 == MH_OK && g_ntRawBufTarget)
        MH_EnableHook(g_ntRawBufTarget);

    bool ok = (s1 == MH_OK) || (s2 == MH_OK) || (s3 == MH_OK) || (s4 == MH_OK);
    if (ok)
        MC_LOG("[Wheel] raw input hooks active (user32 Data=%d Buffer=%d | win32u NtData=%d NtBuffer=%d)",
               (int)s1, (int)s2, (int)s3, (int)s4);
    else
        MC_LOG_ERROR("[Wheel] Raw input hooks failed (Data=%d Buffer=%d NtData=%d NtBuffer=%d)",
                     (int)s1, (int)s2, (int)s3, (int)s4);
    return ok;
}

void setModule(void* moduleHandle)
{
    g_module = static_cast<HMODULE>(moduleHandle);
}

float lastValue()
{
    return g_lastValue;
}

void addWinrtDelta(int wheelDelta)
{
    if (wheelDelta == 0)
        return;
    wheelFeed(wheelDelta, SRC_WINRT);
    if (!g_loggedWinrt)
    {
        g_loggedWinrt = true;
        MC_LOG("[Wheel] WinRT source active");
    }
}

void addWinrtDirectDelta(int wheelDelta)
{
    if (wheelDelta == 0)
        return;
    wheelFeed(wheelDelta, SRC_WINRT_DIRECT);
    if (!g_loggedWinrt)
    {
        g_loggedWinrt = true;
        MC_LOG("[Wheel] WinRT (direct) source active");
    }
}

void addGameDelta(int wheelDelta)
{
    if (wheelDelta == 0)
        return;
    wheelFeed(wheelDelta, SRC_GAME);
    if (!g_loggedGame)
    {
        g_loggedGame = true;
        MC_LOG("[Wheel] MouseDevice::feed source active");
    }
}
float consume()
{
    int units = g_units.exchange(0);
    float value = (float)units / 120.0f;
    if (value != 0.0f)
        g_lastValue = value;
    return value;
}

static void feedWheel(int delta)
{
    if (delta == 0)
        return;
    wheelFeed(delta, SRC_LL);
    if (!g_loggedLL)
    {
        g_loggedLL = true;
        MC_LOG("[Wheel] WH_MOUSE_LL source active");
    }
}

static void feedWheelMsg(int delta)
{
    if (delta == 0)
        return;
    wheelFeed(delta, SRC_MSG);
    if (!g_loggedMsg)
    {
        g_loggedMsg = true;
        MC_LOG("[Wheel] PeekMessageW/GetMessageW source active");
    }
}

// El LL hook es GLOBAL: ve el wheel de TODAS las ventanas (la consola de
// debug incluida). Alimentar el acumulador con scroll ajeno envenena el
// dedup por fuentes y hace que el menu se scrollee solo; y tragarnos el
// evento con el menu abierto mataba el scroll de la propia consola. Solo
// procesamos el wheel que ocurre sobre la ventana del JUEGO.
static bool wheelOverGameWindow(const POINT& pt)
{
    HWND under = WindowFromPoint(pt);
    if (!under)
        return false;
    HWND consoleWnd = GetConsoleWindow();
    if (consoleWnd && (under == consoleWnd || IsChild(consoleWnd, under)))
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(under, &pid);
    return pid == GetCurrentProcessId();
}

static LRESULT CALLBACK LLMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == WM_MOUSEWHEEL)
    {
        const MSLLHOOKSTRUCT* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        int delta = (short)HIWORD(info->mouseData);
        if (delta != 0 && wheelOverGameWindow(info->pt))
        {
            feedWheel(delta);
            if (mc::menu::visible())
                return 1;
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

// Un WM_MOUSEWHEEL bombeado por nuestro proceso puede venir de la consola
// de debug (HWND del mismo proceso): no alimenta el acumulador.
static bool msgWheelIsForeign(const MSG* msg)
{
    if (!msg || !msg->hwnd)
        return false;
    HWND consoleWnd = GetConsoleWindow();
    return consoleWnd && (msg->hwnd == consoleWnd || IsChild(consoleWnd, msg->hwnd));
}

static BOOL WINAPI hkPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax,
                                  UINT wRemoveMsg)
{
    BOOL r = oPeek(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    if (r && lpMsg && lpMsg->message == WM_MOUSEWHEEL && (wRemoveMsg & PM_REMOVE) &&
        !msgWheelIsForeign(lpMsg))
    {
        int delta = (short)HIWORD(lpMsg->wParam);
        feedWheelMsg(delta);
        if (mc::menu::visible())
            lpMsg->message = WM_NULL;
    }
    return r;
}

static BOOL WINAPI hkGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    BOOL r = oGet(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    if (r && lpMsg && lpMsg->message == WM_MOUSEWHEEL && !msgWheelIsForeign(lpMsg))
    {
        int delta = (short)HIWORD(lpMsg->wParam);
        feedWheelMsg(delta);
        if (mc::menu::visible())
            lpMsg->message = WM_NULL;
    }
    return r;
}

static bool installMessageHooks()
{
    if (g_msgHooked)
        return true;
    if (!hook::initialize())
    {
        MC_LOG_ERROR("[Wheel] MinHook init failed");
        return false;
    }

    MH_STATUS s1 = MH_CreateHookApiEx(L"user32.dll", "PeekMessageW",
                                      reinterpret_cast<LPVOID>(&hkPeekMessageW),
                                      reinterpret_cast<LPVOID*>(&oPeek), &g_peekTarget);
    MH_STATUS s2 = MH_CreateHookApiEx(L"user32.dll", "GetMessageW",
                                      reinterpret_cast<LPVOID>(&hkGetMessageW),
                                      reinterpret_cast<LPVOID*>(&oGet), &g_getTarget);

    if (s1 == MH_OK && g_peekTarget)
        MH_EnableHook(g_peekTarget);
    if (s2 == MH_OK && g_getTarget)
        MH_EnableHook(g_getTarget);

    g_msgHooked = (s1 == MH_OK) || (s2 == MH_OK);
    if (g_msgHooked)
        MC_LOG("[Wheel] PeekMessageW/GetMessageW hooks active (Peek=%d Get=%d)", (int)s1, (int)s2);
    else
        MC_LOG_ERROR("[Wheel] Message hooks failed (Peek=%d Get=%d)", (int)s1, (int)s2);
    return g_msgHooked;
}

static DWORD WINAPI WheelThread(LPVOID)
{
    g_hook = SetWindowsHookExW(WH_MOUSE_LL, LLMouseProc, g_module, 0);
    if (!g_hook)
    {
        MC_LOG_ERROR("[Wheel] SetWindowsHookEx failed: %lu -> trying PeekMessageW", GetLastError());
        installMessageHooks();
        return 0;
    }

    MC_LOG("[Wheel] WH_MOUSE_LL installed");

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hook)
    {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
    MC_LOG("[Wheel] Hook removed");
    return 0;
}

// ---------------------------------------------------------------------------
// NOTA: Raw Input (RegisterRawInputDevices) esta PROHIBIDO aqui: el registro
// por uso de dispositivo es unico por proceso, asi que el nuestro REEMPLAZA el
// del propio juego (camara muerta) y RIDEV_REMOVE borra el registro de todo el
// proceso. El scroll del menu llega por WinRT PointerWheelChanged
// (suscrito en el UI thread desde InputSystem) o por los hooks de mensajes.
// ---------------------------------------------------------------------------

bool install()
{
    MC_LOG("[Wheel] installing (module=%p)", g_module);

    if (g_thread || g_msgHooked)
        return true;
    if (!g_module)
    {
        MC_LOG_ERROR("[Wheel] Module handle not set");
        return false;
    }

    // La via del juego ACTIVO: siempre activa (no interfiere: solo observa
    // el RAWINPUT que el propio juego va a leer).
    installRawInputHooks();

    // Hooks de mensajes: SIEMPRE. Cuando la ventana del juego esta ACTIVA el
    // WH_MOUSE_LL no ve el wheel (el juego usa raw input / CoreWindow), pero
    // si el bucle de mensajes del juego recibe WM_MOUSEWHEEL lo capturamos
    // aqui. Con el acumulador g_msgUnits separado no hay doble conteo con el
    // LL hook (consume() elige una unica fuente).
    installMessageHooks();

    g_thread = CreateThread(nullptr, 0, WheelThread, nullptr, 0, &g_threadId);
    if (!g_thread)
    {
        MC_LOG_ERROR("[Wheel] CreateThread failed: %lu", GetLastError());
    }
    else
    {
        MC_LOG("[Wheel] hook thread started (id=%lu)", g_threadId);
    }

    return g_thread != nullptr || g_msgHooked;
}

void uninstall()
{
    if (g_msgHooked)
    {
        if (g_peekTarget)
            MH_DisableHook(g_peekTarget);
        if (g_getTarget)
            MH_DisableHook(g_getTarget);
        g_msgHooked = false;
    }

    if (g_rawTarget || g_rawBufTarget || g_ntRawTarget || g_ntRawBufTarget)
    {
        if (g_rawTarget)
            MH_DisableHook(g_rawTarget);
        if (g_rawBufTarget)
            MH_DisableHook(g_rawBufTarget);
        if (g_ntRawTarget)
            MH_DisableHook(g_ntRawTarget);
        if (g_ntRawBufTarget)
            MH_DisableHook(g_ntRawBufTarget);
        g_rawTarget = nullptr;
        g_rawBufTarget = nullptr;
        g_ntRawTarget = nullptr;
        g_ntRawBufTarget = nullptr;
    }

    if (g_thread)
    {
        PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_thread, 1000);
        CloseHandle(g_thread);
        g_thread = nullptr;
        g_threadId = 0;
    }

    g_units.store(0);
    g_lastSource.store(-1);
}

}
