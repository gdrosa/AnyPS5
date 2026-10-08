#include "SceTypes.hpp"
#include "prx/libc/include/FileStream.hpp"
#include "prx/libc/include/ApplicationHeap.hpp"
#include <algorithm>
#include <array>
#include <clocale>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

extern "C" {
int APS5_VABI snprintf_nid_postfix(char*, std::size_t, const char*, ...);
int APS5_VABI sprintf_nid_postfix(char*, const char*, ...);
int APS5_VABI sprintf_s_nid_postfix(char*, std::size_t, const char*, ...);
int APS5_VABI snprintf_s_nid_postfix(char*, std::size_t, const char*, ...);
int APS5_VABI vsnprintf_nid_postfix(char*, std::size_t, const char*, VaList*);
int APS5_VABI vsprintf_nid_postfix(char*, const char*, VaList*);
int APS5_VABI vsprintf_s_nid_postfix(char*, std::size_t, const char*, VaList*);
int APS5_VABI vasprintf_nid_postfix(char**, const char*, VaList*);
int APS5_VABI fprintf_nid_postfix(FileStream*, const char*, ...);
int APS5_VABI vfprintf_nid_postfix(FileStream*, const char*, VaList*);
int APS5_VABI printf_nid_postfix(const char*, ...);
int APS5_VABI libc_printf_nid_postfix(const char*, ...);
int APS5_VABI printf_s_nid_postfix(const char*, ...);
int APS5_VABI vprintf_nid_postfix(const char*, VaList*);
void APS5_VABI free_nid_postfix(void*);
int* APS5_VABI __error_nid_postfix();
}

std::atomic<unsigned> checks{}, failures{};
thread_local bool failNext = false;
void* APS5_VABI Allocate(std::size_t bytes) {
    if (failNext) { failNext = false; return nullptr; }
    return std::malloc(bytes);
}
void APS5_VABI Free(void* pointer) { std::free(pointer); }
void* APS5_VABI UnusedCalloc(std::size_t, std::size_t) { std::abort(); }
void* APS5_VABI UnusedRealloc(void*, std::size_t) { std::abort(); }
void* APS5_VABI UnusedAlign(std::size_t, std::size_t) { std::abort(); }
void* APS5_VABI UnusedRealign(void*, std::size_t, std::size_t) { std::abort(); }
int APS5_VABI UnusedPosix(void**, std::size_t, std::size_t) { std::abort(); }
void Check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL %s\n", name); }
}

using BufferCall = int (APS5_VABI*)(char*, std::size_t, const char*, VaList*);
int APS5_VABI BufferList(BufferCall call, char* out, std::size_t size, const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    VaList list;
    std::memcpy(&list, args, sizeof(list));
    const auto original = list;
    const int result = call(out, size, format, &list);
    Check(std::memcmp(&list, &original, sizeof(list)) == 0, "buffer va_list preserved");
    __builtin_sysv_va_end(args);
    return result;
}
int APS5_VABI StringList(char* out, const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    VaList list;
    std::memcpy(&list, args, sizeof(list));
    const auto original = list;
    const int result = vsprintf_nid_postfix(out, format, &list);
    Check(std::memcmp(&list, &original, sizeof(list)) == 0, "string va_list preserved");
    __builtin_sysv_va_end(args);
    return result;
}
int APS5_VABI AllocatedList(char** out, const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    VaList list;
    std::memcpy(&list, args, sizeof(list));
    const auto original = list;
    const int result = vasprintf_nid_postfix(out, format, &list);
    Check(std::memcmp(&list, &original, sizeof(list)) == 0, "allocated va_list preserved");
    __builtin_sysv_va_end(args);
    return result;
}
int APS5_VABI FileList(FileStream* file, const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    VaList list;
    std::memcpy(&list, args, sizeof(list));
    const auto original = list;
    const int result = vfprintf_nid_postfix(file, format, &list);
    Check(std::memcmp(&list, &original, sizeof(list)) == 0, "file va_list preserved");
    __builtin_sysv_va_end(args);
    return result;
}
int APS5_VABI PrintList(const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    VaList list;
    std::memcpy(&list, args, sizeof(list));
    const auto original = list;
    const int result = vprintf_nid_postfix(format, &list);
    Check(std::memcmp(&list, &original, sizeof(list)) == 0, "print va_list preserved");
    __builtin_sysv_va_end(args);
    return result;
}

