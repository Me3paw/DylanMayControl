// Passive View snapshots at the evidenced native binding call; no View calls.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace native_ui_probe {
// Native uses View virtual +0x258, proving at least 76 pointer-sized slots.
constexpr unsigned kWords = 76, kCapacity = 16;
struct Record {
    std::uint64_t qpc, view_key, table_key, words[kWords];
    DWORD thread;
    bool readable;
};
static SRWLOCK lock = SRWLOCK_INIT;
static std::atomic<bool> recording{false};
static std::atomic<unsigned> attempts{0};
static std::atomic<std::uint64_t> dropped{0};
static Record queue[kCapacity];
static std::uint64_t seen[kCapacity];
static unsigned count = 0, seen_count = 0;
static bool started = false;
static std::uint64_t exe_base = 0, cohtml_base = 0;

static bool start(const std::atomic<bool>& stopping, bool verified) {
    if (!verified || stopping.load()) return false;
    AcquireSRWLockExclusive(&lock);
    bool ok = !started && !stopping.load();
    if (ok) {
        started = true;
        exe_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        cohtml_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"cohtml.WindowsDesktop.dll"));
        recording.store(true);
    }
    ReleaseSRWLockExclusive(&lock);
    return ok;
}

// Only inside the live two-argument native binding call, before its original.
// Addresses in the queue are opaque numbers. The worker never follows them.
static void observe(void* view) {
    if (!recording.load()) return;
    DWORD error = GetLastError();
    ++attempts;
    if (recording.load()) {
        if (!TryAcquireSRWLockExclusive(&lock)) ++dropped;
        else {
            if (recording.load()) {
                const auto key = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(view));
                bool duplicate = false;
                for (unsigned i = 0; i < seen_count; ++i) if (seen[i] == key) duplicate = true;
                if (!duplicate && (!view || count == kCapacity || seen_count == kCapacity)) ++dropped;
                else if (!duplicate) {
                    Record row{};
                    row.view_key = key;
                    row.thread = GetCurrentThreadId();
                    LARGE_INTEGER tick{};
                    QueryPerformanceCounter(&tick);
                    row.qpc = static_cast<std::uint64_t>(tick.QuadPart);
                    SIZE_T bytes = 0;
                    HANDLE process = GetCurrentProcess();
                    if (ReadProcessMemory(process, view, &row.table_key, sizeof(row.table_key), &bytes) &&
                        bytes == sizeof(row.table_key) && row.table_key &&
                        ReadProcessMemory(process, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(row.table_key)),
                                          row.words, sizeof(row.words), &bytes) && bytes == sizeof(row.words)) {
                        row.readable = true;
                    } else {
                        // Failed/partial snapshots never become a method table.
                        std::memset(row.words, 0, sizeof(row.words));
                    }
                    seen[seen_count++] = key;
                    queue[count++] = row;
                }
            }
            ReleaseSRWLockExclusive(&lock);
        }
    }
    --attempts;
    SetLastError(error);
}

static void stop() {
    AcquireSRWLockExclusive(&lock);
    recording.store(false);
    ReleaseSRWLockExclusive(&lock);
    while (attempts.load()) Sleep(1);
}

static void flush(FILE* log) {
    if (!log) return;
    Record rows[kCapacity];
    AcquireSRWLockExclusive(&lock);
    unsigned size = count;
    std::memcpy(rows, queue, size * sizeof(rows[0]));
    count = 0;
    const auto exe = exe_base, cohtml = cohtml_base;
    ReleaseSRWLockExclusive(&lock);
    for (unsigned i = 0; i < size; ++i) {
        const auto& r = rows[i];
        std::fprintf(log, "{\"event\":\"native_ui_view_snapshot\",\"qpc\":%llu,\"thread\":%lu,"
            "\"view_key\":%llu,\"table_key\":%llu,\"exe_base\":%llu,\"cohtml_base\":%llu,"
            "\"readable\":%s,\"vtable_words\":[", r.qpc, r.thread, r.view_key, r.table_key,
            exe, cohtml, r.readable ? "true" : "false");
        if (r.readable) for (unsigned n = 0; n < kWords; ++n)
            std::fprintf(log, "%s%llu", n ? "," : "", r.words[n]);
        std::fprintf(log, "]}\n");
    }
    const auto lost = dropped.exchange(0);
    if (lost) std::fprintf(log, "{\"event\":\"native_ui_view_snapshot_dropped\",\"count\":%llu}\n", lost);
    if (size || lost) std::fflush(log);
}
} // namespace native_ui_probe
