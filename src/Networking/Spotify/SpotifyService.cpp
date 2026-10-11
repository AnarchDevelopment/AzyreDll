#include "SpotifyService.hpp"

#include "Config/Config.hpp"
#include "Framework/Log.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <shellapi.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <thread>

namespace mc::spotify {

namespace {

// ============ Constantes ============

constexpr int kPort = 8888;
constexpr const char* kRedirectUri = "http://localhost:8888/callback";
constexpr const char* kScope = "user-read-playback-state user-read-currently-playing";

constexpr const wchar_t* kTokenHost = L"accounts.spotify.com";
constexpr const wchar_t* kApiHost = L"api.spotify.com";

constexpr int kDefaultPollMs = 1000;
constexpr size_t kMaxHttpBody = 4 * 1024 * 1024; // caratulas ~640px jpg

// ============ Estado ============

std::mutex g_mu; // protege todo lo de abajo
std::string g_clientId, g_clientSecret;
bool g_credsLoaded = false;

std::string g_accessToken, g_refreshToken;
long long g_expiresAt = 0; // epoch seconds

Track g_track;
std::string g_artUrl;                 // url de g_artBytes
std::vector<unsigned char> g_artBytes;
bool g_artPending = false;

std::string g_lastError;

std::thread g_srvThread, g_pollThread;
std::atomic<bool> g_srvStop{true};
std::atomic<bool> g_pollStop{true};
std::atomic<bool> g_srvUp{false};
std::atomic<int> g_pollMs{kDefaultPollMs};
std::mutex g_srvMu, g_pollMu; // serializan start/stop
std::condition_variable g_srvCv;

HINTERNET g_session = nullptr;
std::mutex g_sessionMu;

// ============ Utilidades ============

std::string credDir()
{
    return config::baseDir() + "\\Spotify";
}

std::string readFileTrimmed(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' ||
                          s.back() == '\t'))
        s.pop_back();
    return s;
}

void writeFile(const std::string& path, const std::string& data)
{
    CreateDirectoryA(config::baseDir().c_str(), nullptr);
    CreateDirectoryA(credDir().c_str(), nullptr);
    std::ofstream out(path, std::ios::trunc | std::ios::binary);
    if (out)
        out << data;
}

void setError(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(g_mu);
    g_lastError = msg;
}

