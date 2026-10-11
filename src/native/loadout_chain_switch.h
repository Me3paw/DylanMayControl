#pragma once
#include "finisher_chooser_scope.h"
#ifdef _WIN32
// The chooser lends only reads of equipment. Selection stays in the existing
// mutable G/LuaEvents callback. A ticket contains IDs and input sequence only.
namespace loadout_chain_switch {
using Choose=std::uint32_t (*)(const combat_mod::ChooserArgs&);
using Input=std::uint16_t (*)(void*,void*,void*);
using Active=std::uint8_t (*)(void*,std::uint64_t,std::uint64_t);
inline Choose choose=nullptr;
inline Input input=nullptr;
inline Active active=nullptr;
inline std::atomic<bool> enabled{false};
struct Ticket {
    bool valid=false;
    std::uint64_t world=0,actor=0,graph=0,cfg=0,definitions=0;
    std::uint32_t revision=0,channel=0,id=0,parent=0,accepted=0,current=0,phase=0,sequence=0;
    std::uint32_t conditions_hash=0,animation_hash=0;
};
inline SRWLOCK ticket_lock=SRWLOCK_INIT;
inline Ticket ticket{};
inline std::atomic<std::uint64_t> held{0},selected{0},cancelled{0};
inline std::atomic<std::uint32_t> last_id{0},last_entry{UINT32_MAX};
inline bool copy_ticket(Ticket& out) {
    if (!enabled || !TryAcquireSRWLockShared(&ticket_lock)) return false;
    out=ticket; ReleaseSRWLockShared(&ticket_lock); return out.valid;
}
inline void clear(const Ticket& old) {
    if (!TryAcquireSRWLockExclusive(&ticket_lock)) return;
    if (ticket.valid && ticket.actor==old.actor && ticket.id==old.id && ticket.sequence==old.sequence) {
        ticket.valid=false; CRMOD_DIAGNOSTIC(++cancelled);
    }
    ReleaseSRWLockExclusive(&ticket_lock);
}
inline bool component(const loadout_actor_resolver::Rows& rows,std::uint32_t hash,
        std::size_t stride,std::uintptr_t& address) {
    const auto& m=rows.metadata;
    std::array<std::uint32_t,loadout_actor_resolver::kCapacity> keys{},offsets{};
    if (!m.key_count || m.key_count>keys.size() || !loadout_actor_resolver::current(rows) ||
        !loadout_actor_resolver::copy(m.keys,keys.data(),m.key_count*4) ||
        !loadout_actor_resolver::copy(m.offsets,offsets.data(),m.key_count*4)) return false;
    for (unsigned i=0;i<m.key_count;++i) if (keys[i]==hash && offsets[i]) {
        std::uintptr_t base=0;
        return loadout_attack_probe::add(rows.location.chunk,offsets[i],base) &&
            loadout_attack_probe::row_address(base,rows.location.row,stride,address) &&
            loadout_lookup_probe::readable_span(address,stride) && loadout_actor_resolver::current(rows);
    }
    return false;
}
inline bool key_for(const loadout_actor_resolver::Rows& rows,std::uintptr_t address,
        std::size_t stride,std::uint32_t& hash) {
    const auto& m=rows.metadata;
    std::array<std::uint32_t,loadout_actor_resolver::kCapacity> keys{},offsets{};
    if (!m.key_count || m.key_count>keys.size() || !loadout_actor_resolver::current(rows) ||
        !loadout_actor_resolver::copy(m.keys,keys.data(),m.key_count*4) ||
        !loadout_actor_resolver::copy(m.offsets,offsets.data(),m.key_count*4)) return false;
    bool found=false;
    for (unsigned i=0;i<m.key_count;++i) {
        std::uintptr_t base=0,row=0;
        if (!offsets[i] || !loadout_attack_probe::add(rows.location.chunk,offsets[i],base) ||
            !loadout_attack_probe::row_address(base,rows.location.row,stride,row) || row!=address) continue;
        if (found) return false;
        found=true; hash=keys[i];
    }
    return found && loadout_actor_resolver::current(rows);
}
inline bool sequence_current(const Ticket& t,const loadout_actor_resolver::Rows& rows) {
    std::uint32_t current=0,phase=0,sequence=0;
    return rows.location.world==t.world && rows.location.actor==t.actor &&
        loadout_actor_resolver::field(rows.combo,0x20,current) && current==t.current &&
        loadout_actor_resolver::field(rows.combo,0x2c,phase) && phase==t.phase &&
        loadout_actor_resolver::field(rows.combo,0x3c,sequence) && sequence==t.sequence &&
        loadout_actor_resolver::current(rows);
}
inline bool window_mode(void* combo,void* state,std::uint8_t& mode) {
    std::uint32_t phase=0;
    if (!active || !loadout_actor_resolver::field(reinterpret_cast<std::uintptr_t>(combo),0x2c,phase)) return false;
    mode=phase==3 && !active(state,UINT64_MAX,1) ? 1 : 0;
    return true;
}
// Called under the original equipment callback's current actor/G loan. The
// original chooser rechecks the captured next attack, including its window.
inline bool permit(const loadout_actor_resolver::Rows& rows,const loadout_player_catalogue::Catalogue& source,std::uint64_t graph,
        std::uint32_t channel,std::uint32_t id) {
    Ticket t{};
    if (!copy_ticket(t) || t.graph!=graph || t.channel!=channel || t.id!=id || !choose ||
        !source.valid || t.world!=source.world || t.actor!=source.actor || t.cfg!=source.cfg_key ||
        t.definitions!=source.definitions_key || t.revision!=source.revision ||
        !sequence_current(t,rows)) return false;
    std::uintptr_t conditions=0,animation=0,loadout=0,cfg=0,mud=0;
    std::uint64_t nodes=0;
    if (!component(rows,t.conditions_hash,0x38,conditions) || !component(rows,t.animation_hash,0x48,animation) ||
        !loadout_attack_probe::row_address(rows.loadout_base,rows.location.row,0x10,loadout) ||
        !loadout_attack_probe::row_address(rows.cfg_base,rows.location.row,0xa0,cfg) ||
        !loadout_attack_probe::row_address(rows.mud_base,rows.location.row,8,mud) ||
        !loadout_actor_resolver::field(rows.combo,0,nodes)) return false;
    const auto parent=combat_native::at(nodes,t.parent,combat_native::graph::kNode);
    if (!parent) return false;
    combat_mod::ChooserArgs a{reinterpret_cast<void*>(parent),0,reinterpret_cast<void*>(rows.combo),
        reinterpret_cast<void*>(conditions),reinterpret_cast<void*>(loadout),reinterpret_cast<void*>(mud),
        reinterpret_cast<void*>(rows.logic),reinterpret_cast<void*>(cfg),reinterpret_cast<void*>(graph),reinterpret_cast<void*>(animation)};
    if (!window_mode(a.combo_data,a.state_set,a.phase_is_2)) return false;
    auto& b=combat_native::binding();
    if (t.parent!=t.current && !combat_native::legal_node(a,t.parent,b.copy,b.ops,combat_native::SkipRequirement{},0)) return false;
    return choose(a)==t.accepted && sequence_current(t,rows);
}
inline bool first_entry(const combat_mod::ChooserArgs& a,const Ticket& t,std::uint32_t& chosen) {
    auto& b=combat_native::binding();
    std::uint8_t combo[combat_mod::rows::kMinCombo]{},cfg[0x30]{},node[combat_native::graph::kNode]{};
    if (!choose || !b.ops.complete() ||
        !combat_native::copy_checked(b.copy,combo,reinterpret_cast<std::uintptr_t>(a.combo_data),sizeof combo) ||
        !combat_native::copy_checked(b.copy,cfg,reinterpret_cast<std::uintptr_t>(a.combo_cfg),sizeof cfg)) return false;
    const auto nodes=combat_native::graph::u64(combo,0),defs=combat_native::graph::u64(cfg,8);
    const auto count=combat_native::graph::u32(combo,8);
    if (!count || count>combat_native::graph::kMaxIndex) return false;
    std::uint32_t root=UINT32_MAX;
    for (std::uint32_t i=0;i<count;++i) {
        if (!combat_native::graph::node_at(b.copy,nodes,i,node)) return false;
        if (combat_native::graph::u32(node,0x20)==1 || combat_native::graph::u32(node,0x24)!=i) continue;
        std::uint32_t id=0,category=0;
        if (!combat_native::graph::form_def(b.copy,nodes,defs,node,&id,&category) || id!=t.id || category!=t.channel) continue;
        if (root!=UINT32_MAX) return false;
        root=i;
    }
    if (root==UINT32_MAX) return false;
    auto local=a; local.node=reinterpret_cast<void*>(static_cast<std::uintptr_t>(combat_native::at(nodes,root,combat_native::graph::kNode)));
    if (!window_mode(local.combo_data,local.state_set,local.phase_is_2) ||
        !combat_native::legal_node(local,root,b.copy,b.ops,combat_native::SkipRequirement{},0)) return false;
    const auto result=choose(local);
    std::uint32_t id=0,category=0;
    if (result==UINT32_MAX || !combat_native::graph::node_at(b.copy,nodes,result,node) ||
        combat_native::graph::u32(node,0x20)!=1 ||
        !combat_native::graph::form_def(b.copy,nodes,defs,node,&id,&category) || id!=t.id || category!=t.channel) return false;
    const auto packed=input(reinterpret_cast<void*>(static_cast<std::uintptr_t>(combat_native::at(nodes,result,combat_native::graph::kNode))),a.combo_data,a.combo_cfg);
    if (!(packed&0x100) || (packed&0xff)>3 || ((packed&0xff)>>1)!=t.channel) return false;
    chosen=result; return true;
}
// Returns true only when this queued transition owns the chooser result.
inline bool intercept(const combat_mod::ChooserArgs& a,std::uint32_t native_result,
        bool native_accepted,std::uint32_t& result) {
    if (!enabled || !input || !choose || !finisher_chooser_scope::live(a)) return false;
    const auto& loan=*finisher_chooser_scope::current;
    Ticket t{}; bool have=copy_ticket(t);
    if (have && (t.world!=loan.world || t.actor!=loan.actor || t.graph!=loan.graph ||
        t.cfg!=loan.cfg || t.definitions!=loan.definitions || t.revision!=loan.revision || !sequence_current(t,loan.rows))) {
        clear(t); have=false;
    }
    if (have) {
        if (native_result!=UINT32_MAX && !native_accepted) return false; // Preserve native non-attack/cancel transitions.
        if (native_accepted) {
            std::uint64_t nodes=0;
            if (!loadout_actor_resolver::field(reinterpret_cast<std::uintptr_t>(a.combo_data),0,nodes)) return false;
            const auto packed=input(reinterpret_cast<void*>(static_cast<std::uintptr_t>(combat_native::at(nodes,native_result,combat_native::graph::kNode))),a.combo_data,a.combo_cfg);
            if (!(packed&0x100) || (packed&0xff)>3 || ((packed&0xff)>>1)!=t.channel) return false;
        }
        // A newer unsent choice replaces the earlier ticket. An already
        // issued native equip completes before another selection is allowed.
        if (TryAcquireSRWLockShared(&loadout_player_catalogue::state_lock)) {
            const auto pending=loadout_player_catalogue::control.pending_choice(static_cast<loadout_control::Channel>(t.channel));
            const auto& source=loadout_player_catalogue::published.catalogue;
            const bool replaced=!loadout_player_catalogue::control.has_in_flight() &&
                ((!pending.assigned && source.loadout[t.channel]!=t.id) || (pending.assigned && pending.id!=t.id));
            ReleaseSRWLockShared(&loadout_player_catalogue::state_lock);
            if (replaced) { clear(t); return false; }
        }
        std::uint32_t equipped=0;
        if (!loadout_actor_resolver::field(reinterpret_cast<std::uintptr_t>(a.loadout),t.channel*4,equipped)) return false;
        if (equipped==t.id) {
            std::uint32_t first=UINT32_MAX;
            if (first_entry(a,t,first) && finisher_chooser_scope::live(a)) {
                result=first; clear(t); CRMOD_DIAGNOSTIC(last_id.store(t.id)); CRMOD_DIAGNOSTIC(last_entry.store(first)); CRMOD_DIAGNOSTIC(++selected); return true;
            }
        }
        // Keep native cancels/non-attack choices. Do not consume the old
        // weapon's buffered attack while its native preset is being changed.
        if (native_accepted) {
            std::uint64_t nodes=0;
            if (!loadout_actor_resolver::field(reinterpret_cast<std::uintptr_t>(a.combo_data),0,nodes)) return false;
            const auto packed=input(reinterpret_cast<void*>(static_cast<std::uintptr_t>(combat_native::at(nodes,native_result,combat_native::graph::kNode))),a.combo_data,a.combo_cfg);
            if ((packed&0x100) && (packed&0xff)<=3 && ((packed&0xff)>>1)==t.channel) {
                result=UINT32_MAX; CRMOD_DIAGNOSTIC(++held); return true;
            }
        }
        return false;
    }
    if (!native_accepted || native_result==UINT32_MAX) return false;
    std::uint8_t c[combat_mod::rows::kMinCombo]{},cfg[0x30]{},n[combat_native::graph::kNode]{};
    auto& b=combat_native::binding();
    if (!combat_native::copy_checked(b.copy,c,reinterpret_cast<std::uintptr_t>(a.combo_data),sizeof c) ||
        !combat_native::copy_checked(b.copy,cfg,reinterpret_cast<std::uintptr_t>(a.combo_cfg),sizeof cfg) ||
        !combat_native::graph::node_at(b.copy,combat_native::graph::u64(c,0),native_result,n) ||
        combat_native::graph::u32(c,0x20)==UINT32_MAX) return false;
    const auto packed=input(reinterpret_cast<void*>(static_cast<std::uintptr_t>(combat_native::at(combat_native::graph::u64(c,0),native_result,combat_native::graph::kNode))),a.combo_data,a.combo_cfg);
    if (!(packed&0x100) || (packed&0xff)>3) return false;
    const unsigned channel=(packed&0xff)>>1;
    loadout_control::DesiredChoice pending{}; bool acquired=false;
    if (!TryAcquireSRWLockShared(&loadout_player_catalogue::state_lock)) return false;
    const auto& source=loadout_player_catalogue::published.catalogue;
    if (source.valid && source.world==loan.world && source.actor==loan.actor && source.graph_key==loan.graph && source.revision==loan.revision) {
        pending=loadout_player_catalogue::control.pending_choice(static_cast<loadout_control::Channel>(channel));
        for (std::size_t i=0;i<source.count;++i) {
            const auto& item=source.items[i];
            if (item.id==pending.id && item.category==channel && item.known && item.acquired) acquired=true;
        }
    }
    ReleaseSRWLockShared(&loadout_player_catalogue::state_lock);
    std::uint32_t equipped=0;
    if (!pending.assigned || !acquired || !loadout_actor_resolver::field(reinterpret_cast<std::uintptr_t>(a.loadout),channel*4,equipped) || equipped==pending.id) return false;
    const auto nodes=combat_native::graph::u64(c,0),parent=reinterpret_cast<std::uintptr_t>(a.node);
    if (parent<nodes || (parent-nodes)%combat_native::graph::kNode || (parent-nodes)/combat_native::graph::kNode>=combat_native::graph::u32(c,8)) return false;
    t={true,loan.world,loan.actor,loan.graph,loan.cfg,loan.definitions,loan.revision,channel,pending.id,
        static_cast<std::uint32_t>((parent-nodes)/combat_native::graph::kNode),native_result,
        combat_native::graph::u32(c,0x20),combat_native::graph::u32(c,0x2c),combat_native::graph::u32(c,0x3c),0,0};
    if (!key_for(loan.rows,reinterpret_cast<std::uintptr_t>(a.conditions),0x38,t.conditions_hash) ||
        !key_for(loan.rows,reinterpret_cast<std::uintptr_t>(a.anim_out),0x48,t.animation_hash) || !finisher_chooser_scope::live(a)) return false;
    if (!TryAcquireSRWLockExclusive(&ticket_lock)) return false;
    ticket=t; ReleaseSRWLockExclusive(&ticket_lock);
    result=UINT32_MAX; CRMOD_DIAGNOSTIC(++held); return true;
}
inline void flush(FILE* log) {
    if constexpr(!diagnostics::enabled)return;
    if (!log) return;
    static ULONGLONG next=0; static unsigned budget=512;
    const auto now=GetTickCount64();
    if (!budget || now<next) return;
    next=now+100;
    const auto h=held.exchange(0),s=selected.exchange(0),c=cancelled.exchange(0);
    if (h || s || c) {
        --budget;
        std::fprintf(log,"{\"event\":\"loadout_chain_switch\",\"old_attack_deferred\":%llu,\"new_first_entry\":%llu,\"ticket_cleared\":%llu,\"last_weapon_id\":%u,\"last_entry_index\":%u}\n",
            static_cast<unsigned long long>(h),static_cast<unsigned long long>(s),static_cast<unsigned long long>(c),last_id.load(),last_entry.load());
    }
}
}
#endif