void Buffers() {
    const char16_t* wide = u"A\u00e9\u20ac\U0001f600Z";
    const std::string bytes = "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80Z";
    const std::size_t boundaries[] = {0, 1, 3, 6, 10, 11};
    for (int precision = -1; precision <= 12; ++precision) {
        std::size_t length = bytes.size();
        if (precision >= 0) {
            length = 0;
            for (const auto boundary : boundaries) if (boundary <= static_cast<std::size_t>(precision)) length = boundary;
        }
        const auto expected = bytes.substr(0, length);
        for (std::size_t size = 0; size <= 16; ++size) {
            for (const auto call : {vsnprintf_nid_postfix, vsprintf_s_nid_postfix}) {
                std::array<char, 32> storage;
                storage.fill('!');
                int count = -1;
                const int result = BufferList(call, size ? storage.data() + 3 : nullptr, size, "%.*ls%n", precision, wide, &count);
                Check(result == static_cast<int>(length) && count == result, "length and %n after truncation");
                const auto copied = size ? std::min(size - 1, length) : 0;
                Check(std::memcmp(storage.data() + 3, expected.data(), copied) == 0, "wide contents");
                if (size) Check(storage[3 + copied] == 0, "output terminator");
                Check(std::all_of(storage.begin(), storage.begin() + 3, [](char ch) { return ch == '!'; }), "prefix guard");
                Check(std::all_of(storage.begin() + 3 + size, storage.end(), [](char ch) { return ch == '!'; }), "suffix guard");
            }
        }
    }
    char output[64];
    const char* mixed = "%S:%lc:%C:%d";
    const std::string expected = "\xc3\xa9:\xe2\x82\xac:A:7";
    Check(sprintf_nid_postfix(output, mixed, u"\u00e9", 0x20ac, 65, 7) == 10 && output == expected, "sprintf");
    Check(sprintf_s_nid_postfix(output, sizeof(output), mixed, u"\u00e9", 0x20ac, 65, 7) == 10 && output == expected, "sprintf_s");
    Check(snprintf_s_nid_postfix(output, sizeof(output), mixed, u"\u00e9", 0x20ac, 65, 7) == 10 && output == expected, "snprintf_s");
    Check(StringList(output, mixed, u"\u00e9", 0x20ac, 65, 7) == 10 && output == expected, "vsprintf");
    Check(snprintf_nid_postfix(output, sizeof(output), "a%lcb%C", 0, 0) == 4 && std::memcmp(output, "a\0b\0", 5) == 0, "wide zero characters");
    Check(snprintf_nid_postfix(output, sizeof(output), "[%4lc]", 0) == 6 && std::memcmp(output, "[   \0]", 7) == 0, "padded wide zero");
    Check(snprintf_nid_postfix(output, sizeof(output), "[%ls]", static_cast<const char16_t*>(nullptr)) == 8 && std::strcmp(output, "[(null)]") == 0, "null wide string");
    const std::u16string large(65536, u'\u00e9');
    Check(snprintf_nid_postfix(nullptr, 0, "%ls", large.c_str()) == 131072, "large length query");
    char* allocated = nullptr;
    Check(AllocatedList(&allocated, "%ls:%d", wide, 7) == 13 && std::string(allocated) == bytes + ":7", "vasprintf");
    free_nid_postfix(allocated);
    allocated = reinterpret_cast<char*>(1);
    failNext = true;
    Check(AllocatedList(&allocated, "%ls", wide) == -1 && allocated == nullptr && *__error_nid_postfix() == 12, "vasprintf allocation failure");
    std::string registerExpected = "\xc3\xa9 1 2 3 4 5 6 7 1.0 2.0 3.0 4.0 5.0 6.0 7.0 8.0 9.0 10.0 1.125";
#ifdef _WIN32
    char decimal[16];
    Check(snprintf_nid_postfix(decimal, sizeof(decimal), "%.1f", 1.) == 3 &&
        (std::strcmp(decimal, "1.0") == 0 || std::strcmp(decimal, "1,0") == 0), "native numeric locale");
    std::replace(registerExpected.begin(), registerExpected.end(), '.', decimal[1]);
#endif
    const int mixedCount = BufferList(vsnprintf_nid_postfix, output, sizeof(output), "%ls %d %d %d %d %d %d %d %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.3Lf",
        u"\u00e9", 1, 2, 3, 4, 5, 6, 7, 1., 2., 3., 4., 5., 6., 7., 8., 9., 10., 1.125L);
    if (mixedCount != static_cast<int>(registerExpected.size()) || output != registerExpected)
        std::fprintf(stderr, "mixed count=%d expected=%zu text=[%s]\n", mixedCount, registerExpected.size(), output);
    Check(mixedCount == static_cast<int>(registerExpected.size()) && output == registerExpected, "mixed register and stack arguments");
}

