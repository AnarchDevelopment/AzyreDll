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
        unsigned long long time = 0;
    };

    static ModuleManager& get();

    Module* add(std::unique_ptr<Module> module);
    void notify(const std::string& text, bool enabled);
    void notifyInfo(const std::string& text);
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

    const std::vector<std::unique_ptr<Module>>& all() const { return modules_; }
    std::vector<Module*> byCategory(Category cat);
    Module* find(const std::string& name);

    Module* binding() const { return binding_; }
    void setBinding(Module* m);

private:
    ModuleManager() = default;

    std::vector<std::unique_ptr<Module>> modules_;
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
