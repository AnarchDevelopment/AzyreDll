#pragma once

#include "Commands/Command.hpp"

#include "Input/KeyNames.hpp"
#include "Modules/ModuleManager.hpp"

namespace mc {

class BindsCommand : public Command
{
public:
    BindsCommand()
        : Command("binds", "binds", "lists all the binded modules")
    {
    }

    void execute(const std::vector<std::string>&) override
    {
        bool any = false;
        for (auto& m : ModuleManager::get().all())
        {
            int k = m->keybind();
            if (k <= 0)
                continue;
            if (!any)
            {
                commands::reply(commands::tag() + "§f showing list of the binded modules:");
                any = true;
            }
            commands::reply("§b " + m->name() + " §7- §f" + input::keyName(k));
        }

        if (!any)
            commands::reply(commands::tag() + "§c no modules binded!");
    }
};

MC_REGISTER_COMMAND(BindsCommand);

}
