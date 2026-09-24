#include "CodePatch.hpp"

#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"

#include <cstring>

namespace mc::patch {

static bool writeExec(uintptr_t addr, const void* data, size_t n)
{
    if (!addr || !data || !n)
        return false;

    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), n, PAGE_EXECUTE_READWRITE, &old))
        return false;

    bool ok = false;
    __try
    {
        memcpy(reinterpret_cast<void*>(addr), data, n);
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }

    DWORD tmp = 0;
    VirtualProtect(reinterpret_cast<void*>(addr), n, old, &tmp);
    return ok;
}

static bool readImage(uintptr_t addr, void* dst, size_t n)
{
    if (!addr || !dst || !n)
        return false;
    if (!mem::isReadable(addr, n))
        return false;

    bool ok = false;
    __try
    {
        memcpy(dst, reinterpret_cast<void*>(addr), n);
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    return ok;
}

static void* allocNear(uintptr_t target, size_t size)
{
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity;
    if (!gran)
        gran = 0x10000;

    uintptr_t start = target & ~(uintptr_t)(gran - 1);
    uintptr_t minAddr = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t maxAddr = (uintptr_t)si.lpMaximumApplicationAddress;

    for (uintptr_t d = 0; d < 0x40000000ull; d += gran)
    {
        for (int i = 0; i < 2; ++i)
        {
            if (d == 0 && i == 1)
                continue;
            uintptr_t cand = (i == 0) ? (start + d) : (start - d);
            if (cand < minAddr || cand + size > maxAddr)
                continue;
            void* p = VirtualAlloc(reinterpret_cast<void*>(cand), size,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p)
                return p;
        }
    }
    return nullptr;
}

uintptr_t patternScanImage(const unsigned char* pattern, size_t len)
{
    if (!pattern || !len)
        return 0;

    uintptr_t base = mem::base();
    size_t imgSize = mem::size();
    if (!base || imgSize <= len)
        return 0;

    uintptr_t imageEnd = base + imgSize;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = base;

    while (addr < imageEnd)
    {
        if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0)
        {
            addr += 0x1000;
            continue;
        }

        uintptr_t regionStart = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (regionStart < base)
            regionStart = base;
        uintptr_t regionEnd = regionStart + mbi.RegionSize;
        if (regionEnd > imageEnd)
            regionEnd = imageEnd;

        bool readable = mbi.State == MEM_COMMIT &&
                        !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                        mem::isReadable(regionStart, 1);

        if (readable && regionEnd > regionStart + len)
        {
            __try
            {
                const unsigned char* haystack = reinterpret_cast<const unsigned char*>(regionStart);
                size_t span = regionEnd - regionStart - len + 1;
                for (size_t i = 0; i < span; ++i)
                {
                    if (haystack[i] == pattern[0] &&
                        memcmp(haystack + i, pattern, len) == 0)
                    {
                        return regionStart + i;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        uintptr_t next = regionStart + mbi.RegionSize;
        if (next <= addr)
        {
            addr += 0x1000;
            continue;
        }
        addr = next;
    }
    return 0;
}

bool CodePatch::init(uintptr_t address, size_t overwriteLength)
{
    if (addr)
        return true;
    if (!address || overwriteLength == 0 || overwriteLength > sizeof(backup))
        return false;
    if (!readImage(address, backup, overwriteLength))
        return false;

    addr = address;
    overwriteLen = overwriteLength;
    return true;
}

bool CodePatch::allocCave(size_t size)
{
    if (cave)
        return true;
    if (!addr)
        return false;
    cave = allocNear(addr, size);
    if (!cave)
    {
        MC_LOG_ERROR("[Patch] allocNear failed for 0x%p", (void*)addr);
        return false;
    }
    return true;
}

bool CodePatch::writeCave(size_t offset, const void* data, size_t len)
{
    if (!cave || !data || !len)
        return false;

    bool ok = false;
    __try
    {
        memcpy(reinterpret_cast<unsigned char*>(cave) + offset, data, len);
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    return ok;
}

bool CodePatch::enableJmpToCave()
{
    if (!addr || !cave || overwriteLen < 5)
        return false;
    if (enabled)
        return true;

    unsigned char buf[16] = {};
    buf[0] = 0xE9;
    int32_t rel = (int32_t)((uintptr_t)cave - (addr + 5));
    memcpy(buf + 1, &rel, 4);
    for (size_t i = 5; i < overwriteLen; ++i)
        buf[i] = 0x90;

    if (!writeExec(addr, buf, overwriteLen))
        return false;
    enabled = true;
    return true;
}

bool CodePatch::enableInline(const unsigned char* replacement, size_t len)
{
    if (!addr || !replacement || len == 0 || len > overwriteLen)
        return false;
    if (enabled)
        return true;

    unsigned char buf[16];
    memcpy(buf, replacement, len);
    for (size_t i = len; i < overwriteLen; ++i)
        buf[i] = 0x90;

    if (!writeExec(addr, buf, overwriteLen))
        return false;
    enabled = true;
    return true;
}

bool CodePatch::disable()
{
    if (!addr || !enabled)
        return false;
    if (!writeExec(addr, backup, overwriteLen))
        return false;
    enabled = false;
    return true;
}

void CodePatch::freeCave()
{
    if (cave)
    {
        VirtualFree(cave, 0, MEM_RELEASE);
        cave = nullptr;
    }
}

}