std::string base64(const std::string& in)
{
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in)
    {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0)
        {
            out.push_back(tbl[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(tbl[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

std::string pctEncode(const std::string& s)
{
    std::string out;
    char buf[4];
    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out.push_back((char)c);
        else
        {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string pctDecode(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) &&
            std::isxdigit((unsigned char)s[i + 2]))
        {
            char hex[3] = {s[i + 1], s[i + 2], 0};
            out.push_back((char)strtol(hex, nullptr, 16));
            i += 2;
        }
        else if (s[i] == '+')
            out.push_back(' ');
        else
            out.push_back(s[i]);
    }
    return out;
}

// Extrae un parametro del query string de una request HTTP.
std::string queryParam(const std::string& query, const char* key)
{
    std::string k = std::string(key) + "=";
    size_t pos = 0;
    while (pos < query.size())
    {
        size_t amp = query.find('&', pos);
        size_t end = (amp == std::string::npos) ? query.size() : amp;
        if (query.compare(pos, k.size(), k) == 0)
            return pctDecode(query.substr(pos + k.size(), end - pos - k.size()));
        if (amp == std::string::npos)
            break;
        pos = amp + 1;
    }
    return {};
}

// ============ WinHTTP ============

HINTERNET httpSession()
{
    std::lock_guard<std::mutex> lock(g_sessionMu);
    if (!g_session)
    {
        g_session = WinHttpOpen(L"AzyreSpotify/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g_session)
            g_session = WinHttpOpen(L"AzyreSpotify/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    nullptr, nullptr, 0);
        if (g_session)
            WinHttpSetTimeouts(g_session, 4000, 4000, 6000, 6000);
        else
            MC_LOG_ERROR("[Spotify] WinHttpOpen failed: %lu", GetLastError());
    }
    return g_session;
}

struct HttpRes
{
    DWORD status = 0;
    std::string body;
    bool ok = false;
};

bool httpReq(bool post, const wchar_t* host, const std::wstring& path,
             const std::vector<std::wstring>& headers, const std::string& body,
             HttpRes& out)
{
    HINTERNET ses = httpSession();
    if (!ses)
        return false;

    HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con)
        return false;

    HINTERNET req = WinHttpOpenRequest(con, post ? L"POST" : L"GET", path.c_str(), nullptr,
                                       nullptr, nullptr, WINHTTP_FLAG_SECURE);
    if (!req)
    {
        WinHttpCloseHandle(con);
        return false;
    }

    for (const std::wstring& h : headers)
        WinHttpAddRequestHeaders(req, h.c_str(), (DWORD)-1,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    DWORD totalLen = (post && !body.empty()) ? (DWORD)body.size() : 0;
    BOOL sent = WinHttpSendRequest(
        req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        totalLen ? (LPVOID)body.data() : WINHTTP_NO_REQUEST_DATA, totalLen, totalLen, 0);
    if (!sent || !WinHttpReceiveResponse(req, nullptr))
    {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        return false;
    }

    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                        WINHTTP_NO_HEADER_INDEX);
    out.status = status;

    char buf[4096];
    DWORD avail = 0, read = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0)
    {
        DWORD chunk = avail < sizeof(buf) ? avail : (DWORD)sizeof(buf);
        if (!WinHttpReadData(req, buf, chunk, &read) || read == 0)
            break;
        if (out.body.size() + read > kMaxHttpBody)
            break;
        out.body.append(buf, read);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    out.ok = true;
    return true;
}

bool splitUrl(const std::string& url, std::string& host, std::string& path)
{
    const char* https = "https://";
    if (url.rfind(https, 0) != 0)
        return false;
    size_t s = strlen(https);
    size_t slash = url.find('/', s);
    host = url.substr(s, slash == std::string::npos ? std::string::npos : slash - s);
    path = slash == std::string::npos ? "/" : url.substr(slash);
    return !host.empty();
}

// ============ Tokens ============

std::string tokenPath()
{
    return credDir() + "\\token.json";
}

void saveTokenLocked()
{
    nlohmann::json j{
        {"access_token", g_accessToken},
        {"refresh_token", g_refreshToken},
        {"expires_at", g_expiresAt},
    };
    writeFile(tokenPath(), j.dump());
}

void loadToken()
{
    std::lock_guard<std::mutex> lock(g_mu);
    std::ifstream in(tokenPath());
    if (!in)
        return;
    nlohmann::json j;
    try
    {
        in >> j;
    }
    catch (...)
    {
        return;
    }
    g_accessToken = j.value("access_token", "");
    g_refreshToken = j.value("refresh_token", "");
    g_expiresAt = j.value("expires_at", (long long)0);
}

// POST /api/token — body form listo. Maneja intercambio de code y refresh.
bool postToken(const std::string& form)
{
    std::string id, secret;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        id = g_clientId;
        secret = g_clientSecret;
    }
    if (id.empty() || secret.empty())
    {
        setError("missing client id/secret");
        return false;
    }

    std::string b64 = base64(id + ":" + secret);
    std::vector<std::wstring> headers{
        L"Content-Type: application/x-www-form-urlencoded",
        L"Authorization: Basic " + std::wstring(b64.begin(), b64.end()),
    };

    HttpRes res;
    if (!httpReq(true, kTokenHost, L"/api/token", headers, form, res) || !res.ok)
    {
        setError("token request failed (network)");
        MC_LOG_ERROR("[Spotify] /api/token: request failed");
        return false;
    }

    if (res.status != 200)
    {
        std::string snippet = res.body.substr(0, 200);
        setError("token HTTP " + std::to_string(res.status) + ": " + snippet);
        MC_LOG_ERROR("[Spotify] /api/token HTTP %lu: %s", res.status, snippet.c_str());
        return false;
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(res.body);
    }
    catch (...)
    {
        setError("token response not JSON");
        return false;
    }

    std::string access = j.value("access_token", "");
    if (access.empty())
    {
        setError("no access_token in response");
        return false;
    }

    long long expiresIn = j.value("expires_in", (long long)3600);
    std::string refresh = j.value("refresh_token", "");

    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_accessToken = access;
        if (!refresh.empty())
            g_refreshToken = refresh;
        g_expiresAt = (long long)time(nullptr) + expiresIn;
        g_lastError.clear();
        saveTokenLocked();
    }
    MC_LOG("[Spotify] token updated (expires in %llds)", expiresIn);
    return true;
}

bool refreshTokens()
{
    std::string refresh;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        refresh = g_refreshToken;
    }
    if (refresh.empty())
        return false;
    return postToken("grant_type=refresh_token&refresh_token=" + pctEncode(refresh));
}

} // namespace

// ============ API publica (definiciones fuera del namespace anonimo) ============

void loadCredentials()
{
    std::string id = readFileTrimmed(credDir() + "\\clientid.txt");
    std::string secret = readFileTrimmed(credDir() + "\\clientsecret.txt");
    std::lock_guard<std::mutex> lock(g_mu);
    g_clientId = id;
    g_clientSecret = secret;
    g_credsLoaded = true;
}

bool hasCredentials()
{
    std::lock_guard<std::mutex> lock(g_mu);
    return !g_clientId.empty() && !g_clientSecret.empty();
}

void setCredentials(const std::string& clientId, const std::string& clientSecret)
{
    {
        std::lock_guard<std::mutex> lock(g_mu);
        bool idChanged = (clientId != g_clientId);
        g_clientId = clientId;
        g_clientSecret = clientSecret;
        g_credsLoaded = true;
        if (idChanged)
        {
            // El token pertenece a la app anterior: descartarlo.
            g_accessToken.clear();
            g_refreshToken.clear();
            g_expiresAt = 0;
        }
    }
    writeFile(credDir() + "\\clientid.txt", clientId);
    writeFile(credDir() + "\\clientsecret.txt", clientSecret);

    std::lock_guard<std::mutex> lock(g_mu);
    if (g_accessToken.empty())
    {
        // No borrar el archivo aqui: clearTokens lo hace. Solo limpiar estado si cambio el id.
        std::remove(tokenPath().c_str());
    }
}

void getCredentials(std::string& clientId, std::string& clientSecret)
{
    std::lock_guard<std::mutex> lock(g_mu);
    clientId = g_clientId;
    clientSecret = g_clientSecret;
}

Status status()
{
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_clientId.empty() || g_clientSecret.empty())
        return Status::NoCredentials;
    if (!g_accessToken.empty())
        return Status::Connected;
    return Status::WaitingAuth;
}

