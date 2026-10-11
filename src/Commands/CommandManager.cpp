#include "CommandManager.hpp"

#include "Command.hpp"

#include "impl/BindCommand.hpp"
#include "impl/BindsCommand.hpp"
#include "impl/ConfigCommand.hpp"
#include "impl/FriendCommand.hpp"
#include "impl/HelpCommand.hpp"
#include "impl/IRCCommand.hpp"
#include "impl/PrefixCommand.hpp"
#include "impl/ScriptCommand.hpp"
#include "impl/ToggleCommand.hpp"

#include "Config/Config.hpp"
#include "Framework/Log.hpp"
#include "Modules/ModuleManager.hpp"
#include "SDK/ChatHook.hpp"

#include <atomic>
#include <cctype>

namespace mc::commands {

namespace {

std::vector<std::unique_ptr<Command>>& registryRef()
{
    static std::vector<std::unique_ptr<Command>> reg;
    return reg;
}

// El hook corre en el hilo de envio de paquetes y el dispatch en el hilo de
// Present: el prefijo vive en un atomic para lectura sin bloqueos.
std::atomic<char>& prefixChar()
{
    static std::atomic<char> c{'.'};
    return c;
}

std::atomic<bool>& prefixLoaded()
{
    static std::atomic<bool> loaded{false};
    return loaded;
}

void ensurePrefixLoaded()
{
    if (prefixLoaded().exchange(true))
        return;
    std::string v = config::globalSetting("prefix", ".");
    if (v.size() == 1 && (unsigned char)v[0] >= 0x21 && (unsigned char)v[0] <= 0x7E)
        prefixChar().store(v[0]);
}

std::vector<std::string> tokenize(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : text)
    {
        if (c == ' ' || c == '\t')
        {
            if (!cur.empty())
            {
                out.push_back(cur);
                cur.clear();
            }
        }
        else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

} // namespace

void registerCommand(std::unique_ptr<Command> cmd)
{
    if (cmd)
        registryRef().push_back(std::move(cmd));
}

const std::vector<std::unique_ptr<Command>>& registry()
{
    return registryRef();
}

bool isCommand(const std::string& msg)
{
    ensurePrefixLoaded();
    const std::string p(1, prefixChar().load());
    if (msg.size() <= p.size())
        return false;
    return msg.compare(0, p.size(), p) == 0;
}

const std::string prefix()
{
    ensurePrefixLoaded();
    return std::string(1, prefixChar().load());
}

void setPrefix(char c)
{
    unsigned char u = (unsigned char)c;
    if (u < 0x21 || u > 0x7E)
        return;
    prefixChar().store(c);
    config::setGlobalSetting("prefix", std::string(1, c));
}

void reply(const std::string& text)
{
    chathook::display(text);
}

std::string tag()
{
    return "§7[§bazyre§7]";
}

std::string toLower(std::string s)
{
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

Module* findModule(const std::string& name)
{
    for (auto& m : ModuleManager::get().all())
    {
        if (toLower(m->name()) == toLower(name))
            return m.get();
    }
    return nullptr;
}

void handle(const std::string& msg)
{
    std::vector<std::string> tokens = tokenize(msg.substr(prefix().size()));
    if (tokens.empty())
        return;

    for (auto& c : registryRef())
    {
        if (!c->matches(tokens[0]))
            continue;

        std::vector<std::string> args(tokens.begin() + 1, tokens.end());
        try
        {
            c->execute(args);
        }
        catch (const std::exception& e)
        {
            MC_LOG_ERROR("[Commands] Exception in %s: %s", c->name().c_str(), e.what());
            reply(tag() + "§c command error!");
        }
        catch (...)
        {
            MC_LOG_ERROR("[Commands] Unknown exception in %s", c->name().c_str());
            reply(tag() + "§c command error!");
        }
        return;
    }

    MC_LOG("[Commands] No native handler for: %s", tokens[0].c_str());
}

} // namespace mc::commands
