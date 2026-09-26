#include "ModuleManager.hpp"

#include "Config/Config.hpp"
#include "Framework/Log.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

namespace {

// Armadura SEH para los loops de dispatch: una falta desactiva el modulo
// culpable en vez de tumbar el juego. try/catch cubre las excepciones C++
// (json, bad_alloc, winrt) que SEH no atrapa y que terminaban en
// std::terminate -> 0xC0000409. SEH y try/catch viven en funciones
// separadas (MSVC no permite ambos en la misma).

static bool sehTick(Module* m)
{
    __try
    {
        m->onTick();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MC_LOG_ERROR("[Guard] Exception 0x%08X in %s::onTick",
                     GetExceptionCode(), m->name().c_str());
        return false;
    }
}

static bool sehFrame(Module* m, float dt)
{
    __try
    {
        m->onFrame(dt);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MC_LOG_ERROR("[Guard] Exception 0x%08X in %s::onFrame",
                     GetExceptionCode(), m->name().c_str());
        return false;
    }
}

static bool sehRender(Module* m)
{
    __try
    {
        m->onRender();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MC_LOG_ERROR("[Guard] Exception 0x%08X in %s::onRender",
                     GetExceptionCode(), m->name().c_str());
        return false;
    }
}

static void cppGuard(const char* fn, Module* m)
{
    try
    {
        throw;
    }
    catch (const std::exception& e)
    {
        MC_LOG_ERROR("[Guard] C++ exception in %s::%s: %s - module disabled",
                     m->name().c_str(), fn, e.what());
    }
    catch (...)
    {
        MC_LOG_ERROR("[Guard] Unknown C++ exception in %s::%s - module disabled",
                     m->name().c_str(), fn);
    }
    m->setEnabled(false);
}

void guardedTick(Module* m)
{
    try
    {
        if (!sehTick(m))
        {
            m->setEnabled(false);
            return;
        }
    }
    catch (...)
    {
        cppGuard("onTick", m);
    }
}

void guardedFrame(Module* m, float dt)
{
    try
    {
        if (!sehFrame(m, dt))
        {
            m->setEnabled(false);
            return;
        }
    }
    catch (...)
    {
        cppGuard("onFrame", m);
    }
}

void guardedRender(Module* m)
{
    try
    {
        if (!sehRender(m))
        {
            m->setEnabled(false);
            return;
        }
    }
    catch (...)
    {
        cppGuard("onRender", m);
    }
}

}

ModuleManager& ModuleManager::get()
{
    static ModuleManager instance;
    return instance;
}

Module* ModuleManager::add(std::unique_ptr<Module> module)
{
    if (!module)
        return nullptr;
    Module* raw = module.get();
    MC_LOG("[Module] Registered: %s (%s)", raw->name().c_str(), categoryName(raw->category()));
    modules_.push_back(std::move(module));
    return raw;
}

void ModuleManager::notify(const std::string& text, bool enabled)
{
    if (quiet_)
        return;

    Notification n;
    n.text = text;
    n.enabled = enabled;
    n.hasState = true;
    n.time = GetTickCount64();
    notifs_.push_back(std::move(n));
    if (notifs_.size() > 8)
        notifs_.erase(notifs_.begin());
}

void ModuleManager::notifyInfo(const std::string& text)
{
    Notification n;
    n.text = text;
    n.enabled = true;
    n.hasState = false;
    n.time = GetTickCount64();
    notifs_.push_back(std::move(n));
    if (notifs_.size() > 8)
        notifs_.erase(notifs_.begin());
}

void ModuleManager::shutdownAll()
{
    for (auto& m : modules_)
        m->onShutdown();
}

std::vector<Module*> ModuleManager::byCategory(Category cat)
{
    std::vector<Module*> out;
    for (auto& m : modules_)
    {
        if (m->category() == cat)
            out.push_back(m.get());
    }
    return out;
}

Module* ModuleManager::find(const std::string& name)
{
    for (auto& m : modules_)
    {
        if (m->name() == name)
            return m.get();
    }
    return nullptr;
}

void ModuleManager::setBinding(Module* m)
{
    binding_ = m;
    for (int k = 0; k < 256; ++k)
        bindPrev_[k] = (GetAsyncKeyState(k) & 0x8000) != 0;
}

void ModuleManager::handleKeybinds()
{
    static bool wasDown[256] = {};
    bool edge[256] = {};
    bool up[256] = {};
    bool down[256] = {};

    for (int k = 0; k < 256; ++k)
    {
        down[k] = (GetAsyncKeyState(k) & 0x8000) != 0;
        edge[k] = down[k] && !wasDown[k];
        up[k] = !down[k] && wasDown[k];
        wasDown[k] = down[k];
    }

    if (binding_)
    {
        for (int k = 1; k < 256; ++k)
        {
            if (k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON ||
                k == VK_XBUTTON1 || k == VK_XBUTTON2)
            {
                bindPrev_[k] = (GetAsyncKeyState(k) & 0x8000) != 0;
                continue;
            }

            bool down = (GetAsyncKeyState(k) & 0x8000) != 0;
            bool pressed = down && !bindPrev_[k];
            bindPrev_[k] = down;
            if (pressed && k != VK_INSERT)
            {
                binding_->setKeybind(k == VK_ESCAPE ? 0 : k);
                std::string boundName = binding_->name();
                setBinding(nullptr);
                config::saveModule(boundName);
                break;
            }
        }
        return;
    }

    if (ImGui::GetIO().WantTextInput)
        return;

    for (auto& m : modules_)
    {
        int key = m->keybind();
        if (key <= 0 || key > 255)
            continue;
        if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON ||
            key == VK_XBUTTON1 || key == VK_XBUTTON2)
            continue;
        if (m->holdToActivate())
        {
            if (edge[key] || up[key])
                m->setEnabled(down[key]);
        }
        else if (edge[key])
            m->toggle();
    }
}

void ModuleManager::onTick()
{
    for (auto& m : modules_)
    {
        if (m->enabled())
            guardedTick(m.get());
    }
}

void ModuleManager::onFrame(float dt)
{
    for (auto& m : modules_)
    {
        if (m->enabled())
            guardedFrame(m.get(), dt);
    }
}

void ModuleManager::onRender()
{
    for (auto& m : modules_)
    {
        if (m->enabled())
            guardedRender(m.get());
    }
}

void ModuleManager::onHurt(void* actor, void* source, float& damage, bool& cancel)
{
    for (auto& m : modules_)
    {
        if (m->enabled())
            m->onHurt(actor, source, damage, cancel);
    }
}

void ModuleManager::onGetFriction(float& friction)
{
    for (auto& m : modules_)
    {
        if (m->enabled())
            m->onGetFriction(friction);
    }
}

void ModuleManager::onMotionPacket(void* packet, bool& cancel)
{
    for (auto& m : modules_)
    {
        if (m->enabled())
            m->onMotionPacket(packet, cancel);
    }
}

}
