#include "IRC.hpp"

#include "Framework/Log.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

namespace mc::irc {

namespace {

// Libera: plaintext 6667 (SSL 6697 requeriria una libreria TLS).
constexpr const char* kServer = "irc.libera.chat";
constexpr const char* kServerName = "Libera";
constexpr const char* kChannel = "#aeglegeneral";
constexpr int kPort = 6667;

std::atomic<Status> g_status{Status::Offline};
std::thread g_thread;
std::atomic<bool> g_stop{false};

std::mutex g_mutex;
std::deque<std::string> g_logQueue;
std::deque<std::string> g_chatQueue;
std::deque<std::string> g_sendQueue;

std::string g_nick;

void pushLog(const std::string& line)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_logQueue.push_back(line);
    if (g_logQueue.size() > 64)
        g_logQueue.pop_front();
}

void pushChat(const std::string& line)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_chatQueue.push_back(line);
    if (g_chatQueue.size() > 128)
        g_chatQueue.pop_front();
}

// IRC nick charset: alfanumerico mas [_-{}[]`^|]. Todo lo demas se reemplaza
// o se suelta; sin espacios ni '§'.
std::string sanitizeNick(const std::string& raw)
{
    std::string out;
    for (char c : raw)
    {
        unsigned char u = (unsigned char)c;
        if (std::isalnum(u) || c == '_' || c == '-' || c == '{' || c == '}' ||
            c == '[' || c == ']' || c == '`' || c == '^' || c == '|')
            out.push_back(c);
        if (out.size() >= 24)
            break;
    }
    return out;
}

std::string defaultNick()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(100, 999);
    return "azyre_" + std::to_string(dist(gen));
}

bool sendLine(SOCKET s, const std::string& line)
{
    std::string data = line + "\r\n";
    const char* p = data.c_str();
    int remaining = (int)data.size();
    while (remaining > 0)
    {
        int n = ::send(s, p, remaining, 0);
        if (n == SOCKET_ERROR || n == 0)
            return false;
        p += n;
        remaining -= n;
    }
    return true;
}

// Prefix de "nick!user@host" en un mensaje IRC.
std::string prefixNick(const std::string& prefix)
{
    size_t bang = prefix.find('!');
    if (bang == std::string::npos)
        return prefix;
    return prefix.substr(0, bang);
}

void handleLine(SOCKET s, const std::string& line)
{
    MC_LOG("[IRC] << %s", line.c_str());

    // Ping del servidor: respuesta inmediata o nos desconectan.
    if (line.compare(0, 5, "PING ") == 0)
    {
        std::string token = line.substr(5);
        sendLine(s, "PONG :" + token);
        return;
    }

    std::string prefix;
    std::string rest = line;
    if (!rest.empty() && rest[0] == ':')
    {
        size_t sp = rest.find(' ');
        if (sp == std::string::npos)
            return;
        prefix = rest.substr(1, sp - 1);
        rest = rest.substr(sp + 1);
    }

    size_t sp = rest.find(' ');
    std::string cmd = (sp == std::string::npos) ? rest : rest.substr(0, sp);
    std::string args = (sp == std::string::npos) ? "" : rest.substr(sp + 1);

    if (cmd == "376" || cmd == "422") // RPL_ENDOFMOTD / ERR_NOMOTD: registrados
    {
        sendLine(s, "JOIN " + std::string(kChannel));
        pushLog(std::string("[IRC] Connected — joining ") + kChannel);
        return;
    }

    if (cmd == "332" || cmd == "366") // topic o End of NAMES: JOIN confirmado
    {
        pushLog(std::string("[IRC] Joined ") + kChannel);
        g_status = Status::Connected;
        return;
    }

    if (cmd == "PRIVMSG")
    {
        // PRIVMSG <target> :<text>
        size_t sp2 = args.find(' ');
        if (sp2 == std::string::npos)
            return;
        std::string target = args.substr(0, sp2);
        std::string text = args.substr(sp2 + 1);
        if (!text.empty() && text[0] == ':')
            text = text.substr(1);

        pushChat(prefixNick(prefix) + ": " + text);
        return;
    }

    // JOIN/PART/QUIT del canal: avisos de connects/desconects en el chat.
    if (cmd == "JOIN")
    {
        pushChat("§a*** " + prefixNick(prefix) + " joined");
        return;
    }

    if (cmd == "PART")
    {
        pushChat("§7*** " + prefixNick(prefix) + " left the channel");
        return;
    }

    if (cmd == "QUIT")
    {
        pushChat("§7*** " + prefixNick(prefix) + " disconnected");
        return;
    }

    if (cmd == "433") // nick en uso: {name} ya existe → {name}_(azyre_{n})
    {
        // El nuevo nick es {prefijo_del_player}_(azyre_{numero}) para que
        // se vea de quien es el alt en el canal.
        std::string base = sanitizeNick(g_nick);
        // Quitar el (azyre_n) de un reintento anterior si existe.
        size_t paren = base.find("_(azyre_");
        if (paren != std::string::npos)
            base = base.substr(0, paren);

        static int retry = 0;
        ++retry;
        std::string newNick = base + "_(azyre_" + std::to_string(retry) + ")";
        pushLog("[IRC] Nick taken — using " + newNick);
        sendLine(s, "NICK :" + newNick);
        return;
    }

    if (cmd == "465") // ERR_YOUREBANNEDSTUPID — ban/kline (DroneBL etc)
    {
        // 465 <nick> :<reason> — la razon suele incluir el DNSBL hit.
        std::string reason = args;
        size_t colon = reason.find(':');
        if (colon != std::string::npos)
            reason = reason.substr(colon + 1);
        pushLog("§c[IRC] BANNED — " + reason);
        pushLog("§c[IRC] Your IP is on a blacklist (DroneBL or similar)");
        g_stop = true; // el server cerrara la conexion: no seguir
        return;
    }

    if (cmd == "ERROR") // ERROR :Closing Link (K-lined/dnsbl etc)
    {
        std::string reason = args;
        size_t colon = reason.find(':');
        if (colon != std::string::npos)
            reason = reason.substr(colon + 1);
        if (reason.find("K-Line") != std::string::npos ||
            reason.find("K-lined") != std::string::npos ||
            reason.find("dronebl") != std::string::npos ||
            reason.find("dnsbl") != std::string::npos ||
            reason.find("blacklist") != std::string::npos)
        {
            pushLog("§c[IRC] BANNED — " + reason);
            pushLog("§c[IRC] Your IP is on a blacklist (DroneBL or similar)");
        }
        else
        {
            pushLog("§c[IRC] Server error: " + reason);
        }
        return;
    }
}

