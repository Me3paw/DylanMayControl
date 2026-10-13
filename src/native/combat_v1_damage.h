#pragma once
#include "combat_v1_policy.h"
#include "gameplay_runtime.h"
#include "combat_v1_runtime.h"

namespace combat_v1_damage {
using Modifier=void (*)(void*,void*,void*,void*);
inline Modifier original=nullptr;
using CopyValues=void* (*)(void*,void*);
using Calculate=void (*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,std::uint8_t,void*,void*,void*,void*);
inline CopyValues copy_original=nullptr;
inline Calculate calculate_original=nullptr;
inline std::atomic<bool> enabled{false};
inline const std::atomic<bool>* stopping=nullptr;
inline std::uintptr_t base=0;
inline constexpr std::uint32_t kRva=0x2709f10,kReturn=0x270b207;
inline constexpr native_pins::Pin pins[]={
    {"v1_damage_final",0x2709150,15,"4c8bdc49895b10498973204d8943185741544155415641574881ec60020000c4c1782973c8c4c178297bb8c441782943a8c44178294b984d8bf9488bf24c8be1"},
    {"v1_damage_modifier",kRva,15,"488bc4488958204c894018488950105556574154415541564157488da828ffffff4881eca0010000c5f82970b8c5f82978a8c578294098c578294888c5782990"},
    {"v1_damage_copy",0x27147e0,15,"48895c240848896c2410488974241848897c24204154415641574883ec20488bd9483bca747f8b7a084c8d3cfd000000"},
    {"v1_damage_call",0x270b1f2,0,"ff4d8bcc4c8d4590488d542470488bcbe809edffff4c8d442470488d5590488bcbe808f2ffff4c896c2438488b858001"}
};
inline bool prepare(std::uintptr_t executable,const std::atomic<bool>& stopped) {
    base=executable;stopping=&stopped;
    for (const auto& pin:pins) if (!native_pins::verify(combat_native::binding().copy,base,pin)) return false;
    return true;
}
inline bool player_source(std::uintptr_t request,std::uintptr_t context) {
    std::uint64_t source=0,world=0;
    if (!loadout_player_catalogue::active() ||
        !loadout_attack_probe::copy(request+0x18,&source,8) || !source ||
        source!=loadout_player_catalogue::observed_player_actor.load() ||
        !loadout_attack_probe::copy(context+0x40,&world,8)) return false;
    bool owned=false;
    if (TryAcquireSRWLockShared(&loadout_player_catalogue::state_lock)) {
        const auto& c=loadout_player_catalogue::published.catalogue;
        owned=c.valid && c.world==world && c.actor==source;
        ReleaseSRWLockShared(&loadout_player_catalogue::state_lock);
    }
    loadout_actor_resolver::Rows rows{};
    return owned && loadout_actor_resolver::resolve(world,source,rows) && loadout_actor_resolver::current(rows);
}
// Scale only the freshly copied base values, before native potency and status
// modifiers. A nested initialization restores its caller's request scope.
struct BaseLoan { std::uintptr_t request=0;combat_v1::Scale scale{};BaseLoan* previous=nullptr; };
inline thread_local BaseLoan* current=nullptr;
inline bool running(){return enabled.load()&&stopping&&!stopping->load();}
__attribute__((noinline)) inline void* copy_values(void* destination,void* source) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    void* result=copy_original(destination,source);const DWORD forwarded=GetLastError();
    if(running()&&current&&reinterpret_cast<std::uintptr_t>(destination)==current->request+0xc8&&
        (caller==base+0x270a0ec||caller==base+0x270a168)) {
        std::uintptr_t values=0;std::uint32_t count=0,capacity=0;std::array<combat_v1::Value,12> copy{};
        const auto vector=reinterpret_cast<std::uintptr_t>(destination);
        if(combat_v1_runtime::read(vector,values)&&combat_v1_runtime::read(vector+8,count)&&
            combat_v1_runtime::read(vector+12,capacity)&&count&&count<=copy.size()&&count<=capacity&&
            loadout_attack_probe::copy(values,copy.data(),count*sizeof(copy[0]))&&
            combat_v1::scale_values(copy.data(),count,current->scale))
            (void)gameplay_runtime::write(reinterpret_cast<void*>(values),copy.data(),count*sizeof(copy[0]));
    }
    SetLastError(forwarded);return result;
}
__attribute__((noinline)) inline void modifier(void* request,void* target,void* context,void* database) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));const DWORD incoming=GetLastError();
    const auto r=reinterpret_cast<std::uintptr_t>(request);std::uint32_t id=0;BaseLoan loan{};
    loan.previous=current;
    if(running()&&caller==base+kReturn&&combat_v1_runtime::read(r+0xd8,id)&&
        player_source(r,reinterpret_cast<std::uintptr_t>(context))&&combat_v1::base_scale(id,loan.scale))loan.request=r;
    current=&loan;SetLastError(incoming);original(request,target,context,database);
    const DWORD forwarded=GetLastError();current=loan.previous;SetLastError(forwarded);
}
// Native result values and totals are complete here, including added channels,
// potency and receiver modifiers. Repeat is a separate final output cost.
__attribute__((noinline)) inline void calculate(void* request,void* result,void* source_mods,void* target_mods,
        void* receiver_mods,void* critical,void* material,void* health,void* target,std::uint8_t mode,
        void* context,void* database,void* resource,void* rules) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    calculate_original(request,result,source_mods,target_mods,receiver_mods,critical,material,health,target,mode,context,database,resource,rules);
    const DWORD forwarded=GetLastError();const auto r=reinterpret_cast<std::uintptr_t>(request),out=reinterpret_cast<std::uintptr_t>(result);
    combat_v1::Scale scale{};
    if(running()&&caller==base+0x2707de0&&player_source(r,reinterpret_cast<std::uintptr_t>(context))&&
        combat_v1_runtime::repeat(r,reinterpret_cast<std::uintptr_t>(context),scale)&&
        (scale.damage!=1||scale.falter!=1)) {
        std::uintptr_t values=0;std::uint32_t count=0,capacity=0;
        struct Totals {std::uint16_t flags,padding;float health,falter;} totals{};
        static_assert(sizeof(Totals)==12);
        std::array<combat_v1::Value,12> copy{};
        if(combat_v1_runtime::read(out+0x80,values)&&combat_v1_runtime::read(out+0x88,count)&&
            combat_v1_runtime::read(out+0x8c,capacity)&&count&&count<=copy.size()&&count<=capacity&&
            combat_v1_runtime::read(out+0xa0,totals)&&std::isfinite(totals.health)&&totals.health>=0&&std::isfinite(totals.falter)&&totals.falter>=0&&
            loadout_attack_probe::copy(values,copy.data(),count*sizeof(copy[0]))&&combat_v1::scale_values(copy.data(),count,scale)&&
            gameplay_runtime::writable(reinterpret_cast<void*>(values),count*sizeof(copy[0]))&&gameplay_runtime::writable(reinterpret_cast<void*>(out+0xa0),sizeof totals)) {
            totals.health*=scale.damage;totals.falter*=scale.falter;
            bool health_channel=false,falter_channel=false;
            for(unsigned i=0;i<count;++i){if(copy[i].type==9)falter_channel=true;else health_channel=true;}
            // Same native FLT_EPSILON negligible-output flags; no added floor.
            if(health_channel&&totals.health<=0x1p-23f)totals.flags|=4;
            if(falter_channel&&totals.falter<=0x1p-23f)totals.flags|=8;
            std::memcpy(reinterpret_cast<void*>(values),copy.data(),count*sizeof(copy[0]));
            std::memcpy(reinterpret_cast<void*>(out+0xa0),&totals,sizeof totals);
        }
    }
    SetLastError(forwarded);
}
}