bool authorized()
{
    std::lock_guard<std::mutex> lock(g_mu);
    return !g_accessToken.empty();
}

std::string lastError()
{
    std::lock_guard<std::mutex> lock(g_mu);
    return g_lastError;
}

void clearTokens()
{
    std::lock_guard<std::mutex> lock(g_mu);
    g_accessToken.clear();
    g_refreshToken.clear();
    g_expiresAt = 0;
    g_track = Track{};
    std::remove(tokenPath().c_str());
    MC_LOG("[Spotify] tokens cleared");
}

void startAuthFlow()
{
    if (!hasCredentials())
    {
        setError("set Client ID and Secret first");
        return;
    }

    std::string id;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        id = g_clientId;
    }

    std::string url = "https://accounts.spotify.com/authorize?response_type=code&client_id=" +
                      pctEncode(id) + "&redirect_uri=" + pctEncode(kRedirectUri) +
                      "&scope=" + pctEncode(kScope);
    MC_LOG("[Spotify] opening browser: %s", url.c_str());
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ============ Servidor local (:8888 /callback) ============

namespace {

bool sendAll(SOCKET s, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        int n = ::send(s, data.data() + sent, (int)(data.size() - sent), 0);
        if (n <= 0)
            return false;
        sent += (size_t)n;
    }
    return true;
}

