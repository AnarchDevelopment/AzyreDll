#pragma once

#include "Commands/Command.hpp"

#include "Config/Config.hpp"
#include "Input/KeyNames.hpp"
#include "Modules/ModuleManager.hpp"

namespace mc {

class BindCommand : public Command
{
public:
    BindCommand()
        : Command("bind", "bind [module] [key]", "binds a key to a module")
    {
    }

    void execute(const std::vector<std::string>& args) override
    {
        if (args.size() < 2)
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

        // NONE / RESET / CLEAR: desbindear
        std::string keyName = commands::toLower(args[1]);
        if (keyName == "none" || keyName == "reset" || keyName == "clear")
        {
            m->setKeybind(0);
            config::saveModule(m->name());
            commands::reply(commands::tag() + "§f the key assigned to §b" + m->name() +
                            " §fwas removed.");
            return;
        }

        int key = input::keyCodeFromName(args[1]);
        if (key <= 0)
        {
            commands::reply(commands::tag() + "§c invalid key §b" + args[1] + "§c!");
            return;
        }

        m->setKeybind(key);
        config::saveModule(m->name());
        commands::reply(commands::tag() + "§f bind §b" + m->name() + "§f with§b " +
                        input::keyName(key) + " §fkeybind");
    }
};

MC_REGISTER_COMMAND(BindCommand);

}
