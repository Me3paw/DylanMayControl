#pragma once
#include "loadout_control.h"
#include <cstdint>
#include <cstring>

namespace loadout_queue_audio {
constexpr std::uint32_t fnv(const char* text) {
    std::uint32_t value=2166136261u;
    while(*text)value=(value*16777619u)^static_cast<unsigned char>(*text++);
    return value;
}
inline constexpr std::uint32_t bank_id=fnv("crmod_weapon_queue_cues");
inline constexpr std::uint32_t events[]={fnv("crmod_queue_strike"),fnv("crmod_queue_cleave"),
    fnv("crmod_queue_glaive"),fnv("crmod_queue_crush"),fnv("crmod_queue_drill"),fnv("crmod_queue_extend")};
inline unsigned cue_index(loadout_control::Channel channel,const char* name) {
    constexpr const char* names[]={"Strike","Cleave","Glaive","Crush","Drill","Extend"};
    if(name)for(unsigned i=0;i<6;++i)
        if(channel==(i<3?loadout_control::Channel::Primary:loadout_control::Channel::Secondary) &&
           std::strcmp(name,names[i])==0)return i;
    return 6;
}
}

#ifdef _WIN32
#include <windows.h>
#include <atomic>
namespace loadout_queue_audio {
// Native nl_audio_global_post_event uses this existing registered 2D object.
inline constexpr std::uint64_t global_object=0x100000001ull;
using Initialized=bool (*)();
using Load=int (*)(const void*,std::uint32_t,std::uint32_t*);
using Post=std::uint32_t (*)(std::uint32_t,std::uint64_t,std::uint32_t,void*,void*,std::uint32_t,void*,std::uint32_t);
using Execute=int (*)(std::uint32_t,int,std::uint64_t,int,int,std::uint32_t);
using Unload=int (*)(std::uint32_t,const void*,std::uint32_t);
inline Initialized initialized=nullptr;
inline Load load=nullptr;
inline Post post=nullptr;
inline Execute execute=nullptr;
inline Unload unload=nullptr;
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
    const auto resource=FindResourceW(module,MAKEINTRESOURCEW(401),MAKEINTRESOURCEW(10));
    const auto size=resource?SizeofResource(module,resource):0;
    const auto data=resource?LockResource(LoadResource(module,resource)):nullptr;
    if(load && post && execute && unload && data && size>64 && size<=2*1024*1024) {
        AcquireSRWLockExclusive(&lock);
        if(!stopping.load()) {
            std::uint32_t actual=0;
            const int result=load(data,size,&actual);load_result.store(result);++attempts;
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
    if(index<6 && ready.load() && shared_stopping && !shared_stopping->load() &&
       TryAcquireSRWLockShared(&lock)) {
        if(ready.load() && !shared_stopping->load() && initialized())
            last_playing_id.store(post(events[index],global_object,0,nullptr,nullptr,0,nullptr,0));
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
