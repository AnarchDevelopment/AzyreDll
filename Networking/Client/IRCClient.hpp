#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>
#include <mutex>
#include <atomic>

enum class IRCStatus {
    Disconnected,
    Connecting,
    Connected,
    ConnectionFailed
};

struct IRCMessage {
    enum class Type {
        System,
        UserMessage,
        Join,
        Part,
        Quit,
        ConfigShare
    };

    Type type;
    std::string sender;
    std::string text;
    std::string timeStr;
    std::string configName;
    std::string comment;
};

class IRCClient {
public:
    static IRCClient& GetInstance();

    bool Connect(const std::string& server, int port, const std::string& nick, const std::string& password, bool useSSL = false);
    void Disconnect(bool userInitiated = true);
    void HandleUnexpectedDisconnect();
    bool SendChannelMessage(const std::string& message);
    bool SendConfigFile(const std::string& filename, const std::string& jsonContent, const std::string& comment = "");
    bool SendRaw(const std::string& rawCommand);

    IRCStatus GetStatus();
    std::vector<IRCMessage> GetMessages();
    std::vector<std::string> GetUsers();
    std::string GetCurrentNick();
    void ClearMessages();
    void PushSystemMessage(const std::string& sender, const std::string& text);

private:
    IRCClient();
    ~IRCClient();

    IRCClient(const IRCClient&) = delete;
    IRCClient& operator=(const IRCClient&) = delete;

    static DWORD WINAPI IRCThreadProc(LPVOID lpParam);

    void Run();
    void CloseSocketInternal();
    void ParseLine(const std::string& line);
    void HandleCommand(const std::string& prefix, const std::string& command,
                       const std::vector<std::string>& params, const std::string& trailing);

    // Adds a message without holding m_dataMutex (uses m_msgMutex internally)
    void PushMessage(IRCMessage::Type type, const std::string& sender, const std::string& text);
    void PushConfigMessage(const std::string& sender, const std::string& configName, const std::string& jsonContent, const std::string& comment = "");
    std::string GetCurrentTimeStr();

    // --- sockets / connection state ---
    SOCKET            m_socket;
    HANDLE            m_readThread;
    std::mutex        m_dataMutex; // guards: m_socket, m_users, m_currentNick

    std::atomic<IRCStatus> m_status;
    std::atomic<bool>      m_running;

    std::string m_server;
    int         m_port;
    std::string m_nick;
    std::string m_targetNick;
    std::string m_currentNick;
    std::string m_password;
    bool        m_useSSL = false;

    std::vector<std::string> m_users;

    // --- message log ---
    std::mutex               m_msgMutex; // only for m_messages
    std::vector<IRCMessage>  m_messages;
};
