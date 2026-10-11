#pragma once

#include "Commands/Command.hpp"

#include "Config/Config.hpp"

namespace mc {

class ConfigCommand : public Command
{
public:
    ConfigCommand()
        : Command("config", "config save/load/delete [name]", "manage config presets")
    {
    }

    void execute(const std::vector<std::string>& args) override
    {
        if (args.size() < 2)
        {
            commands::reply(commands::tag() + "§c usage: §f" + commands::prefix() + usage());
            return;
        }

        const std::string sub = commands::toLower(args[0]);
        const std::string& name = args[1];

        bool ok = false;
        std::string action;
        if (sub == "save")
        {
            ok = config::savePreset(name);
            action = "§a saved";
        }
        else if (sub == "load")
        {
            ok = config::loadPreset(name);
            action = "§a loaded";
        }
        else if (sub == "delete" || sub == "del" || sub == "remove")
        {
            ok = config::deletePreset(name);
            action = "§a removed";
        }
        else
        {
            commands::reply(commands::tag() + "§c usage: §f" + commands::prefix() + usage());
            return;
        }

        if (ok)
            commands::reply(commands::tag() + "§f config §d" + name + action + " §fsuccessfully!");
        else
            commands::reply(commands::tag() + "§c config §d" + name + "§c operation failed!");
    }
};

MC_REGISTER_COMMAND(ConfigCommand);

}