void sendHttpResponse(SOCKET s, int code, const char* reason, const std::string& html)
{
    char head[256];
    snprintf(head, sizeof(head),
             "HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\n"
             "Content-Length: %zu\r\nConnection: close\r\n\r\n",
             code, reason, html.size());
    sendAll(s, std::string(head) + html);
}

void handleCallback(const std::string& code, const std::string& error)
{
    if (!error.empty())
    {
        MC_LOG_ERROR("[Spotify] auth error from Spotify: %s", error.c_str());
        setError("auth error: " + error);
        return;
    }
    if (code.empty())
        return;

    MC_LOG("[Spotify] got authorization code, exchanging for token...");
    if (postToken("grant_type=authorization_code&code=" + pctEncode(code) +
                  "&redirect_uri=" + pctEncode(kRedirectUri)))
        MC_LOG("[Spotify] authorized OK");
}

void handleClient(SOCKET c)
{
    DWORD rcvTimeout = 6000;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcvTimeout, sizeof(rcvTimeout));

    std::string req;
    char buf[2048];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384)
    {
        int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0)
        {
            MC_LOG("[Spotify] recv failed/closed while reading request (n=%d, err=%d)",
                   n, WSAGetLastError());
            return;
        }
        req.append(buf, n);
    }

    // Primera linea: "GET /callback?code=... HTTP/1.1"
    size_t lineEnd = req.find("\r\n");
    std::string line = req.substr(0, lineEnd == std::string::npos ? req.size() : lineEnd);
    MC_LOG("[Spotify] request: %s", line.c_str());
    size_t sp1 = line.find(' ');
    size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos)
        return;

    std::string method = line.substr(0, sp1);
    std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    if (method != "GET")
    {
        sendHttpResponse(c, 405, "Method Not Allowed",
                         "<html><body><h2>405</h2></body></html>");
        return;
    }

    if (target.rfind("/favicon", 0) == 0)
    {
        sendHttpResponse(c, 404, "Not Found", "<html><body><h2>404</h2></body></html>");
        return;
    }

    if (target.rfind("/callback", 0) == 0)
    {
        size_t q = target.find('?');
        std::string query = (q == std::string::npos) ? "" : target.substr(q + 1);
        std::string code = queryParam(query, "code");
        std::string error = queryParam(query, "error");

        if (!error.empty())
        {
            sendHttpResponse(c, 200, "OK",
                             "<html><body style=\"font-family:sans-serif\">"
                             "<h2>Azyre — authorization denied</h2>"
                             "<p>You can close this tab and deny access in the game "
                             "settings if you want.</p></body></html>");
        }
        else if (!code.empty())
        {
            sendHttpResponse(c, 200, "OK",
                             "<html><body style=\"font-family:sans-serif\">"
                             "<h2>Azyre — authorization successful</h2>"
                             "<p>You can close this tab and return to the game.</p>"
                             "</body></html>");
        }
        else
        {
            sendHttpResponse(c, 200, "OK",
                             "<html><body style=\"font-family:sans-serif\">"
                             "<h2>Azyre — Spotify callback</h2>"
                             "<p>Missing authorization code.</p></body></html>");
        }

        // Responder primero para que el navegador no se quede colgado, luego
        // intercambiar el code por el token (bloquea este hilo unos segundos).
        handleCallback(code, error);
        return;
    }

    sendHttpResponse(c, 404, "Not Found", "<html><body><h2>404</h2></body></html>");
}

