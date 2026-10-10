#pragma once
#include "native_ui_invoke_probe.h"
#include "native_ui_guard.h"
#include <climits>
#ifdef _WIN32
namespace native_ui_echo {
inline constexpr char request[] = "cr.echo.request.v1";
inline constexpr char response[] = "cr.echo.response.v1";
inline constexpr char confirmation[] = "cr.echo.confirm.v1";
inline std::atomic<bool> enabled{false};
inline std::atomic<unsigned> stage{0}, failures{0};
inline std::atomic<std::uint64_t> confirmed_generation{0};
inline std::atomic<DWORD> confirmed_thread{0};
struct Pin { std::uintptr_t rva; unsigned char bytes[32]; };
inline constexpr Pin pins[] = {
{0x5edc90,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x55,0x48,0x8b,0xec,0x48,0x83,0xec,0x70,0x48,0x8b,0x81,0x80,0x01,0x00,0x00,0x8b,0xfa,0x48,0x89,0x45,0xc0,0x48}},
{0x5f15c8,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x55,0x48,0x8b,0xec,0x48,0x83,0xec,0x50,0x33,0xff,0x48,0x8b,0xf1,0x40,0x38,0x79,0x49}},
{0x5f1770,{0x48,0x83,0xec,0x28,0x80,0x79,0x48,0x00,0x74,0x0f,0x80,0x79,0x49,0x00,0x75,0x09,0x48,0x83,0xc1,0xf0,0xe8,0xff,0x1d,0xb1,0xff,0x48,0x83,0xc4,0x28,0xc3,0xcc,0xcc}},
{0x5f1790,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x7c,0x24,0x10,0x4c,0x89,0x74,0x24,0x18,0x55,0x48,0x8b,0xec,0x48,0x83,0xec,0x50,0x80,0x79,0x49,0x00,0x4c,0x8b,0xf1,0x0f,0x84}},
{0x5ede50,{0x40,0x53,0x48,0x83,0xec,0x50,0x48,0x8b,0x81,0x60,0x01,0x00,0x00,0x48,0x8b,0xd9,0x48,0x39,0x81,0x80,0x01,0x00,0x00,0x75,0x2f,0x48,0x8b,0x0d,0x20,0xab,0x0f,0x00}},
{0x30669c,{0x83,0x79,0x18,0x00,0x0f,0x95,0xc0,0xc3,0x40,0x53,0x48,0x83,0xec,0x20,0xc7,0x44,0x24,0x30,0x0e,0x00,0x00,0x00,0xba,0x05,0x00,0x00,0x00,0xc6,0x44,0x24,0x34,0x01}},
{0x5ef198,{0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x4c,0x89,0x64,0x24,0x20,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x40,0x48,0x8b,0x81,0x40,0x01}},
{0x5ebf3c,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x7c,0x24,0x18,0x55,0x48,0x8b,0xec,0x48,0x83,0xec,0x60,0x48,0x8b,0xda,0x48,0x8b,0xf9,0x48,0x85,0xd2,0x0f,0x85,0xca,0x00,0x00}}
};
inline bool live(std::uint64_t gen) {
    return enabled.load() && native_ui_invoke_probe::recording.load() &&
        native_ui::sdk::active() && gen && gen == native_ui::sdk::generation.load() &&
        GetCurrentThreadId() == native_ui::sdk::ui_thread.load();
}
inline bool context_shape(void* ctx) {
    std::uintptr_t table = 0, words[7]{}; SIZE_T copied = 0;
    const auto base = native_ui::sdk::guard.cohtml_base;
    constexpr std::uintptr_t expected[7] = {
        0x18e17c,0x5edc90,0x5f15c8,0x5f1770,0x5f1790,0x5ede50,0x30669c};
    if (!ctx || !ReadProcessMemory(GetCurrentProcess(), ctx, &table, sizeof(table), &copied) ||
        copied != sizeof(table) || table != base + 0x607ff8 ||
        !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(table), words,
            sizeof(words), &copied) || copied != sizeof(words)) return false;
    for (unsigned i = 0; i < 7; ++i) if (words[i] != base + expected[i]) return false;
    return true;
}
inline bool binder_shape(void* binder) {
    std::uintptr_t table = 0, reader = 0, writer = 0; SIZE_T copied = 0;
    const auto base = native_ui::sdk::guard.cohtml_base;
    const HANDLE process = GetCurrentProcess();
    return binder && ReadProcessMemory(process,binder,&table,sizeof(table),&copied) &&
        copied == sizeof(table) && table == base + 0x60fb30 &&
        ReadProcessMemory(process,reinterpret_cast<void*>(table+0x1c8),&reader,sizeof(reader),&copied) &&
        copied == sizeof(reader) && reader == base + 0x5ef198 &&
        ReadProcessMemory(process,reinterpret_cast<void*>(table+0xd8),&writer,sizeof(writer),&copied) &&
        copied == sizeof(writer) && writer == base + 0x5ebf3c;
}
using Count = int (*)(void*);
using Error = bool (*)(void*);
using Argument = void* (*)(void*, int);
using Read = void (*)(void*, const char**, std::uint64_t*);
using Begin = void* (*)(void*);
using Write = void (*)(void*, const char*);
using End = void (*)(void*);
inline bool read_one(void* ctx, std::uint64_t gen, char (&owned)[513], std::size_t& size) {
    std::memset(owned,0,sizeof(owned)); size = 0;
    if (!live(gen) || !context_shape(ctx)) return false;
    const auto base = native_ui::sdk::guard.cohtml_base;
    const auto error = reinterpret_cast<Error>(base + 0x30669c);
    const auto count = reinterpret_cast<Count>(base + 0x5ede50);
    const auto argument = reinterpret_cast<Argument>(base + 0x5edc90);
    const auto read = reinterpret_cast<Read>(base + 0x5ef198);
    if (error(ctx) || !live(gen) || count(ctx) != 1 || !live(gen) || error(ctx)) return false;
    void* binder = argument(ctx,0);
    if (!live(gen) || error(ctx) || !binder_shape(binder)) return false;
    const char* temporary = nullptr; std::uint64_t length = 0;
    read(binder,&temporary,&length);
    if (!live(gen) || error(ctx) || !temporary || !length || length > 512) return false;
    SIZE_T copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), temporary, owned,
            static_cast<SIZE_T>(length)+1,&copied) || copied != length+1 ||
        owned[length] != '\0' || std::memchr(owned,'\0',static_cast<std::size_t>(length)))
        return false;
    size = static_cast<std::size_t>(length);
    return true;
}
inline void invoke_owned(native_ui_invoke_probe::Handler* handler, void* ctx,
                         const char* input, std::size_t size) {
    const DWORD saved = GetLastError();
    struct Restore { DWORD value; ~Restore() { SetLastError(value); } } restore{saved};
    const auto gen = handler->generation;
    if (!input || !size || size>512 || !live(gen)) {
        if (live(gen)) ++failures;
        return;
    }
    const auto base = native_ui::sdk::guard.cohtml_base;
    if (size == sizeof(confirmation)-1 && std::memcmp(input,confirmation,size) == 0) {
        if (!live(gen) || stage.load() != 2) return;
        confirmed_generation.store(gen); confirmed_thread.store(GetCurrentThreadId());
        unsigned expected = 2;
        if (!stage.compare_exchange_strong(expected,3)) return;
        reinterpret_cast<End>(base + 0x5f1790)(ctx);
        return; // Final context operation above.
    }
    if (size != sizeof(request)-1 || std::memcmp(input,request,size) != 0) { ++failures; return; }
    unsigned empty = 0;
    if (!stage.compare_exchange_strong(empty,1) || !live(gen)) return;
    void* result = reinterpret_cast<Begin>(base + 0x5f15c8)(ctx);
    if (!result || !live(gen) || !binder_shape(result)) { ++failures; return; }
    reinterpret_cast<Write>(base + 0x5ebf3c)(result,response);
    if (!live(gen) || reinterpret_cast<Error>(base + 0x30669c)(ctx)) { ++failures; return; }
    stage.store(2);
    reinterpret_cast<End>(base + 0x5f1770)(ctx);
    // Final context operation; JS receipt/confirmation can reenter immediately.
}
inline void invoke(native_ui_invoke_probe::Handler* handler, void* ctx) {
    const DWORD saved=GetLastError();
    struct Restore { DWORD value; ~Restore() { SetLastError(value); } } restore{saved};
    const auto gen=handler->generation;
    char input[513]{}; std::size_t size=0;
    if (!read_one(ctx,gen,input,size)) { if (live(gen)) ++failures; return; }
    invoke_owned(handler,ctx,input,size);
}
inline bool prepare(bool opt_in) {
    if (!opt_in || enabled.load() || native_ui_invoke_probe::attempted.load() ||
        !native_ui_invoke_probe::recording.load() || !native_ui::sdk::lifecycle_active()) return false;
    const auto base = native_ui::sdk::guard.cohtml_base;
    for (const auto& pin : pins)
        if (!native_ui_guard::bytes_match(base+pin.rva,pin.bytes,sizeof(pin.bytes))) return false;
    native_ui_invoke_probe::owned_probe_script = native_ui::sdk::owned_bootstrap +
        "\n;(function(){if(window.engine&&typeof window.engine.call==='function'){"
        "window.engine.call('__CRLoadoutAbiProbe_01','cr.echo.request.v1').then(function(value){"
        "if(value==='cr.echo.response.v1'){window.engine.call('__CRLoadoutAbiProbe_01',"
        "'cr.echo.confirm.v1');}});}}());\n";
    if (native_ui_invoke_probe::owned_probe_script.size() > 512*1024) return false;
    enabled.store(true);
    return true;
}
inline void stop() { enabled.store(false); }
inline void flush(FILE* log) {
    if (!log) return;
    static unsigned previous = UINT_MAX, previous_failures = UINT_MAX;
    const auto current = stage.load(), failed = failures.load();
    if (current == previous && failed == previous_failures) return;
    previous = current; previous_failures = failed;
    std::fprintf(log,"{\"event\":\"native_ui_echo\",\"stage\":%u,\"failures\":%u,"
        "\"roundtrip_verified\":%s,\"generation\":%llu,\"thread\":%lu,\"backend_called\":false}\n",
        current,failed,current==3 ? "true" : "false",confirmed_generation.load(),confirmed_thread.load());
    std::fflush(log);
}
} // namespace native_ui_echo
#endif
