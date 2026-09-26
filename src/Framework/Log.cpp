#include "Log.hpp"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <eh.h>
#include <exception>
#include <fstream>

namespace mc::log {

static FILE* g_out = nullptr;
static FILE* g_err = nullptr;
static char g_fileBuf[260] = "azyre_sdk.log";
static bool g_ready = false;

void init(const char* file)
{
    if (file && *file)
    {
        strncpy_s(g_fileBuf, file, _TRUNCATE);
    }
    if (!GetConsoleWindow())
        AllocConsole();

    freopen_s(&g_out, "CONOUT$", "w", stdout);
    freopen_s(&g_err, "CONOUT$", "w", stderr);
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
    if (file && *file)
    {
        strncpy_s(g_fileBuf, file, _TRUNCATE);
    }
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

static LONG WINAPI unhandledLogger(EXCEPTION_POINTERS* ep)
{
    if (ep && ep->ExceptionRecord)
    {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "[FATAL] Unhandled exception 0x%08X @ %p (thread %lu)\n",
                 ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress,
                 GetCurrentThreadId());
        rawFatalWrite(buf);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void __cdecl invalidParamLogger(const wchar_t*, const wchar_t*, const wchar_t*, unsigned,
                                        uintptr_t)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "[FATAL] CRT invalid parameter (thread %lu)\n",
             GetCurrentThreadId());
    rawFatalWrite(buf);
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
    _set_invalid_parameter_handler(invalidParamLogger);
    SetUnhandledExceptionFilter(unhandledLogger);
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
