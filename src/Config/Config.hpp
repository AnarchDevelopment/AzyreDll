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

}
