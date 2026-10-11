#include "../../src/native/combat_native.h"
#include "../../src/native/step_binding.h"
#ifdef _WIN32
#include "../../src/native/held_lock_native.h"
#include "../../src/native/facing_lock_native.h"
#endif
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <utility>
static unsigned char world[0x59000]{},chunk[0x100]{};
static uint64_t generation[2]{},location[2]{};
static uint32_t tags[]{step_binding::kTagPlayer},keys[]{0x49458a06},offsets[]{0x40};
static uint16_t registry_action=0;
static constexpr uint64_t base=0x140000000;
static bool copy(void* out,const void* from,size_t size) {
    auto p=reinterpret_cast<uintptr_t>(from);
    if(p==base+0x5ceb500 && size==2){std::memcpy(out,&registry_action,2);return true;}
    for(auto span:{std::pair<uintptr_t,size_t>{reinterpret_cast<uintptr_t>(world),sizeof world},
        {reinterpret_cast<uintptr_t>(chunk),sizeof chunk},{reinterpret_cast<uintptr_t>(generation),sizeof generation},
        {reinterpret_cast<uintptr_t>(location),sizeof location},{reinterpret_cast<uintptr_t>(tags),sizeof tags},
        {reinterpret_cast<uintptr_t>(keys),sizeof keys},{reinterpret_cast<uintptr_t>(offsets),sizeof offsets}})
        if(p>=span.first && size<=span.second && p-span.first<=span.second-size){std::memcpy(out,from,size);return true;}
    return false;
}
template<class T>static void put(void* p,size_t offset,T value){std::memcpy(static_cast<unsigned char*>(p)+offset,&value,sizeof value);}
int main(){
#ifdef _WIN32
    std::atomic<bool> stop{false};
    for(auto& original:held_lock_native::originals)original=reinterpret_cast<void*>(1);
    assert(!held_lock_native::arm(base,true,stop)); // Missing reader reproduces build89 startup failure.
#endif
    assert(step_binding::init_reader(base,copy));
    auto& e=step_binding::env();assert(!e.write && !e.loco_hooked && !e.jump_hooked);
    auto w=reinterpret_cast<uintptr_t>(world),c=reinterpret_cast<uintptr_t>(chunk);
    put(world,0x58510,uint64_t{2});put(world,0x584e8,reinterpret_cast<uintptr_t>(generation));
    put(world,0x58530,reinterpret_cast<uintptr_t>(location));put(world,0x50,c);put(world,0x10448,uint32_t{1});
    put(world,0x18448,reinterpret_cast<uintptr_t>(tags));put(world,0x18450,reinterpret_cast<uintptr_t>(keys));
    put(world,0x18458,reinterpret_cast<uintptr_t>(offsets));put(world,0x18460,uint32_t{1});put(world,0x18464,uint32_t{1});
    uint64_t first=(uint64_t{3}<<32)|1,next=(uint64_t{4}<<32)|1,row=0;
    generation[1]=3;put(chunk,0x10,first);
    assert(step_binding::resolve_player_column(w,first,keys[0],0x40,&row)&&row==c+0x40);
    generation[1]=4;put(chunk,0x10,next);
    assert(!step_binding::resolve_player_column(w,first,keys[0],0x40,&row));
    assert(step_binding::resolve_player_column(w,next,keys[0],0x40,&row)&&row==c+0x40);
#ifdef _WIN32
    assert(held_lock_native::arm(base,true,stop)&&held_lock_native::action==0xffff);
    assert(held_lock_native::refresh_action()&&held_lock_native::action==0);
    registry_action=49635;held_lock_native::proof_valid.store(true);
    assert(held_lock_native::refresh_action()&&held_lock_native::action==49635&&!held_lock_native::proof_valid.load());
    held_lock_native::proof_valid.store(true);assert(held_lock_native::refresh_action()&&held_lock_native::proof_valid.load());
    registry_action=0xffff;assert(!held_lock_native::refresh_action()&&!held_lock_native::proof_valid.load());
    registry_action=49635;assert(held_lock_native::refresh_action());
    assert(facing_lock_native::arm(base,true,stop)&&facing_lock_native::active());
#endif
    assert(!e.write && !e.loco_hooked && !e.jump_hooked);
    puts("PASS: shared reader survives entity generation replacement; hold/direction arm on Win64; stomp writes/hooks absent");
}
