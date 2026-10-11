#pragma once

#include "Modules/Module.hpp"

namespace mc {

// IRC — bridge entre el cliente global (Networking/IRC) y el juego.
// Al entrar a un mundo conecta a Libera (#aeglegeneral) y muestra el log
// de conexion en el chat; los mensajes del canal llegan como
// "[irc] nick: text". El comando .irc envia mensajes al canal.
class IRCModule : public Module
{
public:
    IRCModule();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void onTick() override;

private:
    void drainQueues(bool force);

    ULONGLONG retryAt_ = 0;
    bool announcedJoin_ = false;
};

}
