#pragma once

#include <string>

namespace mc::log {

void init(const char* file = "azyre_sdk.log");
void shutdown();
void write(const char* fmt, ...);

}

#define MC_LOG(...) ::mc::log::write(__VA_ARGS__)
#define MC_LOG_ERROR(...) ::mc::log::write(__VA_ARGS__)
