#pragma once

#include <memory>
#include <string>
#include <vector>

namespace mc {

class Command;
class Module;

namespace commands {

// ¿Empieza el mensaje con el prefijo de comandos?
bool isCommand(const std::string& msg);

// Ejecuta el comando nativo si existe. Si no existe no responde en el chat:
// los scripts pueden manejarlo desde onCommand.
void handle(const std::string& msg);

const std::string prefix();
void setPrefix(char c);

// ---- helpers para las implementaciones ----

void reply(const std::string& text);
std::string tag();
std::string toLower(std::string s);
Module* findModule(const std::string& name);
const std::vector<std::unique_ptr<Command>>& registry();
void registerCommand(std::unique_ptr<Command> cmd);

} // namespace commands

} // namespace mc

#define MC_REGISTER_COMMAND(Class)                                             \
    static bool _mc_cmdreg_##Class = [] {                                      \
        ::mc::commands::registerCommand(std::make_unique<Class>());             \
        return true;                                                            \
    }()
