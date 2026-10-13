#include "IRCModule.hpp"

#include "Networking/IRC/IRC.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Game.hpp"

#include <windows.h>

namespace mc {

IRCModule::IRCModule()
    : Module("IRC", "Bridge to the IRC channel (Libera #aeglegeneral)", Category::Misc, 0)
{
    // Habilitado por defecto: el PlayerList usa la lista del canal para
    // detectar usuarios que estan usando Azyre (nombre con gradiente).
    setEnabled(true);
}

void IRCModule::onEnable()
{
    announcedJoin_ = false;
    retryAt_ = 0;
}

void IRCModule::onDisable()
{
    irc::stop();
}

void IRCModule::onShutdown()
{
    irc::stop();
}

void IRCModule::onTick()
{
    Game& game = Game::get();

    // Conectar cuando hay un player (el nick se toma del jugador).
    if (irc::status() == irc::Status::Offline && game.localFound())
    {
        ULONGLONG now = GetTickCount64();
        if (retryAt_ == 0 || now >= retryAt_)
        {
            irc::start(game.localPlayer().name());
            retryAt_ = now + 30000ull; // reintento tardio si falla
        }
    }

    // Log de conexion: aparece en el chat mientras se conecta.
    for (const std::string& line : irc::pollLog())
        chathook::display("§7[§dIRC§7]§f " + line);

    // Mensajes del canal: "[irc] nick: text".
    for (const std::string& msg : irc::pollChat())
        chathook::display("§7[§dirc§7]§f " + msg);

    // La primera vez que entra al canal, avisar como se chatea.
    if (irc::status() == irc::Status::Connected && !announcedJoin_)
    {
        announcedJoin_ = true;
        chathook::display("§7[§dIRC§7]§f §aConnected — chat with .irc <message>");
    }
}

MC_REGISTER_MODULE(IRCModule);

}
