// Native form metadata only. No selection calls, gameplay writes or UI binding.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace loadout_mod {
namespace detail {
struct FormResult {
    std::uint64_t sequence, qpc, configuration_key;
    DWORD thread;
    std::uint32_t id;
    std::uint8_t raw_found, category;
    bool success_fields_known;
};
static SRWLOCK lock = SRWLOCK_INIT;
static std::atomic<bool> recording{false};
static std::atomic<unsigned> attempts{0};
static std::atomic<std::uint64_t> dropped{0}, sequence{0};
static FormResult queue[256];
static unsigned count = 0;
static bool started = false;
}

// One capture lifetime per process, enabled only after root's pinned hook guard.
static bool start(const std::atomic<bool>& stopping,
                  bool hook_contract_verified = false) {
    if (!hook_contract_verified || stopping.load()) return false;
    AcquireSRWLockExclusive(&detail::lock);
    bool accepted = !detail::started && !stopping.load();
    if (accepted) {
        detail::started = true;
        detail::recording.store(true);
    }
    ReleaseSRWLockExclusive(&detail::lock);
    return accepted;
}

// Called before the native lookup's caller resumes. Failed/empty lookups must
// not read the uninitialized category or an ID pointer native code never read.
static void observe_form_result(const std::uint8_t* out2,
                                const std::uint32_t* id_pointer,
                                const void* cfg) {
    if (!detail::recording.load()) return;
    DWORD error = GetLastError();
    ++detail::attempts;
    if (detail::recording.load()) {
        std::uint64_t sequence = ++detail::sequence;
        if (!TryAcquireSRWLockExclusive(&detail::lock)) {
            ++detail::dropped;
        } else {
            if (detail::recording.load()) {
                if (!out2 || detail::count == 256) {
                    ++detail::dropped;
                } else {
                    detail::FormResult row{};
                    row.sequence = sequence;
                    row.configuration_key = reinterpret_cast<std::uintptr_t>(cfg);
                    row.thread = GetCurrentThreadId();
                    LARGE_INTEGER tick{};
                    QueryPerformanceCounter(&tick);
                    row.qpc = static_cast<std::uint64_t>(tick.QuadPart);
                    std::memcpy(&row.raw_found, out2 + 1, sizeof(row.raw_found));
                    if (row.raw_found == 1 && id_pointer) {
                        std::memcpy(&row.id, id_pointer, sizeof(row.id));
                        std::memcpy(&row.category, out2, sizeof(row.category));
                        row.success_fields_known = true;
                    }
                    detail::queue[detail::count++] = row;
                }
            }
            ReleaseSRWLockExclusive(&detail::lock);
        }
    }
    --detail::attempts;
    SetLastError(error);
}

static void stop() {
    AcquireSRWLockExclusive(&detail::lock);
    detail::recording.store(false);
    ReleaseSRWLockExclusive(&detail::lock);
    while (detail::attempts.load()) Sleep(1);
}

// Sole writer only. configuration_key is an opaque address, not a world epoch;
// samples are not a complete catalogue or an ownership/eligibility predicate.
static void flush(FILE* log) {
    if (!log) return;
    detail::FormResult rows[256];
    AcquireSRWLockExclusive(&detail::lock);
    unsigned size = detail::count;
    std::memcpy(rows, detail::queue, size * sizeof(rows[0]));
    detail::count = 0;
    ReleaseSRWLockExclusive(&detail::lock);
    for (unsigned i = 0; i < size; ++i) {
        const auto& row = rows[i];
        std::fprintf(log,
            "{\"event\":\"form_lookup\",\"sequence\":%llu,\"qpc\":%llu,"
            "\"thread\":%lu,\"configuration_key\":%llu,\"raw_found\":%u,"
            "\"success_fields_known\":%s,",
            static_cast<unsigned long long>(row.sequence),
            static_cast<unsigned long long>(row.qpc), row.thread,
            static_cast<unsigned long long>(row.configuration_key),
            static_cast<unsigned>(row.raw_found),
            row.success_fields_known ? "true" : "false");
        if (row.success_fields_known) {
            std::fprintf(log, "\"id\":%u,\"category\":%u}\n",
                         static_cast<unsigned>(row.id),
                         static_cast<unsigned>(row.category));
        } else {
            std::fprintf(log, "\"id\":null,\"category\":null}\n");
        }
    }
    std::uint64_t lost = detail::dropped.exchange(0);
    if (lost) {
        std::fprintf(log, "{\"event\":\"form_lookup_dropped\",\"count\":%llu}\n",
                     static_cast<unsigned long long>(lost));
    }
    if (size || lost) std::fflush(log);
}
} // namespace loadout_mod