void ircThread(std::string nick)
{
    MC_LOG("[IRC] thread start");

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        pushLog("[IRC] WSAStartup failed");
        g_status = Status::Offline;
        return;
    }

    pushLog(std::string("[IRC] Connecting to ") + kServerName + " (" + kServer +
            ")...");

    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    if (getaddrinfo(kServer, "6667", &hints, &result) != 0 || !result)
    {
        pushLog("[IRC] DNS failed");
        g_status = Status::Offline;
        WSACleanup();
        return;
    }

    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET)
    {
        pushLog("[IRC] socket() failed");
        freeaddrinfo(result);
        g_status = Status::Offline;
        WSACleanup();
        return;
    }

    // Non-blocking connect para poder abortar con g_stop.
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);

    int rc = connect(sock, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    if (rc == SOCKET_ERROR)
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sock, &set);
        struct timeval tv{};
        tv.tv_sec = 10;

        if (select(0, nullptr, &set, nullptr, &tv) <= 0)
        {
            pushLog("[IRC] Connection timed out");
            closesocket(sock);
            g_status = Status::Offline;
            WSACleanup();
            return;
        }
    }

    // De vuelta a blocking para el loop de lectura.
    nb = 0;
    ioctlsocket(sock, FIONBIO, &nb);

    // Receive timeout: el loop despierta cada 500ms para chequear g_stop.
    DWORD rcvTimeout = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcvTimeout, sizeof(rcvTimeout));

    pushLog("[IRC] Connected — registering as " + nick);

    sendLine(sock, "NICK " + nick);
    sendLine(sock, "USER azyre 0 * :Azyre Client");

    std::string buffer;
    char chunk[1024];
    std::chrono::steady_clock::time_point lastSendCheck{};

    while (!g_stop)
    {
        // Drenar la cola de salida.
        auto now = std::chrono::steady_clock::now();
        if (now - lastSendCheck > std::chrono::milliseconds(50))
        {
            lastSendCheck = now;
            std::deque<std::string> pending;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                pending.swap(g_sendQueue);
            }
            for (const std::string& text : pending)
                sendLine(sock, "PRIVMSG " + std::string(kChannel) + " :" + text);
        }

        int n = recv(sock, chunk, sizeof(chunk), 0);
        if (n > 0)
        {
            buffer.append(chunk, n);
            size_t pos;
            while ((pos = buffer.find("\r\n")) != std::string::npos)
            {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);
                if (!line.empty())
                    handleLine(sock, line);
            }
        }
        else if (n == 0)
        {
            pushLog("[IRC] Disconnected by server");
            break;
        }
        else
        {
            int err = WSAGetLastError();
            if (err != WSAETIMEDOUT)
            {
                pushLog("[IRC] Connection lost");
                break;
            }
        }
    }

    if (sock != INVALID_SOCKET)
    {
        if (g_stop)
            sendLine(sock, "QUIT :Azyre out");
        shutdown(sock, SD_BOTH);
        closesocket(sock);
    }

    g_status = Status::Offline;
    WSACleanup();
    MC_LOG("[IRC] thread exit");
}

} // namespace

void start(const std::string& nick)
{
    // Thread previo termino pero no se hizo join (salida natural o ban):
    // limpiarlo antes de poder arrancar de nuevo (reconect funciona).
    if (g_thread.joinable())
    {
        g_stop = true;
        g_thread.join();
    }

    g_stop = false;
    g_nick = sanitizeNick(nick);
    if (g_nick.empty())
        g_nick = defaultNick();

    g_status = Status::Connecting;
    g_thread = std::thread(ircThread, g_nick);
}

void stop()
{
    if (!g_thread.joinable())
        return;

    g_stop = true;
    g_thread.join();
    g_status = Status::Offline;
}

bool send(const std::string& text)
{
    if (g_status != Status::Connected)
        return false;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_sendQueue.push_back(text);
    return true;
}

std::vector<std::string> pollLog()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string> out(g_logQueue.begin(), g_logQueue.end());
    g_logQueue.clear();
    return out;
}

std::vector<std::string> pollChat()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string> out(g_chatQueue.begin(), g_chatQueue.end());
    g_chatQueue.clear();
    return out;
}

Status status()
{
    return g_status;
}

}
