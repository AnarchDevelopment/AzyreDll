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
    for (const char* keyword : keywords)
    {
        if (lower.find(keyword) != std::string::npos)
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
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c == 0x0D || c == 0x0A)
            return {};
        if (c == 0xA7)
        {
            if (i + 1 < raw.size())
                ++i;
            continue;
        }
        if (c == 0xC2 && i + 1 < raw.size() &&
            static_cast<unsigned char>(raw[i + 1]) == 0xA7)
        {
            ++i;
            continue;
        }
        if (c < 32 || c > 126)
            continue;
        out.push_back(static_cast<char>(c));
    }
    return out;
}

inline bool isBot(const std::string& rawName)
{
    if (rawName.empty())
        return true;

    const std::string name = sanitize(rawName);
    if (name.empty())
        return true;

    if (g_filterInvalidNames)
    {
        // Bedrock player usernames cannot contain spaces; server NPC labels often do.
        if (name.find(' ') != std::string::npos)
            return true;
        if (name.size() < 3 || name.size() > 40)
            return true;
    }

    if (g_filterKeywords)
    {
        std::string lower = name;
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (hasKeyword(lower))
            return true;
    }

    return false;
}

} // namespace mc::botfilter
