#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Notifications : public Module
{
public:
    Notifications();

    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    void renderAzyre();
    void renderRise(float dt);

    int mode_ = 0; // 0 = Azyre (toasts), 1 = Rise (NotificationComponent)

    // Estado del modo Rise: una notificacion a la vez (cola, como Rise).
    long long curId_ = -1;       // id de la notificacion en pantalla
    long long lastShownId_ = 0;  // ultima ya consumida
    float curTime_ = 0.0f;       // tiempo en pantalla de la actual
    float animVal_ = 0.0f;       // valor del tween (1.0 = reposo, 1.1 = salida)
    float animFrom_ = 0.0f;
    float animTarget_ = 0.0f;
    double animT0_ = 0.0;
    bool animActive_ = false;
};

}