SOCKET makeListener(int family, const char* addr, int& error)
{
    error = 0;
    SOCKET s = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        error = WSAGetLastError();
        return INVALID_SOCKET;
    }

    // This is a single-owner OAuth callback endpoint. SO_REUSEADDR on Windows
    // can let another process bind the same port and makes delivery ambiguous.
    BOOL exclusive = TRUE;
    if (setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   (const char*)&exclusive, sizeof(exclusive)) == SOCKET_ERROR)
    {
        error = WSAGetLastError();
        closesocket(s);
        return INVALID_SOCKET;
    }

    if (family == AF_INET)
    {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(kPort);
        if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1)
        {
            error = WSAEINVAL;
            closesocket(s);
            return INVALID_SOCKET;
        }
        if (bind(s, (sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR)
        {
            error = WSAGetLastError();
            closesocket(s);
            return INVALID_SOCKET;
        }
    }
    else
    {
        DWORD only = 1; // sin dual-stack: 127.0.0.1 ya tiene su socket aparte
        if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY,
                       (const char*)&only, sizeof(only)) == SOCKET_ERROR)
        {
            error = WSAGetLastError();
            closesocket(s);
            return INVALID_SOCKET;
        }

        sockaddr_in6 sa{};
        sa.sin6_family = AF_INET6;
        sa.sin6_port = htons(kPort);
        if (inet_pton(AF_INET6, addr, &sa.sin6_addr) != 1)
        {
            error = WSAEINVAL;
            closesocket(s);
            return INVALID_SOCKET;
        }
        if (bind(s, (sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR)
        {
            error = WSAGetLastError();
            closesocket(s);
            return INVALID_SOCKET;
        }
    }

    if (listen(s, 4) == SOCKET_ERROR)
    {
        error = WSAGetLastError();
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

void serverThread()
{
    MC_LOG("[Spotify] callback server thread start (127.0.0.1:%d + [::1])", kPort);

    WSADATA wsa{};
    int startupError = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (startupError != 0)
    {
        MC_LOG_ERROR("[Spotify] WSAStartup failed: %d", startupError);
        setError("Winsock startup failed (" + std::to_string(startupError) + ")");
        g_srvUp = false;
        g_srvStop = true;
        g_srvCv.notify_all();
        return;
    }

    // Dos listeners: los navegadores resuelven localhost a ::1 y a 127.0.0.1;
    // atendemos ambos para que el redirect funcione sin importar cual usen.
    int error4 = 0;
    int error6 = 0;
    SOCKET s4 = makeListener(AF_INET, "127.0.0.1", error4);
    SOCKET s6 = makeListener(AF_INET6, "::1", error6);
    if (s4 == INVALID_SOCKET && s6 == INVALID_SOCKET)
    {
        MC_LOG_ERROR("[Spotify] cannot bind localhost:%d (IPv4 WSA %d, IPv6 WSA %d)",
                     kPort, error4, error6);
        const bool inUse = error4 == WSAEADDRINUSE || error6 == WSAEADDRINUSE;
        setError(std::string(inUse ? "Port 8888 is already in use" :
                                    "Cannot bind localhost:8888") +
                 " (IPv4 WSA " + std::to_string(error4) +
                 ", IPv6 WSA " + std::to_string(error6) + ")");
        WSACleanup();
        g_srvUp = false;
        g_srvStop = true; // marca "no corriendo" para poder reintentar despues
        g_srvCv.notify_all();
        return;
    }
    if (s4 == INVALID_SOCKET)
        MC_LOG_ERROR("[Spotify] IPv4 loopback unavailable (WSA %d), using [::1] only", error4);
    if (s6 == INVALID_SOCKET)
        MC_LOG_ERROR("[Spotify] IPv6 loopback unavailable (WSA %d), using 127.0.0.1 only", error6);

    g_srvUp = true;
    setError("");
    g_srvCv.notify_all();
    MC_LOG("[Spotify] listening on localhost:%d/callback", kPort);

    while (!g_srvStop)
    {
        fd_set rd;
        FD_ZERO(&rd);
        if (s4 != INVALID_SOCKET)
            FD_SET(s4, &rd);
        if (s6 != INVALID_SOCKET)
            FD_SET(s6, &rd);

        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 500000;

        int r = select(0, &rd, nullptr, nullptr, &tv);
        if (r <= 0)
            continue;

        for (SOCKET l : {s4, s6})
        {
            if (l == INVALID_SOCKET || !FD_ISSET(l, &rd))
                continue;
            SOCKET c = accept(l, nullptr, nullptr);
            if (c == INVALID_SOCKET)
                continue;
            handleClient(c);
            closesocket(c);
        }
    }

    if (s4 != INVALID_SOCKET)
        closesocket(s4);
    if (s6 != INVALID_SOCKET)
        closesocket(s6);
    WSACleanup();
    g_srvUp = false;
    g_srvCv.notify_all();
    MC_LOG("[Spotify] callback server stopped");
}

// ============ Polling ============

bool ensureFreshToken()
{
    std::string access;
    long long expiresAt;
    bool hasRefresh;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        access = g_accessToken;
        expiresAt = g_expiresAt;
        hasRefresh = !g_refreshToken.empty();
    }
    if (access.empty())
    {
        if (hasRefresh)
            return refreshTokens();
        return false;
    }
    if ((long long)time(nullptr) >= expiresAt - 30 && hasRefresh)
        return refreshTokens();
    return true;
}

void downloadArtwork(const std::string& url)
{
    std::string host, path;
    if (!splitUrl(url, host, path))
        return;

    std::wstring whost(host.begin(), host.end());
    std::wstring wpath(path.begin(), path.end());

    HttpRes res;
    if (!httpReq(false, whost.c_str(), wpath, {}, "", res) || !res.ok || res.status != 200)
    {
        MC_LOG("[Spotify] artwork download failed (HTTP %lu)", res.status);
        return;
    }

    std::lock_guard<std::mutex> lock(g_mu);
    g_artUrl = url;
    g_artBytes.assign(res.body.begin(), res.body.end());
    g_artPending = true;
}

void fetchNowPlaying()
{
    std::string access;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        access = g_accessToken;
    }
    if (access.empty())
        return;

    auto doRequest = [&](std::string& token) -> HttpRes {
        HttpRes res;
        std::vector<std::wstring> headers{L"Authorization: Bearer " +
                                          std::wstring(token.begin(), token.end())};
        httpReq(false, kApiHost, L"/v1/me/player/currently-playing", headers, "", res);
        return res;
    };

    HttpRes res = doRequest(access);

    if (res.status == 401)
    {
        if (!refreshTokens())
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_lastError = "unauthorized (token revoked?)";
            g_track = Track{};
            return;
        }
        std::string fresh;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            fresh = g_accessToken;
        }
        res = doRequest(fresh);
    }

    if (!res.ok)
        return; // fallo de red puntual: se reintenta en el proximo tick

    if (res.status == 204)
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_track = Track{};
        return;
    }

    if (res.status != 200)
    {
        MC_LOG("[Spotify] currently-playing HTTP %lu", res.status);
        return;
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(res.body);
    }
    catch (...)
    {
        return;
    }

    if (!j.contains("item") || j["item"].is_null())
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_track = Track{};
        return;
    }

    const auto& item = j["item"];
    Track t;
    t.valid = true;
    t.playing = j.value("is_playing", false);
    t.title = item.value("name", "");
    t.durationMs = item.value("duration_ms", (long long)0);
    t.progressMs = j.value("progress_ms", (long long)0);
    t.progressAtTick = GetTickCount64();

    if (item.contains("artists") && item["artists"].is_array())
    {
        for (const auto& a : item["artists"])
        {
            if (!t.artists.empty())
                t.artists += ", ";
            t.artists += a.value("name", "");
        }
    }
    if (item.contains("album") && item["album"].is_object())
    {
        const auto& album = item["album"];
        t.album = album.value("name", "");
        if (album.contains("images") && album["images"].is_array() && !album["images"].empty())
            t.artworkUrl = album["images"][0].value("url", "");
    }

    std::string prevArt;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        prevArt = g_artUrl;
        g_track = t;
        g_lastError.clear();
    }

    if (!t.artworkUrl.empty() && t.artworkUrl != prevArt)
        downloadArtwork(t.artworkUrl);
}

