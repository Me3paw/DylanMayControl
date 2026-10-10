#pragma once
#include "native_ui.h"
#ifdef _WIN32
#include <new>
#include <cstddef>
#include <cstdio>
namespace native_ui_invoke_probe {
constexpr unsigned kWords = 7, kCapacity = 8;
using Allocate = void* (*)(std::size_t);
using Delete = void (*)(void*);
using Register = std::uintptr_t (*)(void*, const char*, void*);
struct Handler;
using AfterCapture = void (*)(Handler*, void*);
inline AfterCapture after_capture = nullptr;
inline Allocate allocate = nullptr;
inline Delete deallocate = nullptr;
inline SRWLOCK lock = SRWLOCK_INIT;
inline std::atomic<bool> recording{false}, attempted{false};
inline std::atomic<std::uint64_t> attempted_generation{0};
inline std::atomic<std::uint64_t> dropped{0}, registered_handle{0}, registered_generation{0};
inline std::string owned_probe_script;
struct Record {
    std::uint64_t qpc = 0, generation = 0, handler_key = 0, context_key = 0, table_key = 0;
    std::uint64_t words[kWords]{};
    DWORD thread = 0;
    bool readable = false;
};
inline Record queue[kCapacity];
inline unsigned count = 0;
struct Handler {
    const std::uintptr_t* table;
    std::atomic<void*> user_data;
    const std::uint64_t generation;
    Handler(const std::uintptr_t* methods, std::uint64_t value)
        : table(methods), user_data(nullptr), generation(value) {}
};
static_assert(sizeof(Handler) == 24 && offsetof(Handler, user_data) == 8 &&
              offsetof(Handler, generation) == 16, "Win64 IEventHandler shape");
inline void* destroy(Handler* handler, std::uint32_t flags) {
    const DWORD saved = GetLastError();
    native_ui::sdk::CallbackScope scope;
    // The registry calls flags0, then its own Cohtml free(tag11).
    handler->~Handler();
    if (flags & 1u) deallocate(handler);
    SetLastError(saved);
    return handler;
}
inline const void* get_user_data(Handler* handler) {
    return handler->user_data.load();
}
inline void set_user_data(Handler* handler, void* value) {
    handler->user_data.store(value);
}
inline void invoke(Handler* handler, void* context) {
    const DWORD saved = GetLastError();
    native_ui::sdk::CallbackScope scope;
    const auto gen = handler->generation;
    if (recording.load() && native_ui::sdk::active() &&
        gen == native_ui::sdk::generation.load() &&
        GetCurrentThreadId() == native_ui::sdk::ui_thread.load()) {
        if (!TryAcquireSRWLockExclusive(&lock)) ++dropped;
        else {
            if (recording.load() && native_ui::sdk::active()) {
                if (count == kCapacity) ++dropped;
                else {
                    Record row{};
                    row.generation = gen;
                    row.handler_key = reinterpret_cast<std::uintptr_t>(handler);
                    row.context_key = reinterpret_cast<std::uintptr_t>(context);
                    row.thread = GetCurrentThreadId();
                    LARGE_INTEGER tick{}; QueryPerformanceCounter(&tick);
                    row.qpc = static_cast<std::uint64_t>(tick.QuadPart);
                    SIZE_T copied = 0;
                    const HANDLE process = GetCurrentProcess();
                    if (context && ReadProcessMemory(process, context, &row.table_key,
                            sizeof(row.table_key), &copied) && copied == sizeof(row.table_key) &&
                        row.table_key && ReadProcessMemory(process,
                            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(row.table_key)),
                            row.words, sizeof(row.words), &copied) && copied == sizeof(row.words))
                        row.readable = true;
                    else std::memset(row.words, 0, sizeof(row.words));
                    queue[count++] = row;
                }
            }
            ReleaseSRWLockExclusive(&lock);
        }
    }
    if (after_capture) after_capture(handler, context);
    SetLastError(saved);
}
inline const std::uintptr_t methods[4] = {
    reinterpret_cast<std::uintptr_t>(&destroy), reinterpret_cast<std::uintptr_t>(&invoke),
    reinterpret_cast<std::uintptr_t>(&get_user_data), reinterpret_cast<std::uintptr_t>(&set_user_data)
};
inline bool exact(std::uintptr_t address, const unsigned char* expected, std::size_t size) {
    unsigned char copied[16]{}; SIZE_T length = 0;
    return size <= sizeof(copied) && ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(address), copied, size, &length) && length == size &&
        std::memcmp(copied, expected, size) == 0;
}
inline bool prepare(bool opt_in, bool registration_loaded_contract_verified = false) {
    const auto& guard = native_ui::sdk::guard;
    if (!opt_in || recording.load() || attempted.load() ||
        !registration_loaded_contract_verified || !native_ui::sdk::lifecycle_active() ||
        !guard.executable_and_dll_hashes_verified || !guard.lifecycle_signatures_verified)
        return false;
    const auto base = guard.cohtml_base;
    const HMODULE module = reinterpret_cast<HMODULE>(base);
    const auto alloc = GetProcAddress(module, "??2IEventHandler@cohtml@@SAPEAX_K@Z");
    const auto free = GetProcAddress(module, "??3IEventHandler@cohtml@@SAXPEAX@Z");
    static constexpr unsigned char alloc_bytes[] = {0xba,0x11,0,0,0,0xe9,0x36,0x67,0xed,0xff};
    static constexpr unsigned char free_bytes[] = {0xba,0x11,0,0,0,0xe9,0x42,0x66,0xed,0xff};
    if (reinterpret_cast<std::uintptr_t>(alloc) != base + 0x18a620 ||
        reinterpret_cast<std::uintptr_t>(free) != base + 0x18a614 ||
        !exact(base + 0x18a620, alloc_bytes, sizeof(alloc_bytes)) ||
        !exact(base + 0x18a614, free_bytes, sizeof(free_bytes))) return false;
    allocate = reinterpret_cast<Allocate>(alloc);
    deallocate = reinterpret_cast<Delete>(free);
    owned_probe_script = native_ui::sdk::owned_bootstrap +
        "\n;(function(){if(window.engine&&typeof window.engine.call==='function'){"
        "window.engine.call('__CRLoadoutAbiProbe_01');}}());\n";
    if (owned_probe_script.size() > 512 * 1024) return false;
    recording.store(true);
    return true;
}
inline const char* register_current(void* current_view, std::uint64_t expected_generation) {
    const DWORD saved = GetLastError();
    struct Restore { DWORD value; ~Restore() { SetLastError(value); } } restore{saved};
    if (!recording.load() || !native_ui::sdk::active() || !expected_generation ||
        expected_generation != native_ui::sdk::generation.load() ||
        reinterpret_cast<std::uintptr_t>(current_view) != native_ui::sdk::view_key.load() ||
        GetCurrentThreadId() != native_ui::sdk::ui_thread.load() ||
        !native_ui::sdk::view_shape(current_view)) return nullptr;
    const auto* table = *reinterpret_cast<const std::uintptr_t* const*>(current_view);
    if (table[0x1c0 / 8] != native_ui::sdk::guard.cohtml_base + 0x5571cc) return nullptr;
    auto previous=attempted_generation.load();
    if (previous>=expected_generation ||
        !attempted_generation.compare_exchange_strong(previous,expected_generation)) return nullptr;
    attempted.store(true);
    void* storage = allocate(sizeof(Handler));
    if (!storage) return nullptr;
    auto* handler = new (storage) Handler(methods, expected_generation);
    const auto handler_key = reinterpret_cast<std::uintptr_t>(handler);
    const auto reg = reinterpret_cast<Register>(table[0x1c0 / 8]);
    const auto result = reg(current_view, "__CRLoadoutAbiProbe_01", handler);
    // The SDK always consumes handler. Never dereference it after this call.
    registered_handle.store(result);
    registered_generation.store(expected_generation);
    if (result != handler_key || !recording.load() || !native_ui::sdk::active() ||
        native_ui::sdk::generation.load() != expected_generation) return nullptr;
    return owned_probe_script.c_str();
}
inline void stop() { recording.store(false); }
inline void flush(FILE* log) {
    if (!log) return;
    Record copied[kCapacity];
    AcquireSRWLockExclusive(&lock);
    const unsigned size = count;
    std::memcpy(copied, queue, size * sizeof(Record)); count = 0;
    ReleaseSRWLockExclusive(&lock);
    for (unsigned i = 0; i < size; ++i) {
        const auto& row = copied[i];
        std::fprintf(log, "{\"event\":\"native_ui_args_context\",\"qpc\":%llu,\"thread\":%lu,"
            "\"generation\":%llu,\"handler_key\":%llu,\"context_key\":%llu,\"table_key\":%llu,"
            "\"cohtml_base\":%llu,\"readable\":%s,\"vtable_words\":[", row.qpc, row.thread,
            row.generation, row.handler_key, row.context_key, row.table_key,
            static_cast<unsigned long long>(native_ui::sdk::guard.cohtml_base),
            row.readable ? "true" : "false");
        if (row.readable) for (unsigned n = 0; n < kWords; ++n)
            std::fprintf(log, "%s%llu", n ? "," : "", row.words[n]);
        std::fprintf(log, "],\"snapshot_result_methods_called\":false,\"backend_called\":false}\n");
    }
    const auto lost = dropped.exchange(0);
    if (lost) std::fprintf(log, "{\"event\":\"native_ui_args_context_dropped\",\"count\":%llu}\n", lost);
    if (size || lost) std::fflush(log);
}
} // namespace native_ui_invoke_probe
#endif
