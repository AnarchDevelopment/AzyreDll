#pragma once

#include "Commands/Command.hpp"

#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Misc/SkinStealer/SkinStealer.hpp"

namespace mc {

class SkinCommand final : public Command
{
public:
    SkinCommand()
        : Command("skin", "skin steal <playername|all>",
                  "save a loaded player's skin as a PNG")
    {
    }

    void execute(const std::vector<std::string>& args) override
    {
        if (args.size() < 2 || commands::toLower(args[0]) != "steal")
        {
            usageError();
            return;
        }

        auto* stealer = dynamic_cast<SkinStealer*>(ModuleManager::get().find("SkinStealer"));
        if (!stealer)
        {
            commands::reply(commands::tag() + "§c SkinStealer is unavailable!");
            return;
        }

        if (commands::toLower(args[1]) == "all")
        {
            const auto [saved, failed] = stealer->saveAll();
            commands::reply(commands::tag() + "§f saved §b" + std::to_string(saved) +
                            "§f skin(s); failed §c" + std::to_string(failed) +
                            "§f. Files are in §bAzyre\\Skins§f.");
            return;
        }

        std::string playerName;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (!playerName.empty())
                playerName += ' ';
            playerName += args[i];
        }

        std::string savedPath;
        const int result = stealer->savePlayer(playerName, savedPath);
        if (result > 0)
            commands::reply(commands::tag() + "§f saved §b" + playerName +
                            "§f skin to §bAzyre\\Skins§f.");
        else if (result == 0)
            commands::reply(commands::tag() + "§c player §b" + playerName +
                            "§c is not loaded in this world!");
        else
            commands::reply(commands::tag() + "§c could not read or save §b" +
                            playerName + "§c's skin!");
    }

private:
    void usageError()
    {
        commands::reply(commands::tag() + "§c usage: §f" + commands::prefix() + usage());
    }
};

MC_REGISTER_COMMAND(SkinCommand);

} // namespace mc
