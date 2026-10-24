#include "Features/ChatSender.hpp"

#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace mc::chatsend {

namespace {

// Layout std::string MSVC x64 (confirmado por off::textPacket:
// Message=+0x38, MessageSize=+0x48, MessageCap=+0x50):
//   +0x00 ptr/local-buf | +0x10 size | +0x18 cap (cap==15 -> SSO)
constexpr uint64_t kSsoCap = 15;

// Strings del TextPacket. SourceName/Message confirmados por Offsets.hpp;
// los tres siguientes son el layout tipico (xuid, platform id, filtered)
// — se manejan con guarda de "parece heap string" para no tocar POD.
constexpr uintptr_t kStrOffs[] = {
    off::textPacket::SourceName,
    off::textPacket::Message,
    0x58,
    0x78,
    0x98,
};
constexpr size_t kStrCount = sizeof(kStrOffs) / sizeof(kStrOffs[0]);
constexpr size_t kMessageIdx = 1;

constexpr size_t kTplSize = 0xC0; // TextPacket completo con margen

// Estado de cada string del template:
//   0 = inline/SSO (bytes propios, nada que liberar)
//   1 = HEAP ALOCADO POR NOSOTROS (liberar al refrescar/free)
//   2 = opaco (ptr ajeno/copiado crudo — NUNCA liberar)
enum StrOwn : unsigned char
{
    kOwnNone = 0,
    kOwnUs = 1,
    kOwnForeign = 2,
};

std::mutex g_mu;
std::vector<unsigned char> g_tpl;
unsigned char g_tplOwn[kStrCount] = {};
bool g_haveTpl = false;

void readStrFields(const unsigned char* pkt, uintptr_t off,
                   uintptr_t& ptr, uint64_t& size, uint64_t& cap)
{
    std::memcpy(&ptr, pkt + off, sizeof(ptr));
    std::memcpy(&size, pkt + off + 0x10, sizeof(size));
    std::memcpy(&cap, pkt + off + 0x18, sizeof(cap));
}

bool looksHeapString(uintptr_t ptr, uint64_t size, uint64_t cap)
{
    return cap > kSsoCap && cap < 0x10000 && size <= cap &&
           ptr > 0x10000 && mem::isReadable(ptr, (size_t)size + 1);
}

void writeStrFields(unsigned char* pkt, uintptr_t off, uintptr_t ptr,
                    uint64_t size, uint64_t cap)
{
    std::memcpy(pkt + off, &ptr, sizeof(ptr));
    std::memcpy(pkt + off + 0x10, &size, sizeof(size));
    std::memcpy(pkt + off + 0x18, &cap, sizeof(cap));
}

// Copia el string `off` del paquete FUENTE (juego o nuestro buffer ya
// copiado) a nuestro buffer dst. Devuelve el ownership resultante.
unsigned char deepCopyStr(unsigned char* dst, uintptr_t off, const unsigned char* srcPkt)
{
    uintptr_t ptr = 0;
    uint64_t size = 0, cap = 0;
    readStrFields(srcPkt, off, ptr, size, cap);

    if (cap <= kSsoCap || !looksHeapString(ptr, size, cap))
    {
        // SSO (bytes inline) o campo no-creible: copia cruda de 0x20.
        std::memmove(dst + off, srcPkt + off, 0x20);
        return cap <= kSsoCap ? kOwnNone : kOwnForeign;
    }

    unsigned char* buf = (unsigned char*)malloc((size_t)size + 1);
    if (!buf)
        return kOwnForeign;
    if (size > 0 && !mem::rawRead(buf, ptr, (size_t)size))
    {
        free(buf);
        std::memcpy(dst + off, srcPkt + off, 0x20);
        return kOwnForeign;
    }
    buf[size] = 0;
    writeStrFields(dst, off, (uintptr_t)buf, size, size > kSsoCap ? size : kSsoCap + 1);
    return kOwnUs;
}

void freeStrIfOurs(unsigned char* pkt, uintptr_t off, unsigned char own)
{
    if (own != kOwnUs)
        return;
    uintptr_t ptr = 0;
    uint64_t size = 0, cap = 0;
    readStrFields(pkt, off, ptr, size, cap);
    if (ptr)
        free((void*)ptr);
}

// Escribe `s` como std::string MSVC en nuestra copia del paquete.
unsigned char setStr(unsigned char* pkt, uintptr_t off, const std::string& s)
{
    size_t len = s.size();
    if (len <= kSsoCap)
    {
        // SSO: los primeros 16 bytes son el buffer local (el "ptr" esta
        // unionado con ellos y no se usa), size, cap=15.
        unsigned char local[16] = {};
        std::memcpy(local, s.data(), len);
        std::memcpy(pkt + off, local, sizeof(local));
        uint64_t sz = (uint64_t)len;
        uint64_t cap = kSsoCap;
        std::memcpy(pkt + off + 0x10, &sz, sizeof(sz));
        std::memcpy(pkt + off + 0x18, &cap, sizeof(cap));
        return kOwnNone;
    }

    unsigned char* buf = (unsigned char*)malloc(len + 1);
    if (!buf)
        return kOwnNone;
    std::memcpy(buf, s.data(), len);
    buf[len] = 0;
    writeStrFields(pkt, off, (uintptr_t)buf, (uint64_t)len, (uint64_t)len);
    return kOwnUs;
}

uint32_t g_rng = 0xC0FFEE42u;

uint32_t nextRand()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

} // namespace

