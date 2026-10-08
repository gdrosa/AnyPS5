#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/GuestLocale.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
extern GuestLocale::Implementation* _ZSt21_sceLibcClassicLocale_nid_postfix;
std::size_t APS5_VABI _ZNSt7collateIcE7_GetcatEPPKNSt6locale5facetEPKS1__nid_postfix(GuestLocale::Facet**, const GuestLocale::Implementation* const*);
void APS5_VABI _ZdlPv_nid_postfix(void*);
}

struct Allocation { unsigned char* raw; std::size_t offset, bytes; };
std::map<void*, Allocation> live;
std::mutex heapMutex;
std::atomic<unsigned> failures{0}, cases{0}, checks{0};
thread_local unsigned residue = 0;
thread_local bool failNext = false;
thread_local void* lastBase = nullptr;
thread_local std::size_t lastBytes = 0;

void Check(bool condition) {
    ++checks;
    if (!condition) ++failures;
}

void* APS5_VABI Allocate(std::size_t bytes) {
    if (failNext) { failNext = false; return nullptr; }
    auto* raw = static_cast<unsigned char*>(std::malloc(bytes + 160));
    if (raw == nullptr) throw std::bad_alloc();
    std::memset(raw, 0xa7, bytes + 160);
    const auto offset = 64 + ((residue - reinterpret_cast<std::uintptr_t>(raw)) & 31);
    void* base = raw + offset;
    std::lock_guard lock(heapMutex);
    live.emplace(base, Allocation{raw, offset, bytes});
    lastBase = base;
    lastBytes = bytes;
    return base;
}

void APS5_VABI Free(void* pointer) {
    std::lock_guard lock(heapMutex);
    const auto found = live.find(pointer);
    if (found == live.end()) std::abort();
    const auto value = found->second;
    for (std::size_t i = 0; i < value.offset; ++i) Check(value.raw[i] == 0xa7);
    for (std::size_t i = value.offset + value.bytes; i < value.bytes + 160; ++i) Check(value.raw[i] == 0xa7);
    std::free(value.raw);
    live.erase(found);
}

void* APS5_VABI UnusedCalloc(std::size_t, std::size_t) { std::abort(); }
void* APS5_VABI UnusedRealloc(void*, std::size_t) { std::abort(); }
void* APS5_VABI UnusedAlign(std::size_t, std::size_t) { std::abort(); }
void* APS5_VABI UnusedRealign(void*, std::size_t, std::size_t) { std::abort(); }
int APS5_VABI UnusedPosix(void**, std::size_t, std::size_t) { std::abort(); }

struct Output { std::uint64_t before; GuestLocale::String value; std::uint64_t after; };

void Convert(const GuestLocale::CollateFacet* facet, std::size_t length, bool allocationFailure = false) {
    ++cases;
    std::string text(length, 'x');
    for (std::size_t i = 0; i < length; ++i) text[i] = static_cast<char>(1 + i % 255);
    const auto inputCopy = text;
    const char* input = text.data();
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto page = static_cast<std::size_t>(info.dwPageSize);
    const auto readable = ((length + page - 1) / page + 1) * page;
    auto* pages = static_cast<char*>(VirtualAlloc(nullptr, readable + page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (pages == nullptr) std::abort();
    DWORD old = 0;
    if (!VirtualProtect(pages + readable, page, PAGE_NOACCESS, &old)) std::abort();
    input = pages + readable - length;
    if (length != 0) std::memcpy(const_cast<char*>(input), text.data(), length);
#endif
    Output output;
    std::memset(&output, 0xcd, sizeof(output));
    const auto unchanged = output;
    failNext = allocationFailure;
    const auto& table = *reinterpret_cast<const GuestLocale::CollateVtable*>(facet->base.vtable);
    try {
        auto* result = table.transform(&output.value, facet, input, input + length);
        Check(!allocationFailure);
        Check(result == &output.value);
        Check(output.before == unchanged.before && output.after == unchanged.after);
        Check(result->reserved == unchanged.value.reserved && result->size == length);
        char* data = result->capacity > 15 ? result->pointer : result->buffer;
        Check(std::memcmp(data, text.data(), length) == 0 && data[length] == 0);
        Check(std::memcmp(input, inputCopy.data(), length) == 0);
        if (length > 15) {
            void* allocation = data;
            if (result->capacity + 1 >= 4096) {
                Check(reinterpret_cast<std::uintptr_t>(data) % 32 == 0);
                std::memcpy(&allocation, data - sizeof(allocation), sizeof(allocation));
                Check(allocation == lastBase);
                const auto shift = reinterpret_cast<std::uintptr_t>(data) - reinterpret_cast<std::uintptr_t>(allocation);
                Check(shift >= sizeof(void*) && shift <= 39);
                Check(lastBytes >= shift + result->capacity + 1);
            } else Check(allocation == lastBase);
            _ZdlPv_nid_postfix(allocation);
        }
    } catch (const std::bad_alloc&) {
        Check(allocationFailure);
        Check(std::memcmp(&output, &unchanged, sizeof(output)) == 0);
    } catch (const std::exception& error) {
        if (failures.fetch_add(1) == 0) std::printf("First rejection (%zu bytes): %s\n", length, error.what());
    }
    failNext = false;
#ifdef _WIN32
    if (!VirtualFree(pages, 0, MEM_RELEASE)) std::abort();
#endif
}

int main() {
#ifdef _WIN32
    char path[32768];
    GetModuleFileNameA(GetModuleHandleA("libc.prx"), path, sizeof(path));
    std::printf("LIBC %s\n", path);
#endif
    const std::array<void*, 10> api{reinterpret_cast<void*>(&Allocate), reinterpret_cast<void*>(&Free),
        reinterpret_cast<void*>(&UnusedCalloc), reinterpret_cast<void*>(&UnusedRealloc),
        reinterpret_cast<void*>(&UnusedAlign), reinterpret_cast<void*>(&UnusedRealign), reinterpret_cast<void*>(&UnusedPosix)};
    ApplicationHeapRegister_nid_no_patch(api.data());
    GuestLocale::Facet* facet = nullptr;
    _ZNSt7collateIcE7_GetcatEPPKNSt6locale5facetEPKS1__nid_postfix(&facet, &_ZSt21_sceLibcClassicLocale_nid_postfix);
    const auto* collate = reinterpret_cast<const GuestLocale::CollateFacet*>(facet);
    for (residue = 0; residue < 32; ++residue) {
        for (auto length : {0u, 1u, 15u, 16u, 4094u, 4095u, 4096u, 8191u, 65536u}) Convert(collate, length);
        Convert(collate, 4095, true);
    }
    std::vector<std::thread> threads;
    for (unsigned worker = 0; worker < 4; ++worker) threads.emplace_back([collate, worker] {
        residue = worker * 8;
        for (unsigned i = 0; i < 64; ++i) Convert(collate, 4095 + i * 17);
    });
    for (auto& thread : threads) thread.join();
    facet->vtable->deleteObject(facet);
    Check(live.empty());
    std::printf("Cases: %u; checks: %u; failures: %u\n", cases.load(), checks.load(), failures.load());
    return failures.load() == 0 ? 0 : 1;
}
