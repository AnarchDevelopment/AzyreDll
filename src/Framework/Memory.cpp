#include "Memory.hpp"

#include <windows.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace mc::mem {

struct ModuleInfo
{
    uintptr_t base = 0;
    size_t size = 0;
};

static const ModuleInfo& moduleInfo()
{
    static const ModuleInfo info = [] {
        ModuleInfo out;
        HMODULE mod = GetModuleHandleW(nullptr);
        out.base = reinterpret_cast<uintptr_t>(mod);
        if (out.base)
        {
            auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(out.base);
            auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(out.base + dos->e_lfanew);
            out.size = nt->OptionalHeader.SizeOfImage;
        }
        return out;
    }();
    return info;
}

uintptr_t base()
{
    return moduleInfo().base;
}

size_t size()
{
    return moduleInfo().size;
}

uintptr_t resolve(uintptr_t rva)
{
    return base() + rva;
}

bool isReadable(uintptr_t addr, size_t len)
{
    if (!addr || !len)
        return false;

    uintptr_t cur = addr;
    uintptr_t end = addr + len;
    while (cur < end)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void*>(cur), &mbi, sizeof(mbi)) == 0)
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            return false;
        DWORD prot = mbi.Protect & 0xFF;
        bool readable = prot == PAGE_READONLY || prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
                        prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
                        prot == PAGE_EXECUTE_WRITECOPY;
        if (!readable)
            return false;
        uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= cur)
            return false;
        cur = regionEnd;
    }
    return true;
}

static bool safeMemcpy(void* dst, const void* src, size_t len)
{
    __try
    {
        std::memcpy(dst, src, len);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool rawRead(void* dst, uintptr_t src, size_t len)
{
    if (!dst || !src)
        return false;
    if (!isReadable(src, len))
        return false;
    return safeMemcpy(dst, reinterpret_cast<void*>(src), len);
}

bool rawWrite(uintptr_t dst, const void* src, size_t len)
{
    if (!dst || !src || !len)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<void*>(dst), &mbi, sizeof(mbi)) == 0)
        return false;
    if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE)
        return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(dst), len, PAGE_READWRITE, &oldProtect))
        return false;
    bool ok = safeMemcpy(reinterpret_cast<void*>(dst), src, len);
    DWORD tmp = 0;
    VirtualProtect(reinterpret_cast<void*>(dst), len, oldProtect, &tmp);
    return ok;
}

bool rawWriteImage(uintptr_t dst, const void* src, size_t len)
{
    if (!dst || !src || !len)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<void*>(dst), &mbi, sizeof(mbi)) == 0)
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Type != MEM_IMAGE && mbi.Type != MEM_PRIVATE)
        return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    DWORD prot = mbi.Protect & 0xFF;
    bool writable = prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE ||
                    prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_WRITECOPY;
    if (writable)
        return safeMemcpy(reinterpret_cast<void*>(dst), src, len);

    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(dst), len, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    bool ok = safeMemcpy(reinterpret_cast<void*>(dst), src, len);
    DWORD tmp = 0;
    VirtualProtect(reinterpret_cast<void*>(dst), len, oldProtect, &tmp);
    return ok;
}

uintptr_t readPtr(uintptr_t addr)
{
    return read<uintptr_t>(addr);
}

std::string readString(uintptr_t addr)
{
    if (!isReadable(addr, 32))
        return {};

    size_t strSize = read<size_t>(addr + 0x10);
    size_t strCap = read<size_t>(addr + 0x18);
    if (strSize > 0x1000 || strSize == 0)
        return {};

    if (strCap < 16)
    {
        char buf[16]{};
        if (!rawRead(buf, addr, strSize))
            return {};
        return std::string(buf, strSize);
    }

    uintptr_t ptr = read<uintptr_t>(addr);
    if (!isReadable(ptr, strSize))
        return {};
    std::string out(strSize, '\0');
    if (!rawRead(out.data(), ptr, strSize))
        return {};
    return out;
}

static bool regionScannable(const MEMORY_BASIC_INFORMATION& mbi)
{
    if (mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Type != MEM_PRIVATE)
        return false;
    if (mbi.Protect & PAGE_GUARD)
        return false;
    if (!(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
        return false;
    if (mbi.RegionSize > (SIZE_T)64 * 1024 * 1024)
        return false;
    return true;
}

static bool allFull(const std::vector<VtableQuery>& queries, const std::vector<std::vector<uintptr_t>>& out)
{
    for (size_t i = 0; i < queries.size(); ++i)
    {
        if (out[i].size() < queries[i].maxCount)
            return false;
    }
    return true;
}

size_t scanMulti(const std::vector<VtableQuery>& queries, std::vector<std::vector<uintptr_t>>& out)
{
    out.assign(queries.size(), {});
    if (queries.empty())
        return 0;
    if (allFull(queries, out))
        return 0;

    std::vector<uintptr_t> targets(queries.size());
    for (size_t i = 0; i < queries.size(); ++i)
        targets[i] = resolve(queries[i].rva);

    constexpr size_t kChunk = 1 << 20;
    std::vector<uint8_t> buffer(kChunk);

    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0x10000;
    const uintptr_t addrEnd = 0x00007FFFFFFE0000ull;

    while (addr < addrEnd)
    {
        if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0)
        {
            addr += 0x1000;
            continue;
        }

        size_t regionSize = mbi.RegionSize ? mbi.RegionSize : 0x1000;
        uintptr_t next = addr + regionSize;
        if (next <= addr)
            next = addr + 0x1000;

        if (regionScannable(mbi))
        {
            uintptr_t start = (reinterpret_cast<uintptr_t>(mbi.BaseAddress) + 7) & ~uintptr_t(7);
            uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + regionSize;

            while (start + sizeof(uintptr_t) <= end && !allFull(queries, out))
            {
                size_t chunkLen = (size_t)(end - start);
                if (chunkLen > kChunk)
                    chunkLen = kChunk;
                chunkLen &= ~size_t(7);
                if (chunkLen < sizeof(uintptr_t))
                    break;

                if (rawRead(buffer.data(), start, chunkLen))
                {
                    const uintptr_t* words = reinterpret_cast<const uintptr_t*>(buffer.data());
                    size_t count = chunkLen / sizeof(uintptr_t);
                    for (size_t i = 0; i < count; ++i)
                    {
                        uintptr_t value = words[i];
                        for (size_t q = 0; q < queries.size(); ++q)
                        {
                            if (out[q].size() >= queries[q].maxCount || value != targets[q])
                                continue;
                            uintptr_t candidate = start + i * sizeof(uintptr_t);
                            if (queries[q].validate && !(*queries[q].validate)(candidate))
                                continue;
                            out[q].push_back(candidate);
                        }
                    }
                }

                start += chunkLen;
            }
        }

        addr = next;
    }

    size_t total = 0;
    for (const auto& r : out)
        total += r.size();
    return total;
}

uintptr_t findByVtable(uintptr_t vtableRva, const ValidateFn& validate)
{
    std::vector<std::vector<uintptr_t>> out;
    VtableQuery query{vtableRva, validate ? &validate : nullptr, 1};
    scanMulti({query}, out);
    return out[0].empty() ? 0 : out[0].front();
}

size_t findAllByVtable(uintptr_t vtableRva, std::vector<uintptr_t>& out, size_t maxCount,
                       const ValidateFn& validate)
{
    std::vector<std::vector<uintptr_t>> results;
    VtableQuery query{vtableRva, validate ? &validate : nullptr, maxCount};
    scanMulti({query}, results);
    out = std::move(results[0]);
    return out.size();
}

}
