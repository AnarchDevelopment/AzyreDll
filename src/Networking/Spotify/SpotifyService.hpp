#pragma once

#include <string>
#include <vector>

namespace mc::spotify {

// Estado de la cancion que se esta escuchando (copia thread-safe).
struct Track
{
    bool valid = false;
    bool playing = false;
    std::string title;
    std::string artists;
    std::string album;
    std::string artworkUrl;
    long long progressMs = 0;
    long long durationMs = 0;
    // GetTickCount64() al medir progressMs (para extrapolar el avance).
    unsigned long long progressAtTick = 0;
};

enum class Status
{
    NoCredentials, // faltan clientid.txt / clientsecret.txt
    WaitingAuth,   // credenciales OK, esperando /callback?code=
    Connected,     // access token valido
};

// Ciclo de vida. startServer() abre el socket localhost:8888 con el endpoint
// /callback (se llama al inyectar); shutdown() lo detiene. Idempotentes.
bool startServer(); // espera al bind y devuelve si localhost:8888 quedo escuchando
void shutdown();
bool serverRunning();

// Credenciales en <base>/Spotify/clientid.txt y <base>/Spotify/clientsecret.txt.
void loadCredentials();
bool hasCredentials();
void setCredentials(const std::string& clientId, const std::string& clientSecret);
void getCredentials(std::string& clientId, std::string& clientSecret);

Status status();
bool authorized();
std::string lastError(); // ultimo fallo (token/API), vacio si no hay

// Abre el navegador en accounts.spotify.com/authorize con redirect
// http://localhost:8888/callback. Requiere credenciales.
void startAuthFlow();
void clearTokens(); // borra el token persistido (desconectar)

// Hilo que consulta /v1/me/player/currently-playing cada pollIntervalMs.
void startPolling();
void stopPolling();
int pollIntervalMs();
void setPollIntervalMs(int ms);

Track track(); // snapshot bajo mutex

// Caratula descargada pendiente de convertir en textura. La consume el hilo
// de render: devuelve false si no hay imagen nueva.
bool takeArtwork(std::string& url, std::vector<unsigned char>& bytes);

}
