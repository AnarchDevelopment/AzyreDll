#include "ModuleManager.hpp"

#include "Commands/CommandManager.hpp"
#include "Config/Config.hpp"
#include "Framework/Log.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <deque>
#include <mutex>

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

static bool sehCommand(Module* m, const std::string& cmd)
{
    __try
    {
        m->onCommand(cmd);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MC_LOG_ERROR("[Guard] Exception 0x%08X in %s::onCommand",
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

void guardedCommand(Module* m, const std::string& cmd)
{
    try
    {
        if (!sehCommand(m, cmd))
        {
            m->setEnabled(false);
            return;
        }
    }
    catch (...)
    {
        cppGuard("onCommand", m);
    }
}

std::mutex g_cmdMutex;
std::deque<std::string> g_cmdQueue;

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

// Serial unico y monotomico para las notificaciones (lo usa el modo Rise de
// Notifications para llevar la cola de "una a la vez").
static long long s_nextNotifId = 1;

void ModuleManager::notify(const std::string& text, bool enabled, float duration)
{
    if (quiet_)
        return;

    Notification n;
    n.text = text;
    n.enabled = enabled;
    n.hasState = true;
    n.duration = duration;
    n.id = s_nextNotifId++;
    notifs_.push_back(std::move(n));
    if (notifs_.size() > 8)
        notifs_.erase(notifs_.begin());
}

void ModuleManager::notifyInfo(const std::string& text, float duration)
{
    Notification n;
    n.text = text;
    n.enabled = true;
    n.hasState = false;
    n.duration = duration;
    n.id = s_nextNotifId++;
    notifs_.push_back(std::move(n));
    if (notifs_.size() > 8)
        notifs_.erase(notifs_.begin());
}

void ModuleManager::tickNotifications(float dt)
{
    if (notifs_.empty())
        return;

    for (auto& n : notifs_)
    {
        n.timeShown += dt;
        n.isTimeUp = n.timeShown >= n.duration;

        // Easing exponencial hacia el estado objetivo: entra mientras vive,
        // sale al vencer (mismo feel que el estilo Solstice).
        float target = n.isTimeUp ? 0.0f : 1.0f;
        float t = dt * 5.0f;
        if (t > 1.0f)
            t = 1.0f;
        n.slide += (target - n.slide) * t;
    }

    std::erase_if(notifs_, [](const Notification& n) {
        return n.isTimeUp && n.timeShown > n.duration + 3.0f;
    });
}

void ModuleManager::shutdownAll()
{
    for (auto& m : modules_)
        m->onShutdown();
    graveyard_.clear();
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

    // Vanilla chat is outside ImGui, so WantTextInput is not sufficient. The
    // client screen stack is checked without virtual dispatch; while the
    // ChatScreen vtable is on top, keybinds must not toggle or capture keys.
    if (Game::get().client().chatOpen())
    {
        if (binding_)
        {
            for (int k = 0; k < 256; ++k)
                bindPrev_[k] = down[k];
        }
        return;
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
    std::vector<std::string> pendingCommands;
    {
        std::lock_guard<std::mutex> lock(g_cmdMutex);
        pendingCommands.assign(g_cmdQueue.begin(), g_cmdQueue.end());
        g_cmdQueue.clear();
    }

    if (!pendingCommands.empty())
    {
        // Snapshot: los comandos nativos pueden remover/modular la lista
        // (ej. .script remove) y el dispatch debe seguir valido.
        std::vector<Module*> active;
        for (auto& m : modules_)
        {
            if (m->enabled())
                active.push_back(m.get());
        }

        for (const std::string& cmd : pendingCommands)
        {
            mc::commands::handle(cmd);
            for (Module* m : active)
                guardedCommand(m, cmd);
        }
    }

    for (auto& m : modules_)
    {
        if (m->enabled())
            guardedTick(m.get());
    }

    sweepRemoved();
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

void ModuleManager::queueCommand(const std::string& cmd)
{
    std::lock_guard<std::mutex> lock(g_cmdMutex);
    g_cmdQueue.push_back(cmd);
    if (g_cmdQueue.size() > 32)
        g_cmdQueue.pop_front();
}

bool ModuleManager::remove(const std::string& name)
{
    for (size_t i = 0; i < modules_.size(); ++i)
    {
        if (modules_[i]->name() != name)
            continue;
        Module* m = modules_[i].get();

        bool wasQuiet = quiet_;
        quiet_ = true;
        m->setEnabled(false);
        quiet_ = wasQuiet;
        m->onShutdown();

        graveyard_.push_back(std::move(modules_[i]));
        modules_.erase(modules_.begin() + i);
        MC_LOG("[Module] Removed: %s", name.c_str());
        return true;
    }
    return false;
}

void ModuleManager::sweepRemoved()
{
    if (!graveyard_.empty())
        graveyard_.clear();
}

}
