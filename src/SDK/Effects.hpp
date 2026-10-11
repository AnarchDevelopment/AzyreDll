#pragma once

#include <vector>

namespace mc::effects {

struct Effect
{
    int id = 0;    // 1..30
    int ticks = 0; // duracion restante en ticks (20/s)
    int amp = 0;  // amplificador 0-based
};

// Hook del render que cachea el ScreenContext (instalar en el bootstrap,
// desinstalar en el detach). Sin el, active() solo puede sondear bases
// conocidas del scanner.
void install();
void uninstall();

// Lee los efectos activos del player local. Orden: ScreenContext cacheado
// por el hook (camino validado por el prototipo), luego probes de las
// bases del scanner (LocalPlayer/Client/Level). El primer candidato que
// muestre un vector no vacio con entradas sanas queda fijado; si la
// fuente se corrompe (respawn, rejoin) se vuelve a sondear.
std::vector<Effect> active();

// True cuando ya se identifico de donde leer (debug/telemetria).
bool sourceLocked();

}
