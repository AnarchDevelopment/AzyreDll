#pragma once

#include "Modules/Module.hpp"

#include "Features/PacketHelper.hpp"

namespace mc {

// Blink â€” buffer de paquetes de movimiento (estilo Solstice Blink).
// Mientras esta activo tus MovePlayerPacket no salen: el server (y los
// demas jugadores) te ven congelado en la ultima posicion enviada â€” se
// dibuja una caja ahi. Al liberar, la rafaga sale de golpe: el server
// te "teletransporta" a tu posicion real. El movimiento local nunca se
// toca (client-authoritative en este protocolo).
class Blink : public Module
{
public:
    Blink();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void onTick() override;
    void onRender() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        size_t n = blink::pendingCount();
        return n ? std::to_string(n) + "p" : "";
    }

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    float maxDurationMs_ = 0.0f; // 0 = manual (toggle)
    ULONGLONG activatedAt_ = 0;
};

}
