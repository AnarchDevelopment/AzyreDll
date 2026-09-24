#include "Log.hpp"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
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
    SetConsoleTitle(L"Azyre SDK - Debug");

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo{};
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    g_ready = true;
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
