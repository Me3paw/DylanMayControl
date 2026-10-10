#pragma once
#include "combat_native.h"
#include "loadout_actor_resolver.h"
#include "loadout_player_catalogue.h"
#include "finisher_test_mode.h"
#ifdef _WIN32
namespace finisher_chooser_scope {
struct Loan {
    uint64_t world=0,actor=0,graph=0;
    uint64_t cfg=0,definitions=0,ui_stacks=0;
    uint32_t revision=0;
    combat_mod::ChooserArgs args{};
    loadout_actor_resolver::Rows rows{};
    bool valid=false;
};
inline thread_local const Loan* current=nullptr;
struct LocalLoadoutBorrow { void* local=nullptr; std::uintptr_t original=0; void* combo=nullptr; void* cfg=nullptr; void* graph=nullptr; };
inline thread_local LocalLoadoutBorrow loadout_borrow{};
inline bool capture(const combat_mod::ChooserArgs& a,Loan& out) {
    out={};
    if (!loadout_player_catalogue::active() || !TryAcquireSRWLockShared(&loadout_player_catalogue::state_lock)) return false;
    const auto& source=loadout_player_catalogue::published.catalogue;
    const bool valid=source.valid && source.actor==loadout_player_catalogue::observed_player_actor.load();
    if (valid) {
        out.world=source.world; out.actor=source.actor; out.graph=source.graph_key;
        out.cfg=source.cfg_key; out.definitions=source.definitions_key; out.ui_stacks=source.ui_stacks_key;
        out.revision=source.revision; out.args=a;
    }
    ReleaseSRWLockShared(&loadout_player_catalogue::state_lock);
    if (!valid || out.graph!=reinterpret_cast<uintptr_t>(a.graph) || !loadout_actor_resolver::resolve(out.world,out.actor,out.rows)) return false;
    uintptr_t loadout=0,cfg=0,mud=0; const auto row=out.rows.location.row;
    if (!loadout_attack_probe::row_address(out.rows.loadout_base,row,0x10,loadout) ||
        !loadout_attack_probe::row_address(out.rows.cfg_base,row,0xa0,cfg) || !loadout_attack_probe::row_address(out.rows.mud_base,row,8,mud)) return false;
    const bool shadow=loadout_borrow.local==a.loadout &&
        loadout_borrow.original==loadout && loadout_borrow.combo==a.combo_data && loadout_borrow.cfg==a.combo_cfg && loadout_borrow.graph==a.graph;
    out.valid=out.rows.combo==reinterpret_cast<uintptr_t>(a.combo_data) && out.rows.logic==reinterpret_cast<uintptr_t>(a.state_set) &&
        (loadout==reinterpret_cast<uintptr_t>(a.loadout) || shadow) && cfg==reinterpret_cast<uintptr_t>(a.combo_cfg) &&
        mud==reinterpret_cast<uintptr_t>(a.unlock_data) && loadout_actor_resolver::current(out.rows);
    return out.valid;
}
struct Scope {
    Loan loan{}; const Loan* previous;
    explicit Scope(const combat_mod::ChooserArgs& a):previous(current) { current=capture(a,loan)?&loan:nullptr; }
    ~Scope() { current=previous; }
};
inline bool live() {
    return current && current->valid && current->actor==loadout_player_catalogue::observed_player_actor.load() && loadout_actor_resolver::current(current->rows);
}
inline bool live(const combat_mod::ChooserArgs& a) {
    if (!live()) return false;
    const auto& b=current->args;
    return a.node==b.node && a.phase_is_2==b.phase_is_2 && a.combo_data==b.combo_data && a.conditions==b.conditions &&
        a.loadout==b.loadout && a.unlock_data==b.unlock_data && a.state_set==b.state_set && a.combo_cfg==b.combo_cfg &&
        a.graph==b.graph && a.anim_out==b.anim_out;
}
// Published acquisition is selection intent only. The mutable callback must
// recapture the native catalogue and run its real selection predicate again.
inline bool acquired(const Loan& loan,uint32_t id) {
    if (!loan.valid || !loadout_actor_resolver::current(loan.rows) || !TryAcquireSRWLockShared(&loadout_player_catalogue::state_lock)) return false;
    const auto& source=loadout_player_catalogue::published.catalogue;
    bool found=false;
    if (source.valid && source.world==loan.world && source.actor==loan.actor && source.graph_key==loan.graph &&
        source.cfg_key==loan.cfg && source.definitions_key==loan.definitions && source.revision==loan.revision) {
        for (size_t i=0;i<source.count;++i) {
            const auto& item=source.items[i];
            if (item.id==id && item.category==2 && item.known && item.acquired) { found=true; break; }
        }
    }
    ReleaseSRWLockShared(&loadout_player_catalogue::state_lock);
    return found && loadout_actor_resolver::current(loan.rows);
}
inline bool acquired(uint32_t id) { return live() && acquired(*current,id); }
// Current native update_graph_state invocation spans both its chooser calls
// and the later d2b0 commit. This is a stack borrow, not a simulation frame.
struct GraphLoan {
    Loan owner{};
    combat_mod::ChooserArgs args{};
    uint32_t chosen=UINT32_MAX;
    uint8_t authored_press=0;
    bool remap=false;
};
inline thread_local GraphLoan* graph_current=nullptr;
inline bool graph_live() {
    return graph_current && graph_current->owner.valid && loadout_player_catalogue::active() &&
        graph_current->owner.actor==loadout_player_catalogue::observed_player_actor.load() &&
        loadout_actor_resolver::current(graph_current->owner.rows);
}
struct GraphScope {
    GraphLoan loan{}; GraphLoan* previous;
    LocalLoadoutBorrow previous_borrow{};
    std::array<uint64_t,10> local_columns{};
    std::array<uint32_t,4> local_loadout{};
    void* forwarded=nullptr;
    GraphScope(void* columns,void* graph):previous(graph_current),previous_borrow(loadout_borrow),forwarded(columns) {
        graph_current=nullptr;
        loadout_borrow={};
        uint64_t q[10]{};
        if (!combat_native::copy_checked(combat_native::binding().copy,q,reinterpret_cast<uintptr_t>(columns),sizeof q) || q[9]>UINT32_MAX) return;
        auto row=[&](unsigned col,size_t stride) { return reinterpret_cast<void*>(static_cast<uintptr_t>(combat_native::at(q[col],q[9],stride))); };
        loan.args={nullptr,0,row(0,0x48),row(1,0x38),row(2,0x10),row(3,8),row(5,0x810),row(4,0xa0),graph,row(6,0x48)};
        if (capture(loan.args,loan.owner)) {
            graph_current=&loan;
            if (combat_native::copy_checked(combat_native::binding().copy,local_loadout.data(),reinterpret_cast<uintptr_t>(loan.args.loadout),sizeof local_loadout)) {
                const auto address=reinterpret_cast<uintptr_t>(local_loadout.data()),offset=q[9]*0x10;
                if (address<=offset || address-offset<0x10000) return;
                local_columns=std::to_array(q); local_columns[2]=address-offset;
                loadout_borrow={local_loadout.data(),reinterpret_cast<uintptr_t>(loan.args.loadout),loan.args.combo_data,loan.args.combo_cfg,graph};
                loan.args.loadout=local_loadout.data(); forwarded=local_columns.data();
                // Native follow-ups read the active unlocked finisher in this
                // invocation only; the saved native selection is unchanged.
                uint8_t combo[combat_mod::rows::kMinCombo]{},cfg[0x30]{},node[combat_native::graph::kNode]{};
                const auto cp=combat_native::binding().copy;
                if (combat_native::copy_checked(cp,combo,reinterpret_cast<uintptr_t>(loan.args.combo_data),sizeof combo) &&
                    combat_native::copy_checked(cp,cfg,reinterpret_cast<uintptr_t>(loan.args.combo_cfg),sizeof cfg)) {
                    uint32_t id=0,category=0;
                    if (combat_native::graph::node_at(cp,combat_native::graph::u64(combo,0),combat_native::graph::u32(combo,0x20),node) &&
                        combat_native::graph::form_def(cp,combat_native::graph::u64(combo,0),combat_native::graph::u64(cfg,8),node,&id,&category) &&
                        category==2 && (acquired(loan.owner,id) || (finisher_test_mode::active() && finisher_test_mode::known(id)))) local_loadout[2]=id;
                }
            }
        }
    }
    ~GraphScope() { graph_current=previous; loadout_borrow=previous_borrow; }
};
inline bool select_form(const combat_mod::ChooserArgs& a,uint32_t id) {
    if (!live(a) || (!acquired(id) && !(finisher_test_mode::active() && finisher_test_mode::known(id))) ||
        !graph_live() || loadout_borrow.local!=a.loadout) return false;
    static_cast<uint32_t*>(loadout_borrow.local)[2]=id;
    return live(a);
}
}
#endif
