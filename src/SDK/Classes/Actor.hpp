#pragma once

#include "Framework/Memory.hpp"
#include "Framework/Math.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

#include <cmath>
#include <array>
#include <excpt.h>
#include <string>

namespace mc {

class Actor
{
public:
    Actor() = default;
    explicit Actor(uintptr_t a) : addr(a) {}

    bool valid() const
    {
        if (!addr || !mem::isReadable(addr, sizeof(uintptr_t)))
            return false;
        uintptr_t v = mem::read<uintptr_t>(addr);
        if (v != mem::resolve(vt::LocalPlayer) &&
            v != mem::resolve(vt::RemotePlayer) &&
            v != mem::resolve(vt::ServerPlayer))
            return false;
        float x = mem::read<float>(addr + off::player::PosX);
        float y = mem::read<float>(addr + off::player::PosY);
        float z = mem::read<float>(addr + off::player::PosZ);
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    uintptr_t address() const { return addr; }
    uintptr_t vtable() const { return mem::read<uintptr_t>(addr); }

    Vec3 pos() const
    {
        return {
            mem::read<float>(addr + off::player::PosX),
            mem::read<float>(addr + off::player::PosY),
            mem::read<float>(addr + off::player::PosZ),
        };
    }

    // Posicion REAL del actor (mPosition, +0xC): la que escribe Actor::setPos.
    // pos() devuelve la esquina MIN del AABB (+0xB4) — util para ESP pero no
    // para logica de movimiento.
    Vec3 posExact() const
    {
        return {
            mem::read<float>(addr + off::player::PosTrueX),
            mem::read<float>(addr + off::player::PosTrueY),
            mem::read<float>(addr + off::player::PosTrueZ),
        };
    }

    void setPos(const Vec3& v)
    {
        mem::write<float>(addr + off::player::PosX, v.x);
        mem::write<float>(addr + off::player::PosY, v.y);
        mem::write<float>(addr + off::player::PosZ, v.z);
    }

    float pitch() const { return mem::read<float>(addr + off::player::Pitch); }
    void setPitch(float v) { mem::write<float>(addr + off::player::Pitch, v); }

    float yaw() const { return mem::read<float>(addr + off::player::Yaw); }
    void setYaw(float v) { mem::write<float>(addr + off::player::Yaw, v); }

    Vec3 velocity() const
    {
        return {
            mem::read<float>(addr + off::player::VelocityX),
            mem::read<float>(addr + off::player::VelocityY),
            mem::read<float>(addr + off::player::VelocityZ),
        };
    }

    void setVelocity(const Vec3& v)
    {
        mem::write<float>(addr + off::player::VelocityX, v.x);
        mem::write<float>(addr + off::player::VelocityY, v.y);
        mem::write<float>(addr + off::player::VelocityZ, v.z);
    }

    void setVelocityXZ(const Vec3& v)
    {
        mem::write<float>(addr + off::player::VelocityX, v.x);
        mem::write<float>(addr + off::player::VelocityZ, v.z);
    }

    void zeroVelocity()
    {
        setVelocity({});
    }

    std::string name() const
    {
        uintptr_t nameAddr = addr + off::player::Name;
        char raw[64] = {};
        size_t strLen = 0;
        size_t strCap = 0;
        mem::rawRead(&strLen, nameAddr + 16, sizeof(strLen));
        mem::rawRead(&strCap, nameAddr + 24, sizeof(strCap));

        bool ok = false;
        if (strCap < 16 && strLen < 16)
        {
            size_t n = strLen > 0 ? strLen : 15;
            if (n > 63)
                n = 63;
            ok = mem::rawRead(raw, nameAddr, n);
        }
        else
        {
            uintptr_t heapPtr = 0;
            if (mem::rawRead(&heapPtr, nameAddr, sizeof(heapPtr)) && heapPtr > 0x10000)
            {
                size_t n = strLen;
                if (n == 0 || n > 60)
                    n = 60;
                ok = mem::rawRead(raw, heapPtr, n);
            }
        }
        if (!ok)
            mem::rawRead(raw, nameAddr, 15);
        raw[63] = '\0';

        std::string out;
        out.reserve(24);
        for (size_t i = 0; i < 63 && raw[i] != '\0'; ++i)
        {
            unsigned char c = (unsigned char)raw[i];
            if (c == 0xA7)
            {
                if (i + 1 < 63)
                    ++i;
                continue;
            }
            if (c == 0x0D || c == 0x0A)
                break;
            if (c < 32 || c > 126)
                continue;
            out.push_back((char)c);
        }
        return out;
    }

    bool onGround() const { return mem::read<uint8_t>(addr + off::player::OnGround) != 0; }
    void setOnGround(bool v) { mem::write<uint8_t>(addr + off::player::OnGround, v ? 1 : 0); }

    bool isHurt() const { return mem::read<uint8_t>(addr + off::player::IsHurt) != 0; }
    int hurtTime() const { return (int)mem::read<uint8_t>(addr + off::player::HurtTime); }

    uintptr_t levelAddr() const { return mem::read<uintptr_t>(addr + off::player::LevelPtr); }

    bool isLocal() const
    {
        return valid() && vtable() == mem::resolve(vt::LocalPlayer);
    }

    Vec3 eyePos() const
    {
        Vec3 p = pos();
        p.y += off::gameconst::EyeHeight;
        return p;
    }

    Vec3 headPos() const
    {
        Vec3 p = pos();
        p.y += off::gameconst::PlayerHeight;
        return p;
    }

    float height() const
    {
        float h = 0.0f;
        if (mem::rawRead(&h, addr + off::player::Height, sizeof(h)) && h >= 0.1f && h <= 2.5f)
            return h;
        return off::gameconst::PlayerHeight;
    }

    int health() const
    {
        const float hp = currentHealth();
        return std::isfinite(hp) && hp >= 0.0f
            ? static_cast<int>(std::lround(hp))
            : 0;
    }

    float maxHealth() const
    {
        // Actor + 0x220 is not max health. The native hurt path copies the
        // current health attribute into that cache, so using it as the HUD
        // denominator makes the bar stay full as health drops.
        return valid() ? 20.0f : 0.0f;
    }

    float cachedHealth() const
    {
        if (!addr || !mem::isReadable(addr + off::player::Health, sizeof(int)))
            return -1.0f;
        int value = 0;
        if (!mem::rawRead(&value, addr + off::player::Health, sizeof(value)) ||
            value < 0 || value > 40)
            return -1.0f;
        return static_cast<float>(value);
    }

    float currentHealth() const
    {
        return cachedHealth();
    }

    bool skinRgba(std::array<uint8_t, off::skin::PixelBytes>& pixels) const
    {
        if (!valid())
            return false;

        uintptr_t skinInfo = 0;
        uintptr_t skinVtable = 0;
        if (!mem::rawRead(&skinInfo, addr + off::player::SkinInfo, sizeof(skinInfo)) ||
            !skinInfo ||
            !mem::rawRead(&skinVtable, skinInfo, sizeof(skinVtable)))
            return false;

        uintptr_t pixelsBegin = 0;
        uintptr_t pixelsEnd = 0;
        if (skinVtable == mem::resolve(vt::ServerSkinInfoData))
        {
            if (!mem::rawRead(&pixelsBegin, skinInfo + off::skin::ServerPixelsBegin,
                              sizeof(pixelsBegin)) ||
                !mem::rawRead(&pixelsEnd, skinInfo + off::skin::ServerPixelsEnd,
                              sizeof(pixelsEnd)))
                return false;
        }
        else if (skinVtable == mem::resolve(vt::ClientSkinInfoData))
        {
            uintptr_t image = 0;
            if (!mem::rawRead(&image, skinInfo + off::skin::ClientImage, sizeof(image)) ||
                !image ||
                !mem::rawRead(&pixelsBegin, image + off::skin::ImagePixelsBegin,
                              sizeof(pixelsBegin)) ||
                !mem::rawRead(&pixelsEnd, image + off::skin::ImagePixelsEnd,
                              sizeof(pixelsEnd)))
                return false;
        }
        else
            return false;

        if (!pixelsBegin || pixelsEnd < pixelsBegin ||
            pixelsEnd - pixelsBegin != pixels.size() ||
            !mem::isReadable(pixelsBegin, pixels.size()))
            return false;

        return mem::rawRead(pixels.data(), pixelsBegin, pixels.size());
    }

    bool swing() const
    {
        if (!valid())
            return false;

        const uintptr_t fn = mem::resolve(off::fn::ActorSwing);
        if (!fn)
            return false;

        __try
        {
            reinterpret_cast<void(__fastcall*)(void*)>(fn)(
                reinterpret_cast<void*>(addr));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

protected:
    uintptr_t addr = 0;
};

}
