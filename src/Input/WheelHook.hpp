#pragma once

#include <windows.h>

namespace mc::wheel {

void setModule(void* moduleHandle);
bool install();
void uninstall();
void addWinrtDelta(int wheelDelta);
float consume();
float lastValue();

}
