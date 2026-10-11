#pragma once

#include "Commands/Command.hpp"

#include <algorithm>

namespace mc {

class HelpCommand : public Command
{
public:
    HelpCommand()
        : Command("help", "help", "shows the available commands")
    {
    }

    void execute(const std::vector<std::string>&) override
    {
        commands::reply(commands::tag() + "§f available commands:");

        std::vector<const Command*> sorted;
        for (auto& c : commands::registry())
            sorted.push_back(c.get());
        std::sort(sorted.begin(), sorted.end(),
                  [](const Command* a, const Command* b) { return a->name() < b->name(); });

        for (const Command* c : sorted)
        {
            commands::reply("§b " + commands::prefix() + c->name() + " §7- §f" +
                            c->description());
        }
    }
};

MC_REGISTER_COMMAND(HelpCommand);

}
