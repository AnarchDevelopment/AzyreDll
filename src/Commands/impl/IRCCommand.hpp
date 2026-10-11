#pragma once

#include "Commands/Command.hpp"
#include "Networking/IRC/IRC.hpp"
#include "SDK/Game.hpp"

namespace mc {

// .irc {message} — envia el mensaje al canal IRC (#aegleggeneral).
class IRCCommand : public Command
{
public:
    IRCCommand()
        : Command("irc", "irc <message>", "sends a message to the IRC channel")
    {
        addAlias("i");
    }

    void execute(const std::vector<std::string>& args) override
    {
        // .irc reconnect — recicla la conexion.
        if (!args.empty() && commands::toLower(args[0]) == "reconnect")
        {
            irc::stop();
            irc::start(gameNick());
            commands::reply(commands::tag() + "§f reconnecting...");
            return;
        }

        if (args.empty())
        {
            commands::reply(commands::tag() + "§c usage: §f" + commands::prefix() +
                           "irc <message> | .irc reconnect");
            return;
        }

        // Reunir el mensaje (los espacios son parte del texto).
        std::string message;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0)
                message += ' ';
            message += args[i];
        }

        if (!irc::send(message))
        {
            commands::reply(commands::tag() + "§c not connected to IRC yet");
            return;
        }

        // Eco local con el mismo formato que los mensajes entrantes, con
        // (me) para distinguir que es tuyo.
        commands::reply("§7[§dirc§7]§f §d(me) " + gameNick() + "§7:§f " + message);
    }

private:
    static std::string gameNick()
    {
        Game& game = Game::get();
        if (game.localFound())
            return game.localPlayer().name();
        return "azyre";
    }
};

MC_REGISTER_COMMAND(IRCCommand);

}
