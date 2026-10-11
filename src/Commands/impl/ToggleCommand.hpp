#pragma once

#include "Commands/Command.hpp"

#include "Modules/ModuleManager.hpp"

namespace mc {

class ToggleCommand : public Command
{
public:
    ToggleCommand()
        : Command("toggle", "toggle [module]", "toggles a module")
    {
        addAlias("t");
    }

    void execute(const std::vector<std::string>& args) override
    {
        if (args.empty())
        {
            commands::reply(commands::tag() + "§c usage: §f" + commands::prefix() + usage());
            return;
        }

        Module* m = commands::findModule(args[0]);
        if (!m)
        {
            commands::reply(commands::tag() + "§c module §b" + args[0] + "§c not found!");
            return;
        }

        m->toggle();
        commands::reply(commands::tag() + " §ftoggled §b" + m->name());
    }
};

MC_REGISTER_COMMAND(ToggleCommand);

}
