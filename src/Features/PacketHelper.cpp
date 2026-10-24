#include "Features/PacketHelper.hpp"

#include "Framework/Memory.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

#include <mutex>

namespace mc::blink {

namespace {

std::atomic<bool> g_active{false};

// La ultima posicion que el server vio del player (la del ultimo
// MovePlayerPacket que SI salio).
std::atomic<float> g_sentX{0.0f};
std::atomic<float> g_sentY{0.0f};
std::atomic<float> g_sentZ{0.0f};

// Buffer de paquetes capturados. Cada entrada es una copia PLANA de los
// bytes del paquete (el MovePlayerPacket es POD: vtable + floats/ints —
// sin shared_ptrs ni ownership, el dump lo confirmo). La vtable copiada
// apunta a las funciones del juego: al reenviar, el Write serializa
// normal.
constexpr size_t kPacketBytes = 0x60; // datos hasta +0x34 con margen

std::mutex g_mu;
std::vector<std::vector<unsigned char>> g_buffer;

// Tope de seguridad: ~30 segundos de paquetes a 20pps. Si el usuario se
// olvida el blink puesto, liberamos solos (los servers viejos cortan la
// conexion si no llega nada en ~30-60s).
constexpr size_t kMaxBuffered = 600;

} // namespace

bool onOutgoingPacket(uintptr_t p)
{
    if (!p)
        return false;

    bool isMovePacket =
        mem::read<uintptr_t>(p) == mem::resolve(vt::MovePlayerPacket);
    if (!isMovePacket)
        return false;

    // Posicion del paquete (+0x18 x, +0x1C y, +0x20 z del dump).
    float x = mem::read<float>(p + off::movePlayerPacket::X);
    float y = mem::read<float>(p + off::movePlayerPacket::Y);
    float z = mem::read<float>(p + off::movePlayerPacket::Z);

    if (!g_active.load(std::memory_order_acquire))
    {
        // No blinkeado: el paquete pasa — el server nos ve aqui.
        g_sentX.store(x, std::memory_order_relaxed);
        g_sentY.store(y, std::memory_order_relaxed);
        g_sentZ.store(z, std::memory_order_relaxed);
        return false;
    }

    // Blinkeado: copiar los bytes y NO enviar. El original muere con el
    // juego (el caller lo libera al retornar) — nuestra copia es la que
    // sobrevive.
    std::vector<unsigned char> copy(kPacketBytes);
    if (!mem::rawRead(copy.data(), p, kPacketBytes))
        return false; // lectura fallo: dejar pasar (no romper el envio)

    std::lock_guard<std::mutex> lock(g_mu);
    if (g_buffer.size() < kMaxBuffered)
        g_buffer.push_back(std::move(copy));
    else
    {
        // Buffer lleno (blink demasiado largo): liberar todo YA para no
        // perder el paquete en vuelo ni cortar la conexion.
        std::vector<std::vector<unsigned char>> drain;
        drain.swap(g_buffer);
        // Reenviar fuera del lock.
        for (auto& pkt : drain)
            chathook::sendRawPacket(pkt.data());
        g_buffer.push_back(std::move(copy));
    }
    return true;
}

bool active()
{
    return g_active.load(std::memory_order_acquire);
}

void activate()
{
    std::lock_guard<std::mutex> lock(g_mu);
    g_buffer.clear();
    g_active.store(true, std::memory_order_release);
}

void deactivate()
{
    std::vector<std::vector<unsigned char>> drain;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_active.store(false, std::memory_order_release);
        drain.swap(g_buffer);
    }

    // Rafaga: reenviar todos en orden FIFO. El server procesa el
    // teletransporte completo de una vez.
    for (auto& pkt : drain)
        chathook::sendRawPacket(pkt.data());
}

Vec3 lastSent()
{
    return {
        g_sentX.load(std::memory_order_relaxed),
        g_sentY.load(std::memory_order_relaxed),
        g_sentZ.load(std::memory_order_relaxed),
    };
}

size_t pendingCount()
{
    std::lock_guard<std::mutex> lock(g_mu);
    return g_buffer.size();
}

} // namespace mc::blink
