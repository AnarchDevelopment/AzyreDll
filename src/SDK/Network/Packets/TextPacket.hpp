#pragma once

#include <cstdint>

namespace mc::packets {

// ============================================================================
// TextPacket — layout REA-confirmado en el build 0.15.10.0 por decompile del
// ctor generico sub_1400DBE80 y del ctor de chat sub_1400DBA90:
//
//   +0x00 vtable (0xE4BDC0)      +0x08 int = 2 (id de paquete)
//   +0x0C int = 1                +0x10 BYTE  tipo de texto:
//                                        0 = raw (solo mensaje)
//                                        1 = chat  (source + mensaje)
//                                        2/3 = popup/tip (con parametros)
//                                        4/5 = jukebox/translate
//   +0x18 std::string SOURCE     +0x38 std::string MENSAJE
//   +0x58 std::vector<Param>     (24 bytes: begin/end/cap)
//   total 0x70
//
// El serializer (sub_1400DBB30) escribe el tipo y, segun el switch de +0x10,
// source (+0x18) y/o message (+0x38); para 2/3 recorre el vector de +0x58.
// NO existen xuid/platform-id/filtered en este build: esos campos son de
// protocolos posteriores y leerlos aqui caia fuera del paquete.
// ============================================================================
struct TextPacket
{
    static constexpr uintptr_t VTableRva = 0xE4BDC0;
    static constexpr uintptr_t TypeOffset = 0x08;      // int: id de paquete (2)
    static constexpr uintptr_t FieldOffset = 0x0C;     // int: constante 1
    static constexpr uintptr_t TextTypeOffset = 0x10;  // BYTE: 0 raw / 1 chat / 5 translate
    static constexpr uintptr_t SourceNameOffset = 0x18;
    static constexpr uintptr_t MessageOffset = 0x38;
    static constexpr uintptr_t ParamsOffset = 0x58;    // std::vector<Param> (32 B por item)
    static constexpr uintptr_t PacketSize = 0x70;

    static constexpr uint8_t TextTypeRaw = 0;
    static constexpr uint8_t TextTypeChat = 1;
    static constexpr uint8_t TextTypeTranslate = 5;

    // std::string MSVC x64: el objeto ocupa 0x20 y guarda size en +0x10 y
    // capacity en +0x18 respecto del inicio del string.
    static constexpr uintptr_t StringSizeFieldOffset = 0x10;
    static constexpr uintptr_t StringCapacityFieldOffset = 0x18;
    static constexpr uintptr_t StringStorageBytes = 0x20;
    static constexpr uint64_t StringInlineCapacity = 15;

    static constexpr uintptr_t MessageSizeOffset = MessageOffset + StringSizeFieldOffset;
    static constexpr uintptr_t MessageCapacityOffset = MessageOffset + StringCapacityFieldOffset;
};

} // namespace mc::packets
