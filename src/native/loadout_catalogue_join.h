#pragma once
#include "loadout_current_node.h"
namespace loadout_catalogue_join {
struct Facts {
    std::uint64_t graph_key=0,row_key=0,tree_key_address=0;
    std::uint32_t revision=0,tree_index=0;
};
// Current borrowed G/entity-row catalogue association, not actor/frame authority.
template<class Reader>
inline bool inspect(std::uintptr_t graph,std::uintptr_t melee_unlock_row,
                    Facts& out,Reader read) {
    out={};
    if (!graph || !melee_unlock_row) return false;
    std::uintptr_t revision_address=0,active_address=0,address=0,last=0,tree=0;
    std::uint32_t revision=0,row[2]{},count=0;
    std::uint64_t trees=0;
    std::uint8_t active=0;
    if (!loadout_attack_probe::add(graph,0x34,active_address) ||
        !loadout_attack_probe::add(graph,0x30,revision_address) ||
        !read(active_address,&active,sizeof(active)) || !active ||
        !read(revision_address,&revision,sizeof(revision)) || revision==UINT32_MAX ||
        !loadout_attack_probe::add(melee_unlock_row,7,last) || !read(melee_unlock_row,row,sizeof(row)) ||
        row[1]!=revision || !read(graph,&trees,sizeof(trees)) ||
        !loadout_attack_probe::add(graph,8,address) || !read(address,&count,sizeof(count)) ||
        !trees || !count || row[0]>=count || trees>(std::numeric_limits<std::uintptr_t>::max)() ||
        !loadout_attack_probe::add(static_cast<std::uintptr_t>(trees),
            static_cast<std::uint64_t>(count)*0x50-1,last) ||
        !loadout_attack_probe::row_address(trees,row[0],0x50,tree)) return false;
    std::uint32_t key=0,native_index=0;
    if (!read(tree,&key,sizeof(key)) || key!=loadout_current_node::kMeleeTree ||
        !loadout_attack_probe::add(tree,0x48,address) ||
        !read(address,&native_index,sizeof(native_index)) || native_index!=row[0]) return false;
    std::uint32_t final_revision=0;
    std::uint8_t final_active=0;
    if (!read(revision_address,&final_revision,sizeof(final_revision)) || final_revision!=revision ||
        !read(active_address,&final_active,sizeof(final_active)) || !final_active) return false;
    Facts result{};
    result.graph_key=graph; result.row_key=melee_unlock_row; result.tree_key_address=tree;
    result.revision=revision; result.tree_index=row[0];
    out=result; return true;
}
} // namespace loadout_catalogue_join
