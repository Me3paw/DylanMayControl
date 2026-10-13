#pragma once
#include "combat_v1_policy.h"
#include "finisher_binding.h"
#include <bit>

namespace combat_v1_runtime {
using End=void (*)(void*,void*,void*);
using Start=void (*)(void*,void*);
using Create=std::uint32_t (*)(void*,void*);
using Transfer=std::uint32_t (*)(std::uint32_t,void*);
using Emit=void (*)(void*,void*,void*,void*,void*,void*,void*,void*);
using Phase=void (*)(std::uint8_t,std::uint64_t,void*,void*,void*);
inline void (*start_before)(void*)=nullptr;
inline void (*start_after)(const loadout_actor_resolver::Rows&,bool)=nullptr;
inline void (*end_after)(const loadout_actor_resolver::Rows&,std::uint16_t,bool,void*)=nullptr;
inline End end_original=nullptr;
inline Start start_original=nullptr;
inline Create create_original=nullptr;
inline Transfer transfer_original=nullptr;
inline Emit emit_original=nullptr;
inline Phase phase_original=nullptr;
inline std::atomic<bool> enabled{false};
inline const std::atomic<bool>* stopping=nullptr;
inline std::uintptr_t base=0;
inline std::atomic<std::uintptr_t> player_logic{0};
inline SRWLOCK lock=SRWLOCK_INIT;
struct Guard { Guard(){AcquireSRWLockExclusive(&lock);} ~Guard(){ReleaseSRWLockExclusive(&lock);} };
struct Use { std::uint32_t node=UINT32_MAX,form=0,input_node=0,input_epoch=0; combat_v1::Token token{}; bool uppercut_pending=false; };
struct Request {
    std::uintptr_t queue=0;
    std::uint32_t sequence=0,id=0;
    std::uint64_t origin[2]{};
    combat_v1::Token token{};
};
inline std::uint64_t world=0,actor=0;
inline std::uintptr_t cfg_key=0;
inline combat_v1::Repetition repetition{};
inline Use pending{},active{};
inline std::uint64_t competitors=0;
inline combat_v1::HitTokens groups{};
inline std::uintptr_t group_container=0;
inline std::array<Request,256> requests{};
inline std::array<std::uint32_t,2> weapons{};
inline constexpr native_pins::Pin pins[]={
    {"v1_commit_call",0x239f5a8,0,"8973204c8bc3488b7424584c6bf0384c8bce4c0333498bd6c5f877e828e7ffff80bc2441010000000fb78c244001000066894b2466c743350000897b30c64337"},
    {"v1_attack_start",0x23a8820,15,"48895c240848896c2410488974241857415641574883ec704c8b4148488beac5fc1011c5fc1049204969d8b00000004969f0500300004d69c810080000c4e3f9"},
    {"v1_hit_group",0x24927d0,15,"48895c241048896c241848897424205741544155415641574883ec20488bea488bf9488bcae8c6ca00004c8be8448b4d10458d41014489451044894870488b1f"},
    {"v1_hit_emit",0x2497e00,15,"488bc44c8948204c894018488950104889480853565741544155415641574881ecd0020000c5f82970b8c5f82978a8c578294098c578294888c578299078ffff"}
    ,{"v1_attack_end",0x23a85f0,15,"4c8bdc55564881ecd8000000c5fc10094d8963204d8be04d896be84d8d4308c4e3f916c8014d8973e04c8bea4c8b7158ba01000000c5fc114c2460498bf64d89"}
    ,{"v1_hit_transfer",0x2492a00,14,"488b024c8bca448b42084d6bc0784c03c0493bc0740e3b4870740f4883c078493bc075f2b8ffffffffc38b52108d4a01418949108950708bc2c3"}
    ,{"v1_attack_phase",0x23a73e0,15,"48895c240848896c2410488974241857415641574883ec40488bbc24800000004d8bf1498be8488bda84c9740a41c740"}
};
inline bool running() { return enabled.load() && stopping && !stopping->load(); }
template<class T> inline bool read(std::uintptr_t p,T& out) { return loadout_attack_probe::copy(p,&out,sizeof out); }
inline bool owner(loadout_actor_resolver::Rows& out) {
    std::uint64_t w=0,a=loadout_player_catalogue::observed_player_actor.load();
    if (!a || !loadout_player_catalogue::active() || !TryAcquireSRWLockShared(&loadout_player_catalogue::state_lock)) return false;
    const auto& c=loadout_player_catalogue::published.catalogue;
    if (c.valid && c.actor==a) w=c.world;
    ReleaseSRWLockShared(&loadout_player_catalogue::state_lock);
    return w && loadout_actor_resolver::resolve(w,a,out) && out.attack;
}
// Caller holds our short lock; never hold it while calling the game.
inline void identity(std::uint64_t w,std::uint64_t a,std::uintptr_t cfg) {
    if (world==w && actor==a && cfg_key==cfg) return;
    // Other observers retain use IDs; never recycle a serial after owner/config changes.
    world=w;actor=a;cfg_key=cfg;repetition.different_weapon();pending={};active={};groups={};requests={};weapons={};competitors=0;group_container=0;
}
inline void catalogue(const loadout_player_catalogue::Catalogue& source) {
    if (!running() || !source.valid) return;
    const std::array<std::uint32_t,2> equipped={source.loadout[0],source.loadout[1]};
    Guard guard;
    if (world==source.world && actor==source.actor && weapons!=equipped) {
        repetition.different_weapon();weapons=equipped;
    }
}
inline void commit(void* node,void* combo,void* cfg,std::uint16_t input,bool direct) {
    if (!running() || !finisher_chooser_scope::graph_live()) return;
    const auto& loan=*finisher_chooser_scope::graph_current;
    if (combo!=loan.args.combo_data || cfg!=loan.args.combo_cfg) return;
    std::uint8_t c[combat_mod::rows::kMinCombo]{},n[combat_native::graph::kNode]{},configuration[0x30]{};
    std::array<std::uint32_t,2> equipped{};
    const auto cp=combat_native::binding().copy;
    if (!combat_native::copy_checked(cp,c,reinterpret_cast<std::uintptr_t>(combo),sizeof c) ||
        !combat_native::copy_checked(cp,configuration,reinterpret_cast<std::uintptr_t>(cfg),sizeof configuration) ||
        !read(reinterpret_cast<std::uintptr_t>(loan.args.loadout),equipped)) return;
    const auto index=combat_native::graph::u32(c,0x20);
    const auto nodes=combat_native::graph::u64(c,0);
    std::uint32_t form=0,category=0;
    if (reinterpret_cast<std::uintptr_t>(node)!=combat_native::at(nodes,index,combat_native::graph::kNode) ||
        !combat_native::graph::node_at(cp,nodes,index,n) || combat_native::graph::u32(n,0x20)!=1 ||
        !combat_native::graph::form_def(cp,nodes,combat_native::graph::u64(configuration,8),n,&form,&category) || category>2) return;
    player_logic.store(loan.owner.rows.logic,std::memory_order_relaxed);
    auto kind=category==0?combat_v1::Category::Primary:category==1?combat_v1::Category::Secondary:combat_v1::Category::Finisher;
    if(category==1&&form==444122270u)kind=combat_v1::Category::Drill;
    Guard guard;
    identity(loan.owner.world,loan.owner.actor,reinterpret_cast<std::uintptr_t>(cfg));
    if (weapons!=equipped) { repetition.different_weapon();weapons=equipped; }
    const auto epoch=combat_native::graph::u32(c,0x3c);
    const auto input_node=combat_v1::logical_node(combat_native::graph::u32(n,0x10));
    if(combat_v1::inherit_native_input(pending.token.serial,pending.form,form,input,direct,pending.input_epoch,epoch)) {
        pending.node=index;pending.input_node=input_node;return;
    }
    if(!(input>>8)) {pending={index,form,input_node,epoch,{}};return;} // Unpaired automatic node has no proven input token.
    // An Uppercut that never entered its native charged subrecord is the
    // exempt normal attack, including a canceled selection before charging.
    if (pending.uppercut_pending) repetition.different_weapon();
    const bool uppercut=form==2848499412u;
    pending={index,form,input_node,epoch,uppercut?repetition.reserve():repetition.select({form,input_node,kind}),uppercut};
}
// Called by the Step arbitration observer AFTER native pair evaluation, only
// for the real consumer callsite. A winner can itself be rejected recursively.
inline void note_competitor(const loadout_actor_resolver::Rows& rows,std::uint64_t mask) {
    if (!mask || !std::has_single_bit(mask) || std::countr_zero(mask)>=50 || mask==1) return;
    Guard guard;
    if (world==rows.location.world && actor==rows.location.actor) competitors|=mask;
}
inline void end(void* columns,void* events,void* status) {
    const DWORD incoming=GetLastError();
    loadout_actor_resolver::Rows rows{};std::uint64_t q[12]{};
    const bool owned=running() && read(reinterpret_cast<std::uintptr_t>(columns),q) && q[11]<=UINT32_MAX && owner(rows) &&
        combat_native::at(q[0],q[11],0x810)==rows.logic && combat_native::at(q[1],q[11],0xb0)==rows.attack;
    SetLastError(incoming);end_original(columns,events,status);const DWORD forwarded=GetLastError();
    if (owned && loadout_actor_resolver::current(rows)) {
        std::uint16_t result=0;std::uint8_t cached=0;
        if (read(rows.logic+0x1e,result) && read(rows.logic+0x20,cached) && cached && (result&0x11)) {
            std::uint64_t candidates=0;
            { Guard guard;if(world==rows.location.world && actor==rows.location.actor)candidates=competitors; }
            bool interrupted=false;
            while (candidates) {
                const auto index=std::countr_zero(candidates);candidates&=candidates-1;
                std::uint16_t other_result=0;std::uint8_t state=0,consumed=0;
                const auto slot=rows.logic+index*0x28;
                if (!read(slot+0x1c,state) || !read(slot+0x1e,other_result) || !read(slot+0x20,consumed)) {interrupted=true;break;}
                if ((state&6) && (!consumed || !(other_result&0x11) || (other_result&8))) interrupted=true;
            }
            {
            Guard guard;
            if (world==rows.location.world && actor==rows.location.actor) {
                if (result&1) active={};
                competitors=0;
            }
            }
            if (end_after) end_after(rows,result,interrupted,status);
        }
    }
    SetLastError(forwarded);
}
inline void phase(std::uint8_t initial,std::uint64_t source,void* attack,void* events,void* cfg) {
    phase_original(initial,source,attack,events,cfg);const DWORD forwarded=GetLastError();
    if (running() && !initial && source==loadout_player_catalogue::observed_player_actor.load()) {
        loadout_actor_resolver::Rows rows{};std::uint32_t node=UINT32_MAX,subrecord=0;
        if (owner(rows) && rows.attack==reinterpret_cast<std::uintptr_t>(attack) &&
            read(rows.attack,node) && read(rows.attack+0x10,subrecord) && subrecord) {
            Guard guard;
            if (world==rows.location.world && actor==source && cfg_key==reinterpret_cast<std::uintptr_t>(cfg) &&
                active.node==node && active.uppercut_pending && pending.token.serial==active.token.serial) {
                active.token=repetition.select({active.form,active.input_node,combat_v1::Category::Finisher});
                active.uppercut_pending=false;pending=active;
            }
        }
    }
    SetLastError(forwarded);
}
inline void start(void* columns,void* context) {
    const DWORD incoming=GetLastError();
    loadout_actor_resolver::Rows rows{};std::uint64_t q[10]{};std::uint8_t was_pending=0;
    const bool owned=running() && read(reinterpret_cast<std::uintptr_t>(columns),q) && q[9]<=UINT32_MAX &&
        owner(rows) && combat_native::at(q[1],q[9],0xb0)==rows.attack && read(rows.attack+0x30,was_pending) && was_pending;
    if (running() && start_before) start_before(columns);
    SetLastError(incoming);start_original(columns,context);const DWORD forwarded=GetLastError();
    bool accepted=false;
    if (owned && loadout_actor_resolver::current(rows)) {
        std::uint32_t node=UINT32_MAX;std::uint8_t now_pending=1,flags=0;
        if (read(rows.attack,node) && read(rows.attack+0x30,now_pending) && !now_pending && read(rows.attack+0xa0,flags) && (flags&1)) {
            Guard guard;
            if (world==rows.location.world && actor==rows.location.actor) {active=node==pending.node?pending:Use{};competitors=0;accepted=true;}
        }
    }
    if (owned && start_after) start_after(rows,accepted);
    SetLastError(forwarded);
}
inline void save_group(const loadout_actor_resolver::Rows& rows,std::uintptr_t hit_requests,
                       std::uint32_t generation,combat_v1::Token token) {
    if (loadout_actor_resolver::current(rows)) {
        std::uintptr_t data=0;std::uint32_t count=0;
        std::array<std::uint32_t,128> live{};bool have_live=read(hit_requests,data) && read(hit_requests+8,count) && count<=live.size();
        for (std::uint32_t i=0;have_live && i<count;++i) have_live=read(data+i*0x78+0x70,live[i]);
        Guard guard;
        if (world==rows.location.world && actor==rows.location.actor) {
            if (group_container!=hit_requests) { groups={};group_container=hit_requests; }
            (void)groups.save(generation,token,have_live?live.data():nullptr,count);
        }
    }
}
__attribute__((noinline)) inline std::uint32_t create(void* hit_ids,void* container) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    const DWORD incoming=GetLastError();
    loadout_actor_resolver::Rows rows{};std::uintptr_t hit_requests=0;
    std::uint32_t node=UINT32_MAX;combat_v1::Token token{};
    const bool melee=caller==base+0x23acffb || caller==base+0x23abca7 || caller==base+0x23abf00;
    bool owned=running() && owner(rows) && read(rows.attack,node) &&
        loadout_chain_switch::component(rows,step_binding::fnv1a("heron::hit::component::HitRequests"),0x48,hit_requests) &&
        hit_requests==reinterpret_cast<std::uintptr_t>(container);
    if (owned) { Guard guard;if (melee && world==rows.location.world && actor==rows.location.actor && active.node==node) {token=active.token;token.node=node;} }
    SetLastError(incoming);const auto generation=create_original(hit_ids,container);const DWORD forwarded=GetLastError();
    if (owned) {
        save_group(rows,hit_requests,generation,token);
    }
    SetLastError(forwarded);return generation;
}
// Detaching a Split renumbers the SAME owned hit group. Preserve its token,
// including the old generation for contacts already queued before detachment.
__attribute__((noinline)) inline std::uint32_t transfer(std::uint32_t previous,void* container) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    const DWORD incoming=GetLastError();
    loadout_actor_resolver::Rows rows{};std::uintptr_t hit_requests=0;combat_v1::Token token{};
    const bool owned=running() && caller==base+0x238b0b0 && owner(rows) &&
        loadout_chain_switch::component(rows,step_binding::fnv1a("heron::hit::component::HitRequests"),0x48,hit_requests) &&
        hit_requests==reinterpret_cast<std::uintptr_t>(container);
    if (owned) { Guard guard;
        if (world==rows.location.world && actor==rows.location.actor && group_container==hit_requests)
            token=groups.find(previous);
    }
    SetLastError(incoming);const auto generation=transfer_original(previous,container);const DWORD forwarded=GetLastError();
    if (owned && generation!=UINT32_MAX) save_group(rows,hit_requests,generation,token);
    SetLastError(forwarded);return generation;
}
using Hit=combat_v1::HitIdentity;
inline void emit(void* columns,void* damageable,void* fx,void* status,void* velocity,void* queue,void* events,void* physics) {
    const DWORD incoming=GetLastError();
    std::uint64_t q[9]{},w=0,source=0,hit_base=0,definitions=0;
    std::uint32_t hit_count=0,definition_count=0,before=0;
    std::array<Hit,128> hits{};std::size_t count=0;
    bool owned=running() && read(reinterpret_cast<std::uintptr_t>(columns),q) && q[8]<=UINT32_MAX &&
        read(reinterpret_cast<std::uintptr_t>(damageable),w) && read(q[7]+0x10+q[8]*8,source) &&
        source==loadout_player_catalogue::observed_player_actor.load() &&
        read(q[2]+q[8]*0x78+0x20,hit_base) && read(q[2]+q[8]*0x78+0x28,hit_count) && hit_count<=hits.size() &&
        read(q[4]+q[8]*0x28+0x18,definitions) && read(q[4]+q[8]*0x28+0x20,definition_count) && definition_count<=512 &&
        read(reinterpret_cast<std::uintptr_t>(queue)+8,before);
    if (owned) for (std::uint32_t i=0;i<hit_count;++i) {
        std::array<std::uint8_t,0x90> h{};
        if (!read(hit_base+i*0x90,h)) {owned=false;break;}
        std::uint32_t index=0;std::uint16_t flags=0;
        std::memcpy(&index,h.data()+0x78,4);std::memcpy(&flags,h.data()+0x84,2);
        if (index>=definition_count || !(flags&4)) continue;
        auto& hit=hits[count];std::memcpy(&hit.victim,h.data(),8);std::memcpy(hit.origin,h.data()+0x50,16);
        std::memcpy(&hit.generation,h.data()+0x7c,4);
        if (!read(definitions+index*0x28+0x18,hit.id)) {owned=false;break;}
        ++count;
    }
    SetLastError(incoming);emit_original(columns,damageable,fx,status,velocity,queue,events,physics);const DWORD forwarded=GetLastError();
    std::uint64_t data=0;std::uint32_t after=0;
    if (owned && read(reinterpret_cast<std::uintptr_t>(queue),data) && read(reinterpret_cast<std::uintptr_t>(queue)+8,after) &&
        after>=before && after-before<=hits.size()) {
        for (std::uint32_t i=before;i<after;++i) {
            std::array<std::uint8_t,0xf0> r{};
            if (!read(data+i*0xf0,r)) break;
            std::uint64_t producer=0,victim=0;Request request{};
            std::memcpy(&producer,r.data()+0x18,8);std::memcpy(&victim,r.data()+0x50,8);
            if (producer!=source) continue;
            request.queue=reinterpret_cast<std::uintptr_t>(queue);std::memcpy(&request.id,r.data()+0xd8,4);
            std::memcpy(&request.sequence,r.data()+0xe0,4);std::memcpy(request.origin,r.data(),16);
            std::uint32_t generation=0;
            const Hit identity{victim,{request.origin[0],request.origin[1]},request.id,0};
            if (!combat_v1::hit_generation(hits.data(),count,identity,generation)) continue;
            Guard guard;
            if (world!=w || actor!=source) continue;
            request.token=groups.find(generation);
            if (request.token.serial) requests[request.sequence%requests.size()]=request;
        }
    }
    SetLastError(forwarded);
}
inline bool repeat(std::uintptr_t request,std::uintptr_t context,combat_v1::Scale& scale) {
    if (!running()) return false;
    Request key{};std::uint64_t source=0,w=0;
    if (!read(request+0xe0,key.sequence) || !read(request+0xd8,key.id) || !read(request,key.origin) ||
        !read(request+0x18,source) || !read(context+0x40,w) || !read(context+0xa0,key.queue)) return false;
    Guard guard;auto& saved=requests[key.sequence%requests.size()];
    if (world!=w || actor!=source || saved.queue!=key.queue || saved.sequence!=key.sequence || saved.id!=key.id ||
        !saved.token.serial || std::memcmp(saved.origin,key.origin,16)) return false;
    scale=saved.token.repeat;saved={};return true;
}
inline bool prepare(std::uintptr_t executable,const std::atomic<bool>& stopped) {
    base=executable;stopping=&stopped;
    for (const auto& pin:pins) if (!native_pins::verify(combat_native::binding().copy,base,pin)) return false;
    finisher_binding::commit_observer=&commit;
    loadout_player_catalogue::catalogue_observer=&catalogue;return true;
}
}
