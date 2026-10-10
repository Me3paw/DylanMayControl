#pragma once
#include "loadout_attack_probe.h"
#include "loadout_current_node.h"
#include <array>
namespace loadout_selection_scope {
enum : unsigned { kArguments=1, kOuterWord=2, kMenuId=4, kInputA0=8,
    kInput268=16, kArenaCounter=32, kMessageCounter=64 };
// Copied diagnostics only: neither pointer samples nor counters grant native authority.
struct Snapshot {
    std::uint64_t argument_array=0, outer_first_word=0;
    std::array<std::uint64_t,7> arguments{};
    std::uint64_t input_world_a0=0, input_world_268=0;
    std::uint64_t arena_counter=0, message_counter=0;
    std::uint32_t menu_id=0;
    unsigned copied=0;
};
template<class Reader>
inline Snapshot capture(std::uintptr_t outer,std::uintptr_t system,Reader read) {
    Snapshot out{}; std::uintptr_t address=0; std::uint64_t scalar=0;
    if (outer && read(outer,&scalar,sizeof(scalar))) {
        out.outer_first_word=scalar; out.copied|=kOuterWord;
    }
    if (!loadout_attack_probe::add(system,0x58,address) ||
        !read(address,&scalar,sizeof(scalar)) || !scalar) return out;
    std::array<std::uint64_t,7> arguments{};
    if (!read(static_cast<std::uintptr_t>(scalar),arguments.data(),sizeof(arguments))) return out;
    out.argument_array=scalar; out.arguments=arguments; out.copied|=kArguments;
    std::uint32_t id=0;
    if (loadout_attack_probe::add(arguments[0],0x3c,address) && read(address,&id,sizeof(id))) {
        out.menu_id=id; out.copied|=kMenuId;
    }
    const auto copy_word=[&](std::uint64_t base,unsigned offset,std::uint64_t& value,unsigned bit) {
        std::uint64_t temporary=0;
        if (loadout_attack_probe::add(base,offset,address) &&
            read(address,&temporary,sizeof(temporary))) { value=temporary; out.copied|=bit; }
    };
    copy_word(arguments[5],0xa0,out.input_world_a0,kInputA0);
    copy_word(arguments[5],0x268,out.input_world_268,kInput268);
    copy_word(arguments[2],0x2058,out.arena_counter,kArenaCounter);
    copy_word(arguments[2],0x1048,out.message_counter,kMessageCounter);
    return out;
}
} // namespace loadout_selection_scope
#ifdef _WIN32
#include <atomic>
#include <cstdio>
namespace loadout_selection_scope {
using Shim = void (*)(void*,void*,void*);
inline void* original_shim=nullptr;
inline void (*after_original)()=nullptr; // Set once before hooks are enabled.
inline const std::atomic<bool>* shared_stopping=nullptr;
inline std::atomic<bool> recording{false},stopped{false},stop_incomplete{false};
inline std::atomic<unsigned> attempts{0};
inline std::atomic<std::uint64_t> calls{0},dropped{0};
inline constexpr unsigned routine_record_limit=8192;
inline std::atomic<unsigned> routine_admissions{0};
struct Borrowed { std::uint64_t call; std::uintptr_t outer,system; };
inline thread_local const Borrowed* borrowed=nullptr;
struct Record {
    Snapshot value{};
    loadout_current_node::Facts current_node{};
    unsigned lookup_status=0;
    bool tree_called=false,node_called=false;
    std::uint64_t scope_call=0,qpc=0;
    DWORD thread=0;
    unsigned kind=0;
    std::uint32_t choice_id=0;
    bool arguments_match=false;
};
using LookupObserver=void (*)(Record&);
inline LookupObserver lookup_observer=nullptr;
inline SRWLOCK queue_lock=SRWLOCK_INIT;
inline Record queue[128]{};
inline unsigned count=0;
inline bool active() {
    return recording.load() && !stopped.load() && shared_stopping && !shared_stopping->load();
}
inline void publish(const Borrowed& scope,unsigned kind,std::uint32_t choice_id=0,
                    void* graph=nullptr,void* lua=nullptr) {
    if (!active()) return;
    // Cap routine telemetry, preserving actual choices and the live bridge.
    if (kind==0 || kind==1) {
        if (routine_admissions.load(std::memory_order_relaxed)>=routine_record_limit) return;
        if (routine_admissions.fetch_add(1,std::memory_order_relaxed)>=routine_record_limit) return;
    }
    const DWORD saved=GetLastError(); ++attempts;
    if (active()) {
        Record row{}; row.scope_call=scope.call; row.kind=kind; row.choice_id=choice_id;
        row.thread=GetCurrentThreadId();
        LARGE_INTEGER tick{}; QueryPerformanceCounter(&tick);
        row.qpc=static_cast<std::uint64_t>(tick.QuadPart);
        row.value=capture(scope.outer,scope.system,&loadout_attack_probe::copy);
        row.arguments_match=(kind>=2) && (row.value.copied&kArguments) &&
            row.value.arguments[1]==reinterpret_cast<std::uintptr_t>(graph) &&
            row.value.arguments[2]==reinterpret_cast<std::uintptr_t>(lua);
        if (kind==0 && active() && lookup_observer) lookup_observer(row);
        if (active()) {
            if (!TryAcquireSRWLockExclusive(&queue_lock)) { if (active()) ++dropped; }
            else {
                if (active()) { if (count==128) ++dropped; else queue[count++]=row; }
                ReleaseSRWLockExclusive(&queue_lock);
            }
        }
    }
    --attempts; SetLastError(saved);
}
inline void shim(void* first,void* middle,void* third) {
    const DWORD incoming=GetLastError();
    const Borrowed current{++calls,reinterpret_cast<std::uintptr_t>(first),
        reinterpret_cast<std::uintptr_t>(third)};
    DWORD forwarded=0;
    {
        struct RestoreBorrow { const Borrowed* previous; ~RestoreBorrow() { borrowed=previous; } };
        RestoreBorrow restore{borrowed}; borrowed=&current;
        publish(current,0);
        SetLastError(incoming);
        reinterpret_cast<Shim>(original_shim)(first,middle,third);
        forwarded=GetLastError();
        if (active() && after_original) after_original();
        publish(current,1);
    }
    SetLastError(forwarded);
}
// Observe only actual choices made inside this thread's current shim invocation.
inline void observe_choice(std::uint32_t id,void* graph,void* lua,bool after) {
    const DWORD saved=GetLastError();
    if (borrowed) publish(*borrowed,after ? 3u : 2u,id,graph,lua);
    SetLastError(saved);
}
inline bool arm(bool opt_in,bool loaded_unique_pin_verified,bool pinned,
                const std::atomic<bool>& stopping) {
    if (!opt_in || !loaded_unique_pin_verified || !pinned || !original_shim ||
        stopped.load() || recording.load() || stopping.load()) return false;
    shared_stopping=&stopping; recording.store(true);
    if (stopping.load()) recording.store(false);
    return recording.load();
}
inline bool stop() {
    stopped.store(true); recording.store(false); const auto begin=GetTickCount64();
    while (attempts.load() && GetTickCount64()-begin<100) SwitchToThread();
    const bool complete=attempts.load()==0; stop_incomplete.store(!complete); return complete;
}
inline bool flush(FILE* log) {
    if (!log || !TryAcquireSRWLockExclusive(&queue_lock)) return false;
    Record rows[128]; const unsigned size=count;
    std::memcpy(rows,queue,size*sizeof(Record)); count=0; ReleaseSRWLockExclusive(&queue_lock);
    for (unsigned i=0;i<size;++i) {
        const auto& r=rows[i]; const auto& s=r.value;
        std::fprintf(log,
            "{\"event\":\"loadout_selection_scope\",\"scope_call\":%llu,\"kind\":%u,"
            "\"qpc\":%llu,\"thread\":%lu,\"copied\":%u,\"outer_first_word\":%llu,"
            "\"input_world_a0\":%llu,\"input_world_268\":%llu,\"graph_key\":%llu,"
            "\"lua_key\":%llu,\"menu_id\":%u,\"choice_id\":%u,\"arguments_match\":%s,"
            "\"arena_counter\":\"%llu\",\"message_counter\":\"%llu\","
            "\"lookup_status\":%u,\"tree_called\":%s,\"node_called\":%s,"
            "\"lookup_node_id\":%u,\"lookup_raw_flags\":%u,\"current_pointer_equal\":%s,"
            "\"native_reservation_verified\":false,\"world_epoch_verified\":false,"
            "\"player_scope_verified\":false,\"gameplay_called_by_mod\":false}\n",
            static_cast<unsigned long long>(r.scope_call),r.kind,
            static_cast<unsigned long long>(r.qpc),r.thread,s.copied,
            static_cast<unsigned long long>(s.outer_first_word),
            static_cast<unsigned long long>(s.input_world_a0),
            static_cast<unsigned long long>(s.input_world_268),
            static_cast<unsigned long long>(s.arguments[1]),
            static_cast<unsigned long long>(s.arguments[2]),s.menu_id,r.choice_id,
            r.arguments_match ? "true" : "false",
            static_cast<unsigned long long>(s.arena_counter),
            static_cast<unsigned long long>(s.message_counter),r.lookup_status,
            r.tree_called ? "true" : "false",r.node_called ? "true" : "false",
            r.current_node.native_id,static_cast<unsigned>(r.current_node.raw_flags),
            r.current_node.current_pointer_equal ? "true" : "false");
    }
    const auto lost=dropped.exchange(0);
    if (lost) std::fprintf(log,"{\"event\":\"loadout_selection_scope_dropped\",\"count\":%llu}\n",
        static_cast<unsigned long long>(lost));
    if (size || lost) std::fflush(log);
    return true;
}
} // namespace loadout_selection_scope
#endif