void onOutgoingTextPacket(uintptr_t packet)
{
    if (!packet)
        return;
    if (mem::read<uintptr_t>(packet) != mem::resolve(vt::TextPacket))
        return;

    uint8_t type = mem::read<uint8_t>(packet + off::textPacket::Type);
    if (type != 1) // solo chat (translate/popup/tip no sirven de plantilla)
        return;

    std::string msg = mem::readString(packet + off::textPacket::Message);
    if (msg.empty() || msg[0] == '/')
        return;

    std::vector<unsigned char> tpl(kTplSize);
    unsigned char own[kStrCount] = {};
    if (!mem::rawRead(tpl.data(), packet, kTplSize))
        return;
    for (size_t i = 0; i < kStrCount; ++i)
        own[i] = deepCopyStr(tpl.data(), kStrOffs[i], tpl.data());

    std::lock_guard<std::mutex> lock(g_mu);
    if (g_haveTpl)
    {
        for (size_t i = 0; i < kStrCount; ++i)
            freeStrIfOurs(g_tpl.data(), kStrOffs[i], g_tplOwn[i]);
    }
    g_tpl = std::move(tpl);
    std::memcpy(g_tplOwn, own, sizeof(own));
    g_haveTpl = true;
}

bool haveTemplate()
{
    std::lock_guard<std::mutex> lock(g_mu);
    return g_haveTpl;
}

bool send(const std::string& text)
{
    if (text.empty() || text.size() > 256)
        return false;

    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_haveTpl)
        return false;

    std::vector<unsigned char> pkt = g_tpl;
    unsigned char own[kStrCount];
    std::memcpy(own, g_tplOwn, sizeof(own));

    // Duplicar los strings HEAP de la plantilla: el clon no puede
    // compartir buffers (la plantilla vive tras el send; ademas el
    // Message se pisa con el nuestro). Se copia TODO, Message incluido,
    // para que el clon sea dueno exclusivo de sus buffers.
    for (size_t i = 0; i < kStrCount; ++i)
    {
        if (own[i] != kOwnUs)
            continue;
        own[i] = deepCopyStr(pkt.data(), kStrOffs[i], g_tpl.data());
    }

    // Message nuestro: liberar la copia clonada y pisar por completo.
    {
        uintptr_t oldPtr = 0;
        uint64_t oldSize = 0, oldCap = 0;
        readStrFields(pkt.data(), off::textPacket::Message, oldPtr, oldSize, oldCap);
        if (own[kMessageIdx] == kOwnUs && oldPtr)
            free((void*)oldPtr);
        own[kMessageIdx] = setStr(pkt.data(), off::textPacket::Message, text);
    }

    bool ok = chathook::sendRawPacket(pkt.data());

    for (size_t i = 0; i < kStrCount; ++i)
        freeStrIfOurs(pkt.data(), kStrOffs[i], own[i]);
    return ok;
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

}
