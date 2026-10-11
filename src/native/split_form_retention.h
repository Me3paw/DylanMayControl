#pragma once
#include <cstdint>

namespace split_form_retention {
inline constexpr std::uint32_t kPivot=0x87b82de3, kOrbit=0xc25e131c;
inline constexpr std::uint32_t kPredicateRva=0x2379bc0, kSpawnReturnRva=0x238a467;
inline constexpr const char* kPredicatePin="4883ec384180783400743b8b02413b40087333488d148048c1";
inline constexpr const char* kSpawnCallPin="4d8bc3498bd18b4818c5f877e859f7feff0fb6f0";
inline bool retain(std::uint32_t id,std::uint16_t flags) {
    return (id==kPivot || id==kOrbit) && !(flags&0x200) && (flags&0x3f)==0x08;
}
}

#ifdef _WIN32
#include "loadout_actor_resolver.h"
#include "loadout_player_catalogue.h"
#include "feature_status.h"
#include "step_binding.h"
namespace split_form_retention {
using Predicate=bool (*)(std::uint32_t,void*,void*);
inline Predicate original=nullptr;
inline std::uintptr_t executable_base=0;
inline const std::atomic<bool>* stopping=nullptr;
inline std::atomic<bool> enabled{false};

inline bool owned_split(std::uint32_t id,void* mud,void* graph) {
    if ((id!=kPivot && id!=kOrbit) || !loadout_player_catalogue::active() ||
        !loadout_lookup_probe::enabled.load() || loadout_lookup_probe::stopped.load()) return false;
    const auto world=step_binding::mail().world.load();
    const auto actor=loadout_player_catalogue::observed_player_actor.load();
    loadout_actor_resolver::Rows rows{};
    std::uintptr_t current_mud=0;
    std::uint32_t tree_index=UINT32_MAX;
    if (!world || !actor || !loadout_actor_resolver::resolve(world,actor,rows) ||
        !loadout_attack_probe::row_address(rows.mud_base,rows.location.row,8,current_mud) ||
        current_mud!=reinterpret_cast<std::uintptr_t>(mud) ||
        !loadout_attack_probe::copy(current_mud,&tree_index,sizeof tree_index)) return false;

    // Re-read ownership from this native graph invocation, never from the UI cache.
    loadout_lookup_probe::Lookup tree{},node{};
    const auto tree_get=reinterpret_cast<loadout_lookup_probe::Getter>(executable_base+0x1e019a0);
    const auto node_get=reinterpret_cast<loadout_lookup_probe::Getter>(executable_base+0x1e01a30);
    if (tree_get(&tree,graph,loadout_current_node::kMeleeTree)!=&tree || tree.found!=1 ||
        node_get(&node,reinterpret_cast<void*>(tree.pointer),id)!=&node || node.found!=1) return false;
    loadout_current_node::Facts state{};
    return loadout_current_node::inspect(reinterpret_cast<std::uintptr_t>(graph),tree.pointer,node.pointer,
               id,state,&loadout_attack_probe::copy) && state.tree_index==tree_index && retain(id,state.raw_flags) &&
        loadout_actor_resolver::current(rows) && world==step_binding::mail().world.load() &&
        actor==loadout_player_catalogue::observed_player_actor.load();
}

// Only request_form_spawning may retain these models. Equipment and every other
// caller keep the native predicate; detached timers/reattachment stay native.
__attribute__((noinline)) inline bool predicate(std::uint32_t id,void* mud,void* graph) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    const DWORD incoming=GetLastError();
    const bool selected=original(id,mud,graph);
    const DWORD forwarded=GetLastError();
    if (selected || !enabled.load() || !stopping || stopping->load() ||
        caller!=executable_base+kSpawnReturnRva ||
        !feature_status::registry().active(feature_status::Feature::Finisher)) return selected;
    SetLastError(incoming);
    const bool keep=owned_split(id,mud,graph);
    SetLastError(forwarded);
    return keep;
}
}
#endif
