#pragma once

#include <windows.h>

// Macros de compatibilidad cross-toolchain. Se incluyen DESPUES de windows.h:
// MinGW tambien define __try/__except en sus headers, y aqui las remapeamos.
#include "Framework/Compat.hpp"
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include <nlohmann/json.hpp>
