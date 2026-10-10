#pragma once
#include "loadout_actor_resolver.h"
#include "loadout_player_catalogue.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
namespace loadout_lua_activation {
constexpr std::uint32_t kEvent=0xe7d001e6;
constexpr unsigned kChainLimit=32;
using Consumer=void (*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
using Lookup=std::uint32_t (*)(std::uintptr_t,std::uint64_t);
using Getter=std::uintptr_t (*)(std::uintptr_t,std::uint32_t);
using Decoder=int (*)(std::uintptr_t,std::uintptr_t,std::uint32_t);
using Pcall=int (*)(std::uintptr_t,int,int,int);
inline void* originals[5]{};
inline std::uintptr_t verified_base=0;
inline const std::atomic<bool>* shared_stopping=nullptr;
inline std::atomic<bool> enabled{false},stopped{false};
inline bool active() { return enabled.load() && !stopped.load() && shared_stopping && !shared_stopping->load(); }
template<class T> inline bool field(std::uintptr_t p,std::uint64_t offset,T& out) { return loadout_actor_resolver::field(p,offset,out); }
struct NativeRecord {
    std::uint32_t key=0;
    std::int32_t reference=0;
    std::uint32_t next=UINT32_MAX;
    std::uint8_t unknown[12]{};
    std::uint64_t owner=0;
};
static_assert(sizeof(NativeRecord)==0x20);
static_assert(offsetof(NativeRecord,owner)==0x18);
struct Entry { std::uint32_t index=UINT32_MAX; NativeRecord record{}; };
struct Chain {
    std::array<Entry,kChainLimit> entries{};
    unsigned count=0,seen=0,nonzero=0,succeeded=0;
    bool valid=false,argument_seen=false,finished=false;
    std::uint32_t id=0,packed=0;
    std::uint64_t watch_token=0;
};
struct Pending {
    bool valid=false,matched=false;
    Entry entry{};
    std::uint32_t id=0,packed=0;
    std::uint64_t serial=0;
};
struct Frame {
    std::uintptr_t state=0,events=0,registry=0,L=0,world=0;
    std::uint64_t actor=0,serial=0;
    bool in_pcall=false,current_valid=false,current_matched=false;
    Entry current{}; Chain chain{}; Pending pending{};
};
inline thread_local Frame* current_frame=nullptr;
// ponytail: one primary flight; per-channel watches follow proven secondary execution.
struct Watch {
    bool valid=false,dispatched=false,failed=false,notification_missing=false;
    std::uint64_t token=0,world=0,actor=0,graph=0,events=0;
    std::uint32_t id=0,callbacks=0;
};
inline SRWLOCK watch_lock=SRWLOCK_INIT;
inline Watch watch{};
inline std::uint64_t next_token=0;
inline std::uint64_t begin(std::uint64_t world,std::uint64_t actor,std::uint64_t graph,std::uint64_t events,std::uint32_t id) {
    const DWORD error=GetLastError(); std::uint64_t token=0;
    if (active() && world && graph && events && id && TryAcquireSRWLockExclusive(&watch_lock)) {
        if (next_token!=UINT64_MAX) { token=++next_token; watch={true,false,false,false,token,world,actor,graph,events,id,0}; }
        ReleaseSRWLockExclusive(&watch_lock);
    }
    SetLastError(error); return token;
}
inline void cancel(std::uint64_t token) {
    const DWORD error=GetLastError();
    if (TryAcquireSRWLockExclusive(&watch_lock)) { if (watch.token==token) watch.valid=false; ReleaseSRWLockExclusive(&watch_lock); }
    SetLastError(error);
}
inline bool observed(std::uint64_t token,std::uint64_t world,std::uint64_t actor,std::uint64_t graph,std::uint64_t events,std::uint32_t id) {
    const DWORD error=GetLastError(); bool result=false;
    if (active() && token && TryAcquireSRWLockShared(&watch_lock)) {
        result=watch.valid && watch.dispatched && watch.callbacks && watch.token==token && watch.world==world &&
            watch.actor==actor && watch.graph==graph && watch.events==events && watch.id==id;
        ReleaseSRWLockShared(&watch_lock);
    }
    SetLastError(error); return result;
}
inline bool failed(std::uint64_t token) {
    const DWORD error=GetLastError(); bool result=false;
    if (active() && token && TryAcquireSRWLockShared(&watch_lock)) {
        result=watch.valid && watch.token==token && watch.failed;
        ReleaseSRWLockShared(&watch_lock);
    }
    SetLastError(error); return result;
}
// This build can consume the native notification without any Lua listener.
// The native producer still updates the graph/preset; the bridge must check
// that selected preset separately before acknowledging the switch.
inline bool notification_consumed_without_handler(std::uint64_t token,std::uint64_t world,
        std::uint64_t actor,std::uint64_t graph,std::uint64_t events,std::uint32_t id) {
    const DWORD error=GetLastError(); bool result=false;
    if (active() && token && TryAcquireSRWLockShared(&watch_lock)) {
        result=watch.valid && !watch.failed && watch.notification_missing && watch.token==token &&
            watch.world==world && watch.actor==actor && watch.graph==graph && watch.events==events && watch.id==id;
        ReleaseSRWLockShared(&watch_lock);
    }
    SetLastError(error); return result;
}
inline void fail(const Frame& f,std::uint64_t token) {
    if (!token || !TryAcquireSRWLockExclusive(&watch_lock)) return;
    if (watch.valid && watch.token==token && watch.world==f.world && watch.events==f.events && watch.actor==f.actor)
        watch.failed=true;
    ReleaseSRWLockExclusive(&watch_lock);
}
inline std::atomic<std::uint64_t> consumer_calls{0},consumer_frames{0},world_calls{0},event_lookups{0},
    missing_handlers{0},decoded_arguments{0},decode_rejections{0},protected_calls{0};
inline std::atomic<std::uint64_t> last_world{0},last_events{0},last_registry{0};
inline std::uint64_t matching_watch(const Frame& f,std::uint32_t id) {
    std::uint64_t token=0;
    if (TryAcquireSRWLockShared(&watch_lock)) {
        if (watch.valid && watch.world==f.world && watch.actor==f.actor && watch.events==f.events && watch.id==id) token=watch.token;
        ReleaseSRWLockShared(&watch_lock);
    }
    return token;
}
inline bool live(const Frame& f) {
    std::uint64_t L=0,R=0,ctx=0,W=0; loadout_actor_resolver::Location actor{};
    return active() && current_frame==&f && field(f.state,8,L) && L==f.L &&
        field(f.events,8,R) && R==f.registry && field(f.L,0x78,ctx) && ctx &&
        loadout_actor_resolver::copy(static_cast<std::uintptr_t>(ctx),&W,8) && W==f.world &&
        loadout_player_catalogue::observed_player_actor.load()==f.actor && loadout_actor_resolver::locate(f.world,f.actor,actor);
}
inline bool read_record(const Frame& f,std::uint32_t index,NativeRecord& out,std::uintptr_t* address=nullptr) {
    std::uint64_t base=0; std::uintptr_t p=0; NativeRecord temporary{};
    if (!field(f.registry,0x60,base) || !loadout_attack_probe::row_address(base,index&0xffffffu,0x20,p) ||
        !loadout_actor_resolver::copy(p,&temporary,sizeof(temporary))) return false;
    out=temporary; if (address) *address=p; return true;
}
inline void build_chain(Frame& f,std::uint32_t head) {
    f.chain={}; f.pending={}; f.current_valid=false; if (head==UINT32_MAX) return;
    auto index=head;
    for (unsigned n=0;n<kChainLimit;++n) {
        for (unsigned i=0;i<n;++i) if ((f.chain.entries[i].index&0xffffffu)==(index&0xffffffu)) return;
        NativeRecord record{}; if (!read_record(f,index,record)) return;
        f.chain.entries[n]={index,record}; f.chain.count=n+1; if (record.reference) ++f.chain.nonzero;
        if (record.next==UINT32_MAX) { f.chain.valid=live(f); return; }
        index=record.next;
    }
}
inline void finish_chain(Frame& f) {
    auto& c=f.chain;
    if (c.finished || !c.valid || !c.nonzero || c.seen!=c.count || c.succeeded!=c.nonzero ||
        !c.argument_seen || !c.watch_token || !live(f)) return;
    if (!TryAcquireSRWLockExclusive(&watch_lock)) return;
    if (watch.valid && watch.token==c.watch_token && watch.world==f.world && watch.actor==f.actor &&
        watch.events==f.events && watch.id==c.id) { watch.dispatched=true; watch.callbacks=c.nonzero; c.finished=true; }
    ReleaseSRWLockExclusive(&watch_lock);
}
struct Argument { bool valid=false; std::uint32_t id=0; };
inline Argument argument(const Frame& f,std::uintptr_t arena,std::uintptr_t L,std::uint32_t packed) {
    Argument result{}; std::uintptr_t expected=0,p=0;
    if (!live(f) || L!=f.L || !loadout_attack_probe::add(f.events,0x1858,expected) || arena!=expected) return result;
    const auto page=packed>>16,offset=packed&0xffffu; if (page>=256 || offset>0xfffau) return result;
    std::uint64_t page_pointer=0; unsigned char bytes[6]{};
    if (!field(arena,static_cast<std::uint64_t>(page)*8,page_pointer) ||
        !loadout_attack_probe::add(static_cast<std::uintptr_t>(page_pointer),offset,p) ||
        !loadout_actor_resolver::copy(p,bytes,sizeof(bytes)) || bytes[0]!=2 || bytes[5]!=0x0c) return result;
    std::uint32_t id=0; std::memcpy(&id,bytes+1,4);
    std::uint64_t count=0; if (!field(f.events,0x1048,count) || !count || count>4096) return result;
    for (std::uint64_t i=0;i<count;++i) {
        std::uint64_t messages=0; std::uint32_t message[3]{};
        if (!field(f.events,0x848+(i/0x1555)*8,messages) ||
            !loadout_attack_probe::row_address(messages,i%0x1555,12,p) ||
            !loadout_actor_resolver::copy(p,message,sizeof(message))) return result;
        if (message[0]==kEvent && message[1]==packed && message[2]==UINT32_MAX) { result={id!=0,id}; return result; }
    }
    return result;
}
struct Log {
    std::uint64_t qpc=0,world=0,actor=0,events=0,token=0; Entry callback{};
    std::uint32_t id=0,packed=0; int result=0; bool matched=false,callback_owner_valid=false;
};
inline SRWLOCK log_lock=SRWLOCK_INIT;
inline std::array<Log,64> logs{};
inline unsigned log_count=0,logged=0;
inline std::atomic<std::uint64_t> dropped{0};
inline void record(Log row) {
    if (!TryAcquireSRWLockExclusive(&log_lock)) { ++dropped; return; }
    if (logged<1024) {
        if (log_count==logs.size()) ++dropped;
        else { LARGE_INTEGER tick{}; QueryPerformanceCounter(&tick); row.qpc=static_cast<std::uint64_t>(tick.QuadPart);
            logs[log_count++]=row; ++logged; }
    }
    ReleaseSRWLockExclusive(&log_lock);
}
inline void consumer_hook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d,std::uintptr_t e,std::uintptr_t g,std::uintptr_t h) {
    const DWORD incoming=GetLastError(); Frame f{}; Frame* previous=current_frame; bool entered=false;
    if (active()) {
        ++consumer_calls;
        std::uint64_t L=0,R=0,ctx=0,W=0;
        if (field(a,8,L) && L && field(b,8,R) && R && field(static_cast<std::uintptr_t>(L),0x78,ctx) &&
            ctx && loadout_actor_resolver::copy(static_cast<std::uintptr_t>(ctx),&W,8) && W) {
            f.state=a; f.events=b; f.registry=R; f.L=L; f.world=W;
            f.actor=loadout_player_catalogue::observed_player_actor.load(); current_frame=&f; entered=true;
            ++consumer_frames; if (live(f)) ++world_calls;
            last_world.store(W); last_events.store(b); last_registry.store(R);
        }
    }
    SetLastError(incoming); reinterpret_cast<Consumer>(originals[0])(a,b,c,d,e,g,h);
    const DWORD outgoing=GetLastError(); if (entered) { finish_chain(f); current_frame=previous; } SetLastError(outgoing);
}
inline std::uint32_t lookup_hook(std::uintptr_t R,std::uint64_t key) {
    const DWORD incoming=GetLastError(); SetLastError(incoming);
    const auto result=reinterpret_cast<Lookup>(originals[1])(R,key); const DWORD outgoing=GetLastError(); auto* f=current_frame;
    if (f && !f->in_pcall && R==f->registry && live(*f)) {
        ++f->serial; f->chain={}; f->pending={}; f->current_valid=false; if (key==kEvent) build_chain(*f,result);
        if (key==kEvent) {
            ++event_lookups;
            if (result==UINT32_MAX) {
                ++missing_handlers;
                if (TryAcquireSRWLockExclusive(&watch_lock)) {
                    if (watch.valid && watch.world==f->world && watch.events==f->events && watch.actor==f->actor)
                        watch.notification_missing=true;
                    ReleaseSRWLockExclusive(&watch_lock);
                }
            }
        }
    }
    SetLastError(outgoing); return result;
}
inline std::uintptr_t getter_hook(std::uintptr_t R,std::uint32_t index) {
    const DWORD incoming=GetLastError(); SetLastError(incoming);
    const auto result=reinterpret_cast<Getter>(originals[2])(R,index); const DWORD outgoing=GetLastError(); auto* f=current_frame;
    if (f && !f->in_pcall && R==f->registry && f->chain.count && live(*f)) {
        f->pending={}; f->current_valid=false; f->current_matched=false;
        NativeRecord record{}; std::uintptr_t address=0;
        if (result && read_record(*f,index,record,&address) && address==result) {
            f->current={index,record}; f->current_valid=true; auto& chain=f->chain;
            if (chain.seen<chain.count) {
                const auto& expected=chain.entries[chain.seen++];
                f->current_matched=index==expected.index && std::memcmp(&record,&expected.record,sizeof(record))==0;
            }
            if (!f->current_matched) chain.valid=false; if (!record.reference) finish_chain(*f);
        } else f->chain.valid=false;
    }
    SetLastError(outgoing); return result;
}
inline int decoder_hook(std::uintptr_t arena,std::uintptr_t L,std::uint32_t packed) {
    const DWORD incoming=GetLastError(); auto* f=current_frame; Argument value{}; std::uint64_t serial=0;
    if (f && !f->in_pcall && f->current_valid && f->current.record.reference) { serial=f->serial; value=argument(*f,arena,L,packed); }
    if (serial) { if (value.valid) ++decoded_arguments; else ++decode_rejections; }
    SetLastError(incoming); const int result=reinterpret_cast<Decoder>(originals[3])(arena,L,packed); const DWORD outgoing=GetLastError();
    if (f && current_frame==f && serial==f->serial && value.valid && result==1 && live(*f)) {
        auto& chain=f->chain;
        if (!chain.argument_seen) {
            chain.argument_seen=true; chain.id=value.id; chain.packed=packed; chain.watch_token=matching_watch(*f,value.id);
        } else if (chain.id!=value.id || chain.packed!=packed) chain.valid=false;
        f->pending={true,f->current_matched,f->current,value.id,packed,serial};
    } else if (f && serial && current_frame==f && serial==f->serial) f->chain.valid=false;
    SetLastError(outgoing); return result;
}
inline int pcall_hook(std::uintptr_t L,int nargs,int nresults,int errfunc) {
    const DWORD incoming=GetLastError(); auto* f=current_frame; Pending pending{}; Log row{}; bool captured=false;
    if (f && !f->in_pcall && f->pending.valid) {
        pending=f->pending; f->pending={}; captured=L==f->L && nargs==1 && nresults==0 && errfunc==1 && live(*f);
        if (captured) {
            row.world=f->world; row.actor=f->actor; row.events=f->events; row.token=f->chain.watch_token;
            row.callback=pending.entry; row.id=pending.id; row.packed=pending.packed; row.matched=pending.matched;
            loadout_actor_resolver::Location owner{};
            row.callback_owner_valid=loadout_actor_resolver::locate(f->world,pending.entry.record.owner,owner); f->in_pcall=true;
        } else f->chain.valid=false;
    }
    SetLastError(incoming); const int result=reinterpret_cast<Pcall>(originals[4])(L,nargs,nresults,errfunc); const DWORD outgoing=GetLastError();
    if (captured && current_frame==f) {
        ++protected_calls;
        f->in_pcall=false; row.result=result;
        if (result!=0 && pending.matched) fail(*f,row.token);
        if (f->serial!=pending.serial || !pending.matched || result!=0 || !live(*f)) f->chain.valid=false;
        else ++f->chain.succeeded;
        record(row); finish_chain(*f);
    }
    SetLastError(outgoing); return result;
}
struct Hook { std::uint32_t rva; const char* pin; void* detour; void** original; };
inline std::array<Hook,5> hook_specs() {
    return {{
        {0x1a02d70,"488bc4488958184889702048895010488948085741544155415641574881ec90",reinterpret_cast<void*>(&consumer_hook),&originals[0]},
        {0x1a084e0,"4889542410534883ec3049bab301000000010000440fb6ca488bd948b825232284e49cf2cb",reinterpret_cast<void*>(&lookup_hook),&originals[1]},
        {0x1a08910,"81e2ffffff0048c1e20548035160488bc2c3",reinterpret_cast<void*>(&getter_hook),&originals[2]},
        {0x1a014b0,"40564883ec60488bf24183f8ff750833c04883c4605ec34889",reinterpret_cast<void*>(&decoder_hook),&originals[3]},
        {0x2c4fc90,"48895c24084889742410574883ec404533d2418bf0448bda48",reinterpret_cast<void*>(&pcall_hook),&originals[4]}
    }};
}
// Fixed RVAs guarded by exact executable hash and loaded bytes; no uniqueness claim for registry helpers.
inline bool verify(std::uintptr_t base,bool exact_executable_verified) {
    if (!base || !exact_executable_verified || enabled.load() || stopped.load()) return false;
    for (const auto& hook:hook_specs()) {
        std::uintptr_t address=0; unsigned char bytes[64]{}; const auto size=std::strlen(hook.pin)/2; MEMORY_BASIC_INFORMATION memory{};
        if (!size || size>sizeof(bytes) || !loadout_attack_probe::add(base,hook.rva,address) ||
            !VirtualQuery(reinterpret_cast<void*>(address),&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
            reinterpret_cast<std::uintptr_t>(memory.AllocationBase)!=base || (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
            !(memory.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
            !loadout_actor_resolver::copy(address,bytes,size)) return false;
        const auto nibble=[](char ch) { return static_cast<unsigned>(ch<='9' ? ch-'0' : ch-'a'+10); };
        for (std::size_t i=0;i<size;++i) if (bytes[i]!=nibble(hook.pin[i*2])*16+nibble(hook.pin[i*2+1])) return false;
    }
    verified_base=base; return true;
}
inline bool arm(const std::atomic<bool>& stopping,bool pinned) {
    if (!verified_base || !pinned || enabled.load() || stopped.load() || stopping.load()) return false;
    for (const auto original:originals) if (!original) return false;
    shared_stopping=&stopping; enabled.store(true); if (stopping.load()) enabled.store(false); return enabled.load();
}
inline void stop() { stopped.store(true); enabled.store(false); }
inline void flush(FILE* file) {
    if (!file || !TryAcquireSRWLockExclusive(&log_lock)) return;
    std::array<Log,64> batch{}; const unsigned count=log_count;
    std::memcpy(batch.data(),logs.data(),count*sizeof(Log)); log_count=0; ReleaseSRWLockExclusive(&log_lock);
    for (unsigned i=0;i<count;++i) {
        const auto& r=batch[i];
        std::fprintf(file,"{\"event\":\"loadout_lua_callback\",\"qpc\":%llu,\"event_key\":%u,\"record_key\":%u,\"record_index\":%u,"
            "\"lua_reference\":%d,\"callback_owner\":\"%llu\",\"callback_owner_valid\":%s,\"world\":%llu,\"actor\":\"%llu\","
            "\"lua_arena_key\":%llu,\"watch_token\":\"%llu\",\"id\":%u,\"argument_offset\":%u,\"pcall_result\":%d,"
            "\"chain_match\":%s,\"active_weapon_verified\":false,\"causal_attribution\":\"not_claimed\",\"effect_attribution\":\"not_claimed\"}\n",
            static_cast<unsigned long long>(r.qpc),kEvent,r.callback.record.key,r.callback.index,r.callback.record.reference,
            static_cast<unsigned long long>(r.callback.record.owner),r.callback_owner_valid?"true":"false",
            static_cast<unsigned long long>(r.world),static_cast<unsigned long long>(r.actor),static_cast<unsigned long long>(r.events),
            static_cast<unsigned long long>(r.token),r.id,r.packed,r.result,r.matched?"true":"false");
    }
    const auto lost=dropped.exchange(0);
    if (lost) std::fprintf(file,"{\"event\":\"loadout_lua_callback_dropped\",\"count\":%llu}\n",static_cast<unsigned long long>(lost));
    static ULONGLONG next_summary=0; static unsigned summaries=0;
    bool watching=false;
    if (TryAcquireSRWLockShared(&watch_lock)) { watching=watch.valid; ReleaseSRWLockShared(&watch_lock); }
    const auto now=GetTickCount64();
    if (summaries<128 && (watching || summaries<8) && now>=next_summary) {
        next_summary=now+1000; ++summaries;
        const auto n=[](const auto& value) { return static_cast<unsigned long long>(value.load()); };
        std::fprintf(file,"{\"event\":\"loadout_lua_dispatch_status\",\"consumer_calls\":%llu,\"consumer_frames\":%llu,\"current_world_calls\":%llu,"
            "\"event_lookups\":%llu,\"missing_handlers\":%llu,\"decoded_arguments\":%llu,\"decode_rejections\":%llu,\"protected_calls\":%llu,"
            "\"last_world\":%llu,\"last_events\":%llu,\"last_registry\":%llu}\n",
            n(consumer_calls),n(consumer_frames),n(world_calls),n(event_lookups),n(missing_handlers),n(decoded_arguments),n(decode_rejections),n(protected_calls),
            n(last_world),n(last_events),n(last_registry));
    }
}
}
#endif
