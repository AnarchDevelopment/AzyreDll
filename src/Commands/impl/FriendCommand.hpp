#pragma once

#include "Commands/Command.hpp"

#include "Features/Friends.hpp"

namespace mc {

class FriendCommand : public Command
{
public:
    FriendCommand()
        : Command("friend", "friend add/list/remove [player]", "manages your friends")
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

        // El nombre puede llevar espacios: se une el resto de los argumentos.
        std::string player;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (!player.empty())
                player += ' ';
            player += args[i];
        }

        if (sub == "add")
        {
            if (player.empty())
            {
                usageError();
                return;
            }
            if (friends::isFriend(player))
            {
                commands::reply(commands::tag() + "§c " + player + " is already your friend!");
                return;
            }
            friends::toggle(player);
            config::saveAll();
            commands::reply(commands::tag() + "§f friend §b" + player + " §fadded");
        }
        else if (sub == "remove" || sub == "rem" || sub == "del")
        {
            if (player.empty())
            {
                usageError();
                return;
            }
            if (!friends::isFriend(player))
            {
                commands::reply(commands::tag() + "§c " + player + " is not your friend!");
                return;
            }
            friends::toggle(player);
            config::saveAll();
            commands::reply(commands::tag() + "§f friend §b" + player + " §fremoved");
        }
        else if (sub == "list")
        {
            auto& fl = friends::list();
            if (fl.empty())
            {
                commands::reply(commands::tag() + "§c your friends list is empty!");
                return;
            }
            commands::reply(commands::tag() + "§f your friends:");
            for (const auto& n : fl)
                commands::reply("§b " + n);
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

MC_REGISTER_COMMAND(FriendCommand);

}
