#include "Log.hpp"

#include <windows.h>
#include <psapi.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#ifdef _MSC_VER
#include <eh.h>
#endif
#include <exception>
#include <fstream>

namespace mc::log {

static FILE* g_out = nullptr;
static FILE* g_err = nullptr;
static char g_fileBuf[260] = "azyre_sdk.log";
static bool g_ready = false;

static void copyPath(char* dst, size_t cap, const char* src)
{
    if (!dst || cap == 0 || !src)
        return;
#ifdef _MSC_VER
    strncpy_s(dst, cap, src, _TRUNCATE);
#else
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
#endif
}

void init(const char* file)
{
    copyPath(g_fileBuf, sizeof(g_fileBuf), file);
    if (!GetConsoleWindow())
        AllocConsole();

#ifdef _MSC_VER
    freopen_s(&g_out, "CONOUT$", "w", stdout);
    freopen_s(&g_err, "CONOUT$", "w", stderr);
#else
    g_out = std::freopen("CONOUT$", "w", stdout);
    g_err = std::freopen("CONOUT$", "w", stderr);
#endif
    SetConsoleTitle(L"Azyre - Debug");

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo{};
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    g_ready = true;
}

void setFile(const char* file)
{
    copyPath(g_fileBuf, sizeof(g_fileBuf), file);
}

// =====================================================================
// Diagnostico de crashes: 0xC0000409 (abort/fastfail del CRT) casi siempre
// es una excepcion C++ sin capturar que dispara std::terminate. Estos
// handlers la nombran en el log ANTES de morir, con escritura Win32
// directa (sin depender del CRT en punto de muerte).
// =====================================================================

static void rawFatalWrite(const char* text)
{
    HANDLE f = CreateFileA(g_fileBuf, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(f, text, (DWORD)strlen(text), &written, nullptr);
    CloseHandle(f);
}

// Mapea una direccion a "modulo + RVA" para poder leerla en IDA.
static void fmtFrame(char* buf, size_t cap, int idx, void* frame)
{
    HMODULE hm = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      (LPCWSTR)(void*)frame, &hm);
    uintptr_t a = (uintptr_t)frame;
    uintptr_t mb = (uintptr_t)hm;

    char mod[64] = "??";
    if (hm)
    {
        if (mb == (uintptr_t)GetModuleHandleW(nullptr))
            snprintf(mod, sizeof(mod), "game");
        else
        {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(hm, path, MAX_PATH);
            const wchar_t* slash = wcsrchr(path, L'\\');
            const wchar_t* base = slash ? slash + 1 : path;
            char name[32] = "dll";
            wcstombs(name, base, sizeof(name) - 1);
            snprintf(mod, sizeof(mod), "%s", name);
        }
    }

    snprintf(buf, cap, "[FATAL]   #%02d %p  RVA 0x%llX (%s)\n",
             idx, frame,
             (unsigned long long)(a >= mb ? a - mb : a), mod);
}

// El [FATAL] debe verse TAMBIEN en la consola (el archivo azyre_sdk.log a
// veces no esta en el CWD que mira el usuario). OJO: NO usar OutputDebugString
// aqui — genera la excepcion 0x40010006 que el handler capturaria otra vez
// (feedback loop infinito de logs).
static void fatalOut(const char* text)
{
    rawFatalWrite(text);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteConsoleA(out, text, (DWORD)strlen(text), &written, nullptr);
    }
}

// Codigo de excepciones informativas que NO son crashes y deben ignorarse:
// 0x40010006/0x4001000A = OutputDebugString, 0xE06D7133 = C++ EH de MSVC
// (x86), 0xE06D7363 = C++ EH de MSVC (x64 — el throw/catch interno de
// d3d11 y demas modulos del SO los genera a rafagas: NO son crashes y sin
// este filtro se comian la ventana de throttle del vectoredLogger, que es
// exactamente como el crash real quedo invisible en el log), 0x80000003 =
// int3 del debugger, 0x4000001F = WOW64.
static bool isInformational(DWORD code)
{
    return code == 0x40010006 || code == 0x4001000A || code == 0xE06D7133 ||
           code == 0xE06D7363 || code == 0x80000003 || code == 0x4000001F;
}

