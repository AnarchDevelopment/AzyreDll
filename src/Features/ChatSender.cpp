#include "Features/ChatSender.hpp"

#include "SDK/Network/Packets/Packets.hpp"

#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Network/Packets/PacketSender.hpp"
#include "SDK/UI/Hooks/ChatHook.hpp"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// ============================================================================
// Envio de chat — ruta NATIVA (REA, build 0.15.10.0).
//
// El juego envia el chat en sub_1400DE4A0 (metodo del ChatScreen) con:
//   1. sub_1400DBA90(pkt, &source, &mensaje) — arma TextPacket tipo 1
//      (source = "*(*(client+0x290)+0xE10)", mensaje = ChatScreen+0x118).
//   2. sender = *(*(client+0xE8)+0x98); sender->vtable[2](sender, pkt)
//      (la misma cadena que PacketSender::sendGameplay).
//   3. sub_1400DE9D0(...) agrega la linea local al log de chat.
//
// Se arma el paquete a mano (no se clona una "plantilla" capturada: el layout
// anterior leia xuid/platform-id que NO existen en este build y quedaba fuera
// del paquete). El source name se toma del ultimo TextPacket real del juego
// (fuente autoritativa) o del nombre del jugador local.
// ============================================================================

namespace mc::chatsend {

namespace {

constexpr uint64_t kSsoCap = packets::TextPacket::StringInlineCapacity;
constexpr size_t kPacketSize = packets::TextPacket::PacketSize;

// Client -> string con el nombre de chat del jugador (REA: sub_1400DE4A0
// lee *(*(client+656)+3600) y lo pasa como source del TextPacket).
constexpr uintptr_t kClientChatNameOwner = 0x290;
constexpr uintptr_t kChatNameOffset = 0xE10;

std::mutex g_mu;
std::string g_sourceName;   // capturado del ultimo chat real del juego

uint32_t g_rng = 0xC0FFEE42u;

uint32_t nextRand()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

void writeU64(unsigned char* dst, uint64_t v)
{
    std::memcpy(dst, &v, sizeof(v));
}

void writeU32(unsigned char* dst, uint32_t v)
{
    std::memcpy(dst, &v, sizeof(v));
}

// Escribe `s` como std::string MSVC x64 (ptr/local-buf, size, cap).
// Devuelve el buffer a liberar (nullptr si quedo en SSO).
unsigned char* writeStringField(unsigned char* pkt, uintptr_t off, const std::string& s)
{
    const size_t len = s.size();
    if (len <= kSsoCap)
    {
        std::memset(pkt + off, 0, packets::TextPacket::StringStorageBytes);
        std::memcpy(pkt + off, s.data(), len);
        writeU64(pkt + off + packets::TextPacket::StringSizeFieldOffset, (uint64_t)len);
        writeU64(pkt + off + packets::TextPacket::StringCapacityFieldOffset, kSsoCap);
        return nullptr;
    }

    unsigned char* buf = (unsigned char*)malloc(len + 1);
    if (!buf)
    {
        std::memset(pkt + off, 0, packets::TextPacket::StringStorageBytes);
        writeU64(pkt + off + packets::TextPacket::StringCapacityFieldOffset, kSsoCap);
        return nullptr;
    }
    std::memcpy(buf, s.data(), len);
    buf[len] = 0;
    writeU64(pkt + off, (uint64_t)(uintptr_t)buf);
    writeU64(pkt + off + packets::TextPacket::StringSizeFieldOffset, (uint64_t)len);
    writeU64(pkt + off + packets::TextPacket::StringCapacityFieldOffset, (uint64_t)len);
    return buf;
}

bool plausibleName(const std::string& s)
{
    if (s.empty() || s.size() > 40)
        return false;
    for (unsigned char c : s)
    {
        if (c < 0x20 || c == 0x7F)
            return false;
    }
    return true;
}

std::string readChatNameOwner(uintptr_t client)
{
    if (!client || !mem::isReadable(client + kClientChatNameOwner, sizeof(uintptr_t)))
        return {};
    const uintptr_t owner = mem::read<uintptr_t>(client + kClientChatNameOwner);
    if (!owner || !mem::isReadable(owner + kChatNameOffset,
                                   packets::TextPacket::StringStorageBytes))
        return {};

    const std::string name = mem::readString(owner + kChatNameOffset);
    return plausibleName(name) ? name : std::string();
}

// Nombre que el juego pone como source del chat. Prioridad:
//   1. el del ultimo TextPacket real del juego (exacto, con prefijos de team),
//   2. el de la cadena que usa sub_1400DE4A0 (client+0x290 -> +0xE10),
//   3. el nombre del actor local.
std::string chatSourceName()
{
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (plausibleName(g_sourceName))
            return g_sourceName;
    }

    const uintptr_t client = Game::get().clientAddr();
    std::string name = readChatNameOwner(client);
    if (!name.empty())
        return name;

