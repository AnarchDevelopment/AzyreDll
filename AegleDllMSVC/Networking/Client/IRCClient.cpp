#include "IRCClient.hpp"
#include <sstream>
#include <algorithm>
#include <map>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::vector<std::string> SplitString(const std::string& str, char delim) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream ss(str);
    while (std::getline(ss, token, delim))
        tokens.push_back(token);
    return tokens;
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
IRCClient& IRCClient::GetInstance() {
    static IRCClient instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Ctor / Dtor
// ---------------------------------------------------------------------------
IRCClient::IRCClient()
    : m_socket(INVALID_SOCKET),
      m_readThread(nullptr),
      m_status(IRCStatus::Disconnected),
      m_running(false),
      m_port(6667)
{}

IRCClient::~IRCClient() {
    Disconnect();
}

// ---------------------------------------------------------------------------
// Static thread proc – no lambda to avoid MSVC/GCC ABI issues with captures
// ---------------------------------------------------------------------------
DWORD WINAPI IRCClient::IRCThreadProc(LPVOID lpParam) {
    if (lpParam)
        static_cast<IRCClient*>(lpParam)->Run();
    return 0;
}

// ---------------------------------------------------------------------------
// Connect  (render thread)
// ---------------------------------------------------------------------------
bool IRCClient::Connect(const std::string& server, 
    int port, const std::string& nick, 
    const std::string& password, 
    bool useSSL
) {
    // Tear down any existing connection first
    Disconnect();

    {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_server      = server;
        m_port        = port;
        m_nick        = nick;
        m_targetNick  = nick;
        m_currentNick = nick;
        m_password    = password;
        m_useSSL      = useSSL;
        m_users.clear();
        m_socket      = INVALID_SOCKET;
    }
    {
        std::lock_guard<std::mutex> lk(m_msgMutex);
        m_messages.clear();
    }

    PushMessage(IRCMessage::Type::System, "Client", "Resolving " + server + "...");

    m_running = true;
    m_status  = IRCStatus::Connecting;

    m_readThread = CreateThread(NULL, 0, IRCClient::IRCThreadProc, this, 0, NULL);
    return m_readThread != nullptr;
}

// ---------------------------------------------------------------------------
// Disconnect  (render thread OR background thread)
// ---------------------------------------------------------------------------
void IRCClient::Disconnect(bool userInitiated) {
    m_running = false;

    CloseSocketInternal();

    if (m_readThread != nullptr) {
        DWORD res = WaitForSingleObject(m_readThread, 3000);
        if (res == WAIT_TIMEOUT)
            TerminateThread(m_readThread, 0);
        CloseHandle(m_readThread);
        m_readThread = nullptr;
    }

    if (m_status != IRCStatus::Disconnected) {
        m_status = IRCStatus::Disconnected;
        PushMessage(IRCMessage::Type::System, "Client", "Disconnected.");
    }
}

void IRCClient::HandleUnexpectedDisconnect() {
    m_status = IRCStatus::ConnectionFailed;
    PushMessage(IRCMessage::Type::System, "Client", "Connection lost. Click Retry to reconnect.");
}

// ---------------------------------------------------------------------------
// CloseSocketInternal  – closes the socket under the data lock
// ---------------------------------------------------------------------------
void IRCClient::CloseSocketInternal() {
    SOCKET s = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        s = m_socket;
        m_socket = INVALID_SOCKET;
    }
    if (s != INVALID_SOCKET)
        closesocket(s);
}

// ---------------------------------------------------------------------------
// SendRaw  – thread-safe socket write
// ---------------------------------------------------------------------------
bool IRCClient::SendRaw(const std::string& rawCommand) {
    SOCKET s;
    {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        s = m_socket;
    }
    if (s == INVALID_SOCKET) return false;

    std::string msg = rawCommand + "\r\n";
    int sent = send(s, msg.c_str(), (int)msg.size(), 0);
    return sent != SOCKET_ERROR;
}

