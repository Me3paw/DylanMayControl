#pragma once
#include "loadout_attack_probe.h"
namespace loadout_current_node {
inline constexpr std::uint32_t kMeleeTree=0x79ca637b;
struct Facts {
    std::uint64_t graph_key=0,tree_key_address=0,node_key=0;
    std::uint32_t native_id=0,tree_index=0,node_index=0;
    std::uint16_t raw_flags=0,raw_low_six_bits=0;
    bool current_pointer_equal=false;
    bool native_case2_selected=false;
    bool native_case3_selectable=false;
};
// POD diagnostics from found=true lookups inside the caller's current G loan.
// Pointer equality and native predicate bits do not grant player/frame authority.
template<class Reader>
inline bool inspect(std::uintptr_t graph,std::uintptr_t found_tree,std::uintptr_t found_node,
                    std::uint32_t expected_id,Facts& out,Reader read) {
    out={};
    if (!graph || !found_tree || !found_node || !expected_id) return false;
    std::uintptr_t address=0,last=0;
    std::uint64_t trees=0,nodes=0;
    std::uint32_t tree_count=0,node_count=0,tree_key=0;
    unsigned char header[12]{};
    if (!loadout_attack_probe::add(graph,sizeof(header)-1,last) || !read(graph,header,sizeof(header))) return false;
    std::memcpy(&trees,header,8);std::memcpy(&tree_count,header+8,4);
    if (!trees || !tree_count || trees>(std::numeric_limits<std::uintptr_t>::max)() ||
        !loadout_attack_probe::add(static_cast<std::uintptr_t>(trees),
            static_cast<std::uint64_t>(tree_count)*0x50-1,last) || found_tree<trees)
        return false;
    const auto tree_offset=static_cast<std::uint64_t>(found_tree)-trees;
    if (tree_offset%0x50 || tree_offset/0x50>=tree_count) return false;
    const auto tree_index=static_cast<std::uint32_t>(tree_offset/0x50);
    if (!read(found_tree,&tree_key,sizeof(tree_key)) || tree_key!=kMeleeTree ||
        !loadout_attack_probe::add(found_tree,8,address) ||
        !loadout_attack_probe::add(address,sizeof(header)-1,last) || !read(address,header,sizeof(header))) return false;
    std::memcpy(&nodes,header,8);std::memcpy(&node_count,header+8,4);
    if (!nodes || !node_count || nodes>(std::numeric_limits<std::uintptr_t>::max)() ||
        !loadout_attack_probe::add(static_cast<std::uintptr_t>(nodes),
            static_cast<std::uint64_t>(node_count)*0x268-1,last) || found_node<nodes)
        return false;
    const auto node_offset=static_cast<std::uint64_t>(found_node)-nodes;
    if (node_offset%0x268 || node_offset/0x268>=node_count) return false;
    const auto node_index=static_cast<std::uint32_t>(node_offset/0x268);
    std::uint32_t id=0,ref[2]{}; std::uint16_t flags=0;
    unsigned char node[14]{};
    if (!loadout_attack_probe::add(found_node,0x1e8,address) ||
        !loadout_attack_probe::add(address,sizeof(node)-1,last) || !read(address,node,sizeof(node))) return false;
    std::memcpy(&id,node,4);std::memcpy(ref,node+4,8);std::memcpy(&flags,node+12,2);
    if (id!=expected_id ||
        ref[0]>=tree_count || ref[0]!=tree_index || ref[1]>=node_count || ref[1]!=node_index)
        return false;
    std::uintptr_t round_tree=0,round_node=0;
    if (!loadout_attack_probe::row_address(trees,ref[0],0x50,round_tree) || round_tree!=found_tree ||
        !loadout_attack_probe::row_address(nodes,ref[1],0x268,round_node) || round_node!=found_node)
        return false;
    Facts result{};
    result.graph_key=graph; result.tree_key_address=found_tree; result.node_key=found_node;
    result.native_id=id; result.tree_index=tree_index; result.node_index=node_index;
    result.raw_flags=flags; result.raw_low_six_bits=static_cast<std::uint16_t>(flags&0x3f);
    result.current_pointer_equal=true;
    result.native_case2_selected=(flags&0x20)!=0;
    result.native_case3_selectable=(flags&0x08)!=0 && (flags&0x10)==0;
    out=result; return true;
}
} // namespace loadout_current_node
