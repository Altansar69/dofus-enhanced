#pragma once
// Test-only: include from exactly one source file of a test executable. While
// armed, every heap block released through operator delete is scanned for any
// 6-byte window of the fake keys, so a plaintext copy of the key released
// without being wiped fails the test. std::string keeps every valid key (16
// bytes or more) on the heap, where the scans of the Secret object never look.
// Blocks are zeroed when allocated: a scan sees only what was written during
// the block's last use, never the stale bytes of a copy the test itself freed.
#include <malloc.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>

namespace heapprobe {
constexpr std::string_view Keys[] = {"TEST-KEY-0123456789-NOT-REAL", "TEST-KEY-9876543210-NOT-REAL"};
inline std::atomic_bool armed{false};
inline std::atomic_int hits{0};
inline void* Allocate(size_t size) noexcept {
    void* block = std::malloc(size ? size : 1);
    if (block) std::memset(block, 0, _msize(block));
    return block;
}
inline void Release(void* block) noexcept {
    if (!block) return;
    if (armed.load()) {
        const std::string_view bytes(static_cast<const char*>(block), _msize(block));
        bool found = false;
        for (const auto key : Keys)
            for (size_t i = 0; !found && i + 6 <= key.size(); ++i) found = bytes.find(key.substr(i, 6)) != std::string_view::npos;
        if (found) ++hits;
    }
    std::free(block);
}
inline void Arm() noexcept { hits = 0; armed = true; }
// How many released blocks still held key bytes since Arm.
inline int Disarm() noexcept { armed = false; return hits.load(); }
}
void* operator new(size_t size) {
    if (void* block = heapprobe::Allocate(size)) return block;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept { return heapprobe::Allocate(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return heapprobe::Allocate(size); }
void operator delete(void* block) noexcept { heapprobe::Release(block); }
void operator delete[](void* block) noexcept { heapprobe::Release(block); }
void operator delete(void* block, size_t) noexcept { heapprobe::Release(block); }
void operator delete[](void* block, size_t) noexcept { heapprobe::Release(block); }
void operator delete(void* block, const std::nothrow_t&) noexcept { heapprobe::Release(block); }
void operator delete[](void* block, const std::nothrow_t&) noexcept { heapprobe::Release(block); }
