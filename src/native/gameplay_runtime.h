#pragma once
#include "feature_hooks.h"
#include "collision_observer.h"
#include "camera_lock_log.h"
#include "pusher_probe.h"
#include "loadout_player_catalogue.h"
#include "player_geometry_log.h"
#include "enemy_contact_native.h"
#include "attack_turn_native.h"
#ifdef _WIN32
namespace gameplay_runtime {
struct LogLine { std::size_t size=0; char text[graph_probe::kLineMax]{}; };
inline SRWLOCK log_lock=SRWLOCK_INIT;
inline LogLine lines[16]{};
inline unsigned log_count=0;
inline std::atomic<std::uint64_t> log_dropped{0};
inline std::atomic<bool> stopping{false};
inline bool chooser_ready=false;
inline bool collision_ready=false;
inline collision_observer::Observer collision;
inline collision_observer::Original4 original_collision=nullptr;
inline const std::atomic<bool>* shared_stopping=nullptr;
inline std::atomic<std::uint64_t> collision_calls{0};
inline constexpr std::uint64_t kCollisionCalls=32768;
inline bool copy(void* dst,const void* src,std::size_t size) {
    return loadout_attack_probe::copy(reinterpret_cast<std::uintptr_t>(src),dst,size);
}
inline bool writable(void* dst,std::size_t size) {
    const auto start=reinterpret_cast<std::uintptr_t>(dst);
    if (!combat_native::plausible(start,size)) return false;
    const auto end=start+size;
    for (auto cursor=start;cursor<end;) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor),&memory,sizeof(memory)) ||
            memory.State!=MEM_COMMIT || (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
            !(memory.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
        const auto next=reinterpret_cast<std::uintptr_t>(memory.BaseAddress)+memory.RegionSize;
        if (next<=cursor) return false;
        cursor=next;
    }
    return true;
}
inline bool write(void* dst,const void* src,std::size_t size) {
    const DWORD saved=GetLastError();
    SIZE_T written=0;
    const bool ok=src && size<=0x80 && writable(dst,size) &&
        WriteProcessMemory(GetCurrentProcess(),dst,src,size,&written) && written==size;
    SetLastError(saved); return ok;
}
inline std::uint32_t clock_ms() { return static_cast<std::uint32_t>(GetTickCount64()); }
inline std::uint32_t get_error() { return GetLastError(); }
inline void set_error(std::uint32_t error) { SetLastError(error); }
inline bool collect_accepted(const char* line,std::size_t size) {
    const DWORD error=GetLastError(); bool queued=false;
    if (!stopping.load() && line && size<sizeof(lines[0].text)) {
        if (!TryAcquireSRWLockExclusive(&log_lock)) ++log_dropped;
        else {
            if (log_count==16) ++log_dropped;
            else if (!stopping.load()) {
                auto& out=lines[log_count++]; out.size=size;
                std::memcpy(out.text,line,size);
                queued=true;
            }
            ReleaseSRWLockExclusive(&log_lock);
        }
    }
    SetLastError(error);
    return queued;
}
inline void collect(const char* line,std::size_t size) { (void)collect_accepted(line,size); }
inline bool player_collision(const void* cols) {
    const auto wanted=loadout_player_catalogue::observed_player_actor.load();
    if (!wanted) return false;
    std::uint64_t column=0,row=0,container=0,owner=0;
    const auto address=reinterpret_cast<std::uintptr_t>(cols);
    return loadout_attack_probe::copy(address,&column,sizeof(column)) &&
        loadout_attack_probe::copy(combat_native::at(address,0,0,0x1c*8),&row,sizeof(row)) &&
        row<collision_probe::kMaxRow &&
        loadout_attack_probe::copy(combat_native::at(column,row,0x20,8),&container,sizeof(container)) &&
        loadout_attack_probe::copy(combat_native::at(container,0,0,0x18),&owner,sizeof(owner)) && owner==wanted;
}
inline std::uint64_t qpc_now() { LARGE_INTEGER v{}; QueryPerformanceCounter(&v); return static_cast<std::uint64_t>(v.QuadPart); }
// 0 = unknown scope (recorded), 1 = the observed Player's ComboData row, 2 = another entity (skipped).
inline int player_combo_row(std::uint64_t row) {
    const auto world=step_binding::mail().world.load(std::memory_order_relaxed);
    const auto actor=loadout_player_catalogue::observed_player_actor.load();
    uint64_t combo=0;
    if (!world || !actor || !step_binding::resolve_player_column(world,actor,
        step_binding::kColCombo,0x48,&combo)) return 0;
    return combo==row ? 1 : 2;
}
inline std::uint64_t observed_player() { return loadout_player_catalogue::observed_player_actor.load(); }
inline void collision_detour(void* a,void* b,void* c,void* d) {
    const DWORD incoming=GetLastError();
    const auto overlap_sample=(!stopping.load() && shared_stopping && !shared_stopping->load() && player_collision(a))
        ? enemy_contact_native::begin(a,b) : enemy_contact_native::Invocation{};
    bool observe=false;
    if (!stopping.load() && shared_stopping && !shared_stopping->load() &&
        collision_calls.load()<kCollisionCalls && player_collision(a)) {
        const auto number=collision_calls.fetch_add(1);
        observe=number<kCollisionCalls;
    }
    SetLastError(incoming);
    if (observe) {
        step_binding::feed_world_from_ctx(b);
        pusher_probe::capture(a,0);                       // read-only guarded copies: engine pusher records/vectors (CB53)
        SetLastError(incoming);
        collision.run(original_collision,a,b,c,d);
        const DWORD forwarded=GetLastError();
        pusher_probe::capture(a,1);
        SetLastError(forwarded);
    }
    else original_collision(a,b,c,d);
    const DWORD forwarded=GetLastError();
    enemy_contact_native::end(a,b,overlap_sample);
    SetLastError(forwarded);
}
inline std::uint32_t thread_id() { return GetCurrentThreadId(); }
inline bool history_sink(const char* line,std::size_t size,void* context) {
    return std::fwrite(line,1,size,static_cast<FILE*>(context))==size;
}
inline bool collision_sink(const char* line,std::size_t size,void* context) {
    return std::fwrite(line,1,size,static_cast<FILE*>(context))==size;
}
inline bool prepare(std::uintptr_t base,const std::atomic<bool>* stop_token=nullptr) {
    shared_stopping=stop_token;
    auto& b=combat_native::binding();
    b.get_last_error=&get_error; b.set_last_error=&set_error;
    chooser_ready=finisher_binding::init(base,&copy,&clock_ms,&collect);
    finisher_binding::set_raw_accept(&collect_accepted);   // discovery lines are retried when the 16-slot queue is full
    finisher_binding::set_history_env(&qpc_now,&player_combo_row);
    camera_lock_log::init(base,&copy,&qpc_now);
    // Keep the shared entity/input reader alive after removing stomp actuation.
    step_binding::init_reader(base,&copy);

    step_binding::set_player_source(&observed_player);
    step_binding::set_qpc_source(&qpc_now);
    pusher_probe::set_qpc_source(&qpc_now);
    step_binding::set_raw_accept(&collect_accepted);   // read-only jump-gate evidence lines (<=256 records per session)
    step_binding::refresh_status();
    const char* const names[]={"collision_wrap"}; const char* bad=nullptr;
    collision_ready=native_pins::verify_set(&copy,base,names,1,&bad);
    if (collision_ready) {
        collision_probe::Config config{};
        config.log_empty=true; // Only the copied Player handle passes the outer filter.
        collision_ready=collision.configure(&copy,&thread_id,&clock_ms,&get_error,&set_error,config);
    }
    return chooser_ready;
}
inline void flush(FILE* log) {
    if (!log) return;
    held_lock_native::flush(log);
    player_geometry_log::flush(log);
    loadout_key_requests::flush(log);
    facing_lock_native::flush(log);
    enemy_contact_native::flush(log);
    attack_turn_native::flush(log);
    collision.try_flush(&collision_sink,log,64,10,nullptr);
    finisher_binding::flush_history(&history_sink,log,16);
    finisher_binding::flush_shortcut_diagnostics(log);
    camera_lock_log::flush(&history_sink,log,16);
    pusher_probe::flush(&history_sink,log,8);   // 'P' lines: engine character-vs-body pusher records / entity vectors (read-only)
    step_binding::flush_state(&history_sink,log,16);   // 'S' ground/jump/dodge/float samples + step_probe_scope JSON   // Native lock state and target; copied records only.
    if (!TryAcquireSRWLockExclusive(&log_lock)) return;
    for (unsigned i=0;i<log_count;++i) std::fwrite(lines[i].text,1,lines[i].size,log);
    log_count=0;
    const auto lost=log_dropped.exchange(0);
    ReleaseSRWLockExclusive(&log_lock);
    if (lost) std::fprintf(log,"{\"event\":\"feature_log_dropped\",\"count\":%llu}\n",
        static_cast<unsigned long long>(lost));
    std::fflush(log);
}
inline void tick() {
    static ULONGLONG next=0;
    const auto now=GetTickCount64();
    if (now<next || stopping.load()) return;
    next=now+1000;
    step_binding::refresh_status();
    feature_status::registry().emit_status(feature_status::Feature::Finisher);
    feature_status::registry().emit_status(feature_status::Feature::EnemyStep);
}
inline void stop() {
    stopping.store(true);
    held_lock_native::stop();
    player_geometry_log::stop();
    loadout_key_requests::stop();
    facing_lock_native::stop();
    enemy_contact_native::stop();
    attack_turn_native::stop();
    (void)collision.stop(100);
}
} // namespace gameplay_runtime
#endif
