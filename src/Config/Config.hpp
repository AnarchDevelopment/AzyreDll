#pragma once

#include <string>
#include <vector>

namespace mc::config {

void loadAll();
void saveAll();
void saveModule(const std::string& moduleName);

std::vector<std::string> listPresets();
bool savePreset(const std::string& name);
bool loadPreset(const std::string& name);
bool deletePreset(const std::string& name);

}
