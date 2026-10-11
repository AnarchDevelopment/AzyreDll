#pragma once

#include <string>

namespace mc::chathook {

bool install();
void uninstall();

// Mensaje visible solo en el chat local del cliente (no se envia al servidor).
bool display(const std::string& text);

}
