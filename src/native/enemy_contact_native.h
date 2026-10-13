#pragma once
#include "step_binding.h"
#include "entity_capsule_geometry.h"
#include "mod_bindings.h"
#include "nearby_candidates.h"
#ifdef _WIN32
#include <windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Diagnostics and fresh entity-step contact queries. No native physics pointer
// or contact result survives the callback that resolved it.
namespace enemy_contact_native {
inline constexpr double contact_margin() {return 0.8;}
constexpr uint32_t kController=0x9b382c56, kPhysicsEntity=0x6ebfd07c, kPlane=0xc0853848;
constexpr uint32_t kInput=step_binding::fnv1a("heron::player_input::component::Input");
constexpr uint32_t kRequest=step_binding::fnv1a("heron::jump::component::NextJumpRequest");
static_assert(kInput==0x49458a06 && kRequest==0xce27ff62);
constexpr uint32_t kOverlapRva=0x2d51490, kQueries=16, kRows=256, kArches=4096, kRecords=4096;
constexpr uint8_t kOverlapPin[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0x90,0x48,0x81,0xec,0x70,0x01,0x00,0x00,0xc5};
using OverlapFn=uint64_t (*)(uint64_t,uint64_t,const float*,const float*,float,float);
enum Incomplete : uint32_t { Read=1, Ownership=2, Time=4, QueryCap=8, RowCap=16, ArchCap=32, PartialStart=64, Busy=128 };
struct ErrorGuard { DWORD saved=GetLastError(); ~ErrorGuard() { SetLastError(saved); } };
inline bool cp(uint64_t p,void* out,size_t n) { return combat_native::copy_checked(step_binding::env().copy,out,p,n); }
template<class T> inline bool field(uint64_t p,uint64_t off,T& out) { return cp(combat_native::at(p,0,0,off),&out,sizeof out); }
struct Stamp { step_binding::Loc loc{}; step_binding::Meta meta{}; };
inline bool stamp(uint64_t w,uint64_t a,Stamp& s) {
    return step_binding::locate(w,a,&s.loc) && step_binding::read_meta(w,s.loc.arch,&s.meta);
}
inline bool same(const Stamp& a,const Stamp& b) {
    return a.loc.chunk==b.loc.chunk && a.loc.row==b.loc.row && a.loc.arch==b.loc.arch &&
        a.meta.tags==b.meta.tags && a.meta.keys==b.meta.keys && a.meta.offsets==b.meta.offsets &&
        a.meta.tag_count==b.meta.tag_count && a.meta.key_count==b.meta.key_count;
}
inline bool unchanged(uint64_t w,uint64_t a,const Stamp& before) { Stamp after{}; return stamp(w,a,after) && same(before,after); }
inline bool column(const Stamp& s,uint32_t hash,size_t stride,uint64_t& out) {
    uint32_t keys[step_binding::kMetaMax],offsets[step_binding::kMetaMax]; out=0;
    if (!step_binding::read_u32s(s.meta.keys,s.meta.key_count,keys) ||
        !step_binding::read_u32s(s.meta.offsets,s.meta.key_count,offsets)) return false;
    for (uint32_t i=0;i<s.meta.key_count;++i) if (keys[i]==hash && offsets[i]) {
        out=combat_native::at(combat_native::at(s.loc.chunk,0,0,offsets[i]),s.loc.row,stride); return out!=0;
    }
    return false;
}
struct Player {
    uint64_t world=0,actor=0,state=0,container=0,input=0,request=0,jump=0,air=0;
    Stamp owner{}; alignas(16) float position[4]{},quaternion[4]{};
    float radius=0,height=0; uint8_t pulse=2,njr[2]{0xff,0xff},airborne=2;
};
inline bool player(const void* cols,const void* ctx,Player& p) {
    auto& e=step_binding::env(); uint64_t c[0x1d]{},controller=0,plane=0,owner=0;
    if (!e.player || !field(reinterpret_cast<uintptr_t>(ctx),0,p.world) || !(p.actor=e.player()) ||
        !stamp(p.world,p.actor,p.owner) || !column(p.owner,kController,0x20,controller) ||
        !cp(reinterpret_cast<uintptr_t>(cols),c,sizeof c) || c[0x1c]>=combat_native::cols::kMaxRow ||
        combat_native::at(c[0],c[0x1c],0x20)!=controller ||
        !field(controller,0,p.state) || !field(controller,8,p.container) || !p.state || !p.container ||
        !field(p.container,0x18,owner) || owner!=p.actor ||
        !column(p.owner,kPlane,0x10,plane) || !column(p.owner,kInput,0x40,p.input) ||
        !column(p.owner,kRequest,2,p.request) || !column(p.owner,step_binding::kColJump,0x28,p.jump) ||
        !column(p.owner,step_binding::kColAirborne,0xc,p.air) ||
        !cp(combat_native::at(p.state,0,0,0x10),p.position,sizeof p.position) ||
        !field(p.state,0x20,p.height) || !field(p.state,0x24,p.radius) ||
        !cp(plane,p.quaternion,sizeof p.quaternion) || !cp(p.request,p.njr,2) || !field(p.air,8,p.airborne)) return false;
    float norm=0;
    for (unsigned i=0;i<4;++i) {
        if (!std::isfinite(p.position[i]) || !std::isfinite(p.quaternion[i])) return false;
        norm+=p.quaternion[i]*p.quaternion[i];
    }
    if (!std::isfinite(p.radius) || !std::isfinite(p.height) || p.radius<=0 || p.height<=0 ||
        norm<0.5f || norm>1.5f) return false;
    // Exact caller142778607 uses this clamp; neither radius nor skin is inflated.
    p.height=p.height<2*p.radius?2*p.radius:p.height;
    if (!std::isfinite(p.height)) return false;
    bool pulse=false;
    if (e.have_jump_action && step_binding::read_action(p.input,e.jump_action,&pulse)) p.pulse=pulse?1:0;
    uint64_t world_now=0;
    return field(reinterpret_cast<uintptr_t>(ctx),0,world_now) && world_now==p.world &&
        e.player()==p.actor && unchanged(p.world,p.actor,p.owner);
}
inline bool same_player(const Player& a,const Player& b) {
    return a.world==b.world && a.actor==b.actor && same(a.owner,b.owner) && a.state==b.state &&
        a.container==b.container && a.input==b.input && a.request==b.request && a.jump==b.jump && a.air==b.air &&
        a.radius==b.radius && a.height==b.height && !std::memcmp(a.position,b.position,sizeof a.position) &&
        !std::memcmp(a.quaternion,b.quaternion,sizeof a.quaternion);
}
struct Record {
    uint64_t invocation=0,qpc=0,world=0,actor=0;
    uint32_t ms=0,thread=0,incomplete=0,arches=0,rows=0,enemies=0,bodies=0,queries=0,hits=0,elapsed_us=0;
    uint8_t phase=0,pulse=2,airborne=2,njr[2]{0xff,0xff},jump_phase=0xff,jump_index=0xff,shown=0;
    float position[4]{},quaternion[4]{},radius=0,height=0;
    struct Hit { uint64_t actor=0,key=0; } hit[8]{};
};
struct Invocation { uint64_t serial=0,world=0,actor=0; };
inline std::atomic<bool> enabled{false};
inline const std::atomic<bool>* stopping=nullptr;
inline std::atomic<uint32_t> next_ms{0},cursor_arch{0},cursor_row{0},cursor_body{0},pushed{0};
inline std::atomic<uint64_t> serial{0},dropped{0},unqualified{0};
inline SRWLOCK queue_lock=SRWLOCK_INIT,scan_lock=SRWLOCK_INIT;
inline std::array<Record,64> queue{};
inline unsigned count=0;
inline int64_t frequency=0;
inline bool active() { return enabled.load() && stopping && !stopping->load() && pushed.load()<kRecords; }
inline int64_t tick() { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return t.QuadPart; }
inline bool physics(const void* ctx,uint64_t& world) {
    uint64_t borrow=0; return field(reinterpret_cast<uintptr_t>(ctx),8,borrow) && borrow && field(borrow,0,world) && world;
}
inline bool scene_resource(uint64_t world,uint64_t& wrapper,uint64_t& physics_world) {
    constexpr uint32_t hash=step_binding::fnv1a("coregame::global::PhysicsScene");
    static_assert(hash==0x2eb2d62c);
    uint64_t keys=0,values=0; uint32_t count=0;
    if (!field(world,0x585b0,keys) || !field(world,0x585b8,count) || !field(world,0x585c0,values) ||
        !keys || !values || !count || count>4096) return false;
    uint32_t lo=0,hi=count;
    while (lo<hi) {
        const auto mid=lo+(hi-lo)/2; uint32_t key=0;
        if (!cp(combat_native::at(keys,mid,4),&key,4)) return false;
        if (key<hash) lo=mid+1; else hi=mid;
    }
    uint32_t key=0; uint64_t current_keys=0,current_values=0; uint32_t current_count=0;
    return lo<count && cp(combat_native::at(keys,lo,4),&key,4) && key==hash &&
        cp(combat_native::at(values,lo,8),&wrapper,8) && wrapper && field(wrapper,0,physics_world) && physics_world &&
        field(world,0x585b0,current_keys) && current_keys==keys && field(world,0x585c0,current_values) && current_values==values &&
        field(world,0x585b8,current_count) && current_count==count;
}
inline std::atomic<uint64_t> verified_scene_world{0};
inline std::atomic<uint64_t> fresh_queries{0},fresh_contacts{0},fresh_unavailable{0};
using GeometryQuery=uint8_t (*)(const void*,const void*,const void*,const void*,const uint32_t*,void*);
using PointDistance=float (*)(const float*,const void*,const void*,float*,uint32_t*,const uint32_t*);
inline GeometryQuery geometry_query=nullptr;
inline PointDistance point_distance=nullptr;
inline HMODULE physics_module=nullptr;
struct ContactProbe {
    uint32_t ms=0,type=UINT32_MAX; uint64_t actor=0,key=0; uint8_t flags=0,hit=0;
    float position[3]{},center_distance_sq=-1;
    entity_capsule_geometry::Pose player_pose{},shape_pose{};
    entity_capsule_geometry::Capsule player_capsule{},shape_capsule{};
    bool geometry_captured=false,target_capsule=false,gap_known=false,qualified=false;double surface_gap=0,margin=0.035;
};
struct ContactScan {
    uint32_t ms=0,rows=0,queries=0,elapsed_us=0,incomplete=0,nearby_candidates=0,nearby_checked=0;
    uint64_t actor=0,selected=0,hit=0; bool selected_seen=false;
};
inline std::array<ContactScan,64> contact_scans{};
inline unsigned contact_scan_count=0;
inline std::atomic<unsigned> contact_scans_logged{0};
inline SRWLOCK contact_log_lock=SRWLOCK_INIT;
inline std::array<ContactProbe,128> contact_log{};
inline unsigned contact_log_count=0;
inline std::atomic<unsigned> contact_logged{0},contact_log_dropped{0};
struct ControllerProbe {
    uint32_t ms=0;uint64_t actor=0;bool present=false,owned=false,overlap=false,gap_known=false,physical=false,qualified=false;double gap=0,margin=0.035;
    entity_capsule_geometry::Pose player_pose{},target_pose{};
    entity_capsule_geometry::Capsule player_capsule{},target_capsule{};
};
inline std::array<ControllerProbe,64> controller_probes{};
inline unsigned controller_probe_count=0;
inline std::atomic<unsigned> controller_probe_total{0};
inline bool observe_controller(const Player& player,const Stamp& target,uint64_t actor,bool physical=false,double margin=contact_margin()) {
    if(!geometry_query) return false;
    ControllerProbe r{};r.ms=static_cast<uint32_t>(GetTickCount64());r.actor=actor;r.physical=physical;r.margin=margin;
    uint64_t controller=0,state=0,container=0,owner=0,plane=0;
    float position[4]{},quaternion[4]{},height=0,radius=0;
    r.present=column(target,kController,0x20,controller);
    r.owned=r.present && field(controller,0,state) && state && field(controller,8,container) && container &&
        field(container,0x18,owner) && owner==actor && column(target,kPlane,0x10,plane) &&
        cp(state+0x10,position,sizeof position) && cp(plane,quaternion,sizeof quaternion) &&
        field(state,0x20,height) && field(state,0x24,radius) &&
        entity_capsule_geometry::capsule(player.position,player.quaternion,player.radius,player.height,r.player_capsule,r.player_pose) &&
        entity_capsule_geometry::capsule(position,quaternion,radius,height<2*radius?2*radius:height,r.target_capsule,r.target_pose) &&
        unchanged(player.world,actor,target) && unchanged(player.world,player.actor,player.owner);
    if(r.owned) {
        const uint32_t flags=1;
        r.overlap=geometry_query(&r.player_capsule,&r.player_pose,&r.target_capsule,&r.target_pose,&flags,nullptr)!=0;
        r.gap_known=entity_capsule_geometry::surface_gap(r.player_capsule,r.player_pose,r.target_capsule,r.target_pose,r.gap);
        uint64_t current_controller=0,current_state=0,current_container=0,current_owner=0,current_plane=0;
        float current_position[4]{},current_quaternion[4]{},current_height=0,current_radius=0;
        r.owned=column(target,kController,0x20,current_controller) && current_controller==controller &&
            field(controller,0,current_state) && current_state==state && field(controller,8,current_container) && current_container==container &&
            field(container,0x18,current_owner) && current_owner==actor && column(target,kPlane,0x10,current_plane) && current_plane==plane &&
            cp(state+0x10,current_position,sizeof current_position) && !std::memcmp(position,current_position,sizeof position) &&
            cp(plane,current_quaternion,sizeof current_quaternion) && !std::memcmp(quaternion,current_quaternion,sizeof quaternion) &&
            field(state,0x20,current_height) && current_height==height && field(state,0x24,current_radius) && current_radius==radius &&
            unchanged(player.world,actor,target) && unchanged(player.world,player.actor,player.owner);
        r.qualified=r.owned && physical && r.gap_known && r.gap<=margin;
    }
    if(controller_probe_total.load()<4096 && TryAcquireSRWLockExclusive(&contact_log_lock)) {
        if(controller_probe_count<controller_probes.size()) {controller_probes[controller_probe_count++]=r;++controller_probe_total;}
        else ++contact_log_dropped;
        ReleaseSRWLockExclusive(&contact_log_lock);
    }
    return r.qualified;
}
inline void record_contact(uint64_t actor,uint64_t key,uint8_t flags,uint32_t type,bool hit,const float* position,float distance=-1,
    const entity_capsule_geometry::Capsule* capsule=nullptr,const entity_capsule_geometry::Pose* player_pose=nullptr,
    const entity_capsule_geometry::Pose* shape_pose=nullptr,const entity_capsule_geometry::Capsule* target_capsule=nullptr,bool qualified=false,double margin=contact_margin()) {
    if (contact_logged.load()>=4096 || !TryAcquireSRWLockExclusive(&contact_log_lock)) return;
    if(contact_log_count<contact_log.size()) {
        auto& r=contact_log[contact_log_count++];r={static_cast<uint32_t>(GetTickCount64()),type,actor,key,flags,static_cast<uint8_t>(hit),{},distance};
        std::memcpy(r.position,position,sizeof r.position);r.qualified=qualified;r.margin=margin;
        if(capsule && player_pose && shape_pose) {
            r.player_capsule=*capsule;r.player_pose=*player_pose;r.shape_pose=*shape_pose;r.geometry_captured=true;
        }
        if(target_capsule) {
            r.shape_capsule=*target_capsule;r.target_capsule=true;
            if(capsule && player_pose && shape_pose) r.gap_known=entity_capsule_geometry::surface_gap(*capsule,*player_pose,*target_capsule,*shape_pose,r.surface_gap);
        }
        ++contact_logged;
    } else ++contact_log_dropped;
    ReleaseSRWLockExclusive(&contact_log_lock);
}
inline uint64_t method(uint64_t object,unsigned offset) {
    uint64_t vt=0,fn=0; MEMORY_BASIC_INFORMATION page{};
    if (!field(object,0,vt) || !field(vt,offset,fn) || !fn ||
        !VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(fn)),&page,sizeof page) || page.AllocationBase!=physics_module) return 0;
    return fn;
}
inline bool body_valid(uint64_t physics_world,uint64_t key) {
    uint32_t size=0; uint64_t keys=0,actual=0,bodies=0,body=0,vt=0,method=0;
    return static_cast<uint32_t>(key)!=0xffffffffu && field(physics_world,0x2d8,size) &&
        static_cast<uint32_t>(key)<size && field(physics_world,0x2d0,keys) &&
        cp(combat_native::at(keys,static_cast<uint32_t>(key),8),&actual,8) && actual==key &&
        field(physics_world,0x1d0,bodies) && cp(combat_native::at(bodies,static_cast<uint32_t>(key),8),&body,8) &&
        body && field(body,0,vt) && vt && field(vt,0xd0,method) && method;
}
inline bool physical_overlap(const Player& player,uint64_t scene,uint64_t key,uint64_t target,unsigned& queried,int64_t deadline,bool* physical=nullptr,double margin=contact_margin(),unsigned query_limit=32) {
    using namespace entity_capsule_geometry;
    (void)target;
    if(!geometry_query || !physics_module || !body_valid(scene,key)) return false;
    uint64_t table=0,body=0;
    if(!field(scene,0x1d0,table) || !cp(combat_native::at(table,static_cast<uint32_t>(key),8),&body,8)) return false;
    const auto count_fn=method(body,0xc8),shapes_fn=method(body,0xd0),pose_fn=method(body,0xa8);
    if(!count_fn || !shapes_fn || !pose_fn) { ++fresh_unavailable; return false; }
    const auto count=reinterpret_cast<uint32_t (*)(void*)>(count_fn)(reinterpret_cast<void*>(body));
    if(!count || count>64) return false;
    std::array<uint64_t,64> shapes{};
    const auto got=reinterpret_cast<uint32_t (*)(void*,void*,uint32_t,uint32_t)>(shapes_fn)(reinterpret_cast<void*>(body),shapes.data(),count,0);
    if(got!=count || !body_valid(scene,key)) return false;
    alignas(16) Pose actor_pose{},capsule_pose{};
    Capsule cap{};
    reinterpret_cast<void* (*)(void*,void*)>(pose_fn)(reinterpret_cast<void*>(body),&actor_pose);
    if(!valid(actor_pose) || !capsule(player.position,player.quaternion,player.radius,player.height,cap,capsule_pose)) return false;
    // Contact allowance expands only this local query; the live controller stays native.
    if(!inflate_query(cap,margin))return false;
    for(unsigned i=0;i<count && queried<std::min(32u,query_limit) && tick()<deadline;++i) {
        const auto shape=shapes[i],flags_fn=method(shape,0x130),local_fn=method(shape,0x60),geometry_fn=method(shape,0x48);
        if(!flags_fn || !local_fn || !geometry_fn) { ++fresh_unavailable; continue; }
        uint8_t flags=0; uint32_t type=UINT32_MAX;
        reinterpret_cast<void* (*)(void*,void*)>(flags_fn)(reinterpret_cast<void*>(shape),&flags);
        const auto geometry=reinterpret_cast<uint64_t (*)(void*)>(geometry_fn)(reinterpret_cast<void*>(shape));
        if(!geometry || !field(geometry,0,type)) continue;
        if(!entity_shape(flags,type)) continue;
        alignas(16) Pose local_pose{},world_pose{};
        reinterpret_cast<void* (*)(void*,void*)>(local_fn)(reinterpret_cast<void*>(shape),&local_pose);
        if(!compose(actor_pose,local_pose,world_pose) || !body_valid(scene,key)) continue;
        if(physical) *physical=true;
        const uint32_t query_flags=1;
        ++queried; ++fresh_queries;
        const bool qualified=geometry_query(&cap,&capsule_pose,reinterpret_cast<void*>(geometry),&world_pose,&query_flags,nullptr)!=0;
        if(qualified && body_valid(scene,key)) return true;
    }
    return false;
}
// Resolve the typed PhysicsScene global again for each actual jump press.
// It is accepted only after its identity matched a native controller borrow.
// Candidate proximity is ONLY a broadphase filter; only exact body geometry
// overlap or capsule surface gap within the fixed margin may authorize a step. No contact result/pointer survives this call.
inline SRWLOCK nearby_lock=SRWLOCK_INIT;
inline nearby_candidates::Cache nearby_cache{};
inline uint64_t discovery_world=0,discovery_player=0;
inline uint32_t discovery_arch=0,discovery_row=0;
// Candidate IDs only. Every jump still resolves current rows, scene and geometry.
inline void discover_nearby(const Player& p) {
    if(!TryAcquireSRWLockExclusive(&nearby_lock)) return;
    struct Unlock {~Unlock(){ReleaseSRWLockExclusive(&nearby_lock);}} unlock;
    nearby_cache.context(p.world,p.actor);
    if(discovery_world!=p.world || discovery_player!=p.actor) {discovery_world=p.world;discovery_player=p.actor;discovery_arch=discovery_row=0;}
    uint64_t header=0,total=0;
    if(!field(p.world,0x58478,header) || !field(header,8,total) || !total || total>0xffff) return;
    discovery_arch%=static_cast<uint32_t>(total);
    const auto started=tick();unsigned inspected=0;
    for(unsigned ai=0;ai<128 && tick()-started<frequency/1000 && inspected<256;++ai) {
        const auto arch=discovery_arch;step_binding::Meta meta{};
        uint32_t keys[step_binding::kMetaMax]{},offsets[step_binding::kMetaMax]{};
        uint32_t transform=0;bool physical=false,controller=false;
        if(step_binding::read_meta(p.world,static_cast<uint16_t>(arch),&meta) &&
            step_binding::read_u32s(meta.keys,meta.key_count,keys) && step_binding::read_u32s(meta.offsets,meta.key_count,offsets)) {
            for(unsigned i=0;i<meta.key_count;++i) {if(keys[i]==0x6cfbb2a9) transform=offsets[i];if(keys[i]==kPhysicsEntity && offsets[i]) physical=true;if(keys[i]==kController && offsets[i]) controller=true;}
        }
        uint64_t chunk=0;uint32_t rows=0;
        if(transform && (physical || controller) && cp(combat_native::at(p.world,arch,8,0x50),&chunk,8) && chunk &&
            cp(combat_native::at(p.world,arch,4,0x10448),&rows,4) && rows<=combat_native::cols::kMaxRow) {
            for(;discovery_row<rows && inspected<256 && tick()-started<frequency/1000;++discovery_row) {
                ++inspected;uint64_t actor=0;float position[4]{};
                if(!cp(combat_native::at(chunk,discovery_row,8,0x10),&actor,8) || actor==p.actor || !step_binding::is_enemy(p.world,actor) ||
                    !cp(combat_native::at(chunk+transform,discovery_row,0x20,0x10),position,sizeof position)) continue;
                float distance=0;for(unsigned i=0;i<3;++i) {const float d=position[i]-p.position[i];distance+=d*d;}
                nearby_cache.offer(actor,distance,controller,static_cast<uint32_t>(GetTickCount64()));
            }
            if(discovery_row<rows) break;
        }
        discovery_arch=(arch+1)%static_cast<uint32_t>(total);discovery_row=0;
    }
}
inline bool contact_now(uint64_t world,uint64_t actor,uint64_t* hit_actor) {
    ErrorGuard guard;
    if (!hit_actor || !enabled.load() || !stopping || stopping->load() || verified_scene_world.load()!=world) return false;
    *hit_actor=0;
    Player p{}; uint64_t wrapper=0,scene=0,controller=0,plane=0,owner=0;
    p.world=world; p.actor=actor;
    if (!scene_resource(world,wrapper,scene) || !stamp(world,actor,p.owner) ||
        !column(p.owner,kController,0x20,controller) || !field(controller,0,p.state) || !p.state ||
        !field(controller,8,p.container) || !p.container || !field(p.container,0x18,owner) || owner!=actor ||
        !column(p.owner,kPlane,0x10,plane) || !cp(p.state+0x10,p.position,sizeof p.position) ||
        !cp(plane,p.quaternion,sizeof p.quaternion) || !field(p.state,0x20,p.height) || !field(p.state,0x24,p.radius)) {
        ++fresh_unavailable; return false;
    }
    float norm=0;
    for (unsigned i=0;i<4;++i) { if (!std::isfinite(p.position[i]) || !std::isfinite(p.quaternion[i])) return false; norm+=p.quaternion[i]*p.quaternion[i]; }
    if (!std::isfinite(p.height) || !std::isfinite(p.radius) || p.radius<=0 || p.height<=0 || norm<0.5f || norm>1.5f) return false;
    if (p.height<2*p.radius) p.height=2*p.radius;
    uint64_t header=0,arches=0;
    if (!field(world,0x58478,header) || !field(header,8,arches) || !arches || arches>0xffff) return false;
    const auto started=tick(); unsigned queried=0,scanned=0;
#if CRMOD_COMBAT_V1
    const double margin=contact_margin(); // Fixed80cm contact reach; legacy saved tuning cannot override it.
#else
    constexpr double margin=0;
#endif
    uint64_t selected_column=0,selected_actor=0;Stamp priority{};
    const bool have_priority=step_binding::resolve_player_column(world,actor,
        step_binding::fnv1a("heron::camera_lock_on::component::CameraSelectedTarget"),0x10,&selected_column) &&
        cp(selected_column,&selected_actor,8) && selected_actor!=actor && stamp(world,selected_actor,priority);
    ContactScan scan{};scan.ms=static_cast<uint32_t>(GetTickCount64());scan.actor=actor;scan.selected=have_priority ? selected_actor : 0;
    struct FinishScan {
        ContactScan& scan;unsigned& queried;unsigned& scanned;int64_t started;
        ~FinishScan() {
            scan.rows=scanned;scan.queries=queried;scan.elapsed_us=static_cast<uint32_t>((tick()-started)*1000000/frequency);
            if(scan.elapsed_us>=4000) scan.incomplete|=Time;
            if(queried>=32) scan.incomplete|=QueryCap;
            if(scanned>=4096) scan.incomplete|=RowCap;
        }
    } finish{scan,queried,scanned,started};
    if(arches>4096) scan.incomplete|=ArchCap;
    const auto expired=[&]() { return tick()-started>=frequency/250; }; // 4ms cap between calls, only on a fresh jump.
    const auto query_entity=[&](uint64_t target,const Stamp& target_stamp) -> bool {
            if(!step_binding::is_enemy(world,target)) return false;
            if(have_priority && target==selected_actor) scan.selected_seen=true;
            uint64_t reference=0;
            if(!column(target_stamp,kPhysicsEntity,0x10,reference) || expired()) return false;
            uint8_t bytes[16]{}; uint32_t index=UINT32_MAX; uint64_t table=0,descriptor=0,bodies=0; uint16_t count=0;
            if (!cp(reference,bytes,sizeof bytes)) return false; std::memcpy(&index,bytes+8,4);
            if (index==UINT32_MAX || !field(scene,0xc8,table) || !table ||
                !field(combat_native::at(table,index,0x70),0x20,descriptor) || !descriptor ||
                !field(descriptor,0x90,bodies) || !field(descriptor,0xa8,count) || (count && !bodies)) return false;
            const auto query_start=queried;
            for (uint32_t body=0;body<count && queried<32 && queried-query_start<8 && !expired();++body) {
                uint64_t key=0,flags_table=0; uint16_t flags=0;
                if (!cp(combat_native::at(bodies,body,8),&key,8) || !body_valid(scene,key) ||
                    !field(scene,0x198,flags_table) || !cp(combat_native::at(flags_table,static_cast<uint32_t>(key),12,2),&flags,2) || !(flags&0x10)) continue;
                uint64_t w2=0,s2=0,current_bodies=0,current_descriptor=0,current_table=0;
                uint16_t current_count=0; float current_position[4]{},current_quaternion[4]{};
                uint8_t current_bytes[16]{};
                if (!scene_resource(world,w2,s2) || w2!=wrapper || s2!=scene || !unchanged(world,actor,p.owner) ||
                    !unchanged(world,target,target_stamp) || !cp(p.state+0x10,current_position,sizeof current_position) ||
                    std::memcmp(current_position,p.position,sizeof current_position) ||
                    !cp(plane,current_quaternion,sizeof current_quaternion) || std::memcmp(current_quaternion,p.quaternion,sizeof current_quaternion) ||
                    !cp(reference,current_bytes,sizeof current_bytes) || std::memcmp(current_bytes,bytes,sizeof bytes)) continue;
                bool physical=false;
                const bool body_hit=physical_overlap(p,scene,key,target,queried,started+frequency/250,&physical,margin,std::min(32u,query_start+8));
                const bool hit=body_hit;
                if (hit && scene_resource(world,w2,s2) && w2==wrapper && s2==scene &&
                    unchanged(world,actor,p.owner) && unchanged(world,target,target_stamp) && step_binding::is_enemy(world,target) && body_valid(scene,key) &&
                    cp(p.state+0x10,current_position,sizeof current_position) && !std::memcmp(current_position,p.position,sizeof current_position) &&
                    cp(plane,current_quaternion,sizeof current_quaternion) && !std::memcmp(current_quaternion,p.quaternion,sizeof current_quaternion) &&
                    cp(reference,current_bytes,sizeof current_bytes) && !std::memcmp(current_bytes,bytes,sizeof bytes) &&
                    field(scene,0xc8,current_table) && current_table==table &&
                    field(combat_native::at(current_table,index,0x70),0x20,current_descriptor) && current_descriptor==descriptor &&
                    field(descriptor,0x90,current_bodies) && current_bodies==bodies &&
                    field(descriptor,0xa8,current_count) && current_count==count) {
                    *hit_actor=target;scan.hit=target; ++fresh_contacts; return true;
                }
            }
        return false;
    };
    nearby_candidates::Snapshot candidates{};
    if(TryAcquireSRWLockShared(&nearby_lock)) {candidates=nearby_cache.snapshot(world,actor,static_cast<uint32_t>(GetTickCount64()));ReleaseSRWLockShared(&nearby_lock);}
    scan.nearby_candidates=candidates.count;
    // No selected-target gate: all nearby handles precede the old target hint.
    for(unsigned i=0;i<candidates.count && !expired() && queried<32;++i) {
        const auto target=candidates.entries[i].actor;Stamp current{};uint64_t transform=0;float position[4]{};
        if(target==actor || !stamp(world,target,current) || !column(current,0x6cfbb2a9,0x20,transform) || !cp(transform+0x10,position,sizeof position)) continue;
        float distance=0;for(unsigned j=0;j<3;++j) {const auto d=position[j]-p.position[j];distance+=d*d;}
        if(!std::isfinite(distance) || distance>144) continue;
        ++scanned;++scan.nearby_checked;if(query_entity(target,current)) return true;
    }
    for (uint32_t ai=0;ai<arches && ai<4096 && !expired();++ai) {
        const auto arch=static_cast<uint32_t>((ai+(have_priority ? priority.loc.arch : 0))%arches);
        step_binding::Meta meta{}; uint32_t keys[step_binding::kMetaMax]{},offsets[step_binding::kMetaMax]{};
        if (!step_binding::read_meta(world,static_cast<uint16_t>(arch),&meta) ||
            !step_binding::read_u32s(meta.keys,meta.key_count,keys) || !step_binding::read_u32s(meta.offsets,meta.key_count,offsets)) continue;
        uint32_t reference_offset=0,transform_offset=0,controller_offset=0;
        for (uint32_t i=0;i<meta.key_count;++i) { if (keys[i]==kPhysicsEntity) reference_offset=offsets[i]; if (keys[i]==0x6cfbb2a9) transform_offset=offsets[i]; if(keys[i]==kController) controller_offset=offsets[i]; }
        if (!transform_offset || (!reference_offset && !controller_offset)) continue;
        uint64_t chunk=0; uint32_t rows=0;
        if (!cp(combat_native::at(world,arch,8,0x50),&chunk,8) || !chunk || !cp(combat_native::at(world,arch,4,0x10448),&rows,4) || rows>combat_native::cols::kMaxRow) continue;
        for (uint32_t ri=0;ri<rows && !expired() && scanned<4096 && queried<32;++ri) {
            const auto row=(ri+(have_priority && priority.loc.arch==arch ? priority.loc.row : 0))%rows;
            ++scanned; uint64_t target=0; float position[4]{};
            if (!cp(combat_native::at(chunk,row,8,0x10),&target,8) || target==actor ||
                !cp(combat_native::at(chunk+transform_offset,row,0x20,0x10),position,sizeof position)) continue;
            float distance=0; bool finite=true;
            for (unsigned i=0;i<3;++i) { finite=finite && std::isfinite(position[i]); const auto d=position[i]-p.position[i]; distance+=d*d; }
            if (!finite || distance>144) continue; // broadphase only, never contact authority
            if(!step_binding::is_enemy(world,target)) continue;
            if(have_priority && target==selected_actor) scan.selected_seen=true;
            Stamp target_stamp{}; if (!stamp(world,target,target_stamp) || target_stamp.loc.arch!=arch || target_stamp.loc.row!=row) continue;
            if(query_entity(target,target_stamp)) return true;
        }
        if (scanned>=4096 || queried>=32) break;
    }
    return false;
}
inline void push(const Record& r) {
    if (!TryAcquireSRWLockExclusive(&queue_lock)) { ++dropped; return; }
    if (count<queue.size() && pushed.load()<kRecords) { queue[count++]=r; ++pushed; }
    else ++dropped;
    ReleaseSRWLockExclusive(&queue_lock);
}
inline void capture(const void* cols,const void* ctx,const Invocation& invocation,uint8_t phase) {
    ErrorGuard guard; Record r{}; r.invocation=invocation.serial; r.phase=phase;
    r.ms=static_cast<uint32_t>(GetTickCount64()); r.thread=GetCurrentThreadId();
    const int64_t started=tick(); r.qpc=static_cast<uint64_t>(started);
    Player p{}; uint64_t physics_world=0;
    if (!active() || !player(cols,ctx,p) || p.world!=invocation.world || p.actor!=invocation.actor || !physics(ctx,physics_world)) {
        r.incomplete=Ownership; r.world=invocation.world; r.actor=invocation.actor; push(r); return;
    }
    r.world=p.world; r.actor=p.actor; r.pulse=p.pulse; r.airborne=p.airborne;
    std::memcpy(r.njr,p.njr,2); std::memcpy(r.position,p.position,sizeof r.position);
    std::memcpy(r.quaternion,p.quaternion,sizeof r.quaternion); r.radius=p.radius; r.height=p.height;
    if (!field(p.jump,0x14,r.jump_phase) || !field(p.jump,0x20,r.jump_index)) r.incomplete|=Read;
    if (!TryAcquireSRWLockExclusive(&scan_lock)) { r.incomplete|=Busy; push(r); return; }
    const auto expired=[&]() { return tick()-started>=frequency/500; }; // 2ms between native calls; a single call cannot be preempted.
    uint64_t archetype_header=0,total=0;
    if (!field(p.world,0x58478,archetype_header) || !field(archetype_header,8,total) || !total || total>0xffff) r.incomplete|=Read;
    else {
        uint32_t arch=cursor_arch.load()%static_cast<uint32_t>(total),row_start=cursor_row.load(),body_start=cursor_body.load();
        if (row_start || body_start) r.incomplete|=PartialStart;
        const uint32_t max_arch=total<kArches?static_cast<uint32_t>(total):kArches;
        bool halt=false;
        for (uint32_t ai=0;ai<max_arch && !halt;++ai) {
            if (expired()) { r.incomplete|=Time; break; }
            ++r.arches; step_binding::Meta meta{}; bool enemy=false,dead=false;
            uint64_t chunk=0; uint32_t rows=0;
            if (!step_binding::read_meta(p.world,static_cast<uint16_t>(arch),&meta) ||
                !step_binding::has_tag(meta,step_binding::kHashNPCEnemy,&enemy) ||
                !step_binding::has_tag(meta,step_binding::kTagDead,&dead)) r.incomplete|=Read;
            else if (enemy && !dead) {
                if (!cp(combat_native::at(p.world,arch,8,0x50),&chunk,8) ||
                    !cp(combat_native::at(p.world,arch,4,0x10448),&rows,4) || rows>combat_native::cols::kMaxRow || (rows && !chunk)) r.incomplete|=Read;
                else for (uint32_t ri=row_start;ri<rows;++ri) {
                    cursor_arch.store(arch); cursor_row.store(ri);
                    cursor_body.store(ri==row_start?body_start:0);
                    if (expired() || r.queries>=kQueries || r.rows>=kRows) {
                        r.incomplete|=expired()?Time:(r.queries>=kQueries?QueryCap:RowCap); halt=true; break;
                    }
                    ++r.rows; uint64_t actor=0,physics_row=0; Stamp target{};
                    uint8_t component[16]{}; uint32_t index=0;
                    if (!cp(combat_native::at(chunk,ri,8,0x10),&actor,8) || !stamp(p.world,actor,target) ||
                        target.loc.arch!=arch || target.loc.row!=ri || !step_binding::is_enemy(p.world,actor)) { r.incomplete|=Read; continue; }
                    ++r.enemies;
                    if (!column(target,kPhysicsEntity,0x10,physics_row)) { r.incomplete|=Read; continue; }
                    uint64_t entity_table=0,descriptor=0,keys=0; uint16_t body_count=0;
                    if (!cp(physics_row,component,16)) { r.incomplete|=Read; continue; }
                    std::memcpy(&index,component+8,4);
                    if (index==0xffffffffu || !field(physics_world,0xc8,entity_table) || !entity_table ||
                        !field(combat_native::at(entity_table,index,0x70),0x20,descriptor) || !descriptor ||
                        !field(descriptor,0x90,keys) || !field(descriptor,0xa8,body_count) || (body_count && !keys)) { r.incomplete|=Read; continue; }
                    for (uint32_t bi=ri==row_start?body_start:0;bi<body_count;++bi) {
                        cursor_body.store(bi);
                        if (expired() || r.queries>=kQueries) { r.incomplete|=expired()?Time:QueryCap; halt=true; break; }
                        ++r.bodies; uint64_t key=0; uint16_t flags=0; uint64_t flag_table=0;
                        if (!cp(combat_native::at(keys,bi,8),&key,8) || !body_valid(physics_world,key) ||
                            !field(physics_world,0x198,flag_table) || !cp(combat_native::at(flag_table,static_cast<uint32_t>(key),12,2),&flags,2)) { r.incomplete|=Read; continue; }
                        if (!(flags&0x10)) continue; // Same queryable-body predicate as native1427783a0.
                        uint64_t current_physics=0,current_keys=0; uint16_t current_count=0; uint8_t current_component[16]{};
                        Player current{};
                        if (!physics(ctx,current_physics) || current_physics!=physics_world || !player(cols,ctx,current) || !same_player(p,current) ||
                            !unchanged(p.world,actor,target) || !cp(physics_row,current_component,16) || std::memcmp(component,current_component,16) ||
                            !field(descriptor,0x90,current_keys) || current_keys!=keys || !field(descriptor,0xa8,current_count) || current_count!=body_count ||
                            !body_valid(physics_world,key)) { r.incomplete|=Ownership; halt=true; break; }
                        if (expired()) { r.incomplete|=Time; halt=true; break; }
                        ++r.queries;
                        const bool hit=reinterpret_cast<OverlapFn>(step_binding::env().base+kOverlapRva)(physics_world,key,p.position,p.quaternion,p.radius,p.height)!=0;
                        if (!unchanged(p.world,actor,target) || !step_binding::is_enemy(p.world,actor) ||
                            !physics(ctx,current_physics) || current_physics!=physics_world || !body_valid(physics_world,key) ||
                            !cp(physics_row,current_component,16) || std::memcmp(component,current_component,16) ||
                            !field(descriptor,0x90,current_keys) || current_keys!=keys || !field(descriptor,0xa8,current_count) || current_count!=body_count) {
                            r.incomplete|=Ownership; halt=true; break;
                        }
                        if (hit) { ++r.hits; if (r.shown<8) r.hit[r.shown++]={actor,key}; }
                    }
                    if (halt) break;
                    body_start=0; cursor_body.store(0);
                    cursor_row.store(ri+1);
                }
            }
            if (!halt) { arch=(arch+1)%static_cast<uint32_t>(total); row_start=0; body_start=0; cursor_arch.store(arch); cursor_row.store(0); cursor_body.store(0); }
        }
        if (total>max_arch) r.incomplete|=ArchCap;
    }
    Player after{}; uint64_t current_physics=0;
    if (!player(cols,ctx,after) || !same_player(p,after) || !physics(ctx,current_physics) || current_physics!=physics_world) r.incomplete|=Ownership;
    r.elapsed_us=static_cast<uint32_t>((tick()-started)*1000000/frequency);
    if (r.elapsed_us>=2000) r.incomplete|=Time;
    ReleaseSRWLockExclusive(&scan_lock); push(r);
}
// Quiet runtime validation shares the native collision scope without probing.
inline void verify_scene(const void* cols,const void* ctx) {
    ErrorGuard guard; Player p{};
    if (!enabled.load() || !stopping || stopping->load() || !player(cols,ctx,p)) return;
    uint64_t native_scene=0,wrapper=0,current_scene=0;
    if (physics(ctx,native_scene) && scene_resource(p.world,wrapper,current_scene) && current_scene==native_scene) {
        verified_scene_world.store(p.world);
#if CRMOD_COMBAT_V1
        const auto now=static_cast<uint32_t>(GetTickCount64());auto next=next_ms.load();
        if((!next||static_cast<int32_t>(now-next)>=0)&&next_ms.compare_exchange_strong(next,now+100))discover_nearby(p);
#endif
    }
}
inline Invocation begin(const void* cols,const void* ctx) {
    ErrorGuard guard; Invocation result{}; Player p{};
    uint64_t world=0;
    if (enabled.load() && stopping && !stopping->load() && field(reinterpret_cast<uintptr_t>(ctx),0,world) && verified_scene_world.load()!=world && player(cols,ctx,p)) {
        uint64_t native_scene=0,wrapper=0,current_scene=0;
        if (physics(ctx,native_scene) && scene_resource(p.world,wrapper,current_scene) && current_scene==native_scene) {
            verified_scene_world.store(p.world);
            step_binding::env().contact_now.store(&contact_now);
            step_binding::proof().contact=true; step_binding::proof().cancel=true;
            step_binding::refresh_status();
        }
    }
    if (!enabled.load() || !stopping || stopping->load()) return result;
    const auto now=static_cast<uint32_t>(GetTickCount64()); auto next=next_ms.load();
    if (next && static_cast<int32_t>(now-next)<0) return result;
    if (!player(cols,ctx,p)) { ++unqualified; return result; }
    uint64_t native_scene=0,wrapper=0,current_scene=0;
    if (physics(ctx,native_scene) && scene_resource(p.world,wrapper,current_scene) && current_scene==native_scene) {
        verified_scene_world.store(p.world);
        step_binding::env().contact_now.store(&contact_now);
        step_binding::proof().contact=true; step_binding::proof().cancel=true; // Original jump input performs the stock cancel.
    }
    if (!next_ms.compare_exchange_strong(next,now+100)) return result;
    discover_nearby(p);
    if (!active()) return result;
    result={++serial,p.world,p.actor}; capture(cols,ctx,result,0); return result;
}
inline void end(const void* cols,const void* ctx,const Invocation& invocation) {
    if (invocation.serial) capture(cols,ctx,invocation,1);
}
inline bool arm(uint64_t base,const std::atomic<bool>& stop) {
    ErrorGuard guard; uint8_t bytes[sizeof kOverlapPin]{}; LARGE_INTEGER f{};
    if (!base || base!=step_binding::env().base || enabled.load() || stop.load() ||
        !cp(base+kOverlapRva,bytes,sizeof bytes) || std::memcmp(bytes,kOverlapPin,sizeof bytes) ||
        !QueryPerformanceFrequency(&f) || f.QuadPart<1000) return false;
    physics_module=GetModuleHandleW(L"PhysX_64.dll");
    const auto ctor=physics_module ? GetProcAddress(physics_module,"??0PxShapeGeneratedValues@physx@@QEAA@PEBVPxShape@1@@Z") : nullptr;
    constexpr uint8_t flags_pin[]={0x48,0x8d,0x57,0x54,0x48,0x8b,0x03,0x48,0x8b,0xcb,0xff,0x90,0x30,0x01,0x00,0x00};
    uint8_t actual[sizeof flags_pin]{};
    if(!ctor || !cp(reinterpret_cast<uintptr_t>(ctor)+0xa2,actual,sizeof actual) || std::memcmp(actual,flags_pin,sizeof actual)) return false;
    const auto common=GetModuleHandleW(L"PhysXCommon_64.dll");
    geometry_query=common ? reinterpret_cast<GeometryQuery>(GetProcAddress(common,
        "?overlap@PxGeometryQuery@physx@@SA_NAEBVPxGeometry@2@AEBV?$PxTransformT@M@2@01V?$PxFlags@W4Enum@PxGeometryQueryFlag@physx@@I@2@PEAUPxQueryThreadContext@2@@Z")) : nullptr;
    point_distance=common ? reinterpret_cast<PointDistance>(GetProcAddress(common,
        "?pointDistance@PxGeometryQuery@physx@@SAMAEBV?$PxVec3T@M@2@AEBVPxGeometry@2@AEBV?$PxTransformT@M@2@PEAV32@PEAIV?$PxFlags@W4Enum@PxGeometryQueryFlag@physx@@I@2@@Z")) : nullptr;
    if(!geometry_query || !point_distance) return false;
    frequency=f.QuadPart; stopping=&stop; enabled.store(true);
    if (stop.load()) enabled.store(false); return enabled.load();
}
inline void stop() { enabled.store(false); }
inline void flush(FILE* file) {
    static bool api_reported=false;
    if(file && !api_reported) {
        std::fprintf(file,"{\"event\":\"entity_physical_query_binding\",\"armed\":%s,\"trigger_filter\":true,\"infinite_planes_excluded\":true,\"per_shape_overlap\":true}\n",enabled.load() && geometry_query ? "true":"false");
        api_reported=true;
    }
    if (!file || !TryAcquireSRWLockExclusive(&queue_lock)) return;
    std::array<Record,64> batch{}; const unsigned n=count;
    std::memcpy(batch.data(),queue.data(),n*sizeof(Record)); count=0; ReleaseSRWLockExclusive(&queue_lock);
    for (unsigned i=0;i<n;++i) {
        const auto& r=batch[i];
        std::fprintf(file,"{\"event\":\"enemy_overlap_probe\",\"invocation\":%llu,\"phase\":%u,\"qpc\":%llu,\"ms\":%u,\"thread\":%u,\"world\":%llu,\"actor\":\"%llu\",\"incomplete\":%u,\"complete\":%s,\"queries\":%u,\"hits\":%u,\"arches\":%u,\"rows\":%u,\"enemies\":%u,\"bodies\":%u,\"elapsed_us\":%u,\"jump_pulse\":%u,\"airborne\":%u,\"njr\":[%u,%u],\"jump_phase\":%u,\"jump_index\":%u,\"position\":[%.9g,%.9g,%.9g],\"quaternion\":[%.9g,%.9g,%.9g,%.9g],\"radius\":%.9g,\"height\":%.9g,\"hit_bodies\":[",
            static_cast<unsigned long long>(r.invocation),r.phase,static_cast<unsigned long long>(r.qpc),r.ms,r.thread,
            static_cast<unsigned long long>(r.world),static_cast<unsigned long long>(r.actor),r.incomplete,r.incomplete?"false":"true",
            r.queries,r.hits,r.arches,r.rows,r.enemies,r.bodies,r.elapsed_us,r.pulse,r.airborne,r.njr[0],r.njr[1],r.jump_phase,r.jump_index,
            r.position[0],r.position[1],r.position[2],r.quaternion[0],r.quaternion[1],r.quaternion[2],r.quaternion[3],r.radius,r.height);
        for (unsigned j=0;j<r.shown;++j) std::fprintf(file,"%s{\"actor\":\"%llu\",\"body_key\":\"%llu\"}",j?",":"",
            static_cast<unsigned long long>(r.hit[j].actor),static_cast<unsigned long long>(r.hit[j].key));
        std::fprintf(file,"],\"hits_truncated\":%s,\"contact_authority\":false,\"writes\":false}\n",r.hits>r.shown?"true":"false");
    }
    const auto lost=dropped.exchange(0),unknown=unqualified.exchange(0);
    if (lost || unknown) std::fprintf(file,"{\"event\":\"enemy_overlap_probe_summary\",\"dropped\":%llu,\"unqualified\":%llu}\n",
        static_cast<unsigned long long>(lost),static_cast<unsigned long long>(unknown));
    if(TryAcquireSRWLockExclusive(&contact_log_lock)) {
        for(unsigned i=0;i<contact_log_count;++i) {
            const auto& r=contact_log[i];
            std::fprintf(file,"{\"event\":\"entity_contact_shape\",\"ms\":%u,\"actor\":\"%llu\",\"body\":\"%llu\",\"shape_flags\":%u,\"geometry_type\":%u,\"eligible\":%s,\"overlap\":%s,\"player_position\":[%.9g,%.9g,%.9g],\"center_distance_sq\":%.9g}\n",
                r.ms,static_cast<unsigned long long>(r.actor),static_cast<unsigned long long>(r.key),r.flags,r.type,
                entity_capsule_geometry::entity_shape(r.flags,r.type)?"true":"false",r.hit?"true":"false",r.position[0],r.position[1],r.position[2],r.center_distance_sq);
            if(r.geometry_captured) std::fprintf(file,
                "{\"event\":\"entity_contact_geometry\",\"ms\":%u,\"actor\":\"%llu\",\"body\":\"%llu\",\"player_center\":[%.9g,%.9g,%.9g],\"player_quaternion\":[%.9g,%.9g,%.9g,%.9g],\"player_radius\":%.9g,\"player_half_height\":%.9g,\"shape_center\":[%.9g,%.9g,%.9g],\"shape_quaternion\":[%.9g,%.9g,%.9g,%.9g],\"target_capsule\":%s,\"target_radius\":%.9g,\"target_half_height\":%.9g,\"writes\":false}\n",
                r.ms,static_cast<unsigned long long>(r.actor),static_cast<unsigned long long>(r.key),
                r.player_pose.p[0],r.player_pose.p[1],r.player_pose.p[2],r.player_pose.q[0],r.player_pose.q[1],r.player_pose.q[2],r.player_pose.q[3],
                r.player_capsule.radius,r.player_capsule.half_height,r.shape_pose.p[0],r.shape_pose.p[1],r.shape_pose.p[2],
                r.shape_pose.q[0],r.shape_pose.q[1],r.shape_pose.q[2],r.shape_pose.q[3],r.target_capsule?"true":"false",r.shape_capsule.radius,r.shape_capsule.half_height);
        }
        for(unsigned i=0;i<contact_log_count;++i) {
            const auto& r=contact_log[i];
            std::fprintf(file,"{\"event\":\"entity_surface_contact\",\"ms\":%u,\"actor\":\"%llu\",\"body\":\"%llu\",\"original_overlap\":%s,\"margin_m\":%.9g,\"qualified\":%s}\n",
                r.ms,static_cast<unsigned long long>(r.actor),static_cast<unsigned long long>(r.key),r.hit?"true":"false",r.margin,r.qualified?"true":"false");
            if(r.gap_known) std::fprintf(file,"{\"event\":\"entity_capsule_surface\",\"ms\":%u,\"actor\":\"%llu\",\"body\":\"%llu\",\"surface_gap_m\":%.9g,\"margin_m\":%.9g,\"qualified\":%s}\n",
                r.ms,static_cast<unsigned long long>(r.actor),static_cast<unsigned long long>(r.key),r.surface_gap,r.margin,r.surface_gap<=r.margin?"true":"false");
        }
        for(unsigned i=0;i<contact_scan_count;++i) {
            const auto& r=contact_scans[i];
            std::fprintf(file,"{\"event\":\"entity_contact_scan\",\"ms\":%u,\"player\":\"%llu\",\"selected_target\":\"%llu\",\"selected_seen\":%s,\"hit\":\"%llu\",\"rows\":%u,\"queries\":%u,\"elapsed_us\":%u,\"incomplete\":%u,\"writes\":false}\n",
                r.ms,static_cast<unsigned long long>(r.actor),static_cast<unsigned long long>(r.selected),r.selected_seen?"true":"false",
                static_cast<unsigned long long>(r.hit),r.rows,r.queries,r.elapsed_us,r.incomplete);
            std::fprintf(file,"{\"event\":\"entity_candidate_search\",\"ms\":%u,\"nearby_handles\":%u,\"nearby_checked\":%u,\"hit\":\"%llu\",\"locked_target_required\":false,\"cached_geometry_used\":false}\n",
                r.ms,r.nearby_candidates,r.nearby_checked,static_cast<unsigned long long>(r.hit));
        }
        for(unsigned i=0;i<controller_probe_count;++i) {
            const auto& r=controller_probes[i];
            std::fprintf(file,"{\"event\":\"entity_controller_geometry\",\"ms\":%u,\"actor\":\"%llu\",\"controller_present\":%s,\"owner_verified\":%s,\"capsules_overlap\":%s,\"player_center\":[%.9g,%.9g,%.9g],\"target_center\":[%.9g,%.9g,%.9g],\"player_radius\":%.9g,\"player_half_height\":%.9g,\"target_radius\":%.9g,\"target_half_height\":%.9g,\"contact_authority\":false,\"writes\":false}\n",
                r.ms,static_cast<unsigned long long>(r.actor),r.present?"true":"false",r.owned?"true":"false",r.overlap?"true":"false",
                r.player_pose.p[0],r.player_pose.p[1],r.player_pose.p[2],r.target_pose.p[0],r.target_pose.p[1],r.target_pose.p[2],
                r.player_capsule.radius,r.player_capsule.half_height,r.target_capsule.radius,r.target_capsule.half_height);
        }
        for(unsigned i=0;i<controller_probe_count;++i) {
            const auto& r=controller_probes[i];
            std::fprintf(file,"{\"event\":\"entity_controller_surface\",\"ms\":%u,\"actor\":\"%llu\",\"gap_known\":%s,\"surface_gap_m\":%.9g,\"margin_m\":%.9g,\"physical_body_verified\":%s,\"qualified\":%s,\"separation_required_to_rearm\":false}\n",
                r.ms,static_cast<unsigned long long>(r.actor),r.gap_known?"true":"false",r.gap_known?r.gap:0,r.margin,r.physical?"true":"false",r.qualified?"true":"false");
        }
        controller_probe_count=0;
        contact_scan_count=0;
        const auto contact_lost=contact_log_dropped.exchange(0);
        if(contact_lost) std::fprintf(file,"{\"event\":\"entity_contact_log_dropped\",\"records\":%u}\n",contact_lost);
        contact_log_count=0;ReleaseSRWLockExclusive(&contact_log_lock);
    }
    const auto q=fresh_queries.exchange(0),h=fresh_contacts.exchange(0),u=fresh_unavailable.exchange(0);
    if (q || h || u) std::fprintf(file,"{\"event\":\"entity_step_contact\",\"queries\":%llu,\"contacts\":%llu,\"unavailable\":%llu,\"enemy_only\":false,\"cached_contact_authority\":false}\n",
        static_cast<unsigned long long>(q),static_cast<unsigned long long>(h),static_cast<unsigned long long>(u));
}
} // namespace enemy_contact_native
#endif
