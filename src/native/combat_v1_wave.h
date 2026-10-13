#pragma once
#include "combat_v1_runtime.h"

namespace combat_v1_wave {
using Token=combat_v1::Token;
using Rows=loadout_actor_resolver::Rows;
using Lua=std::uint64_t(*)(void*);
using Self=std::uint64_t*(*)(std::uint64_t*,void*);
using Create=void(*)(void*,void*,void*,void*,void*);
using Pool=void(*)(void*,void*,void*,void*,void*,void*,void*,void*);
using Hit=std::uint64_t(*)(void*,void*,void*);
using Push=void(*)(void*,void*,void*,void*);
inline Lua lua_original=nullptr;
inline Self self=nullptr;
inline Create create_original=nullptr;
inline Pool pool_original=nullptr;
inline Hit hit_original=nullptr;
inline Push push_original=nullptr;
inline std::atomic<bool> enabled{false};
inline SRWLOCK mutex=SRWLOCK_INIT;
struct Guard {Guard(){AcquireSRWLockExclusive(&mutex);}~Guard(){ReleaseSRWLockExclusive(&mutex);}};
struct Queued {std::uintptr_t queue=0;std::uint32_t index=0;std::uint16_t id=0;std::uint64_t source=0,instigator=0,origin[2]{};Token token{};};
struct Active {std::uintptr_t common=0,damage=0;std::uint16_t id=0;std::uint64_t source=0;Token token{};};
inline std::array<Queued,128> queued{};
inline std::array<Active,128> waves{};
inline combat_v1::WaveRows winners{};
inline std::uint64_t world=0,actor=0;
inline constexpr native_pins::Pin pins[]={
    {"v1_wave_lua",0x23cb040,14,"48895c24085641544155415641574881ec10010000ba0100000041b805000000c5f829bc24f00000004c8bf1e87fbf8c004c8d842448010000ba01000000498b"},
    {"v1_wave_self",0x1a30ca0,15,"48895c240848896c2410488974241857415641574883ec20488bfa4c8d0502e6e102488bf14533c9488bcfba01000000bd02000000e846e2250185c0747e49bf"},
    {"v1_wave_create",0x2463120,15,"4c8bdc4d894b204d89431849894b085553565741544157498d6b984881ec38010000488b194d8be18b41084c8bfa4869f840030000488bf14803fb483bdf0f84"},
    {"v1_wave_pool",0x2471310,15,"4c8bdc49895b084989731049897b1841564881ece0000000c5fc1012c5fc105a20c5fb104240488b8424100100004d8bf0488bb424200100004c8d442450c4c1"},
    {"v1_wave_pool_identity",0x2471380,0,"c4e37d19d801c4e3f91644243801498bf94889442440c5fc114c2470c5f9d6542420c5f9d65c2430488d4c2420c5f877e81b573cffc5fc1013488b8424180100"},
    {"v1_wave_hit",0x24672e0,14,"488bc448895820488950105556574154415541564157488da8d8fdffff4881ecf0020000c5f82970b8c5f82978a8c578294098c578294888c578299078ffffff"},
    {"v1_wave_push",0x246adb0,15,"4c8bdc49895b08498973204d894318498953105741544155415641574881eca0010000c4c1782973c8c4c178297bb8c441782943a8c44178294b984d8be94c8b"}
};
inline bool running(){return enabled.load()&&combat_v1_runtime::running();}
using combat_v1_runtime::read;
inline bool current(Rows& rows) {
    if(!running()||!combat_v1_runtime::owner(rows))return false;
    Guard guard;
    if(world!=rows.location.world||actor!=rows.location.actor){world=rows.location.world;actor=rows.location.actor;queued={};waves={};winners={};}
    return true;
}
inline bool wave_request(std::uintptr_t pointer,Queued& out) {
    std::uint32_t damage=0;std::uint8_t present=0;
    return read(pointer,out.instigator)&&read(pointer+8,out.source)&&read(pointer+0x40,out.origin)&&
        read(pointer+0x130,damage)&&damage==0x31f6e1fb&&read(pointer+0x138,present)&&present&&read(pointer+0x13c,out.id);
}
inline bool same_request(const Queued& a,const Queued& b) {
    return a.id==b.id&&a.source==b.source&&a.instigator==b.instigator&&!std::memcmp(a.origin,b.origin,16);
}
inline bool resource(std::uint64_t w,std::uintptr_t& pointer) {
    std::uintptr_t keys=0,values=0;std::uint32_t count=0;
    if(!read(w+0x585b0,keys)||!read(w+0x585b8,count)||count>4096||!read(w+0x585c0,values))return false;
    for(std::uint32_t i=0;i<count;++i){
        std::uint32_t key=0;if(!read(keys+i*4,key))return false;
        if(key!=0xbb6e6704)continue;
        std::uintptr_t keys_after=0,values_after=0;std::uint32_t count_after=0;
        return read(values+i*8,pointer)&&pointer&&read(w+0x585b0,keys_after)&&keys_after==keys&&
            read(w+0x585b8,count_after)&&count_after==count&&read(w+0x585c0,values_after)&&values_after==values;
    }
    return false;
}
inline Token script_token(void* state,Rows& rows) {
    std::uintptr_t context=0;std::uint64_t lua_world=0,child=UINT64_MAX;
    if(!current(rows)||!self||!read(reinterpret_cast<std::uintptr_t>(state)+0x78,context)||!context||!read(context,lua_world)||lua_world!=rows.location.world)return {};
    if(self(&child,state)!=&child||child==rows.location.actor||child==UINT64_MAX)return {};
    std::array<std::uint8_t,0x40> form{};
    if(!step_binding::entity_column_bytes(lua_world,child,step_binding::fnv1a("heron::melee_forms::component::Form"),form.size(),form.data())||!form[0x3a])return {};
    std::uint32_t id=0,generation=0;std::memcpy(&id,form.data()+0x28,4);std::memcpy(&generation,form.data()+0x10,4);
    if(id!=2276994531u&&id!=3260945180u)return {};
    std::uintptr_t body=0,children=0;std::uint32_t count=0;
    if(!loadout_chain_switch::component(rows,step_binding::fnv1a("heron::character_attachment::component::Body"),0x38,body)||
        !read(body+0x18,children)||!read(body+0x20,count)||count>128)return {};
    bool owned=false;
    for(std::uint32_t i=0;i<count;++i){std::uint64_t handle=0;if(!read(children+i*8,handle))return {};owned|=handle==child;}
    if(!owned||!loadout_actor_resolver::current(rows))return {};
    combat_v1_runtime::Guard guard;
    return combat_v1_runtime::world==lua_world&&combat_v1_runtime::actor==rows.location.actor?combat_v1_runtime::groups.find(generation):Token{};
}
inline std::uint64_t lua(void* state) {
    const DWORD incoming=GetLastError();Rows rows{};Token token=script_token(state,rows);
    std::uintptr_t address=0;std::uint32_t index=0;
    const bool owned=token.serial&&resource(rows.location.world,address)&&read(address+8,index);
    SetLastError(incoming);const auto result=lua_original(state);const DWORD forwarded=GetLastError();
    std::uintptr_t current_resource=0,data=0;std::uint32_t after=0;Queued saved{};
    // No TLS authority survives a Lua error/longjmp. Credit only the one request
    // actually appended by this completed native API invocation.
    if(owned&&loadout_actor_resolver::current(rows)&&resource(rows.location.world,current_resource)&&current_resource==address&&
        read(address,data)&&read(address+8,after)&&after==index+1&&wave_request(combat_native::at(data,index,0x340),saved)&&
        saved.source==rows.location.actor&&saved.instigator==rows.location.actor) {
        Guard guard;Queued* empty=nullptr;
        for(auto& item:queued){if(item.queue==address&&item.index==index){item={};empty=&item;break;}if(!item.queue&&!empty)empty=&item;}
        if(empty&&world==rows.location.world&&actor==rows.location.actor){saved.queue=address;saved.index=index;saved.token=token;*empty=saved;}
    }
    SetLastError(forwarded);return result;
}
inline void create(void* requests,void* pools,void* query,void* targets,void* commands) {
    create_original(requests,pools,query,targets,commands);const DWORD forwarded=GetLastError();std::uint32_t count=1;
    if(read(reinterpret_cast<std::uintptr_t>(requests)+8,count)&&!count){Guard guard;for(auto& item:queued)if(item.queue==reinterpret_cast<std::uintptr_t>(requests))item={};}
    SetLastError(forwarded);
}
inline void pool(void* slot,void* columns,void* common,void* damage,void* query,void* commands,void* request,void* definition) {
    const DWORD incoming=GetLastError();Rows rows{};Queued key{};Token token{};
    const bool owned=current(rows);const auto pointer=reinterpret_cast<std::uintptr_t>(request);
    if(owned&&wave_request(pointer,key)) {
        Guard guard;
        for(auto& item:queued) {
            std::uintptr_t data=0;std::uint32_t count=0;
            if(item.queue&&same_request(item,key)&&read(item.queue,data)&&read(item.queue+8,count)&&item.index<count&&
                pointer==combat_native::at(data,item.index,0x340)){token=item.token;item={};break;}
        }
    }
    SetLastError(incoming);pool_original(slot,columns,common,damage,query,commands,request,definition);const DWORD forwarded=GetLastError();
    const bool fresh=owned&&loadout_actor_resolver::current(rows);
    {
        Guard guard;Active* empty=nullptr;
        for(auto& item:waves){if(item.common==reinterpret_cast<std::uintptr_t>(common)){item={};empty=&item;break;}if(!item.common&&!empty)empty=&item;}
        std::uint16_t id=0;std::uint64_t source=0;
        if(fresh&&empty&&token.serial&&world==rows.location.world&&actor==rows.location.actor&&read(reinterpret_cast<std::uintptr_t>(common)+0x220,id)&&id==key.id&&
            read(reinterpret_cast<std::uintptr_t>(common)+0x1b0,source)&&source==rows.location.actor)
            *empty={reinterpret_cast<std::uintptr_t>(common),reinterpret_cast<std::uintptr_t>(damage),id,source,token};
    }
    SetLastError(forwarded);
}
inline std::uint64_t hit(void* context,void* definition,void* target) {
    const DWORD incoming=GetLastError();Rows rows{};std::uint64_t args[9]{};std::uintptr_t data=0;std::uint32_t before=0;
    std::array<std::array<std::uint8_t,0x70>,128> bytes;Token token{};bool relevant=false;
    if(running()&&read(reinterpret_cast<std::uintptr_t>(context),args)) {
        Guard guard;relevant=winners.has(args[8]);for(const auto& item:waves)relevant|=item.token.serial&&item.common==args[0]&&item.damage==args[1];
    }
    bool owned=relevant&&current(rows)&&read(args[8],data)&&read(args[8]+8,before)&&before<=bytes.size();
    if(owned) {
        std::uint16_t id=0;std::uint64_t source=0;
        if(read(args[0]+0x220,id)&&read(args[0]+0x1b0,source)) {Guard guard;for(const auto& item:waves)if(item.common==args[0]&&item.damage==args[1]&&item.id==id&&item.source==source)token=item.token;}
        for(std::uint32_t i=0;owned&&i<before;++i)owned=read(data+i*0x70,bytes[i]);
    }
    SetLastError(incoming);const auto result=hit_original(context,definition,target);const DWORD forwarded=GetLastError();std::uint32_t after=0;
    if(owned&&loadout_actor_resolver::current(rows)&&read(args[8],data)&&read(args[8]+8,after)&&after<=bytes.size()) {
        for(std::uint32_t i=0;i<after;++i) {
            std::array<std::uint8_t,0x70> changed{};if(!read(data+i*0x70,changed)){Guard guard;winners.clear(args[8]);break;}
            if(i>=before||changed!=bytes[i]){Guard guard;
                if(world==rows.location.world&&actor==rows.location.actor)winners.save(args[8],i,changed,token);
                else winners.clear(args[8]);
            }
        }
    } else if(relevant&&args[8]) {Guard guard;winners.clear(args[8]);}
    SetLastError(forwarded);return result;
}
inline void push(void* query,void* intermediate,void* damage,void* physics) {
    const DWORD incoming=GetLastError();Rows rows{};std::uintptr_t input_data=0;std::uint32_t count=0,before=0;
    std::array<Token,128> tokens{};std::array<std::array<std::uint8_t,0x70>,128> inputs{};
    const auto queue=reinterpret_cast<std::uintptr_t>(intermediate),output=reinterpret_cast<std::uintptr_t>(damage);
    bool owned=current(rows)&&read(queue,input_data)&&read(queue+8,count)&&count<=tokens.size()&&read(output+8,before);
    for(std::uint32_t i=0;owned&&i<count;++i){owned=read(input_data+i*0x70,inputs[i]);if(owned){Guard guard;tokens[i]=winners.find(queue,i,inputs[i]);}}
    SetLastError(incoming);push_original(query,intermediate,damage,physics);const DWORD forwarded=GetLastError();
    std::uintptr_t data=0;std::uint32_t after=0;
    if(owned&&loadout_actor_resolver::current(rows)&&read(output,data)&&read(output+8,after)&&after>=before&&after-before==count)for(std::uint32_t i=0;i<count;++i) {
        if(!tokens[i].serial)continue;
        std::array<std::uint8_t,0xf0> result{};if(!read(data+(before+i)*0xf0,result))break;
        std::uint64_t source=0;std::memcpy(&source,result.data()+0x18,8);
        if(source!=rows.location.actor||std::memcmp(result.data(),inputs[i].data(),16)||
            std::memcmp(result.data()+0x18,inputs[i].data()+0x3c,8)||std::memcmp(result.data()+0x50,inputs[i].data()+0x44,8)||
            std::memcmp(result.data()+0xd8,inputs[i].data()+0x4c,4))continue;
        combat_v1_runtime::Request saved{};saved.queue=output;saved.token=tokens[i];
        std::memcpy(&saved.sequence,result.data()+0xe0,4);std::memcpy(&saved.id,result.data()+0xd8,4);std::memcpy(saved.origin,result.data(),16);
        combat_v1_runtime::Guard guard;
        if(combat_v1_runtime::world==rows.location.world&&combat_v1_runtime::actor==source)
            combat_v1_runtime::requests[saved.sequence%combat_v1_runtime::requests.size()]=saved;
    }
    {Guard guard;winners.clear(queue);}
    SetLastError(forwarded);
}
inline bool prepare(std::uintptr_t base) {
    for(const auto& pin:pins)if(!native_pins::verify(combat_native::binding().copy,base,pin))return false;
    self=reinterpret_cast<Self>(base+0x1a30ca0);return true;
}
}
