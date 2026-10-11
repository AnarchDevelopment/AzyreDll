#pragma once

#include "Commands/Command.hpp"

#include "Framework/Log.hpp"
#include "Scripting/Scripting.hpp"

namespace mc {

class ScriptCommand : public Command
{
public:
    ScriptCommand()
        : Command("script", "script reload/remove/list [name]", "manage Lua scripts")
    {
    }

    void execute(const std::vector<std::string>& args) override
    {
        if (args.empty())
        {
            usageError();
            return;
        }

        const std::string sub = commands::toLower(args[0]);

        if (sub == "reload")
        {
            scripts::reload();
            commands::reply(commands::tag() + "§f all scripts reloaded!");
        }
        else if (sub == "list")
        {
            std::vector<scripts::ScriptInfo> all = scripts::list();
            if (all.empty())
            {
                commands::reply(commands::tag() + "§c no scripts loaded!");
                return;
            }
            commands::reply(commands::tag() + "§f list of the scripts:");
            for (const auto& s : all)
            {
                commands::reply("§b " + s.name + (s.errored ? " §c(error)" : ""));
            }
        }
        else if (sub == "remove")
        {
            if (args.size() < 2)
            {
                usageError();
                return;
            }
            if (!scripts::remove(args[1]))
            {
                commands::reply(commands::tag() + "§c script §b" + args[1] + "§c not found!");
                return;
            }
            commands::reply(commands::tag() + "§f script §b" + args[1] + " §fremoved succesfully!");
        }
        else
        {
            usageError();
        }
    }

private:
    void usageError()
    {
        commands::reply(commands::tag() + "§c usage: §f" + commands::prefix() + usage());
    }
};

MC_REGISTER_COMMAND(ScriptCommand);

}
