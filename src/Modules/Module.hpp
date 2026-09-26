#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace mc {

enum class Category
{
    Combat,
    Movement,
    Visuals,
    Misc,
};

const char* categoryName(Category cat);

class Module
{
public:
    Module(std::string name, std::string description, Category category, int keybind = 0);
    virtual ~Module() = default;

    virtual void onEnable() {}
    virtual void onDisable() {}
    virtual void onTick() {}
    virtual void onFrame(float /*dt*/) {}
    virtual void onRender() {}
    virtual void drawSettings() {}
    virtual void onShutdown() {}

    virtual nlohmann::json saveSettings() const { return nlohmann::json{}; }
    virtual void loadSettings(const nlohmann::json& j) { (void)j; }

    virtual void onHurt(void* /*actor*/, void* /*source*/, float& /*damage*/, bool& /*cancel*/) {}
    virtual void onGetFriction(float& /*friction*/) {}
    virtual void onMotionPacket(void* /*packet*/, bool& /*cancel*/) {}

    void toggle();
    void setEnabled(bool enabled);

    virtual bool holdToActivate() const { return false; }

    bool enabled() const { return enabled_; }
    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    Category category() const { return category_; }

    int keybind() const { return keybind_; }
    void setKeybind(int key) { keybind_ = key; }

    bool hasSettings() const { return hasSettings_; }

protected:
    void markHasSettings() { hasSettings_ = true; }

private:
    std::string name_;
    std::string description_;
    Category category_;
    int keybind_;
    bool enabled_ = false;
    bool hasSettings_ = false;
};

}

#include "Modules/ModuleManager.hpp"
