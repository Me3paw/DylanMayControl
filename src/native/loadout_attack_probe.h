#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
namespace loadout_attack_probe {
enum : unsigned { kBundle=1, kWorld=2, kActor=4, kAttackIndices=8,
    kFlagA0=16, kFlagAE=32, kFormPair=64, kDefinition=128 };
struct Snapshot {
    std::uint64_t world=0, chunk=0, row=0, actor=0, attack_key=0;
    unsigned copied=0;
    std::uint32_t attack_index=UINT32_MAX, form_index=UINT32_MAX, variant=UINT32_MAX;
    std::uint32_t form70=0, form74=0, referenced_form_id=0;
    std::uint8_t raw_a0=0, raw_ae=0, category=UINT8_MAX;
};
inline bool add(std::uintptr_t base, std::uint64_t offset, std::uintptr_t& out) {
    out=0;
    if (!base || offset > (std::numeric_limits<std::uintptr_t>::max)()-base) return false;
    out=base+static_cast<std::uintptr_t>(offset); return true;
}
inline bool row_address(std::uint64_t base, std::uint64_t row, unsigned stride,
                        std::uintptr_t& out) {
    out=0;
    if (!stride || row > UINT32_MAX ||
        row > (std::numeric_limits<std::uint64_t>::max)()/stride ||
        base > (std::numeric_limits<std::uintptr_t>::max)()) return false;
    return add(static_cast<std::uintptr_t>(base),row*stride,out);
}
template<class Reader>
inline Snapshot capture(std::uintptr_t columns, std::uintptr_t query, Reader read) {
    Snapshot out{}; std::array<std::uint64_t,11> q{};
    if (!columns || !read(columns,q.data(),sizeof(q))) return out;
    out.copied |= kBundle; out.chunk=q[9]; out.row=q[10];
    std::uint64_t scalar=0;
    if (query && read(query,&scalar,sizeof(scalar))) {
        out.world=scalar; out.copied |= kWorld;
    }
    if (out.row > UINT32_MAX) return out;
    std::uintptr_t address=0, base=0;
    scalar=0;
    if (row_address(out.chunk,out.row,8,base) && add(base,0x10,address) &&
        read(address,&scalar,sizeof(scalar))) {
        out.actor=scalar; out.copied |= kActor;
    }
    if (row_address(q[0],out.row,0x78,base) && add(base,0x70,address)) {
        std::uint32_t pair[2]{};
        if (read(address,pair,sizeof(pair))) {
            out.form70=pair[0]; out.form74=pair[1]; out.copied |= kFormPair;
        }
    }
    if (!row_address(q[5],out.row,0xb0,base)) return out;
    out.attack_key=base;
    if (add(base,8,address)) {
        std::uint32_t indices[3]{};
        if (read(address,indices,sizeof(indices))) {
            out.attack_index=indices[0]; out.form_index=indices[1];
            out.variant=indices[2]; out.copied |= kAttackIndices;
        }
    }
    std::uint8_t byte=0;
    if (add(base,0xa0,address) && read(address,&byte,1)) {
        out.raw_a0=byte; out.copied |= kFlagA0;
    }
    byte=0;
    if (add(base,0xae,address) && read(address,&byte,1)) {
        out.raw_ae=byte; out.copied |= kFlagAE;
    }
    if (!(out.copied & kAttackIndices) || !row_address(q[3],out.row,0xa0,base))
        return out;
    std::uint64_t table=0; std::uint32_t count=0;
    if (!add(base,8,address) || !read(address,&table,sizeof(table)) ||
        !add(base,0x10,address) || !read(address,&count,sizeof(count)) ||
        out.form_index >= count || !row_address(table,out.form_index,0x140,base))
        return out;
    std::uint32_t id=0,category=UINT32_MAX;
    if (read(base,&id,sizeof(id)) && add(base,0x70,address) && read(address,&category,sizeof(category)) && category<4) {
        out.referenced_form_id=id; out.category=static_cast<std::uint8_t>(category); out.copied |= kDefinition;
    }
    return out;
}
} // namespace loadout_attack_probe
#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <cstdio>
#include "performance_log.h"
namespace loadout_attack_probe {
static_assert(sizeof(void*)==8,"Win64 native ABI required");
using FormsCall = void (*)(void*,void*,void*);
inline void* original_forms=nullptr;
inline const std::atomic<bool>* shared_stopping=nullptr;
inline std::atomic<bool> recording{false}, stopped{false};
inline std::atomic<bool> stop_incomplete{false};
inline std::atomic<unsigned> attempts{0};
inline std::atomic<std::uint64_t> calls{0}, dropped{0};
inline SRWLOCK queue_lock=SRWLOCK_INIT;
struct Record { Snapshot value{}; std::uint64_t call=0,qpc=0; DWORD thread=0; unsigned phase=0; };
inline Record queue[256]{};
inline unsigned count=0;
#ifdef CRMOD_API_SELF_CHECK
inline void (*copy_test_barrier)()=nullptr;
#endif
inline bool active() {
    return recording.load() && !stopped.load() && shared_stopping && !shared_stopping->load();
}
inline bool copy(std::uintptr_t address,void* destination,std::size_t size) {
    performance_log::Scope timing(performance_log::Phase::GuardedRead);
#ifdef CRMOD_API_SELF_CHECK
    if (copy_test_barrier) copy_test_barrier();
#endif
    SIZE_T copied=0;
    return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),
        destination,size,&copied) && copied==size;
}
inline void record(std::uint64_t call,unsigned phase,void* columns,void* query) {
    if constexpr(!diagnostics::enabled)return;
    if (!active()) return;
    const DWORD saved=GetLastError();
    ++attempts;
    if (active()) {
        Record row{}; row.call=call; row.phase=phase; row.thread=GetCurrentThreadId();
        LARGE_INTEGER tick{}; QueryPerformanceCounter(&tick);
        row.qpc=static_cast<std::uint64_t>(tick.QuadPart);
        row.value=capture(reinterpret_cast<std::uintptr_t>(columns),
                          reinterpret_cast<std::uintptr_t>(query),&copy);
        if (active()) {
            if (!TryAcquireSRWLockExclusive(&queue_lock)) {
                if (active()) ++dropped;
            } else {
                if (active()) { if (count==256) ++dropped; else queue[count++]=row; }
                ReleaseSRWLockExclusive(&queue_lock);
            }
        }
    }
    --attempts; SetLastError(saved);
}
inline void forms(void* columns,void* query,void* command_buffer) {
    const DWORD incoming=GetLastError(); const auto call=++calls;
    record(call,0,columns,query);
    SetLastError(incoming);
    reinterpret_cast<FormsCall>(original_forms)(columns,query,command_buffer);
    const DWORD forwarded=GetLastError();
    record(call,1,columns,query);
    SetLastError(forwarded);
}
inline bool arm(bool opt_in,bool target_loaded_contract_verified,
                bool module_and_trampoline_pinned,const std::atomic<bool>& stopping) {
    if (!opt_in || !target_loaded_contract_verified || !module_and_trampoline_pinned ||
        !original_forms || stopped.load() || recording.load() || stopping.load()) return false;
    shared_stopping=&stopping; recording.store(true);
    if (stopping.load()) recording.store(false);
    return recording.load();
}
inline bool stop() {
    stopped.store(true); recording.store(false);
    const auto begin=GetTickCount64();
    while (attempts.load() && GetTickCount64()-begin < 100) SwitchToThread();
    const bool complete=attempts.load()==0;
    stop_incomplete.store(!complete);
    return complete;
}
inline bool flush(FILE* log) {
    if (!log || !TryAcquireSRWLockExclusive(&queue_lock)) return false;
    Record copied[256];
    const unsigned size=count; std::memcpy(copied,queue,size*sizeof(Record)); count=0;
    ReleaseSRWLockExclusive(&queue_lock);
    for (unsigned i=0;i<size;++i) {
        const auto& r=copied[i]; const auto& s=r.value;
        std::fprintf(log,
            "{\"event\":\"loadout_actor_attack_snapshot\",\"call\":%llu,\"phase\":%u,"
            "\"qpc\":%llu,\"thread\":%lu,\"copied\":%u,\"world_key\":%llu,"
            "\"chunk_key\":%llu,\"row\":%llu,\"actor_handle\":\"%llu\","
            "\"attack_key\":%llu,\"attack_index\":%u,\"form_index\":%u,\"variant\":%u,"
            "\"raw_a0\":%u,\"raw_ae\":%u,\"form70\":%u,\"form74\":%u,"
            "\"referenced_form_id\":%u,\"category\":%u,\"player_scope_verified\":false,"
            "\"frame_scope_verified\":false,\"switch_permission_verified\":false}\n",
            static_cast<unsigned long long>(r.call),r.phase,
            static_cast<unsigned long long>(r.qpc),r.thread,s.copied,
            static_cast<unsigned long long>(s.world),static_cast<unsigned long long>(s.chunk),
            static_cast<unsigned long long>(s.row),static_cast<unsigned long long>(s.actor),
            static_cast<unsigned long long>(s.attack_key),s.attack_index,s.form_index,s.variant,
            static_cast<unsigned>(s.raw_a0),static_cast<unsigned>(s.raw_ae),s.form70,s.form74,
            s.referenced_form_id,static_cast<unsigned>(s.category));
    }
    const auto lost=dropped.exchange(0);
    if (lost) std::fprintf(log,"{\"event\":\"loadout_actor_attack_dropped\",\"count\":%llu}\n",
        static_cast<unsigned long long>(lost));
    if (size || lost) std::fflush(log);
    return true;
}
} // namespace loadout_attack_probe
#endif
