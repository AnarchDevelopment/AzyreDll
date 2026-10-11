#pragma once

#include <windows.h>

#include <cctype>
#include <cstdlib>
#include <string>

namespace mc::input {

// "R" -> 'R'; "F5" -> VK_F5; "SHIFT" -> VK_SHIFT; "LMB" -> VK_LBUTTON.
// Devuelve 0 si el nombre no es una tecla valida.
inline int keyCodeFromName(std::string k)
{
    if (k.empty())
        return 0;
    if (k.size() == 1)
        return (int)(unsigned char)std::toupper((unsigned char)k[0]);

    for (char& c : k)
        c = (char)std::toupper((unsigned char)c);
    if (k == "SPACE")
        return VK_SPACE;
    if (k == "SHIFT")
        return VK_SHIFT;
    if (k == "CTRL" || k == "CONTROL")
        return VK_CONTROL;
    if (k == "ALT")
        return VK_MENU;
    if (k == "TAB")
        return VK_TAB;
    if (k == "ENTER" || k == "RETURN")
        return VK_RETURN;
    if (k == "ESC" || k == "ESCAPE")
        return VK_ESCAPE;
    if (k == "LMB")
        return VK_LBUTTON;
    if (k == "RMB")
        return VK_RBUTTON;
    if (k == "MBUTTON")
        return VK_MBUTTON;
    if (k == "XBUTTON1")
        return VK_XBUTTON1;
    if (k == "XBUTTON2")
        return VK_XBUTTON2;
    if (k[0] == 'F')
    {
        int f = atoi(k.c_str() + 1);
        if (f >= 1 && f <= 24)
            return VK_F1 + f - 1;
    }
    return 0;
}

// Inverso de keyCodeFromName, para mostrar binds en el chat.
inline std::string keyName(int vk)
{
    switch (vk)
    {
    case VK_SPACE: return "SPACE";
    case VK_SHIFT: return "SHIFT";
    case VK_CONTROL: return "CTRL";
    case VK_MENU: return "ALT";
    case VK_TAB: return "TAB";
    case VK_RETURN: return "ENTER";
    case VK_ESCAPE: return "ESC";
    case VK_LBUTTON: return "LMB";
    case VK_RBUTTON: return "RMB";
    case VK_MBUTTON: return "MBUTTON";
    case VK_XBUTTON1: return "XBUTTON1";
    case VK_XBUTTON2: return "XBUTTON2";
    default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F24)
        return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= 'A' && vk <= 'Z')
        return std::string(1, (char)vk);
    if (vk >= '0' && vk <= '9')
        return std::string(1, (char)vk);
    return "?";
}

}
