#pragma once
#include "loadout_lookup_probe.h"
#ifdef _WIN32
namespace loadout_native_selection {
using Producer=void (*)(std::uint32_t,void*,void*);
using Predicate=std::uint8_t (*)(std::uint8_t,const std::uint32_t*,void*,std::uint8_t,void*,void*);
enum class Outcome : unsigned { Disabled,NoCurrentLoan,InvalidArguments,LookupFailed,MixedState,PreflightRejected,AlreadySelected,Submitted };
struct Result {
    Outcome outcome=Outcome::Disabled;
    unsigned lookup_status=loadout_lookup_probe::Disabled;
    loadout_current_node::Facts node{};
};
inline std::atomic<bool> enabled{false},stopped{false};
inline std::uintptr_t executable_base=0;
inline Producer producer=nullptr;
using QueueCommand=void (*)(std::uint8_t,std::uint64_t,void*);
inline QueueCommand queue_command=nullptr;
inline constexpr unsigned char queue_pin[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x41,0x56,0x48,0x83,0xec,0x30,0x49,0x8b,0xd8};
inline bool queue_matches() {
    unsigned char actual[sizeof queue_pin]{};
    return executable_base && loadout_attack_probe::copy(executable_base+0x1e01f40,actual,sizeof actual) && !std::memcmp(actual,queue_pin,sizeof actual);
}
inline constexpr unsigned char predicate_pin[25]={0x40,0x56,0x41,0x54,0x41,0x57,0x48,0x83,0xec,0x30,0x8b,0x02,0x45,0x0f,0xb6,0xe1,0x8b,0x52,0x04,0x4d,0x8b,0xf8,0x48,0x69,0xf2};
inline bool predicate_matches() {
    unsigned char bytes[sizeof(predicate_pin)]{};
    return executable_base && loadout_attack_probe::copy(executable_base+0x1e01ca0,bytes,sizeof(bytes)) &&
        std::memcmp(bytes,predicate_pin,sizeof(bytes))==0;
}
inline bool prepare(std::uintptr_t base,Producer original,bool executable_verified,bool producer_verified,bool module_pinned) {
    if (!base || !original || !executable_verified || !producer_verified || !module_pinned || enabled.load() || stopped.load()) return false;
    executable_base=base;
    if (!predicate_matches() || !queue_matches() || loadout_lookup_probe::exe_base!=base || !loadout_lookup_probe::enabled.load() ||
        !loadout_lookup_probe::pins_match()) { executable_base=0; return false; }
    producer=original;queue_command=reinterpret_cast<QueueCommand>(base+0x1e01f40); enabled.store(true); return true;
}
inline Result submit_current(std::uint32_t id,bool (*before_commit)(void*),void* context,std::uint32_t old_id=0,bool owned_only=false) {
    const DWORD incoming=GetLastError();
    struct Restore { DWORD value; ~Restore() { SetLastError(value); } } restore{incoming};
    Result result{};
    const auto finish=[&](Outcome outcome) { result.outcome=outcome; return result; };
    if (!enabled.load() || stopped.load() || !producer) return finish(Outcome::Disabled);
    const auto* loan=loadout_selection_scope::borrowed;
    if (!loan || !loadout_selection_scope::active() || !before_commit || !context) return finish(Outcome::NoCurrentLoan);
    const auto call=loan->call;
    const auto current=[&]() {
        const auto* now=loadout_selection_scope::borrowed;
        return enabled.load() && !stopped.load() && loadout_selection_scope::active() && now==loan && now->call==call;
    };
    loadout_selection_scope::Record record{};
    record.kind=0; record.scope_call=call;
    record.value=loadout_selection_scope::capture(loan->outer,loan->system,&loadout_attack_probe::copy);
    if (!(record.value.copied&loadout_selection_scope::kArguments) || !id || !record.value.arguments[1] ||
        !record.value.arguments[2]) return finish(Outcome::InvalidArguments);
    record.value.menu_id=id; record.value.copied|=loadout_selection_scope::kMenuId;
    loadout_lookup_probe::observe(record);
    result.lookup_status=record.lookup_status; result.node=record.current_node;
    if (record.lookup_status!=loadout_lookup_probe::CurrentNode || !current() || !predicate_matches()) return finish(Outcome::LookupFailed);
    const auto low=result.node.raw_low_six_bits;
    if ((result.node.raw_flags&0x200) || (low!=0x08 && low!=0x20)) return finish(Outcome::MixedState);
    std::uint32_t reference[2]{result.node.tree_index,result.node.node_index};
    auto* graph=reinterpret_cast<void*>(static_cast<std::uintptr_t>(record.value.arguments[1]));
    auto* lua=reinterpret_cast<void*>(static_cast<std::uintptr_t>(record.value.arguments[2]));
    const auto predicate=reinterpret_cast<Predicate>(executable_base+0x1e01ca0);
    const std::uint8_t event=low==0x20 ? 2u : 3u;
    if (!predicate(event,reference,graph,0,nullptr,nullptr) || !current()) return finish(Outcome::PreflightRejected);

    loadout_current_node::Facts old{};bool clear_selected=false,clear_marker=false;
    if(owned_only) {
        if(!queue_command || !queue_matches()) return finish(Outcome::Disabled);
        if(low==0x20) return finish(Outcome::AlreadySelected);
        if(old_id && old_id!=id) {
            auto previous=record;previous.value.menu_id=old_id;loadout_lookup_probe::observe(previous);
            if(previous.lookup_status!=loadout_lookup_probe::CurrentNode || !current()) return finish(Outcome::LookupFailed);
            old=previous.current_node;std::uint32_t old_reference[2]{old.tree_index,old.node_index};
            clear_selected=predicate(2,old_reference,graph,0,nullptr,nullptr)!=0;
            clear_marker=predicate(5,old_reference,graph,0,nullptr,nullptr)!=0;
            if(!current()) return finish(Outcome::NoCurrentLoan);
        }
    }
    if (!before_commit(context) || !current()) return finish(Outcome::NoCurrentLoan);
    loadout_selection_scope::observe_choice(id,graph,lua,false);
    if (!current()) return finish(Outcome::NoCurrentLoan);
    SetLastError(incoming);
    if(owned_only) {
        std::uint64_t selected_reference=0,old_reference=0;
        std::memcpy(&selected_reference,reference,sizeof selected_reference);
        const std::uint32_t old_pair[2]{old.tree_index,old.node_index};std::memcpy(&old_reference,old_pair,sizeof old_reference);
        // Native owned-weapon UI path: deselect/clear old, select new. No acquire.
        if(clear_selected) queue_command(2,old_reference,graph);
        if(clear_marker) queue_command(5,old_reference,graph);
        queue_command(3,selected_reference,graph);
    } else producer(id,graph,lua);
    loadout_selection_scope::observe_choice(id,graph,lua,true);
    return finish(Outcome::Submitted);
}
// Reference CFOD activates with commands2/3 inside the same native graph body.
// This path borrows no old pointers: the current Player graph loan and lookup
// references are verified again before either native command is queued.
inline bool activate_finisher(const combat_mod::ChooserArgs& args,uint32_t id) {
    const DWORD incoming=GetLastError();
    struct Restore { DWORD value; ~Restore() { SetLastError(value); } } restore{incoming};
    if(!enabled.load() || stopped.load() || !queue_command || !queue_matches() || !predicate_matches() || !loadout_lookup_probe::pins_match() ||
        !finisher_chooser_scope::live(args) || !finisher_chooser_scope::graph_live() || !finisher_chooser_scope::acquired(id)) return false;
    const auto& owner=finisher_chooser_scope::current->rows;
    const auto graph=reinterpret_cast<uintptr_t>(args.graph);
    auto tree_get=reinterpret_cast<loadout_lookup_probe::Getter>(executable_base+0x1e019a0);
    auto node_get=reinterpret_cast<loadout_lookup_probe::Getter>(executable_base+0x1e01a30);
    loadout_lookup_probe::Lookup tree{},node{};
    if(tree_get(&tree,args.graph,loadout_current_node::kMeleeTree)!=&tree || tree.found!=1 ||
        node_get(&node,reinterpret_cast<void*>(tree.pointer),id)!=&node || node.found!=1) return false;
    loadout_current_node::Facts desired{};
    if(!loadout_current_node::inspect(graph,tree.pointer,node.pointer,id,desired,&loadout_attack_probe::copy) || (desired.raw_flags&0x200)) return false;
    uint32_t native_loadout[4]{};uintptr_t row=0;
    if(!loadout_attack_probe::row_address(owner.loadout_base,owner.location.row,0x10,row) || !loadout_attack_probe::copy(row,native_loadout,sizeof native_loadout)) return false;
    if(native_loadout[2]==id && desired.raw_low_six_bits==0x20) return true;
    const auto predicate=reinterpret_cast<Predicate>(executable_base+0x1e01ca0);
    uint32_t desired_ref[2]{desired.tree_index,desired.node_index};
    if(desired.raw_low_six_bits!=0x08 || !predicate(3,desired_ref,args.graph,0,nullptr,nullptr)) return false;
    loadout_current_node::Facts old{};bool clear=false;
    if(native_loadout[2] && native_loadout[2]!=id) {
        loadout_lookup_probe::Lookup previous{};
        if(node_get(&previous,reinterpret_cast<void*>(tree.pointer),native_loadout[2])!=&previous || previous.found!=1 ||
            !loadout_current_node::inspect(graph,tree.pointer,previous.pointer,native_loadout[2],old,&loadout_attack_probe::copy)) return false;
        uint32_t old_ref[2]{old.tree_index,old.node_index};clear=predicate(2,old_ref,args.graph,0,nullptr,nullptr)!=0;
        if((old.raw_flags&0x200) || (old.raw_low_six_bits==0x20 && !clear)) return false;
    }
    if(!finisher_chooser_scope::live(args) || !finisher_chooser_scope::graph_live() || !loadout_actor_resolver::current(owner)) return false;
    uint64_t packed=0;std::memcpy(&packed,desired_ref,8);
    SetLastError(incoming);
    if(clear){uint32_t old_ref[2]{old.tree_index,old.node_index};uint64_t prior=0;std::memcpy(&prior,old_ref,8);queue_command(2,prior,args.graph);}
    queue_command(3,packed,args.graph);
    feature_status::registry().event(feature_status::Feature::Finisher,feature_status::Event::Applied,graph,reinterpret_cast<uintptr_t>(args.combo_data),id,0x1e01f40,"native finisher activation queued without fixed delay");
    return true;
}
inline void stop() { stopped.store(true); enabled.store(false); }
}
#endif
