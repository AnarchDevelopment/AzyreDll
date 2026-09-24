#pragma once

#include <windows.h>

inline void sendLeftDown()
{
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &in, sizeof(INPUT));
}

inline void sendLeftUp()
{
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(INPUT));
}

inline void sendRightDown()
{
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    SendInput(1, &in, sizeof(INPUT));
}

inline void sendRightUp()
{
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    SendInput(1, &in, sizeof(INPUT));
}
