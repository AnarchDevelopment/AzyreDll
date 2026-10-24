#pragma once

#include "Modules/Module.hpp"
#include "SDK/CodePatch.hpp"

namespace mc {

class Zoom : public Module
{
public:
    Zoom();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    bool ensurePatch();

    int mode_ = 0;          // 0 = Hold, 1 = Toggle
    int key_ = 0x43;        // 'C'
    float level_ = 4.0f;    // Nivel de zoom (X)
    bool smooth_ = true;
    float speed_ = 8.0f;    // Velocidad del lerp (Hold/Toggle)
    bool scroll_ = true;    // Rueda ajusta el nivel mientras se zoomea
    bool smoothCam_ = true; // Camara cinematica mientras se zoomea
    float camSpeed_ = 1.0f; // Multiplicador del filterAmount (1.0 = OptiFine)

    // Runtime
    bool zooming_ = false;
    bool keyWasDown_ = false;
    float factor_ = 1.0f;   // 1.0 = passthrough (sin zoom)
    bool listening_ = false;
    bool bindPrev_[256] = {};

    // Camara cinematica: puerto exacto del MouseFilter de OptiFine 1.8.9
    // (EntityRenderer.updateRenderer + MouseFilter.smooth). Filtro de deltas
    // con doble suavizado y clamp anti-spike: acumula la intencion real del
    // mouse (in), persigue el gap proporcionalmente, y una segunda etapa
    // (rate) amorta los flicks rapidos.
    struct MouseFilter
    {
        float in = 0.0f;    // acumulador del input (objetivo virtual)
        float out = 0.0f;   // acumulador del output aplicado
        float rate = 0.0f;  // smoother secundario (lerp 0.5/frame + clamp)

        void reset()
        {
            in = 0.0f;
            out = 0.0f;
            rate = 0.0f;
        }

        float smooth(float delta, float amount, float lerpK)
        {
            in += delta;
            delta = (in - out) * amount;
            rate += (delta - rate) * lerpK;
            if ((delta > 0.0f && delta > rate) || (delta < 0.0f && delta < rate))
                delta = rate;
            out += delta;
            return delta;
        }
    };

    MouseFilter filterX_;
    MouseFilter filterY_;
    float smoothYaw_ = 0.0f;
    float smoothPitch_ = 0.0f;
    bool camSynced_ = false;

    uintptr_t site_ = 0;
    patch::CodePatch patch_;
};

}
