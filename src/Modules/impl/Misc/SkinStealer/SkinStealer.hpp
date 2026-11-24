#pragma once

#include "Modules/Module.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace mc {

class Actor;

class SkinStealer final : public Module
{
public:
    SkinStealer();

    // Returns 1 on success, 0 if the player is not in the current world, and
    // -1 if the skin could not be read or written.
    int savePlayer(const std::string& playerName, std::string& savedPath) const;
    std::pair<size_t, size_t> saveAll() const;

private:
    bool saveActor(const Actor& actor, std::string& savedPath) const;
};

} // namespace mc