void pollThread()
{
    MC_LOG("[Spotify] poller start (every %dms)", g_pollMs.load());
    while (!g_pollStop)
    {
        bool ok = false;
        try
        {
            if (ensureFreshToken())
            {
                fetchNowPlaying();
                ok = true;
            }
        }
        catch (const std::exception& e)
        {
            MC_LOG_ERROR("[Spotify] poll exception: %s", e.what());
        }
        catch (...)
        {
            MC_LOG_ERROR("[Spotify] poll unknown exception");
        }

        // Dormir en cortos tramos para reaccionar rapido a stopPolling().
        int total = ok ? g_pollMs.load() : 500;
        for (int waited = 0; waited < total && !g_pollStop; waited += 100)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    MC_LOG("[Spotify] poller exit");
}

} // namespace

// ============ API publica ============

bool startServer()
{
    std::unique_lock<std::mutex> lock(g_srvMu);
    if (g_srvThread.joinable())
    {
        if (!g_srvStop)
        {
            // Una llamada previa puede haber creado el hilo, pero el bind aun
            // no termino. Esperar aqui evita abrir OAuth antes del callback.
            if (!g_srvCv.wait_for(lock, std::chrono::seconds(5), [] {
                    return g_srvUp.load() || g_srvStop.load();
                }) || !g_srvUp)
            {
                if (!g_srvStop)
                    setError("Timed out waiting for localhost:8888 listener");
            }
            return g_srvUp;
        }
        g_srvThread.join(); // fallo previo (por ejemplo, puerto ocupado): limpiar
    }

    loadCredentials();
    loadToken();

    g_srvStop = false;
    try
    {
        g_srvThread = std::thread(serverThread);
    }
    catch (const std::exception& e)
    {
        g_srvStop = true;
        setError(std::string("Could not start Spotify callback thread: ") + e.what());
        return false;
    }

    if (!g_srvCv.wait_for(lock, std::chrono::seconds(5), [] {
            return g_srvUp.load() || g_srvStop.load();
        }) && !g_srvUp)
        setError("Timed out waiting for localhost:8888 listener");
    return g_srvUp;
}

