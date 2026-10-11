#pragma once
#include "diagnostics.h"
#include "loadout_control.h"
#include "mod_bindings.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdio>
namespace loadout_key_requests {
struct Context { std::uint64_t world=0,actor=0,graph=0,catalogue_epoch=0,view_generation=0; };
inline bool same(const Context& a,const Context& b) {
    return a.world==b.world && a.actor==b.actor && a.graph==b.graph &&
        a.catalogue_epoch==b.catalogue_epoch && a.view_generation==b.view_generation;
}
inline bool valid(const Context& c) { return c.world && c.graph && c.catalogue_epoch && c.view_generation; }
inline bool game_foreground(HWND window) {
    DWORD pid=0;
    return window && GetForegroundWindow()==window && GetWindowThreadProcessId(window,&pid)!=0 && pid==GetCurrentProcessId();
}
struct Request {
    Context context{}; std::uint64_t focus=0,sequence=0,qpc=0;
    loadout_control::Channel channel=loadout_control::Channel::Primary;
};
struct Completion {
    std::uint64_t sequence=0;
    loadout_control::Channel channel=loadout_control::Channel::Primary;
    loadout_control::Result result=loadout_control::Result::Rejected;
    bool stale=false;
    loadout_control::DesiredChoice desired{};
    std::array<loadout_control::SlotModel,loadout_control::kSlotCount> slots{};
};
inline SRWLOCK lock=SRWLOCK_INIT;
inline std::array<Request,16> requests{};
inline unsigned head=0,count=0;
inline std::array<Completion,32> completions{};
inline unsigned completion_count=0;
inline std::atomic<std::uint64_t> focus_generation{1},dropped{0};
inline std::atomic<bool> stopped{false};
// Only the worker touches edge state; only the verified game callback cycles.
inline bool baseline=false,previous_down[2]{};
inline Context previous_context{};
inline std::uint64_t sequence=0;
inline std::uint64_t binding_revision=0;
inline void invalidate() {
    baseline=false; const auto old=focus_generation.load();
    if (old==UINT64_MAX) { stopped.store(true); return; }
    focus_generation.store(old+1);
}
inline bool input_available(const Context& context,bool catalogue_available) {
    // Menu input belongs to the UI, including Options with no live player catalogue.
    return mod_bindings::menu_active.load() || mod_bindings::panel_open.load() || mod_bindings::capturing.load() ||
        (catalogue_available && valid(context));
}
inline void sample(const Context& context,bool available,HMODULE backend=nullptr) {
    const DWORD error=GetLastError();
    struct Restore { DWORD value; ~Restore() { SetLastError(value); } } restore{error};
    if (stopped.load()) return;
    HWND window=GetForegroundWindow();
    if (!input_available(context,available) || !game_foreground(window)) { mod_bindings::reset_pad_input(); if (baseline) invalidate(); return; }
    const auto pad=mod_bindings::poll_pad(backend);
    mod_bindings::poll_keys();
    if (mod_bindings::editing.load() || mod_bindings::panel_open.load() || mod_bindings::menu_active.load()) { if (baseline) invalidate(); return; }
    bool down[2]{}; const auto bindings=mod_bindings::snapshot();
    for (unsigned i=0;i<2;++i) {
        if (!game_foreground(window)) { if (baseline) invalidate(); return; }
        down[i]=(bindings.key[i] && (GetAsyncKeyState(static_cast<int>(bindings.key[i]))&0x8000)!=0) ||
            (bindings.key_alt[i] && (GetAsyncKeyState(static_cast<int>(bindings.key_alt[i]))&0x8000)!=0) || (bindings.pad[i] && (pad&bindings.pad[i]));
    }
    if (!game_foreground(window)) { if (baseline) invalidate(); return; }
    if (!baseline || !same(context,previous_context) || binding_revision!=bindings.revision) {
        if (baseline) invalidate(); if (stopped.load()) return;
        previous_context=context; previous_down[0]=down[0]; previous_down[1]=down[1]; binding_revision=bindings.revision; baseline=true;
        return; // Focus/reload baseline is never a new press.
    }
    for (unsigned i=0;i<2;++i) {
        const bool edge=down[i] && !previous_down[i]; previous_down[i]=down[i];
        if (!edge) continue;
        if (sequence==UINT64_MAX) { stopped.store(true); return; }
        Request r{}; r.context=context; r.focus=focus_generation.load(); r.sequence=++sequence;
        r.channel=static_cast<loadout_control::Channel>(i);
        if constexpr(diagnostics::enabled) { LARGE_INTEGER tick{}; QueryPerformanceCounter(&tick); r.qpc=static_cast<std::uint64_t>(tick.QuadPart); }
        if (!TryAcquireSRWLockExclusive(&lock)) { CRMOD_DIAGNOSTIC(++dropped); continue; }
        if (count==requests.size()) CRMOD_DIAGNOSTIC(++dropped); else requests[(head+count++)%requests.size()]=r;
        ReleaseSRWLockExclusive(&lock);
    }
}
inline void consume(loadout_control::Control& control,const loadout_control::Adapter& adapter,const Context& current) {
    const DWORD error=GetLastError();
    struct Restore { DWORD value; ~Restore() { SetLastError(value); } } restore{error};
    for (unsigned i=0;i<4 && !stopped.load();++i) {
        Request r{};
        if (!TryAcquireSRWLockExclusive(&lock)) return;
        if (!count) { ReleaseSRWLockExclusive(&lock); return; }
        r=requests[head]; head=(head+1)%requests.size(); --count;
        ReleaseSRWLockExclusive(&lock);
        const bool stale=r.focus!=focus_generation.load() || !same(r.context,current) || !game_foreground(GetForegroundWindow());
        const auto result=stale ? loadout_control::Result::Rejected : control.cycle(adapter,r.channel);
        if constexpr(!diagnostics::enabled) { (void)result;continue; }
        if (!TryAcquireSRWLockExclusive(&lock)) { CRMOD_DIAGNOSTIC(++dropped); continue; }
        if (completion_count==completions.size()) CRMOD_DIAGNOSTIC(++dropped);
        else {
            auto& done=completions[completion_count++];done={r.sequence,r.channel,result,stale,control.desired_choice(adapter,r.channel),{}};
            done.slots=control.menu(adapter,nullptr,0).channels[static_cast<unsigned>(r.channel)].slots;
        }
        ReleaseSRWLockExclusive(&lock);
    }
}
inline void stop() { stopped.store(true); }
inline void flush(FILE* file) {
    if constexpr(!diagnostics::enabled)return;
    if (!file || !TryAcquireSRWLockExclusive(&lock)) return;
    std::array<Completion,32> batch{}; const unsigned n=completion_count;
    for (unsigned i=0;i<n;++i) batch[i]=completions[i]; completion_count=0;
    ReleaseSRWLockExclusive(&lock);
    for (unsigned i=0;i<n;++i) {
        const auto& r=batch[i];
        std::fprintf(file,"{\"event\":\"loadout_key_cycle\",\"sequence\":\"%llu\",\"channel\":%u,\"control_result\":%u,\"stale\":%s,\"desired_assigned\":%s,\"desired_slot\":%u,\"desired_id\":%u,\"native_completion_claimed\":false,\"slots\":[{\"id\":%u,\"assigned\":%s,\"status\":%u,\"selected\":%s},{\"id\":%u,\"assigned\":%s,\"status\":%u,\"selected\":%s},{\"id\":%u,\"assigned\":%s,\"status\":%u,\"selected\":%s}]}\n",
            static_cast<unsigned long long>(r.sequence),static_cast<unsigned>(r.channel),static_cast<unsigned>(r.result),r.stale?"true":"false",r.desired.assigned?"true":"false",r.desired.slot,r.desired.id,
            r.slots[0].slot.id,r.slots[0].slot.assigned?"true":"false",static_cast<unsigned>(r.slots[0].validation.status),r.slots[0].validation.selected?"true":"false",
            r.slots[1].slot.id,r.slots[1].slot.assigned?"true":"false",static_cast<unsigned>(r.slots[1].validation.status),r.slots[1].validation.selected?"true":"false",
            r.slots[2].slot.id,r.slots[2].slot.assigned?"true":"false",static_cast<unsigned>(r.slots[2].validation.status),r.slots[2].validation.selected?"true":"false");
    }
    const auto lost=dropped.exchange(0);
    if (lost) std::fprintf(file,"{\"event\":\"loadout_key_cycle_dropped\",\"count\":%llu}\n",static_cast<unsigned long long>(lost));
}
}