// ---------------------------------------------------------------------------
// SendChannelMessage  (render thread)
// ---------------------------------------------------------------------------
bool IRCClient::SendChannelMessage(const std::string& message) {
    if (m_status != IRCStatus::Connected) return false;
    if (message.empty()) return true;

    std::string myNick = GetCurrentNick();
    auto lines = SplitString(message, '\n');
    bool ok = true;
    for (const auto& line : lines) {
        if (line.empty()) continue;
        if (SendRaw("PRIVMSG #aeglegeneral :" + line))
            PushMessage(IRCMessage::Type::UserMessage, myNick, line);
        else
            ok = false;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Accessors  (render thread)
// ---------------------------------------------------------------------------
IRCStatus IRCClient::GetStatus() {
    return m_status.load();
}

std::vector<IRCMessage> IRCClient::GetMessages() {
    std::lock_guard<std::mutex> lk(m_msgMutex);
    return m_messages;
}

std::vector<std::string> IRCClient::GetUsers() {
    std::lock_guard<std::mutex> lk(m_dataMutex);
    return m_users;
}

std::string IRCClient::GetCurrentNick() {
    std::lock_guard<std::mutex> lk(m_dataMutex);
    return m_currentNick;
}

void IRCClient::ClearMessages() {
    std::lock_guard<std::mutex> lk(m_msgMutex);
    m_messages.clear();
}

void IRCClient::PushSystemMessage(const std::string& sender, const std::string& text) {
    PushMessage(IRCMessage::Type::System, sender, text);
}

// ---------------------------------------------------------------------------
// PushConfigMessage
// ---------------------------------------------------------------------------
void IRCClient::PushConfigMessage(const std::string& sender, const std::string& configName, const std::string& jsonContent, const std::string& comment) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

    std::lock_guard<std::mutex> lk(m_msgMutex);
    IRCMessage msg;
    msg.type       = IRCMessage::Type::ConfigShare;
    msg.sender     = sender;
    msg.text       = jsonContent;
    msg.timeStr    = buf;
    msg.configName = configName;
    msg.comment    = comment;
    m_messages.push_back(msg);

    if (m_messages.size() > 400)
        m_messages.erase(m_messages.begin());
}

// ---------------------------------------------------------------------------
// SendConfigFile  (render thread)
// ---------------------------------------------------------------------------
bool IRCClient::SendConfigFile(const std::string& filename, const std::string& jsonContent, const std::string& comment) {
    if (m_status != IRCStatus::Connected) return false;
    if (filename.empty() || jsonContent.empty()) return false;

    // Remove any separator pipe characters from the comment to prevent parsing bugs
    std::string safeComment = comment;
    safeComment.erase(std::remove(safeComment.begin(), safeComment.end(), '|'), safeComment.end());

    // Compact JSON: remove newlines/tabs for single-line IRC message
    std::string compact = jsonContent;
    compact.erase(std::remove(compact.begin(), compact.end(), '\n'), compact.end());
    compact.erase(std::remove(compact.begin(), compact.end(), '\r'), compact.end());
    compact.erase(std::remove(compact.begin(), compact.end(), '\t'), compact.end());

    std::string prefix = "@CONFIG|" + filename + "|" + safeComment + "|";

    // Single message if it fits
    std::string fullMsg = prefix + compact;
    if (fullMsg.size() < 450) {
        if (SendRaw("PRIVMSG #aeglegeneral :" + fullMsg)) {
            std::string nick = GetCurrentNick();
            PushConfigMessage(nick, filename, compact, safeComment);
            return true;
        }
        return false;
    }

    // Split into multiple parts: @CONFIG|file|comment|PART|TOTAL|data
    int totalParts = (int)(compact.size() / 400) + 1;
    bool ok = true;
    for (int part = 0; part < totalParts; part++) {
        std::string chunk = compact.substr(part * 400, 400);
        std::string partMsg = "@CONFIG|" + filename + "|" + safeComment + "|" + std::to_string(part) + "|" + std::to_string(totalParts) + "|" + chunk;
        if (!SendRaw("PRIVMSG #aeglegeneral :" + partMsg))
            ok = false;
    }
    if (ok) {
        std::string nick = GetCurrentNick();
        PushConfigMessage(nick, filename, compact, safeComment);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// PushMessage  – always safe to call, never holds m_dataMutex
// ---------------------------------------------------------------------------
void IRCClient::PushMessage(IRCMessage::Type type,
                             const std::string& sender,
                             const std::string& text) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

    std::lock_guard<std::mutex> lk(m_msgMutex);
    IRCMessage msg;
    msg.type    = type;
    msg.sender  = sender;
    msg.text    = text;
    msg.timeStr = buf;
    m_messages.push_back(msg);

    if (m_messages.size() > 400)
        m_messages.erase(m_messages.begin());
}

std::string IRCClient::GetCurrentTimeStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Run  – background thread: WSA init → DNS → connect → read loop
// ---------------------------------------------------------------------------
void IRCClient::Run() {
    // Copy connection params under the lock so the render thread can't race
    std::string server, nick, password;
    int port;
    {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        server   = m_server;
        port     = m_port;
        nick     = m_nick;
        password = m_password;
    }

    // 1. Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        m_status = IRCStatus::ConnectionFailed;
        PushMessage(IRCMessage::Type::System, "Client", "WSAStartup failed.");
        m_running = false;
        return;
    }

    // 2. DNS
    struct addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(server.c_str(), portStr.c_str(), &hints, &result) != 0) {
        m_status = IRCStatus::ConnectionFailed;
        PushMessage(IRCMessage::Type::System, "Client",
                    "DNS resolution failed (err " + std::to_string(WSAGetLastError()) + ").");
        WSACleanup();
        m_running = false;
        return;
    }

    if (!m_running) { freeaddrinfo(result); WSACleanup(); return; }

    // 3. Socket
    SOCKET s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (s == INVALID_SOCKET) {
        m_status = IRCStatus::ConnectionFailed;
        PushMessage(IRCMessage::Type::System, "Client",
                    "socket() failed (err " + std::to_string(WSAGetLastError()) + ").");
        freeaddrinfo(result);
        WSACleanup();
        m_running = false;
        return;
    }

    // Publish the socket so CloseSocketInternal() can reach it
    {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_socket = s;
    }

    // Receive timeout: 120 s (long enough to stay alive, short enough to
    // notice a dead server without blocking Disconnect() forever)
    DWORD recvTimeout = 120000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&recvTimeout), sizeof(recvTimeout));

    // 4. TCP connect
    if (connect(s, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        m_status = IRCStatus::ConnectionFailed;
        PushMessage(IRCMessage::Type::System, "Client",
                    "connect() failed (err " + std::to_string(WSAGetLastError()) + ").");
        freeaddrinfo(result);
        CloseSocketInternal();
        WSACleanup();
        m_running = false;
        return;
    }
    freeaddrinfo(result);

    PushMessage(IRCMessage::Type::System, "Client", "TCP connected. Logging in...");

    // 5. IRC registration
    if (!password.empty())
        SendRaw("PASS " + password);
    SendRaw("NICK " + nick);
    SendRaw("USER " + nick + " 0 * :Aegleseeker IRC");

    // 6. Read loop
    char    buf[4096];
    std::string pending;

    while (m_running) {
        // Snapshot the socket each iteration; CloseSocketInternal may set it
        // to INVALID_SOCKET from the render thread
        SOCKET cur;
        {
            std::lock_guard<std::mutex> lk(m_dataMutex);
            cur = m_socket;
        }
        if (cur == INVALID_SOCKET) break;

        int n = recv(cur, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            pending += buf;

            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, pos);
                pending.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                ParseLine(line);
            }
        } else if (n == 0) {
            break; // graceful close
        } else {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT)
                continue; // normal keep-alive timeout
            break; // real error
        }
    }

    CloseSocketInternal();
    WSACleanup();
    m_running = false;
    if (m_status != IRCStatus::ConnectionFailed)
        HandleUnexpectedDisconnect();
}

