#pragma once
#include "loadout_control.h"
#include <cstdint>

namespace loadout_queue_audio {
constexpr std::uint32_t fnv(const char* text) {
    std::uint32_t value=2166136261u;
    while(*text)value=(value*16777619u)^static_cast<unsigned char>(*text++);
    return value;
}
inline constexpr std::uint32_t bank_id=fnv("crmod_weapon_queue_cues");
inline constexpr std::uint32_t events[]={fnv("crmod_queue_strike"),fnv("crmod_queue_cleave"),
    fnv("crmod_queue_glaive"),fnv("crmod_queue_crush"),fnv("crmod_queue_drill"),fnv("crmod_queue_extend")};
inline unsigned cue_index(loadout_control::Channel channel,std::uint32_t form_id) {
    // Native form identities verified by the accepted primary/secondary captures.
    constexpr std::uint32_t forms[]={698021297u,1737076817u,3413371114u,3298993364u,444122270u,2860349769u};
    for(unsigned i=0;i<6;++i)
        if(channel==(i<3?loadout_control::Channel::Primary:loadout_control::Channel::Secondary) &&
           form_id==forms[i])return i;
    return 6;
}
}

#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include "loadout_audio_trace.h"
namespace loadout_queue_audio {
// Native nl_audio_global_post_event uses this existing registered 2D object.
inline constexpr std::uint64_t global_object=0x100000001ull;
using Initialized=bool (*)();
using Load=int (*)(const void*,std::uint32_t,std::uint32_t*);
using Post=std::uint32_t (*)(std::uint32_t,std::uint64_t,std::uint32_t,void*,void*,std::uint32_t,void*,std::uint32_t);
using Execute=int (*)(std::uint32_t,int,std::uint64_t,int,int,std::uint32_t);
using Unload=int (*)(std::uint32_t,const void*,std::uint32_t);
using SourcePosition=int (*)(std::uint32_t,std::int32_t*,bool);
using PlayingEvent=std::uint32_t (*)(std::uint32_t);
inline Initialized initialized=nullptr;
inline Load load=nullptr;
inline Post post=nullptr;
inline Execute execute=nullptr;
inline Unload unload=nullptr;
inline SourcePosition source_position=nullptr;
inline PlayingEvent playing_event=nullptr;
// AK_EnableGetSourcePlayPosition, corroborated by the pinned game's source-tracking bit tests.
inline constexpr std::uint32_t source_position_flag=0x100000;
struct Probe { std::uint32_t playing=0;unsigned cue=0,stage=0;ULONGLONG born=0;bool position_enabled=false; };
inline std::array<Probe,16> probes{};
inline unsigned next_probe=0;
inline bool self_check_requested=false,self_check_done=false;
inline void configure_self_check() {
    if constexpr(!CRMOD_AUDIO_TRACE)return;
    const DWORD saved=GetLastError();wchar_t value[4]{};
    self_check_requested=GetEnvironmentVariableW(L"CRMOD_AUDIO_SELF_CHECK",value,4)==1 && value[0]==L'1';
    SetLastError(saved);
}
inline const std::atomic<bool>* shared_stopping=nullptr;
inline std::atomic<bool> ready{false};
inline SRWLOCK lock=SRWLOCK_INIT;
inline bool attempted=false,loaded=false;
inline unsigned attempts=0;
inline ULONGLONG next_attempt=0;
inline std::atomic<int> load_result{0};
inline std::atomic<std::uint32_t> last_playing_id{0};
template<class T> inline T exported(HMODULE game,const char* name) {
    return reinterpret_cast<T>(GetProcAddress(game,name));
}
// Worker only: Init.bnk can arrive after SoundEngine::IsInitialized becomes true.
inline void prepare(HMODULE module,const std::atomic<bool>& stopping) {
    if(attempted || stopping.load())return;
    const DWORD saved=GetLastError();
    if(GetTickCount64()<next_attempt){SetLastError(saved);return;}
    const auto game=GetModuleHandleW(nullptr);
    initialized=exported<Initialized>(game,"?IsInitialized@SoundEngine@AK@@YA_NXZ");
    if(!initialized || !initialized()){SetLastError(saved);return;}
    attempted=true;shared_stopping=&stopping;
    load=exported<Load>(game,"?LoadBankMemoryCopy@SoundEngine@AK@@YA?AW4AKRESULT@@PEBXIAEAI@Z");
    post=exported<Post>(game,"?PostEvent@SoundEngine@AK@@YAII_KIP6AXW4AkCallbackType@@PEAUAkCallbackInfo@@@ZPEAXIPEAUAkExternalSourceInfo@@I@Z");
    execute=exported<Execute>(game,"?ExecuteActionOnEvent@SoundEngine@AK@@YA?AW4AKRESULT@@IW4AkActionOnEventType@12@_KHW4AkCurveInterpolation@@I@Z");
    unload=exported<Unload>(game,"?UnloadBank@SoundEngine@AK@@YA?AW4AKRESULT@@IPEBXI@Z");
#if CRMOD_AUDIO_TRACE
    source_position=exported<SourcePosition>(game,"?GetSourcePlayPosition@SoundEngine@AK@@YA?AW4AKRESULT@@IPEAH_N@Z");
    playing_event=exported<PlayingEvent>(game,"?GetEventIDFromPlayingID@Query@SoundEngine@AK@@YAII@Z");
#endif
    const auto resource=FindResourceW(module,MAKEINTRESOURCEW(401),MAKEINTRESOURCEW(10));
    const auto size=resource?SizeofResource(module,resource):0;
    const auto data=resource?LockResource(LoadResource(module,resource)):nullptr;
    if(load && post && execute && unload && data && size>64 && size<=2*1024*1024) {
        AcquireSRWLockExclusive(&lock);
        if(!stopping.load()) {
            std::uint32_t actual=0;
            const int result=load(data,size,&actual);load_result.store(result);++attempts;
            loadout_audio_trace::row("audio_bank_load",",\"attempt\":%u,\"result\":%d,\"bank\":%u,\"expected_bank\":%u,\"source_position_query\":%u,\"playing_event_query\":%u",attempts,result,actual,bank_id,source_position?1u:0u,playing_event?1u:0u);
            loaded=result==1 && actual==bank_id;
            if(loaded && !stopping.load())ready.store(true);
            if(result==92 && attempts<60) { // AK_InitBankNotLoaded: wait for the game's own init bank.
                attempted=false;next_attempt=GetTickCount64()+500;
            }
        }
        ReleaseSRWLockExclusive(&lock);
    }
    SetLastError(saved);
}
inline void play(unsigned index) {
    const DWORD saved=GetLastError();
    const char* reason=index>=6?"unknown_form":!ready.load()?"bank_not_ready":!shared_stopping || shared_stopping->load()?"stopping":"audio_lock_busy";
    std::uint32_t playing=0;
    const std::uint32_t flags=CRMOD_AUDIO_TRACE && source_position && loadout_audio_trace::file && !loadout_audio_trace::limited ? source_position_flag : 0;
    if(index<6 && ready.load() && shared_stopping && !shared_stopping->load() &&
       TryAcquireSRWLockShared(&lock)) {
        reason="engine_not_initialized";
        if(ready.load() && !shared_stopping->load() && initialized()) {
            playing=post(events[index],global_object,flags,nullptr,nullptr,0,nullptr,0);
            last_playing_id.store(playing);reason=playing?"posted":"post_failed";
        }
        ReleaseSRWLockShared(&lock);
    }
    loadout_audio_trace::row("audio_play",",\"cue\":%u,\"event_id\":%u,\"ready\":%u,\"load_result\":%d,\"playing_id\":%u,\"position_tracking\":%u,\"reason\":\"%s\"",index,index<6?events[index]:0,ready.load()?1u:0u,load_result.load(),playing,flags?1u:0u,reason);
#if CRMOD_AUDIO_TRACE
    if(playing && loadout_audio_trace::file && !loadout_audio_trace::limited) {
        auto& probe=probes[next_probe];next_probe=(next_probe+1)%probes.size();
        if(probe.playing)loadout_audio_trace::row("audio_probe_evicted",",\"playing_id\":%u",probe.playing);
        probe={playing,index,0,GetTickCount64(),flags!=0};
    }
#endif
    SetLastError(saved);
}
inline void run_self_check() {
    if constexpr(!CRMOD_AUDIO_TRACE)return;
    if(!self_check_requested || self_check_done || !ready.load())return;
    const DWORD saved=GetLastError();self_check_done=true;
    loadout_audio_trace::row("audio_self_check",",\"cues\":6,\"simultaneous\":true,\"player_input\":false");
    for(unsigned cue=0;cue<6;++cue)play(cue);
    SetLastError(saved);
}
inline void probe_progress() {
    if constexpr(!CRMOD_AUDIO_TRACE)return;
    if(!loadout_audio_trace::file || loadout_audio_trace::limited || !ready.load() ||
       !shared_stopping || shared_stopping->load())return;
    const DWORD saved=GetLastError();
    constexpr unsigned delays[]={0,30,150,400,800,1800};
    if(TryAcquireSRWLockShared(&lock)) {
        if(ready.load() && !shared_stopping->load() && initialized && initialized())for(auto& p:probes) {
            const auto age=GetTickCount64()-p.born;
            if(!p.playing || age<delays[p.stage])continue;
            std::int32_t position=-1;
            const int result=p.position_enabled && source_position ? source_position(p.playing,&position,false) : -1;
            const auto event=playing_event?playing_event(p.playing):0;
            loadout_audio_trace::row("audio_source_progress",",\"cue\":%u,\"playing_id\":%u,\"stage\":%u,\"age_ms\":%llu,\"query_result\":%d,\"position_ms\":%d,\"active_event_id\":%u",p.cue,p.playing,p.stage,age,result,position,event);
            if(++p.stage==6)p={};
        }
        ReleaseSRWLockShared(&lock);
    }
    SetLastError(saved);
}
inline void stop() {
    const DWORD saved=GetLastError();ready.store(false);
    AcquireSRWLockExclusive(&lock);
    if(loaded && initialized && initialized()) {
        for(auto event:events)execute(event,0,global_object,0,4,0);
        unload(bank_id,nullptr,0);loaded=false;
    }
    ReleaseSRWLockExclusive(&lock);SetLastError(saved);
}
}
#endif
