#pragma once

#include <string>
#include <vector>

namespace mc::config {

void init();
void loadAll();
void saveAll();
void saveModule(const std::string& moduleName);

std::string baseDir();

std::vector<std::string> listPresets();
bool savePreset(const std::string& name);
bool loadPreset(const std::string& name);
bool deletePreset(const std::string& name);

// Ajustes globales del cliente (Configs\settings.json), independientes del
// estado de los modulos (ej. el prefijo de comandos).
std::string globalSetting(const std::string& key, const std::string& fallback);
void setGlobalSetting(const std::string& key, const std::string& value);

}