static LONG WINAPI unhandledLogger(EXCEPTION_POINTERS* ep)
{
    if (ep && ep->ExceptionRecord)
    {
        EXCEPTION_RECORD* er = ep->ExceptionRecord;
        char buf[512];

        snprintf(buf, sizeof(buf),
                 "[FATAL] Unhandled exception 0x%08X @ %p (thread %lu)\n",
                 er->ExceptionCode, er->ExceptionAddress,
                 GetCurrentThreadId());
        fatalOut(buf);

        // Para 0xC0000005: que leyo/escribio y contra que direccion. Solo
        // para AVs reales: otras excepciones (RPC, Win32-status) cargan
        // basura en ExceptionInformation y el logger imprimia "access
        // READ @ 0x0" fantasma.
        if (er->ExceptionCode == 0xC0000005 && er->ExceptionInformation[0] <= 1)
        {
            snprintf(buf, sizeof(buf),
                     "[FATAL] access %s @ %p\n",
                     er->ExceptionInformation[0] == 0 ? "READ" : "WRITE",
                     (void*)er->ExceptionInformation[1]);
            fatalOut(buf);
        }

        // Backtrace con RVA por frame — se mapea directo en IDA.
        void* frames[32] = {};
        USHORT n = CaptureStackBackTrace(0, 32, frames, nullptr);
        for (USHORT i = 0; i < n && i < 24; ++i)
        {
            fmtFrame(buf, sizeof(buf), (int)i, frames[i]);
            fatalOut(buf);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Excepciones de la maquinaria RPC del SO (RPCRT4 → Windows.Networking.*):
// el runtime convierte codigos de estado Win32 (p.ej. 5 = ERROR_ACCESS_DENIED)
// en RaiseException y el sistema los captura como control flow. Son ruido
// benigno de primera chance: un crash REAL de esos sigue llegando al
// unhandled filter (segunda chance), que no esta sujeto a este filtro.
static bool isRpcNoise(DWORD code, void* address)
{
    static uintptr_t s_rpcBase = [] {
        HMODULE m = GetModuleHandleW(L"RPCRT4.dll");
        return m ? (uintptr_t)m : 0;
    }();
    static uintptr_t s_rpcEnd = [] {
        HMODULE m = GetModuleHandleW(L"RPCRT4.dll");
        if (!m)
            return (uintptr_t)0;
        MODULEINFO mi{};
        GetModuleInformation(GetCurrentProcess(), m, &mi, sizeof(mi));
        return (uintptr_t)m + mi.SizeOfImage;
    }();

    if (!s_rpcBase || !address)
        return false;
    uintptr_t a = (uintptr_t)address;
    if (a < s_rpcBase || a >= s_rpcEnd)
        return false;
    // Codigos de estado Win32 (0 < code < 0x100000): el dialecto de
    // RaiseException de RPCRT4. Los NTSTATUS/AV reales (0xC000xxxx) se
    // dejan pasar.
    return code > 0 && code < 0x10000000;
}

// Vectored handler: corre ANTES que cualquier otro handler. Filtra las
// excepciones informativas (OutputDebugString, C++ EH) que el juego genera
// constantemente y solo loggea los crashes REALES (AV, heap corruption,
// stack overflow). Throttle: max un log cada 10s para evitar floods.
static LONG WINAPI vectoredLogger(EXCEPTION_POINTERS* ep)
{
    if (ep && ep->ExceptionRecord)
    {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        if (!isInformational(code) && !isRpcNoise(code, ep->ExceptionRecord->ExceptionAddress))
        {
            static ULONGLONG s_lastLog = 0;
            ULONGLONG now = GetTickCount64();
            if (s_lastLog == 0 || now - s_lastLog > 10000ULL)
            {
                s_lastLog = now;
                unhandledLogger(ep);
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

#ifdef _MSC_VER
static void __cdecl invalidParamLogger(const wchar_t*, const wchar_t*, const wchar_t*, unsigned,
                                        uintptr_t)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "[FATAL] CRT invalid parameter (thread %lu)\n",
             GetCurrentThreadId());
    rawFatalWrite(buf);
}
#endif

void reportGuard(const char* tag, void* exceptionPointers)
{
    static ULONGLONG s_lastDetail = 0;
    static unsigned s_hits = 0;
    ULONGLONG now = GetTickCount64();
    bool detail = (now - s_lastDetail) > 5000ull;
    if (detail)
        s_lastDetail = now;
    ++s_hits;

    char buf[256];
    snprintf(buf, sizeof(buf), "[Guard][%s] Exception (hit %u, thread %lu)\n",
             tag ? tag : "?", s_hits, GetCurrentThreadId());
    rawFatalWrite(buf);

    EXCEPTION_POINTERS* ep = (EXCEPTION_POINTERS*)exceptionPointers;
    if (!ep || !ep->ExceptionRecord)
        return;

    EXCEPTION_RECORD* er = ep->ExceptionRecord;
    snprintf(buf, sizeof(buf), "[Guard][%s] code 0x%08X @ %p\n",
             tag ? tag : "?", er->ExceptionCode, er->ExceptionAddress);
    rawFatalWrite(buf);

    if (er->ExceptionCode == 0xC0000005 && er->ExceptionInformation[0] <= 1)
    {
        snprintf(buf, sizeof(buf), "[Guard][%s] access %s @ %p\n",
                 tag ? tag : "?",
                 er->ExceptionInformation[0] == 0 ? "READ" : "WRITE",
                 (void*)er->ExceptionInformation[1]);
        rawFatalWrite(buf);
    }

    if (!detail)
        return;

    void* frames[24] = {};
    USHORT n = CaptureStackBackTrace(1, 24, frames, nullptr);
    for (USHORT i = 0; i < n; ++i)
    {
        fmtFrame(buf, sizeof(buf), (int)i, frames[i]);
        rawFatalWrite(buf);
    }
}

static void terminateLogger()
{
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "[FATAL] std::terminate on thread %lu: ",
                     GetCurrentThreadId());

    std::exception_ptr ptr = std::current_exception();
    if (ptr)
    {
        try
        {
            std::rethrow_exception(ptr);
        }
        catch (const std::exception& e)
        {
            n += snprintf(buf + n, sizeof(buf) - n, "%s", e.what());
        }
        catch (...)
        {
            n += snprintf(buf + n, sizeof(buf) - n, "unknown non-standard exception");
        }
    }
    else
    {
        n += snprintf(buf + n, sizeof(buf) - n, "no active exception (explicit terminate/abort)");
    }
    snprintf(buf + n, sizeof(buf) - n, "\n");
    rawFatalWrite(buf);

    abort();
}

void installCrashHandler()
{
    std::set_terminate(terminateLogger);
#ifdef _MSC_VER
    _set_invalid_parameter_handler(invalidParamLogger);
#endif
    SetUnhandledExceptionFilter(unhandledLogger);
    // Vectored: captura AVs ANTES de que el juego/CRT puedan tragarselos.
    AddVectoredExceptionHandler(1, vectoredLogger);
}

void shutdown()
{
    if (g_out) { fclose(g_out); g_out = nullptr; }
    if (g_err && g_err != g_out) { fclose(g_err); g_err = nullptr; }
    FreeConsole();
}

static WORD adequateColor(const char* msg)
{
    if (strstr(msg, "[ERROR]")) return 12;
    if (strstr(msg, "[SUCCESS]")) return 10;
    if (strstr(msg, "[WinRT]")) return 11;
    if (strstr(msg, "[VTable]")) return 13;
    if (strstr(msg, "[MinHook]")) return 9;
    if (strstr(msg, "[Input]")) return 14;
    if (strstr(msg, "[Game]")) return 3;
    if (strstr(msg, "[Module]")) return 2;
    if (strstr(msg, "[DX11]")) return 6;
    return 15;
}

void write(const char* fmt, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char dt[32];
    snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    const WORD kWhite = 15;
    const WORD kGray = 8;
    WORD adequate = adequateColor(buffer);

    if (g_ready)
    {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, kWhite);
        fputc('[', stdout);
        SetConsoleTextAttribute(hConsole, kGray);
        fputs(dt, stdout);
        SetConsoleTextAttribute(hConsole, kWhite);
        fputs("] ", stdout);
        SetConsoleTextAttribute(hConsole, adequate);
        fputs(buffer, stdout);
        fputc('\n', stdout);
        SetConsoleTextAttribute(hConsole, kWhite);
    }

    std::ofstream out(g_fileBuf, std::ios::app);
    if (out.is_open())
    {
        out << '[' << dt << "] " << buffer << '\n';
        out.close();
    }
}

}
