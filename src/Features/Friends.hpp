#pragma once

#include <imgui.h>
#include <string>
#include <unordered_set>

namespace mc::friends {

inline unsigned int colorU32()
{
    return IM_COL32(255, 190, 90, 255);
}

inline std::unordered_set<std::string>& list()
{
    static std::unordered_set<std::string> s;
    return s;
}

inline bool isFriend(const std::string& name)
{
    if (name.empty())
        return false;
    return list().find(name) != list().end();
}

inline bool toggle(const std::string& name)
{
    if (name.empty())
        return false;
    auto& s = list();
    auto it = s.find(name);
    if (it != s.end())
    {
        s.erase(it);
        return false;
    }
    s.insert(name);
    return true;
}

inline void clear()
{
    list().clear();
}

}
