#pragma once
#include "facing_movement.h"
#include "held_lock_native.h"
#include "finisher_chooser_scope.h"
#ifdef _WIN32
namespace facing_lock_native {
using facing_movement::Vec3;
constexpr uint32_t kTransform=step_binding::fnv1a("coregame::component::WorldTransformReadOnly");
constexpr uint32_t kPlane=step_binding::fnv1a("coregame::component::MovementPlane");
constexpr uint32_t kSelected=step_binding::fnv1a("heron::camera_lock_on::component::CameraSelectedTarget");
static_assert(kTransform==0x6cfbb2a9 && kPlane==0xc0853848 && kSelected==0x91f0f60c);
inline std::atomic<bool> enabled{false};
inline const std::atomic<bool>* stopping=nullptr;
inline std::atomic<uint64_t> direction_reads{0};
inline bool active() { return enabled.load() && stopping && !stopping->load() && held_lock_native::active(); }

// Native92: WorldTransformReadOnly is quaternion xyzw at 0, position at 10.
// MovementPlane rotates local +Y into gravity-up; it supplies no character yaw.
inline bool axes(const float q[4],Vec3& forward,Vec3& up) {
    float n=0;
    for (unsigned i=0;i<4;++i) { if (!std::isfinite(q[i])) return false; n+=q[i]*q[i]; }
    if (!std::isfinite(n) || n<0.5f || n>1.5f) return false;
    const float s=1/std::sqrt(n),x=q[0]*s,y=q[1]*s,z=q[2]*s,w=q[3]*s;
    forward={2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)};
    up={2*(x*y-w*z),1-2*(x*x+z*z),2*(y*z+w*x)};
    return facing_movement::finite(forward) && facing_movement::finite(up);
}
struct Entity {
    loadout_actor_resolver::Location location{};
    loadout_actor_resolver::Metadata metadata{};
};
inline bool entity(uint64_t world,uint64_t actor,Entity& out) {
    out={};
    return loadout_actor_resolver::locate(world,actor,out.location) &&
        loadout_actor_resolver::field(world,0x18448+uint64_t(out.location.archetype)*0x20,out.metadata) &&
        out.metadata.tag_count<=loadout_actor_resolver::kCapacity && out.metadata.key_count<=loadout_actor_resolver::kCapacity;
}
inline bool same(const Entity& a,const Entity& b) {
    return loadout_actor_resolver::same(a.location,b.location) &&
        a.metadata.tags==b.metadata.tags && a.metadata.keys==b.metadata.keys && a.metadata.offsets==b.metadata.offsets &&
        a.metadata.tag_count==b.metadata.tag_count && a.metadata.key_count==b.metadata.key_count;
}
struct Column { uint32_t hash; size_t stride; uint64_t* row; };
inline bool columns(const Entity& e,const Column* columns,size_t n,bool player) {
    const auto& m=e.metadata;
    std::array<uint32_t,loadout_actor_resolver::kCapacity> keys{},offsets{},tags{};
    const auto cp=[](uint64_t p,void* dst,size_t size) { return combat_native::copy_checked(step_binding::env().copy,dst,p,size); };
    if ((m.key_count && (!cp(m.keys,keys.data(),size_t(m.key_count)*4) || !cp(m.offsets,offsets.data(),size_t(m.key_count)*4))) ||
        (m.tag_count && !cp(m.tags,tags.data(),size_t(m.tag_count)*4))) return false;
    bool has_player=false;
    for (unsigned i=0;i<m.tag_count;++i) {
        if (tags[i]==step_binding::kTagDead) return false;
        if (tags[i]==step_binding::kTagPlayer) has_player=true;
    }
    if (player && !has_player) return false;
    for (size_t j=0;j<n;++j) {
        *columns[j].row=0;
        for (unsigned i=0;i<m.key_count;++i) if (keys[i]==columns[j].hash) {
            if (!offsets[i]) return false;
            const uint64_t base=combat_native::at(e.location.chunk,0,0,offsets[i]);
            *columns[j].row=combat_native::at(base,e.location.row,columns[j].stride);
            break;
        }
        if (!*columns[j].row) return false;
    }
    Entity now{};
    return entity(e.location.world,e.location.actor,now) && same(e,now);
}
struct Snapshot {
    Entity player{},target_entity{};
    uint64_t input=0,lock=0,selected=0,transform=0,plane=0,movement=0;
    uint64_t target_transform=0,target=0;
    bool down=false,locked=false;
    float raw[4]{},magnitude=0;
    float transform_value[8]{},plane_value[4]{},target_value[8]{};
    facing_movement::Basis basis{};
};
inline bool capture(Snapshot& s) {
    s={}; uint64_t world=0,actor=0;
    if (!active() || !held_lock_native::identity(world,actor) || !entity(world,actor,s.player)) return false;
    const Column player_columns[]={
        {held_lock_native::kInput,0x40,&s.input},{held_lock_native::kLock,0xf0,&s.lock},{kSelected,0x10,&s.selected},
        {kTransform,0x20,&s.transform},{kPlane,0x10,&s.plane},{0xf2358b43,0x80,&s.movement}};
    if (!columns(s.player,player_columns,sizeof(player_columns)/sizeof(player_columns[0]),true) ||
        !held_lock_native::held(world,actor,s.input,s.down)) return false;
    const auto cp=[](uint64_t p,void* dst,size_t size) { return combat_native::copy_checked(step_binding::env().copy,dst,p,size); };
    uint8_t locked=0;
    if (!cp(s.lock,&locked,1)) return false;
    s.locked=s.down && locked!=0;
    if (!s.locked) return true; // A verified release clears gesture history even during native unlock cleanup.
    if (!cp(s.selected,&s.target,8) || s.target==actor || !entity(world,s.target,s.target_entity)) return false;
    const Column target_columns[]={{kTransform,0x20,&s.target_transform}};
    if (!columns(s.target_entity,target_columns,1,false) ||
        !cp(s.transform,s.transform_value,sizeof s.transform_value) || !cp(s.plane,s.plane_value,sizeof s.plane_value) ||
        !cp(s.target_transform,s.target_value,sizeof s.target_value) || !cp(s.movement,s.raw,sizeof s.raw) ||
        !cp(combat_native::at(s.movement,0,0,0x24),&s.magnitude,4) ||
        !std::isfinite(s.magnitude) || s.magnitude<0 || s.magnitude>1.01f) return false;
    Vec3 unused{},up{};
    const Vec3 toward_target={s.target_value[4]-s.transform_value[4],s.target_value[5]-s.transform_value[5],s.target_value[6]-s.transform_value[6]};
    if (!axes(s.plane_value,unused,up) ||
        !facing_movement::basis(toward_target,up,s.basis) || !facing_movement::finite({s.raw[0],s.raw[1],s.raw[2]}) ||
        !facing_movement::finite({s.transform_value[4],s.transform_value[5],s.transform_value[6]}) || !std::isfinite(s.transform_value[7]) ||
        !facing_movement::finite({s.target_value[4],s.target_value[5],s.target_value[6]})) return false;
    uint64_t target_now=0,w=0,a=0; bool down=false; uint8_t locked_now=0; Entity player_now{},target_entity_now{};
    return active() && held_lock_native::identity(w,a) && w==world && a==actor &&
        held_lock_native::held(world,actor,s.input,down) && down==s.down && cp(s.lock,&locked_now,1) && locked_now==locked &&
        cp(s.selected,&target_now,8) && target_now==s.target && entity(world,actor,player_now) && same(s.player,player_now) &&
        entity(world,s.target,target_entity_now) && same(s.target_entity,target_entity_now);
}
inline bool same(const Snapshot& a,const Snapshot& b) {
    return same(a.player,b.player) && a.input==b.input && a.lock==b.lock && a.selected==b.selected &&
        a.transform==b.transform && a.plane==b.plane && a.movement==b.movement && a.down==b.down && a.locked==b.locked &&
        a.target==b.target && a.target_transform==b.target_transform && (!a.locked ||
            (same(a.target_entity,b.target_entity) && std::memcmp(a.raw,b.raw,sizeof a.raw)==0 && a.magnitude==b.magnitude &&
             std::memcmp(a.transform_value,b.transform_value,sizeof a.transform_value)==0 &&
             std::memcmp(a.plane_value,b.plane_value,sizeof a.plane_value)==0 &&
             std::memcmp(a.target_value,b.target_value,sizeof a.target_value)==0));
}