void shutdown()
{
    {
        std::lock_guard<std::mutex> lock(g_srvMu);
        g_srvStop = true;
        if (g_srvThread.joinable())
            g_srvThread.join();
    }
    stopPolling();

    std::lock_guard<std::mutex> lock(g_sessionMu);
    if (g_session)
    {
        WinHttpCloseHandle(g_session);
        g_session = nullptr;
    }
}

bool serverRunning()
{
    return g_srvUp.load();
}

void startPolling()
{
    std::lock_guard<std::mutex> lock(g_pollMu);
    if (g_pollThread.joinable())
    {
        if (!g_pollStop)
            return;
        g_pollThread.join();
    }
    g_pollStop = false;
    g_pollThread = std::thread(pollThread);
}

void stopPolling()
{
    std::lock_guard<std::mutex> lock(g_pollMu);
    g_pollStop = true;
    if (g_pollThread.joinable())
        g_pollThread.join();
}

int pollIntervalMs()
{
    return g_pollMs.load();
}

void setPollIntervalMs(int ms)
{
    if (ms < 250)
        ms = 250;
    if (ms > 10000)
        ms = 10000;
    g_pollMs = ms;
}

Track track()
{
    std::lock_guard<std::mutex> lock(g_mu);
    return g_track;
}

bool takeArtwork(std::string& url, std::vector<unsigned char>& bytes)
{
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_artPending)
        return false;
    // Copia (no move): g_artUrl sigue siendo la referencia de "ya bajada" para
    // no volver a descargar la misma caratula en cada tick.
    url = g_artUrl;
    bytes = g_artBytes;
    g_artPending = false;
    return true;
}

}
