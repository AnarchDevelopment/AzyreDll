#pragma once

#include "Module.hpp"

#include <memory>
#include <vector>

namespace mc {

class ModuleManager
{
public:
    struct Notification
    {
        std::string text;
        bool enabled = false;
        bool hasState = true;
        float duration = 3.0f;
        float timeShown = 0.0f;
        // 0 = fuera de pantalla, 1 = asentada. Anima la entrada diagonal
        // desde la esquina inferior derecha y la salida al vencer.
        float slide = 0.0f;
        bool isTimeUp = false;
        // Serial unico: lo usa el modo Rise de Notifications para saber cual
        // es la "actual" (Rise muestra una a la vez, con cola).
        long long id = -1;
    };

    static ModuleManager& get();

    Module* add(std::unique_ptr<Module> module);
    void notify(const std::string& text, bool enabled, float duration = 3.0f);
    void notifyInfo(const std::string& text, float duration = 3.0f);
    // Avanza la animacion (entrada/salida) con el dt del frame y poda las
    // notificaciones vencidas hace mas de 3s (cuando ya salieron de pantalla).
    void tickNotifications(float dt);
    void shutdownAll();
    void setQuiet(bool quiet) { quiet_ = quiet; }
    bool isQuiet() const { return quiet_; }
    const std::vector<Notification>& notifications() const { return notifs_; }

    void handleKeybinds();
    void onTick();
    void onFrame(float dt);
    void onRender();

    void onHurt(void* actor, void* source, float& damage, bool& cancel);
    void onGetFriction(float& friction);
    void onMotionPacket(void* packet, bool& cancel);

    // Thread-safe: el hook de chat encola comandos desde el hilo de red y
    // onTick los drena en el hilo de Present.
    void queueCommand(const std::string& cmd);

    const std::vector<std::unique_ptr<Module>>& all() const { return modules_; }
    std::vector<Module*> byCategory(Category cat);
    Module* find(const std::string& name);

    // Mueve el modulo a un "graveyard" (queda vivo hasta el sweep de onTick):
    // permite remover durante el dispatch de comandos sin invalidar
    // iteradores. El modulo no vuelve a aparecer en la GUI ni en busquedas.
    bool remove(const std::string& name);

    Module* binding() const { return binding_; }
    void setBinding(Module* m);

private:
    ModuleManager() = default;

    void sweepRemoved();

    std::vector<std::unique_ptr<Module>> modules_;
    std::vector<std::unique_ptr<Module>> graveyard_;
    std::vector<Notification> notifs_;
    Module* binding_ = nullptr;
    bool quiet_ = false;
    bool bindPrev_[256] = {};
};

}

#define MC_REGISTER_MODULE(Class)                                             \
    static bool _mc_reg_##Class = [] {                                        \
        ::mc::ModuleManager::get().add(std::make_unique<Class>());            \
        return true;                                                          \
    }()
