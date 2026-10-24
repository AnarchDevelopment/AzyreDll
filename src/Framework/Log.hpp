#pragma once

#include <string>

namespace mc::log {

void init(const char* file = "azyre_sdk.log");
void setFile(const char* file);
void installCrashHandler();
void shutdown();
void write(const char* fmt, ...);

// Reporte de una excepcion capturada por una guardia SEH (hook de Present,
// resize, etc): escribe por la via Win32 directa (sin heap del CRT, valido
// en contexto de dispatch) con backtrace resuelta a RVA. Throttle propio
// para que un fault por frame no inunde el archivo. Llamar desde el FILTER
// del __except (pre-unwind: la pila aun es la del fault).
void reportGuard(const char* tag, void* exceptionPointers);

}

#define MC_LOG(...) ::mc::log::write(__VA_ARGS__)
#define MC_LOG_ERROR(...) ::mc::log::write(__VA_ARGS__)
