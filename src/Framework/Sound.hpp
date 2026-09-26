#pragma once

namespace mc::sound {

void setModule(void* moduleHandle);
bool init();
void shutdown();

void playOn();
void playOff();

}