void GuardedInput() {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const auto page = static_cast<std::size_t>(info.dwPageSize);
    auto* memory = static_cast<char*>(VirtualAlloc(nullptr, page * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    DWORD old;
    if (!memory || !VirtualProtect(memory + page, page, PAGE_NOACCESS, &old)) { Check(false, "guard allocation"); return; }
#else
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    auto* memory = static_cast<char*>(mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (memory == MAP_FAILED || mprotect(memory + page, page, PROT_NONE)) { Check(false, "guard allocation"); return; }
#endif
    char output[16];
    auto* one = reinterpret_cast<char16_t*>(memory + page - sizeof(char16_t));
    *one = u'A';
    Check(snprintf_nid_postfix(output, sizeof(output), "%.1ls", one) == 1 && std::strcmp(output, "A") == 0, "bounded ASCII without terminator");
    *one = u'\u00e9';
    Check(snprintf_nid_postfix(output, sizeof(output), "%.2ls", one) == 2 && std::strcmp(output, "\xc3\xa9") == 0, "bounded BMP without terminator");
    Check(snprintf_nid_postfix(output, sizeof(output), "%.1ls", one) == 0 && output[0] == 0, "precision below BMP byte length");
    auto* pair = one - 1;
    pair[0] = 0xd83d;
    pair[1] = 0xde00;
    Check(snprintf_nid_postfix(output, sizeof(output), "%.4ls", pair) == 4 && std::strcmp(output, "\xf0\x9f\x98\x80") == 0, "bounded surrogate pair");
    Check(snprintf_nid_postfix(output, sizeof(output), "%.0ls", reinterpret_cast<char16_t*>(memory + page)) == 0 && output[0] == 0, "zero precision does not access source");
#ifdef _WIN32
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, page * 2);
#endif
}

void Streams() {
    const std::string expected = "\xc3\xa9";
    auto* native = std::tmpfile();
    if (!native) { Check(false, "temporary file"); return; }
    FileStream file(native);
    Check(fprintf_nid_postfix(&file, "%ls", u"\u00e9") == 2, "fprintf return");
    Check(FileList(&file, "%S", u"\u00e9") == 2, "vfprintf return");
    std::rewind(native);
    char contents[32]{};
    Check(std::fread(contents, 1, sizeof(contents), native) == 4 && std::string(contents, 4) == expected + expected, "file bytes");
    std::fclose(native);
    native = std::tmpfile();
    if (!native) { Check(false, "stdout capture"); return; }
    std::fflush(stdout);
#ifdef _WIN32
    const int descriptor = _fileno(stdout), saved = _dup(descriptor);
    Check(saved >= 0 && _dup2(_fileno(native), descriptor) == 0, "redirect stdout");
    const int previousMode = _setmode(descriptor, _O_BINARY);
#else
    const int descriptor = fileno(stdout), saved = dup(descriptor);
    Check(saved >= 0 && dup2(fileno(native), descriptor) >= 0, "redirect stdout");
#endif
    Check(printf_nid_postfix("%ls", u"\u00e9") == 2, "printf return");
    Check(libc_printf_nid_postfix("%S", u"\u00e9") == 2, "libc_printf return");
    Check(printf_s_nid_postfix("%ls", u"\u00e9") == 2, "printf_s return");
    Check(PrintList("%S", u"\u00e9") == 2, "vprintf return");
    std::fflush(stdout);
#ifdef _WIN32
    _setmode(descriptor, previousMode);
    _dup2(saved, descriptor);
    _close(saved);
#else
    dup2(saved, descriptor);
    close(saved);
#endif
    std::rewind(native);
    Check(std::fread(contents, 1, sizeof(contents), native) == 8 && std::string(contents, 8) == expected + expected + expected + expected, "stdout bytes");
    std::fclose(native);
}

int main() {
    const std::array<void*, 10> api{reinterpret_cast<void*>(&Allocate), reinterpret_cast<void*>(&Free),
        reinterpret_cast<void*>(&UnusedCalloc), reinterpret_cast<void*>(&UnusedRealloc), reinterpret_cast<void*>(&UnusedAlign),
        reinterpret_cast<void*>(&UnusedRealign), reinterpret_cast<void*>(&UnusedPosix)};
    ApplicationHeapRegister_nid_no_patch(api.data());
#ifdef _WIN32
    const auto module = GetModuleHandleA("libc.prx");
    char path[MAX_PATH];
    if (!module || !GetProcAddress(module, "snprintf_nid_postfix") || !GetModuleFileNameA(module, path, MAX_PATH)) return 2;
    std::printf("LIBC %s\n", path);
    const char* locales[] = {"C", ".UTF8"};
#else
    Dl_info module{};
    if (!dladdr(reinterpret_cast<void*>(&snprintf_nid_postfix), &module)) return 2;
    std::printf("LIBC %s\n", module.dli_fname);
    const char* locales[] = {"C", "C.UTF-8"};
#endif
    for (const auto locale : locales) {
        if (!std::setlocale(LC_ALL, locale)) return 2;
        std::printf("HOST_LOCALE %s\n", locale);
        Buffers();
        GuardedInput();
        Streams();
    }
    std::vector<std::thread> workers;
    for (int thread = 0; thread < 4; ++thread) workers.emplace_back([] {
        for (int iteration = 0; iteration < 64; ++iteration) {
            char buffer[32];
            Check(snprintf_nid_postfix(buffer, sizeof(buffer), "%ls:%d", u"\u00e9", 7) == 4 && std::strcmp(buffer, "\xc3\xa9:7") == 0, "concurrent formatting");
        }
    });
    for (auto& worker : workers) worker.join();
    std::printf("Checks: %u; failures: %u\n", checks.load(), failures.load());
    return failures != 0;
}
