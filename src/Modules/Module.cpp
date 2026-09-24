#include "Module.hpp"

#include "Framework/Log.hpp"
#include "Modules/ModuleManager.hpp"

namespace mc {

const char* categoryName(Category cat)
{
    switch (cat)
    {
    case Category::Combat: return "Combat";
    case Category::Movement: return "Movement";
    case Category::Visuals: return "Visuals";
    case Category::Misc: return "Misc";
    }
    return "Unknown";
}

Module::Module(std::string name, std::string description, Category category, int keybind)
    : name_(std::move(name))
    , description_(std::move(description))
    , category_(category)
    , keybind_(keybind)
{
}

void Module::setEnabled(bool enabled)
{
    if (enabled_ == enabled)
        return;
    enabled_ = enabled;
    if (enabled_)
    {
        onEnable();
        MC_LOG("[Module] %s enabled", name_.c_str());
        ModuleManager::get().notify(name_, true);
    }
    else
    {
        onDisable();
        MC_LOG("[Module] %s disabled", name_.c_str());
        ModuleManager::get().notify(name_, false);
    }
}

void Module::toggle()
{
    setEnabled(!enabled_);
}

}
