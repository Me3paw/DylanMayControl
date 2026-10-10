#pragma once
#include "held_lock_native.h"
#include "loadout_actor_resolver.h"
#include "facing_lock_native.h"
#ifdef _WIN32
namespace player_geometry_log {
using SpeedFn=float* (*)(float*,void*,void*,float*,void*,void*);
inline void* original_speed=nullptr;
inline std::atomic<bool> enabled{false};
inline std::atomic<bool> exhausted{false};
inline std::atomic<uint32_t> next_sample_ms{0};
inline const std::atomic<bool>* stopping=nullptr;
struct Rows {
    uint64_t world=0,actor=0,loco=0,cfg=0,request=0,movement=0;
    uint64_t quaternion=0,transform=0;
};
inline bool resolve(Rows& r) {
    r={};
    if (!held_lock_native::identity(r.world,r.actor)) return false;
    const auto column=[&](uint32_t hash,size_t stride,uint64_t& row) {
        return step_binding::resolve_player_column(r.world,r.actor,hash,stride,&row);
    };
    if (!column(0x654afaf6,0x310,r.loco) || !column(0x2855d24e,0x240,r.cfg) ||
        !column(0x59fd0f7a,0x30,r.request) || !column(0xf2358b43,0x80,r.movement)) return false;
    (void)column(0xc0853848,0x10,r.quaternion);
    (void)column(0x6cfbb2a9,0x20,r.transform);
    return true;
}
inline bool same(const Rows& a,const Rows& b) {
    return a.world==b.world && a.actor==b.actor && a.loco==b.loco &&
        a.cfg==b.cfg && a.request==b.request && a.movement==b.movement &&
        a.quaternion==b.quaternion && a.transform==b.transform;
}
struct Record {
    Rows rows{};
    uint64_t qpc=0,signature=0;
    uint32_t ms=0;
    bool quaternion_ok=false,transform_ok=false;
    unsigned char velocity[16]{},request[0x30]{},movement[0x80]{};
    unsigned char quaternion[16]{},transform[0x20]{},locomotion[0x310]{};
};
inline SRWLOCK lock=SRWLOCK_INIT;
inline std::array<Record,32> queue{};
inline unsigned count=0,pushed=0;
inline uint64_t last_world=0,last_actor=0,last_signature=0;
inline uint32_t last_ms=0;
inline bool have_last=false;
inline std::atomic<uint64_t> dropped{0},unqualified{0};
constexpr unsigned kBudget=4096;
inline bool active() { return enabled.load() && !exhausted.load() && stopping && !stopping->load(); }
inline uint64_t signature(const Record& r) {
    uint64_t h=0xcbf29ce484222325ull;
    const auto mix=[&](const unsigned char* p,size_t n) {
        for (size_t i=0;i<n;++i) h=(h^p[i])*0x100000001b3ull;
    };
    mix(r.velocity,sizeof r.velocity); mix(r.request,sizeof r.request);
    mix(r.movement+0x40,8);
    mix(r.quaternion,sizeof r.quaternion); mix(r.transform,sizeof r.transform);
    return h;
}
inline void push(Record r) {
    if (!TryAcquireSRWLockExclusive(&lock)) { ++dropped; return; }
    const bool new_owner=!have_last || r.rows.world!=last_world || r.rows.actor!=last_actor;
    const auto age=uint32_t(r.ms-last_ms);
    if (pushed<kBudget && (new_owner || (r.signature!=last_signature && age>=100) || age>=1000)) {
        if (count==queue.size()) ++dropped;
        else {
            queue[count++]=r; ++pushed;
            if (pushed==kBudget) exhausted.store(true);
            have_last=true; last_world=r.rows.world; last_actor=r.rows.actor;
            last_signature=r.signature; last_ms=r.ms;
        }
    }
    ReleaseSRWLockExclusive(&lock);
}
inline float* speed_hook(float* out,void* loco,void* cfg,float* request,void* movement,void* curve) {
    const DWORD incoming=GetLastError();
    const auto ms=static_cast<uint32_t>(GetTickCount64());
    const auto next=next_sample_ms.load();
    const bool due=!next || static_cast<int32_t>(ms-next)>=0;
    Rows before{};
    const bool qualified=active() && due && resolve(before) &&
        before.loco==reinterpret_cast<uintptr_t>(loco) && before.cfg==reinterpret_cast<uintptr_t>(cfg) &&
        before.request==reinterpret_cast<uintptr_t>(request) && before.movement==reinterpret_cast<uintptr_t>(movement);
    SetLastError(incoming);
    float* result=reinterpret_cast<SpeedFn>(original_speed)(out,loco,cfg,request,movement,curve);
    const DWORD outgoing=GetLastError();
    if (qualified && active()) {
        Record r{}; r.rows=before; auto& e=step_binding::env();
        const auto cp=[&](uint64_t p,void* dst,size_t n) { return combat_native::copy_checked(e.copy,dst,p,n); };
        Rows after{};
        if (result==out && cp(reinterpret_cast<uintptr_t>(result),r.velocity,16) &&
            cp(before.request,r.request,sizeof r.request) && cp(before.movement,r.movement,sizeof r.movement) &&
            cp(before.loco,r.locomotion,sizeof r.locomotion) && resolve(after) && same(before,after)) {
            r.quaternion_ok=before.quaternion && cp(before.quaternion,r.quaternion,sizeof r.quaternion);
            r.transform_ok=before.transform && cp(before.transform,r.transform,sizeof r.transform);
            Rows final{};
            if (resolve(final) && same(before,final) && active()) {
                LARGE_INTEGER tick{}; QueryPerformanceCounter(&tick);
                r.qpc=static_cast<uint64_t>(tick.QuadPart); r.ms=static_cast<uint32_t>(GetTickCount64());
                r.signature=signature(r); push(r);
                next_sample_ms.store(r.ms+100);
            } else ++unqualified;
        } else ++unqualified;
    } else if (active() && due) ++unqualified;
    SetLastError(outgoing); return result;
}
inline bool arm(bool verified,const std::atomic<bool>& stop) {
    if (!verified || !original_speed || enabled.load() || stop.load()) return false;
    stopping=&stop; enabled.store(true);
    if (stop.load()) enabled.store(false); return enabled.load();
}
inline void stop() { enabled.store(false); }
inline void hex(FILE* file,const unsigned char* p,size_t n) {
    constexpr char digits[]="0123456789abcdef";
    for (size_t i=0;i<n;++i) { std::fputc(digits[p[i]>>4],file); std::fputc(digits[p[i]&15],file); }
}
inline void flush(FILE* file) {
    if (!file || !TryAcquireSRWLockExclusive(&lock)) return;
    std::array<Record,32> batch{}; const unsigned n=count;
    std::memcpy(batch.data(),queue.data(),n*sizeof(Record)); count=0;
    ReleaseSRWLockExclusive(&lock);
    for (unsigned i=0;i<n;++i) {
        const auto& r=batch[i];
        std::fprintf(file,"{\"event\":\"player_geometry\",\"qpc\":%llu,\"ms\":%u,\"world\":%llu,\"actor\":\"%llu\","
            "\"locomotion_row\":%llu,\"config_row\":%llu,\"movement_request_row\":%llu,\"movement_row\":%llu,"
            "\"quaternion_ok\":%s,\"transform_ok\":%s,\"character_facing_verified\":false,\"coordinate_space_verified\":false,\"velocity_hex\":\"",
            static_cast<unsigned long long>(r.qpc),r.ms,static_cast<unsigned long long>(r.rows.world),static_cast<unsigned long long>(r.rows.actor),
            static_cast<unsigned long long>(r.rows.loco),static_cast<unsigned long long>(r.rows.cfg),static_cast<unsigned long long>(r.rows.request),
            static_cast<unsigned long long>(r.rows.movement),r.quaternion_ok?"true":"false",r.transform_ok?"true":"false");
        hex(file,r.velocity,sizeof r.velocity);
        std::fputs("\",\"movement_request_hex\":\"",file); hex(file,r.request,sizeof r.request);
        std::fputs("\",\"raw_movement_hex\":\"",file); hex(file,r.movement,sizeof r.movement);
        std::fputs("\",\"controller_quaternion_hex\":\"",file); hex(file,r.quaternion,sizeof r.quaternion);
        std::fputs("\",\"transform_reference_hex\":\"",file); hex(file,r.transform,sizeof r.transform);
        std::fputs("\",\"locomotion_hex\":\"",file); hex(file,r.locomotion,sizeof r.locomotion);
        std::fputs("\"}\n",file);
    }
    static bool status_written=false;
    if (!status_written) {
        std::fprintf(file,"{\"event\":\"player_geometry_policy\",\"record_limit\":4096,\"change_min_ms\":100,\"heartbeat_ms\":1000,\"writes\":false,\"seam\":\"native_speed_result\"}\n");
        status_written=true;
    }
    const auto lost=dropped.exchange(0),unknown=unqualified.exchange(0);
    if (lost || unknown) std::fprintf(file,"{\"event\":\"player_geometry_summary\",\"queue_dropped\":%llu,\"unqualified\":%llu}\n",static_cast<unsigned long long>(lost),static_cast<unsigned long long>(unknown));
}
}
#endif