// ---------------------------------------------------------------------------
// ParseLine
// ---------------------------------------------------------------------------
void IRCClient::ParseLine(const std::string& line) {
    if (line.empty()) return;

    std::string prefix, command, trailing;
    std::vector<std::string> params;

    size_t pos = 0;

    // Prefix
    if (line[0] == ':') {
        size_t sp = line.find(' ');
        if (sp == std::string::npos) return;
        prefix = line.substr(1, sp - 1);
        pos    = sp + 1;
    }

    // Trailing
    size_t trailPos = line.find(" :", pos);
    std::string paramsStr;
    if (trailPos != std::string::npos) {
        trailing  = line.substr(trailPos + 2);
        paramsStr = line.substr(pos, trailPos - pos);
    } else {
        paramsStr = line.substr(pos);
    }

    auto tokens = SplitString(paramsStr, ' ');
    if (tokens.empty()) return;

    command = tokens[0];
    for (size_t i = 1; i < tokens.size(); ++i)
        if (!tokens[i].empty())
            params.push_back(tokens[i]);

    HandleCommand(prefix, command, params, trailing);
}

// Multi-part config share reassembly buffer
static std::map<std::string, std::map<int, std::string>> s_configParts;

// ---------------------------------------------------------------------------
// HandleCommand
// NOTE: Never hold m_dataMutex when calling PushMessage (uses m_msgMutex).
//       Keep lock scopes as short as possible.
// ---------------------------------------------------------------------------
void IRCClient::HandleCommand(const std::string& prefix,
                               const std::string& command,
                               const std::vector<std::string>& params,
                               const std::string& trailing) {
    // PING – reply immediately, no locks needed
    if (command == "PING") {
        std::string target = trailing.empty()
                             ? (params.empty() ? "" : params[0])
                             : trailing;
        SendRaw("PONG :" + target);
        return;
    }

    // Extract nick from prefix  (nick!user@host)
    std::string sender = prefix;
    size_t excl = prefix.find('!');
    if (excl != std::string::npos)
        sender = prefix.substr(0, excl);

    // --- 001 : Welcome ---
    if (command == "001") {
        m_status = IRCStatus::Connected;
        PushMessage(IRCMessage::Type::System, "Server",
                    "Connected! Welcome to Libera.Chat.");

        std::string pw, nick;
        {
            std::lock_guard<std::mutex> lk(m_dataMutex);
            pw   = m_password;
            nick = m_nick;
        }
        if (!pw.empty()) {
            SendRaw("PRIVMSG NickServ :IDENTIFY " + nick + " " + pw);
        }
        SendRaw("JOIN #aeglegeneral");
    }

    // --- 433 : Nick in use ---
    else if (command == "433") {
        std::string newNick;
        {
            std::lock_guard<std::mutex> lk(m_dataMutex);
            m_currentNick += "_";
            newNick = m_currentNick;
        }
        PushMessage(IRCMessage::Type::System, "Server",
                    "Nickname in use, retrying as " + newNick);
        SendRaw("NICK " + newNick);
    }

    // --- JOIN ---
    else if (command == "JOIN") {
        std::string channel = trailing.empty()
                              ? (params.empty() ? "" : params[0])
                              : trailing;
        if (!channel.empty() && channel[0] == ':')
            channel.erase(channel.begin());

        std::string myNick = GetCurrentNick();

        if (sender == myNick) {
            PushMessage(IRCMessage::Type::System, "Client",
                        "You joined " + channel);
            SendRaw("NAMES " + channel);
        } else {
            {
                std::lock_guard<std::mutex> lk(m_dataMutex);
                if (std::find(m_users.begin(), m_users.end(), sender) == m_users.end()) {
                    m_users.push_back(sender);
                    std::sort(m_users.begin(), m_users.end());
                }
            }
            PushMessage(IRCMessage::Type::Join, sender, "has joined " + channel);
        }
    }

    // --- PART ---
    else if (command == "PART") {
        std::string channel = params.empty() ? "" : params[0];
        std::string myNick  = GetCurrentNick();

        if (sender == myNick) {
            {
                std::lock_guard<std::mutex> lk(m_dataMutex);
                m_users.clear();
            }
            PushMessage(IRCMessage::Type::System, "Client", "You left " + channel);
        } else {
            {
                std::lock_guard<std::mutex> lk(m_dataMutex);
                m_users.erase(
                    std::remove(m_users.begin(), m_users.end(), sender),
                    m_users.end());
            }
            PushMessage(IRCMessage::Type::Part, sender,
                        "has left " + channel + " (" + trailing + ")");
        }
    }

    // --- QUIT ---
    else if (command == "QUIT") {
        std::string myNick = GetCurrentNick();
        if (sender != myNick) {
            {
                std::lock_guard<std::mutex> lk(m_dataMutex);
                m_users.erase(
                    std::remove(m_users.begin(), m_users.end(), sender),
                    m_users.end());
            }
            PushMessage(IRCMessage::Type::Quit, sender,
                        "has quit (" + trailing + ")");
        }
    }

    // --- PRIVMSG ---
    else if (command == "PRIVMSG") {
        std::string target = params.empty() ? "" : params[0];
        if (target == "#aeglegeneral" || target == GetCurrentNick()) {
            // Check for config share: @CONFIG|filename|...
            const std::string configPrefix = "@CONFIG|";
            if (trailing.compare(0, configPrefix.size(), configPrefix) == 0) {
                std::string rest = trailing.substr(configPrefix.size());
                
                size_t sep1 = rest.find('|');
                if (sep1 != std::string::npos) {
                    std::string fname = rest.substr(0, sep1);
                    std::string afterName = rest.substr(sep1 + 1);
                    
                    size_t sep2 = afterName.find('|');
                    if (sep2 != std::string::npos) {
                        std::string comment = afterName.substr(0, sep2);
                        std::string afterComment = afterName.substr(sep2 + 1);
                        
                        size_t sep3 = afterComment.find('|');
                        if (sep3 != std::string::npos) {
                            size_t sep4 = afterComment.find('|', sep3 + 1);
                            if (sep4 != std::string::npos) {
                                // Multi-part: partNum|totalParts|data
                                try {
                                    int partNum = std::stoi(afterComment.substr(0, sep3));
                                    int totalParts = std::stoi(afterComment.substr(sep3 + 1, sep4 - sep3 - 1));
                                    std::string data = afterComment.substr(sep4 + 1);
                                    
                                    s_configParts[fname][partNum] = data;
                                    
                                    if ((int)s_configParts[fname].size() == totalParts) {
                                        std::string fullJson;
                                        for (int i = 0; i < totalParts; i++)
                                            fullJson += s_configParts[fname][i];
                                        s_configParts.erase(fname);
                                        PushConfigMessage(sender, fname, fullJson, comment);
                                    }
                                } catch (...) {
                                    PushConfigMessage(sender, fname, afterComment, comment);
                                }
                            } else {
                                PushConfigMessage(sender, fname, afterComment, comment);
                            }
                        } else {
                            PushConfigMessage(sender, fname, afterComment, comment);
                        }
                    }
                }
            } else {
                PushMessage(IRCMessage::Type::UserMessage, sender, trailing);
            }
        }
    }

    // --- 353 : Names list ---
    else if (command == "353") {
        auto nicks = SplitString(trailing, ' ');
        std::lock_guard<std::mutex> lk(m_dataMutex);
        for (std::string n : nicks) {
            if (n.empty()) continue;
            char c = n[0];
            if (c == '@' || c == '+' || c == '%' || c == '&' || c == '~')
                n.erase(n.begin());
            if (!n.empty() &&
                std::find(m_users.begin(), m_users.end(), n) == m_users.end())
                m_users.push_back(n);
        }
        std::sort(m_users.begin(), m_users.end());
    }

    // --- 366 : End of names – nothing to do ---
    else if (command == "366") { /* done */ }

    // --- NICK change ---
    else if (command == "NICK") {
        std::string newNick = trailing.empty()
                              ? (params.empty() ? "" : params[0])
                              : trailing;
        if (!newNick.empty() && newNick[0] == ':')
            newNick.erase(newNick.begin());

        std::string myNick = GetCurrentNick(); // reads under lock, returns copy

        if (sender == myNick) {
            {
                std::lock_guard<std::mutex> lk(m_dataMutex);
                m_currentNick = newNick;
            }
            PushMessage(IRCMessage::Type::System, "Client",
                        "Your nick is now " + newNick);
        } else {
            {
                std::lock_guard<std::mutex> lk(m_dataMutex);
                m_users.erase(
                    std::remove(m_users.begin(), m_users.end(), sender),
                    m_users.end());
                if (std::find(m_users.begin(), m_users.end(), newNick) == m_users.end()) {
                    m_users.push_back(newNick);
                    std::sort(m_users.begin(), m_users.end());
                }
            }
            PushMessage(IRCMessage::Type::System, sender,
                        "is now known as " + newNick);
        }
    }

    // --- NOTICE ---
    else if (command == "NOTICE") {
        PushMessage(IRCMessage::Type::System, "Notice", trailing);
    }

    // --- ERROR ---
    else if (command == "ERROR") {
        PushMessage(IRCMessage::Type::System, "Error", trailing);
        m_status = IRCStatus::ConnectionFailed;
        m_running = false;
    }

    // --- Numeric error replies (4xx, 5xx) ---
    else if (command.size() == 3 && std::all_of(command.begin(), command.end(), ::isdigit)) {
        int numeric = std::stoi(command);
        if (numeric >= 400 && numeric < 600) {
            std::string errorMsg = "IRC Error " + command + ": " + trailing;
            PushMessage(IRCMessage::Type::System, "Server", errorMsg);
            
            // Handle specific error codes
            if (numeric == 465) {
                PushMessage(IRCMessage::Type::System, "Client", "Your IP is banned from this server. Use a VPN or try another server.");
                m_running = false;
                m_status = IRCStatus::ConnectionFailed;
            } else if (numeric == 471 || numeric == 473 || numeric == 474 || numeric == 475 || numeric == 476 || numeric == 477 || numeric == 478) {
                PushMessage(IRCMessage::Type::System, "Client", "Cannot join channel (banned/invite only/etc).");
                m_running = false;
                m_status = IRCStatus::ConnectionFailed;
            } else if (numeric == 433) {
                // Nick in use - already handled above
            } else if (numeric >= 400 && numeric < 500) {
                // Client errors - might be temporary
                PushMessage(IRCMessage::Type::System, "Client", "Connection error, check credentials or try again later.");
            }
        }
    }
}
