#pragma once

#include "Commands/Command.hpp"

namespace mc {

class PrefixCommand : public Command
{
public:
    PrefixCommand()
        : Command("prefix", "prefix [char]", "changes the command prefix")
    {
    }

    void execute(const std::vector<std::string>& args) override
    {
        if (args.empty())
        {
            commands::reply(commands::tag() + "§f current prefix: §b" + commands::prefix());
            return;
        }

        const std::string& c = args[0];
        if (c.size() != 1 || (unsigned char)c[0] < 0x21 || (unsigned char)c[0] > 0x7E)
        {
            commands::reply(commands::tag() + "§c invalid prefix! use a single printable character");
            return;
        }

        commands::setPrefix(c[0]);
        commands::reply(commands::tag() + "§f prefix setted to §b" + c + ".");
    }
};

MC_REGISTER_COMMAND(PrefixCommand);

}
