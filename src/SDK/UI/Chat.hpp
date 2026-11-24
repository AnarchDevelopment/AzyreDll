#pragma once

#include "Framework/Memory.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"

#include <string>
#include <vector>

namespace mc {

struct ChatMessage
{
    int type = 0;
    std::string sourceName;
    std::string variant;
    std::string text;
};

class Chat
{
public:
    Chat() = default;
    explicit Chat(uintptr_t a) : addr(a) {}

    // El rango cubre las DOS listas del ChatHelper: scrollback (+0x128) y la
    // lista que pinta el HUD (+0x140, termina en +0x150).
    bool valid() const { return addr && mem::isReadable(addr, 0x158); }
    uintptr_t address() const { return addr; }

    std::vector<ChatMessage> messages() const
    {
        std::vector<ChatMessage> out;
        if (!valid())
            return out;

        uintptr_t begin = mem::read<uintptr_t>(addr + off::chain::ChatMessages);
        uintptr_t end = mem::read<uintptr_t>(addr + off::chain::ChatMessages + 8);
        if (!begin || end <= begin)
            return out;

        size_t count = (end - begin) / off::chat::MessageSize;
        if (count > 128)
            count = 128;

        out.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            uintptr_t entry = begin + i * off::chat::MessageSize;
            ChatMessage msg;
            msg.type = mem::read<int>(entry + off::chat::MsgType);
            msg.sourceName = mem::readString(entry + off::chat::MsgSourceName);
            msg.variant = mem::readString(entry + off::chat::MsgVariant);
            msg.text = mem::readString(entry + off::chat::MsgText);
            out.push_back(std::move(msg));
        }
        return out;
    }

private:
    uintptr_t addr = 0;
};

}
