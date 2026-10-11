#pragma once
#include "diagnostics.h"
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace performance_log {
enum class Phase : unsigned { Flush, Tick, InputSample, InputFilter, GuardedRead, Count };
inline constexpr const char* names[]={"worker_flush","worker_tick","worker_input","xinput_filter","guarded_read"};
struct Metric {
    std::atomic<std::uint64_t> calls{0}, samples{0}, ticks{0}, maximum{0};
    bool admit(unsigned stride) { return calls.fetch_add(1,std::memory_order_relaxed)%stride==0; }
    void record(std::uint64_t duration) {
        samples.fetch_add(1,std::memory_order_relaxed);
        ticks.fetch_add(duration,std::memory_order_relaxed);
        auto old=maximum.load(std::memory_order_relaxed);
        while (old<duration && !maximum.compare_exchange_weak(old,duration,std::memory_order_relaxed)) {}
    }
};
inline Metric metrics[static_cast<unsigned>(Phase::Count)];
inline std::atomic<bool> enabled{false};
inline unsigned stride(Phase phase) { return phase==Phase::GuardedRead ? 128 : phase==Phase::InputFilter ? 64 : 1; }
}

#ifdef _WIN32
#include <windows.h>
namespace performance_log {
inline std::uint64_t now() { LARGE_INTEGER tick{}; QueryPerformanceCounter(&tick); return tick.QuadPart; }
class Scope {
    Metric* metric_=nullptr;
    std::uint64_t begin_=0;
public:
    explicit Scope(Phase phase) {
        if constexpr(!diagnostics::enabled)return;
        if (!enabled.load(std::memory_order_relaxed)) return;
        auto& metric=metrics[static_cast<unsigned>(phase)];
        if (!metric.admit(stride(phase))) return;
        const DWORD saved=GetLastError();
        metric_=&metric; begin_=now();
        SetLastError(saved);
    }
    ~Scope() {
        if (!metric_) return;
        const DWORD saved=GetLastError();
        metric_->record(now()-begin_);
        SetLastError(saved);
    }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
inline std::uint64_t frequency=0, next_report=0;
inline void start(FILE* log,bool observe) {
    if constexpr(!diagnostics::enabled)return;
    LARGE_INTEGER value{}; QueryPerformanceFrequency(&value);
    frequency=value.QuadPart;
    enabled.store(observe && frequency!=0,std::memory_order_relaxed);
    if (enabled.load()) {
        std::fprintf(log,"{\"event\":\"performance_logging\",\"qpc_frequency\":%llu,\"interval_ms\":1000,\"cumulative\":true,\"timings_overlap\":true,\"frame_times_measured\":false,\"all_hooks_measured\":false,\"guarded_read_stride\":128,\"xinput_filter_stride\":64}\n",static_cast<unsigned long long>(frequency));
        std::fflush(log);
    }
}
inline void report(FILE* log,bool final=false) {
    if constexpr(!diagnostics::enabled)return;
    if (!enabled.load(std::memory_order_relaxed)) return;
    const DWORD saved=GetLastError();
    const auto tick=now();
    if (!final && tick<next_report) { SetLastError(saved); return; }
    next_report=tick+frequency;
    FILETIME creation{},exit{},kernel{},user{};
    const bool cpu_valid=GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernel,&user)!=FALSE;
    const auto time_value=[](const FILETIME& value) { return (std::uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime; };
    std::fprintf(log,"{\"event\":\"mod_performance\",\"qpc\":%llu,\"wall_unix_ms\":%llu,\"worker_thread\":%lu,\"worker_cpu_valid\":%s,\"worker_cpu_ms\":%.3f,\"log_bytes\":%lld,\"final\":%s,\"phases\":[",static_cast<unsigned long long>(tick),[](){FILETIME wall{};GetSystemTimeAsFileTime(&wall);return static_cast<unsigned long long>(((std::uint64_t(wall.dwHighDateTime)<<32)|wall.dwLowDateTime)/10000-11644473600000ULL);}(),GetCurrentThreadId(),cpu_valid?"true":"false",(time_value(kernel)+time_value(user))/10000.0,static_cast<long long>(_ftelli64(log)),final?"true":"false");
    for (unsigned i=0;i<static_cast<unsigned>(Phase::Count);++i) {
        const auto& m=metrics[i];
        // Concurrent counters are approximate snapshots; sampled sums are never presented as exact total overhead.
        std::fprintf(log,"%s{\"name\":\"%s\",\"calls\":%llu,\"samples\":%llu,\"sampled_ms\":%.6f,\"sampled_max_ms\":%.6f,\"stride\":%u}",i?",":"",names[i],static_cast<unsigned long long>(m.calls.load(std::memory_order_relaxed)),static_cast<unsigned long long>(m.samples.load(std::memory_order_relaxed)),m.ticks.load(std::memory_order_relaxed)*1000.0/frequency,m.maximum.load(std::memory_order_relaxed)*1000.0/frequency,stride(static_cast<Phase>(i)));
    }
    std::fprintf(log,"]}\n"); std::fflush(log);
    SetLastError(saved);
}
}
#endif