inline bool direction_sample(const combat_mod::ChooserArgs& args,combat_mod::DirectionSample* out) {
    const DWORD error=GetLastError(); bool ok=false; Snapshot s{},again{};
    if (out && finisher_chooser_scope::live() && capture(s)) {
        const auto* loan=finisher_chooser_scope::current;
        combat_mod::DirectionSample result{};
        result.now_ms=static_cast<uint32_t>(GetTickCount64()); result.locked=s.locked; result.target_session=s.target;
        Vec3 direction{};
        const Vec3 raw{s.raw[0],s.raw[1],s.raw[2]};
        const Vec3 horizontal=facing_movement::add(raw,facing_movement::mul(s.basis.up,-facing_movement::dot(raw,s.basis.up)));
        if (s.locked) {
            result.stick_magnitude=s.magnitude>1?1:s.magnitude;
            if (facing_movement::unit(horizontal,direction)) {
                result.target_alignment=facing_movement::dot(direction,s.basis.forward);
                if (result.target_alignment>1) result.target_alignment=1;
                if (result.target_alignment<-1) result.target_alignment=-1;
            }
            else result.stick_magnitude=0;
        }
        if (loan->world==s.player.location.world && loan->actor==s.player.location.actor &&
            loan->rows.combo==reinterpret_cast<uintptr_t>(args.combo_data) && loan->rows.logic==reinterpret_cast<uintptr_t>(args.state_set) &&
            loan->graph==reinterpret_cast<uintptr_t>(args.graph) && capture(again) && same(s,again) && finisher_chooser_scope::live()) {
            *out=result; ++direction_reads; ok=true;
        }
    }
    SetLastError(error); return ok;
}

inline bool arm(uint64_t base,bool native_contracts_and_module_pin_verified,const std::atomic<bool>& stop) {
    if (!base || !native_contracts_and_module_pin_verified || enabled.load() || stop.load()) return false;
    stopping=&stop; enabled.store(true);
    if (stop.load()) enabled.store(false); return enabled.load();
}
inline void stop() { enabled.store(false); }
inline void flush(FILE* file) {
    if (!file) return;
    const auto d=direction_reads.exchange(0);
    if (d) std::fprintf(file,"{\"event\":\"locked_target_direction\",\"direction_reads\":%llu,\"direction_reference\":\"player_to_locked_target\",\"movement_writes\":false,\"continuous_facing_override\":false}\n",static_cast<unsigned long long>(d));
}
}
#endif
