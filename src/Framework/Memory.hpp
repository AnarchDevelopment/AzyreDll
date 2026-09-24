#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace mc::mem {

uintptr_t base();
size_t size();
uintptr_t resolve(uintptr_t rva);

bool isReadable(uintptr_t addr, size_t len);
bool rawRead(void* dst, uintptr_t src, size_t len);
bool rawWrite(uintptr_t dst, const void* src, size_t len);
bool rawWriteImage(uintptr_t dst, const void* src, size_t len);

template <typename T>
bool writeImage(uintptr_t addr, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>, "mem::writeImage requires POD");
    return rawWriteImage(addr, &value, sizeof(T));
}

template <typename T>
T read(uintptr_t addr)
{
    static_assert(std::is_trivially_copyable_v<T>, "mem::read requires POD");
    T value{};
    rawRead(&value, addr, sizeof(T));
    return value;
}

template <typename T>
void write(uintptr_t addr, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>, "mem::write requires POD");
    rawWrite(addr, &value, sizeof(T));
}

std::string readString(uintptr_t addr);

uintptr_t readPtr(uintptr_t addr);

using ValidateFn = std::function<bool(uintptr_t)>;

struct VtableQuery
{
    uintptr_t rva = 0;
    const ValidateFn* validate = nullptr;
    size_t maxCount = 1;
};

size_t scanMulti(const std::vector<VtableQuery>& queries, std::vector<std::vector<uintptr_t>>& out);

uintptr_t findByVtable(uintptr_t vtableRva, const ValidateFn& validate = {});
size_t findAllByVtable(uintptr_t vtableRva, std::vector<uintptr_t>& out, size_t maxCount,
                       const ValidateFn& validate = {});

}
