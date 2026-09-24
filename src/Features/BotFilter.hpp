#pragma once

#include <cctype>
#include <string>

namespace mc::botfilter {

inline bool g_filterKeywords = true;
inline bool g_filterInvalidNames = true;

inline bool hasKeyword(const std::string& lower)
{
    static const char* keywords[] = {
        "npc", "shop", "tienda", "merchant", "delivery", "quest", "mision",
        "server", "bot", "youtube", "sub", "review", "rango", "hologram",
        "warp", "click", "toca",
    };
    for (const char* k : keywords)
    {
        if (lower.find(k) != std::string::npos)
            return true;
    }
    return false;
}

inline std::string sanitize(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        unsigned char c = (unsigned char)raw[i];
        if (c == 0x0D || c == 0x0A)
            return {};
        if (c == 0xA7)
        {
            if (i + 1 < raw.size())
                ++i;
            continue;
        }
        if (c == 0xC2 && i + 1 < raw.size() && (unsigned char)raw[i + 1] == 0xA7)
        {
            ++i;
            continue;
        }
        if (c < 32 || c > 126)
            continue;
        out.push_back((char)c);
    }
    return out;
}

inline bool isBot(const std::string& rawName)
{
    if (rawName.empty())
        return true;

    std::string n = sanitize(rawName);
    if (n.empty())
        return true;

    if (g_filterInvalidNames)
    {
        if (n.size() < 3 || n.size() > 40)
            return true;
    }

    if (g_filterKeywords)
    {
        std::string lower = n;
        for (char& c : lower)
            c = (char)tolower((unsigned char)c);
        if (hasKeyword(lower))
            return true;
    }

    return false;
}

}