    LocalPlayer local = Game::get().localPlayer();
    if (local.valid())
        name = local.name();
    return plausibleName(name) ? name : std::string();
}

// Arma el TextPacket de chat (tipo 1: source + mensaje) en `pkt` (0x70 bytes
// ya zeroeados). Devuelve los buffers heap que hay que liberar tras el envio.
void buildChatPacket(unsigned char* pkt, const std::string& source,
                     const std::string& message, unsigned char*& freeSrc,
                     unsigned char*& freeMsg)
{
    const uintptr_t vt = mem::resolve(packets::TextPacket::VTableRva);
    writeU64(pkt, (uint64_t)vt);
    writeU32(pkt + packets::TextPacket::TypeOffset, 2);
    writeU32(pkt + packets::TextPacket::FieldOffset, 1);
    pkt[packets::TextPacket::TextTypeOffset] = packets::TextPacket::TextTypeChat;

    freeSrc = writeStringField(pkt, packets::TextPacket::SourceNameOffset, source);
    freeMsg = writeStringField(pkt, packets::TextPacket::MessageOffset, message);

    // +0x58 = vector<Param> vacio (begin/end/cap ya en cero).
}

} // namespace

void onOutgoingTextPacket(uintptr_t packet)
{
    if (!packet)
        return;
    if (mem::read<uintptr_t>(packet) != mem::resolve(packets::TextPacket::VTableRva))
        return;

    const uint8_t textType =
        mem::read<uint8_t>(packet + packets::TextPacket::TextTypeOffset);
    if (textType != packets::TextPacket::TextTypeChat)
        return;

    const std::string msg =
        mem::readString(packet + packets::TextPacket::MessageOffset);
    if (msg.empty() || msg[0] == '/')
        return;

    const std::string source =
        mem::readString(packet + packets::TextPacket::SourceNameOffset);
    if (!plausibleName(source))
        return;

    std::lock_guard<std::mutex> lock(g_mu);
    g_sourceName = source;
}

bool haveTemplate()
{
    // Ya no hay "plantilla": la ruta nativa existe en cuanto hay cliente.
    return Game::get().clientAddr() != 0;
}

bool send(const std::string& text)
{
    if (text.empty() || text.size() > 256)
        return false;

    const uintptr_t client = Game::get().clientAddr();
    if (!client)
        return false;
    if (!mem::resolve(packets::TextPacket::VTableRva))
        return false;

    const std::string source = chatSourceName();

    alignas(16) unsigned char pkt[kPacketSize] = {};
    unsigned char* freeSrc = nullptr;
    unsigned char* freeMsg = nullptr;
    buildChatPacket(pkt, source, text, freeSrc, freeMsg);

    // Ruta nativa: la misma que usa sub_1400DE4A0 (PacketSender::send).
    bool ok = packets::PacketSender::sendGameplay(pkt);
    if (!ok)
    {
        // Sesiones donde el sender no es PacketSender/Batched: sendToServer es
        // la via que el propio chat del juego termina usando (confirmada por
        // el trace de los TextPacket salientes).
        ok = chathook::sendRawPacket(pkt);
    }

    if (freeSrc)
        free(freeSrc);
    if (freeMsg)
        free(freeMsg);
    return ok;
}

bool sendCommand(const std::string& command)
{
    // This protocol build has no separate command packet in the SDK: the
    // native chat path carries slash commands in a chat TextPacket. Keep the
    // explicit entry point so callers cannot accidentally obfuscate one.
    if (command.size() < 2 || command[0] != '/')
        return false;
    return send(command);
}

std::string obfuscate(const std::string& text, int mode)
{
    if (mode <= 0 || text.empty())
        return text;

    std::string out = text;

    // 3 = Full: case-flip aleatorio en letras ASCII.
    if (mode == 3)
    {
        for (char& c : out)
        {
            if (c >= 'A' && c <= 'Z' && (nextRand() & 3) == 0)
                c = (char)(c - 'A' + 'a');
            else if (c >= 'a' && c <= 'z' && (nextRand() & 3) == 0)
                c = (char)(c - 'a' + 'A');
        }
    }

    // 1/3: zero-width spaces (U+200B = E2 80 8B) en bordes de char UTF-8.
    if (mode == 1 || mode == 3)
    {
        int inserts = 1 + (int)(nextRand() % 3);
        for (int k = 0; k < inserts; ++k)
        {
            std::vector<size_t> bounds;
            for (size_t i = 1; i < out.size(); ++i) // sin tocar el primer byte
            {
                if (((unsigned char)out[i] & 0xC0) != 0x80)
                    bounds.push_back(i);
            }
            if (bounds.empty())
                break;
            size_t pos = bounds[nextRand() % (uint32_t)bounds.size()];
            out.insert(pos, "\xE2\x80\x8B");
        }
    }

    // 2/3: tag corto aleatorio al final (cambia el hash del mensaje).
    if (mode == 2 || mode == 3)
    {
        static const char* kHex = "0123456789abcdef";
        char tag[5];
        int n = 2 + (int)(nextRand() % 3); // 2..4 chars
        for (int i = 0; i < n; ++i)
            tag[i] = kHex[nextRand() % 16];
        tag[n] = '\0';
        out.push_back('_');
        out += tag;
    }

    if (out.size() > 256)
        out.resize(256);
    return out;
}

} // namespace mc::chatsend
