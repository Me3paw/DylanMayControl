#pragma once
#include "facing_movement.h"
namespace attack_turn_native {
struct Lifetime {
    bool sustained=false,started=false,finished=false;
    bool observe(uint32_t age,bool identity_valid,bool attack_matches) {
        if(finished)return false;
        if(!identity_valid || (started && !attack_matches) || (!started && age>250)){finished=true;return false;}
        if(!attack_matches)return false;
        started=true;return true;
    }
    void applied(){if(!sustained)finished=true;}
};
// The melee slot (LogicData+1c) and AttackData+a0 close and reopen with the
// animation windows inside one node; native on_start then re-targets from the
// stick. A sustained request therefore lives for the node, not for a window.
inline bool attack_matches(bool sustained,bool same_node,bool native_window) {
    return same_node && (sustained || native_window);
}
}
#ifdef _WIN32
#include "facing_lock_native.h"
namespace attack_turn_native {
inline std::atomic<bool> enabled{false};
inline const std::atomic<bool>* stopping=nullptr;
struct Request {
    uint64_t world=0,actor=0,graph=0,target=0,ticket=0;
    uint32_t node=UINT32_MAX;
    facing_movement::Vec3 reversed{};
    bool pending=false;uint32_t started_ms=0;
    uint32_t last_guard=1,observed_node=UINT32_MAX;
    uint8_t observed_melee=0,observed_flags=0;
    Lifetime lifetime{};
};
inline SRWLOCK lock=SRWLOCK_INIT;
inline Request request{};
inline uint64_t next_ticket=0;
inline std::atomic<uint64_t> queued{0},applied{0},rejected{0};
inline unsigned diagnostic_total=0;
inline bool current_graph(const Request& request) {
    loadout_player_catalogue::Published published{};
    return loadout_player_catalogue::copy_published(native_ui::sdk::generation.load(),published) &&
        published.catalogue.world==request.world && published.catalogue.actor==request.actor && published.catalogue.graph_key==request.graph;
}
inline bool active() { return enabled.load() && stopping && !stopping->load(); }
inline bool uppercut(const combat_mod::ChooserArgs& args,uint32_t chosen) {
    if(chosen!=65 && chosen!=66)return false;
    uint8_t combo[combat_mod::rows::kMinCombo]{},cfg[0x30]{},node[combat_native::graph::kNode]{};
    uint32_t form=0,category=0;const auto cp=step_binding::env().copy;
    return combat_native::copy_checked(cp,combo,reinterpret_cast<uintptr_t>(args.combo_data),sizeof combo) &&
        combat_native::copy_checked(cp,cfg,reinterpret_cast<uintptr_t>(args.combo_cfg),sizeof cfg) &&
        combat_native::graph::node_at(cp,combat_native::graph::u64(combo,0),chosen,node) &&
        combat_native::graph::u32(node,0x10)==(chosen==65?3856553470u:53223285u) &&
        combat_native::graph::u32(node,0x20)==1 &&
        combat_native::graph::form_def(cp,combat_native::graph::u64(combo,0),combat_native::graph::u64(cfg,8),node,&form,&category) &&
        form==2848499412u && category==2;
}
// Called by the chooser only for a genuinely accepted attack, before native
// attack turning. Direction classification uses the player-to-target vector.
inline void accepted(const combat_mod::ChooserArgs& args,uint32_t chosen,const combat_mod::DirectionSample& direction) {
    const DWORD saved=GetLastError();
    facing_lock_native::Snapshot snapshot{};
    combat_mod::Attack attack=combat_mod::Attack::None;
    if (active() && combat_native::read_attack(args,&attack) && attack!=combat_mod::Attack::None &&
        combat_mod::classify(direction,combat_mod::Tuning{})==combat_mod::Dir::Back &&
        facing_lock_native::capture(snapshot) && snapshot.locked) {
        facing_movement::Vec3 reversed{};
        // Native attack auto-facing can already have changed the pose here.
        // Reverse the captured movement heading, before that attack turn.
        if (facing_movement::reverse_movement({snapshot.raw[0],snapshot.raw[1],snapshot.raw[2]},snapshot.basis.up,reversed) &&
            TryAcquireSRWLockExclusive(&lock)) {
            if (next_ticket!=UINT64_MAX) {
                request={snapshot.player.location.world,snapshot.player.location.actor,
                    reinterpret_cast<uintptr_t>(args.graph),snapshot.target,++next_ticket,
                    chosen,reversed,true,static_cast<uint32_t>(GetTickCount64())};
                request.lifetime.sustained=uppercut(args,chosen);
                ++queued;
            }
            ReleaseSRWLockExclusive(&lock);
        }
    }
    SetLastError(saved);
}
// Native126/127: update columns[0] AttackTargeting4057D9A2/B0,
// [1] AttackData546690F3/B0, [2] MovementInputF2358B43/80, [11] row.
// Only this attack-targeting invocation sees the reversed movement vector.
constexpr uint32_t kUpdateRva=0x236ad30;
constexpr const char* kUpdatePin="4c894c24204c89442418488954241053564154415541564157";
using UpdateFn=void (*)(void*,void*,void*,void*,void*);
inline void* original_update=nullptr;
struct DirectionProbe {uint32_t ms=0,node=0;uint64_t ticket=0;float raw[4]{},command[4]{},output[4]{};bool output_read=false;uint8_t melee=0,flags=0;};
inline std::array<DirectionProbe,64> direction_probes{};
inline unsigned direction_count=0;
inline void update_hook(void* columns,void* targets,void* cfg,void* buffered,void* extra) {
    const DWORD incoming=GetLastError();
    Request pending{};
    if(active() && TryAcquireSRWLockShared(&lock)) {pending=request;ReleaseSRWLockShared(&lock);}
    uint64_t q[12]{},targeting=0;uint32_t node=UINT32_MAX;uint8_t melee=0;
    facing_lock_native::Snapshot s{};loadout_actor_resolver::Rows rows{};
    alignas(16) unsigned char movement[0x80]{};
    void* forwarded=columns;DirectionProbe probe{};bool changed=false,validated=false;
    const auto age=static_cast<uint32_t>(GetTickCount64())-pending.started_ms;
    uint32_t guard=1;
    const bool owner_current=pending.pending && loadout_actor_resolver::resolve(pending.world,pending.actor,rows);
    const bool player_call=owner_current &&
        combat_native::copy_checked(step_binding::env().copy,q,reinterpret_cast<uintptr_t>(columns),sizeof q) &&
        q[11]==rows.location.row && combat_native::at(q[1],q[11],0xb0)==rows.attack;
    if(player_call && (guard=2,facing_lock_native::capture(s)) && s.locked &&
        s.player.location.world==pending.world && s.player.location.actor==pending.actor && s.target==pending.target &&
        (guard=4,loadout_actor_resolver::field(rows.combo,0x20,node)) &&
        (guard=5,current_graph(pending)) &&
        (guard=6,loadout_actor_resolver::field(rows.logic,0x1c,melee)) &&
        (guard=7,step_binding::resolve_player_column(pending.world,pending.actor,0x4057d9a2,0xb0,&targeting)) &&
        (guard=8,combat_native::at(q[0],q[11],0xb0)==targeting) && combat_native::at(q[2],q[11],0x80)==s.movement &&
        combat_native::copy_checked(step_binding::env().copy,movement,s.movement,sizeof movement) && loadout_actor_resolver::current(rows)) {
        validated=true;
        uint8_t attack_flags=0;
        const bool flags_read=loadout_actor_resolver::field(rows.attack,0xa0,attack_flags);
        const bool native_attack=flags_read && attack_matches(pending.lifetime.sustained,node==pending.node,(melee&4) && (attack_flags&1));
        auto direction_vector=pending.reversed;
        const bool direction_valid=!pending.lifetime.sustained || facing_movement::plane_local(s.basis.forward,s.plane_value,direction_vector);
        const bool eligible=pending.lifetime.observe(age,direction_valid,native_attack);
        guard=9;probe.melee=melee;probe.flags=attack_flags;
        float direction[4]{direction_vector.x,direction_vector.y,direction_vector.z,s.raw[3]};
        std::memcpy(movement,direction,sizeof direction);
        if(pending.lifetime.sustained){const float magnitude=1;std::memcpy(movement+0x24,&magnitude,sizeof magnitude);}
        const auto address=reinterpret_cast<uintptr_t>(movement),offset=q[11]*0x80;
        if(eligible && address>offset && address-offset>=0x10000) {
            q[2]=address-offset;forwarded=q;changed=true;guard=0;
            probe.ms=static_cast<uint32_t>(GetTickCount64());probe.node=node;probe.ticket=pending.ticket;
            std::memcpy(probe.raw,s.raw,16);std::memcpy(probe.command,direction,16);
        }
    }
    if(pending.pending && (!owner_current || (player_call && !validated)))pending.lifetime.observe(age,false,false);
    if(player_call && TryAcquireSRWLockExclusive(&lock)) {
        if(request.ticket==pending.ticket) {request.last_guard=guard;request.observed_node=node;request.observed_melee=probe.melee;request.observed_flags=probe.flags;request.lifetime=pending.lifetime;}
        ReleaseSRWLockExclusive(&lock);
    }
    SetLastError(incoming);
    reinterpret_cast<UpdateFn>(original_update)(forwarded,targets,cfg,buffered,extra);
    const DWORD outgoing=GetLastError();
    if(changed) {
        probe.output_read=loadout_actor_resolver::current(rows) &&
            combat_native::copy_checked(step_binding::env().copy,probe.output,targeting+0x90,16);
        if(TryAcquireSRWLockExclusive(&lock)) {
            if(diagnostic_total<4096 && direction_count<direction_probes.size()) {direction_probes[direction_count++]=probe;++diagnostic_total;}
            if(request.ticket==pending.ticket){request.lifetime.applied();request.pending=!request.lifetime.finished;}
            ReleaseSRWLockExclusive(&lock);
        }
        ++applied;
    } else if(pending.pending && (pending.lifetime.finished || (!pending.lifetime.started && age>250)) && TryAcquireSRWLockExclusive(&lock)) {
        if(request.ticket==pending.ticket) request.pending=false;
        ReleaseSRWLockExclusive(&lock);++rejected;
    }
    SetLastError(outgoing);
}
inline bool arm(uint64_t base,bool verified,const std::atomic<bool>& stop) {
    if(!base || !verified || !original_update || enabled.load() || stop.load()) return false;
    stopping=&stop;enabled.store(true);return true;
}
inline void stop() { enabled.store(false);AcquireSRWLockExclusive(&lock);request.pending=false;ReleaseSRWLockExclusive(&lock); }
inline void flush(FILE* file) {
    if(!file) return;
    if(TryAcquireSRWLockExclusive(&lock)) {
        for(unsigned i=0;i<direction_count;++i) {
            const auto& p=direction_probes[i];
            std::fprintf(file,"{\"event\":\"back_attack_direction\",\"ms\":%u,\"node\":%u,\"ticket\":%llu,\"raw\":[%.6g,%.6g,%.6g],\"command\":[%.6g,%.6g,%.6g],\"targeting_direction\":[%.6g,%.6g,%.6g],\"output_read\":%s,\"melee_slot\":%u,\"attack_flags\":%u,\"movement_writes\":false}\n",
                p.ms,p.node,static_cast<unsigned long long>(p.ticket),p.raw[0],p.raw[1],p.raw[2],p.command[0],p.command[1],p.command[2],p.output[0],p.output[1],p.output[2],p.output_read?"true":"false",p.melee,p.flags);
        }
        direction_count=0;ReleaseSRWLockExclusive(&lock);
    }
    Request last{};
    if(TryAcquireSRWLockShared(&lock)) {last=request;ReleaseSRWLockShared(&lock);}
    const auto q=queued.exchange(0),a=applied.exchange(0),r=rejected.exchange(0);
    if(q||a||r) std::fprintf(file,"{\"event\":\"back_attack_direction_summary\",\"queued\":%llu,\"applied\":%llu,\"rejected\":%llu,\"last_guard\":%u,\"expected_node\":%u,\"observed_node\":%u,\"observed_melee_slot\":%u,\"observed_attack_flags\":%u,\"sustained\":%s}\n",static_cast<unsigned long long>(q),static_cast<unsigned long long>(a),static_cast<unsigned long long>(r),last.last_guard,last.node,last.observed_node,last.observed_melee,last.observed_flags,last.lifetime.sustained?"true":"false");
}
}
#endif
