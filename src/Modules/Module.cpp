#include "Module.hpp"

#include "Config/Config.hpp"
#include "Framework/Log.hpp"
#include "Framework/Sound.hpp"
#include "Modules/ModuleManager.hpp"

namespace mc {

namespace {

// Armadura para enable/disable: SEH (faltas) + try/catch (excepciones C++)
// en funciones separadas (MSVC no permite ambos en la misma).

static bool sehEnable(Module* m)
{
    __try
    {
        m->onEnable();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MC_LOG_ERROR("[Guard] Exception 0x%08X in %s::onEnable",
                     GetExceptionCode(), m->name().c_str());
        return false;
    }
}

static bool sehDisable(Module* m)
{
    __try
    {
        m->onDisable();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MC_LOG_ERROR("[Guard] Exception 0x%08X in %s::onDisable",
                     GetExceptionCode(), m->name().c_str());
        return false;
    }
}

void guardedEnable(Module* m)
{
    try
    {
        if (!sehEnable(m))
        {
            m->setEnabled(false);
            return;
        }
    }
    catch (const std::exception& e)
    {
        MC_LOG_ERROR("[Guard] C++ exception in %s::onEnable: %s - module disabled",
                     m->name().c_str(), e.what());
        m->setEnabled(false);
    }
    catch (...)
    {
        MC_LOG_ERROR("[Guard] Unknown C++ exception in %s::onEnable - module disabled",
                     m->name().c_str());
        m->setEnabled(false);
    }
}

void guardedDisable(Module* m)
{
    try
    {
        sehDisable(m);
    }
    catch (const std::exception& e)
    {
        MC_LOG_ERROR("[Guard] C++ exception in %s::onDisable: %s", m->name().c_str(),
                     e.what());
    }
    catch (...)
    {
        MC_LOG_ERROR("[Guard] Unknown C++ exception in %s::onDisable",
                     m->name().c_str());
    }
}

}

const char* categoryName(Category cat)
{
    switch (cat)
    {
    case Category::Combat: return "Combat";
    case Category::Movement: return "Movement";
    case Category::Visuals: return "Visuals";
    case Category::Misc: return "Misc";
    case Category::Scripting: return "Scripting";
    case Category::Interface: return "Interface";
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
        guardedEnable(this);
        MC_LOG("[Module] %s enabled", name_.c_str());
        ModuleManager::get().notify(name_, true);
        if (!ModuleManager::get().isQuiet())
            sound::playOn();
    }
    else
    {
        guardedDisable(this);
        MC_LOG("[Module] %s disabled", name_.c_str());
        ModuleManager::get().notify(name_, false);
        if (!ModuleManager::get().isQuiet())
            sound::playOff();
    }
    config::saveModule(name_);
}

void Module::toggle()
{
    setEnabled(!enabled_);
}

}
