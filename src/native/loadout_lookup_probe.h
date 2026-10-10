#pragma once
#include "loadout_selection_scope.h"
#include "loadout_current_node.h"
#ifdef _WIN32
namespace loadout_lookup_probe {
struct Lookup {
    std::uintptr_t pointer=0;
    std::uint8_t found=0;
    std::uint8_t padding[7]{};
};
static_assert(sizeof(Lookup)==16 && offsetof(Lookup,found)==8,"proved Win64 lookup result");
using Getter=Lookup* (*)(Lookup*,void*,std::uint32_t);
enum Status : unsigned { Disabled, InvalidLoan, InvalidGraph, EmptyGraph, TreeMissing,
    InvalidTree, EmptyNodes, InvalidNodes, ReturnMismatch, FoundFlagInvalid, NodeMissing,
    ReferenceInvalid, CurrentNode, Retired };
inline std::atomic<bool> enabled{false},stopped{false};
inline std::uintptr_t exe_base=0;
#ifdef CRMOD_API_SELF_CHECK
inline Getter test_tree=nullptr,test_node=nullptr;
inline bool (*test_pins)()=nullptr;
#endif
inline constexpr unsigned char tree_pin[25]={
    0x48,0x8b,0x02,0x8b,0x52,0x08,0x48,0x8d,0x14,0x92,0x48,0xc1,0xe2,0x04,
    0x48,0x03,0xd0,0x48,0x3b,0xc2,0x74,0x0e,0x44,0x39,0x00};
inline constexpr unsigned char node_pin[25]={
    0x48,0x8b,0x42,0x18,0x4c,0x8b,0xd1,0x48,0x8b,0x00,0x80,0x78,0x19,0x00,
    0x0f,0x85,0x81,0x00,0x00,0x00,0x4c,0x8b,0x5a,0x08,0x0f};
inline bool match(std::uintptr_t rva,const unsigned char (&pin)[25]) {
    unsigned char bytes[25]{};
    return exe_base && loadout_attack_probe::copy(exe_base+rva,bytes,sizeof(bytes)) &&
        std::memcmp(bytes,pin,sizeof(bytes))==0;
}
inline bool pins_match() {
#ifdef CRMOD_API_SELF_CHECK
    if (test_pins) return test_pins();
#endif
    return match(0x1dda190,tree_pin) && match(0x1dda220,node_pin);
}
inline bool readable_span(std::uintptr_t address,std::uint64_t size) {
    std::uintptr_t last=0;
    if (!size || !loadout_attack_probe::add(address,size-1,last)) return false;
    auto cursor=address;
    for (unsigned i=0;i<64;++i) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<void*>(cursor),&region,sizeof(region))!=sizeof(region) ||
            region.State!=MEM_COMMIT || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        const DWORD protection=region.Protect&0xff;
        if (protection!=PAGE_READONLY && protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
            protection!=PAGE_EXECUTE_READ && protection!=PAGE_EXECUTE_READWRITE &&
            protection!=PAGE_EXECUTE_WRITECOPY) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        std::uintptr_t end=0;
        if (!region.RegionSize || cursor<base ||
            !loadout_attack_probe::add(base,region.RegionSize-1,end) || cursor>end) return false;
        if (end>=last) return true;
        cursor=end+1;
    }
    return false;
}
inline bool live(std::uint64_t scope_call) {
    const auto* scope=loadout_selection_scope::borrowed;
    return enabled.load() && !stopped.load() && loadout_selection_scope::active() &&
        scope && scope->call==scope_call;
}
inline void observe(loadout_selection_scope::Record& record) {
    const DWORD saved=GetLastError();
    struct Restore { DWORD error; ~Restore() { SetLastError(error); } } restore{saved};
    record.current_node={}; record.lookup_status=Disabled;
    record.tree_called=false; record.node_called=false;
    if (!enabled.load() || stopped.load() || record.kind!=0) return;
    if (!live(record.scope_call) ||
        (record.value.copied&(loadout_selection_scope::kArguments|loadout_selection_scope::kMenuId)) !=
        (loadout_selection_scope::kArguments|loadout_selection_scope::kMenuId)) {
        record.lookup_status=InvalidLoan; return;
    }
    const auto graph=static_cast<std::uintptr_t>(record.value.arguments[1]);
    std::uintptr_t address=0;
    std::uint64_t trees=0; std::uint32_t count=0;
    if (!graph || !loadout_attack_probe::copy(graph,&trees,sizeof(trees)) ||
        !loadout_attack_probe::add(graph,8,address) ||
        !loadout_attack_probe::copy(address,&count,sizeof(count))) {
        record.lookup_status=InvalidGraph; return;
    }
    if (!count) { record.lookup_status=EmptyGraph; return; }
    if (count>128 || !readable_span(static_cast<std::uintptr_t>(trees),
            static_cast<std::uint64_t>(count)*0x50) || !pins_match() || !live(record.scope_call)) {
        record.lookup_status=InvalidGraph; return;
    }
    Lookup tree{};
    record.tree_called=true;
    auto get_tree=reinterpret_cast<Getter>(exe_base+0x1dda190);
#ifdef CRMOD_API_SELF_CHECK
    if (test_tree) get_tree=test_tree;
#endif
    if (get_tree(&tree,reinterpret_cast<void*>(graph),loadout_current_node::kMeleeTree)!=&tree) {
        record.lookup_status=ReturnMismatch; return;
    }
    if (!live(record.scope_call)) { record.lookup_status=Retired; return; }
    if (!tree.found) { record.lookup_status=TreeMissing; return; }
    if (tree.found!=1) { record.lookup_status=FoundFlagInvalid; return; }
    if (tree.pointer<trees || (tree.pointer-trees)%0x50 || (tree.pointer-trees)/0x50>=count) {
        record.lookup_status=InvalidTree; return;
    }
    std::uint64_t nodes=0,head=0,first=0;
    std::uint32_t node_count=0,key=0;
    if (!loadout_attack_probe::copy(tree.pointer,&key,sizeof(key)) || key!=loadout_current_node::kMeleeTree ||
        !loadout_attack_probe::add(tree.pointer,8,address) || !loadout_attack_probe::copy(address,&nodes,sizeof(nodes)) ||
        !loadout_attack_probe::add(tree.pointer,0x10,address) ||
        !loadout_attack_probe::copy(address,&node_count,sizeof(node_count))) {
        record.lookup_status=InvalidTree; return;
    }
    if (!node_count) { record.lookup_status=EmptyNodes; return; }
    if (node_count>2048 || !readable_span(static_cast<std::uintptr_t>(nodes),
            static_cast<std::uint64_t>(node_count)*0x268) ||
        !loadout_attack_probe::add(tree.pointer,0x18,address) || !loadout_attack_probe::copy(address,&head,sizeof(head)) ||
        !readable_span(static_cast<std::uintptr_t>(head),0x28) ||
        !loadout_attack_probe::copy(static_cast<std::uintptr_t>(head),&first,sizeof(first)) ||
        !readable_span(static_cast<std::uintptr_t>(first),0x28) || !pins_match() || !live(record.scope_call)) {
        record.lookup_status=InvalidNodes; return;
    }
    Lookup node{};
    record.node_called=true;
    auto get_node=reinterpret_cast<Getter>(exe_base+0x1dda220);
#ifdef CRMOD_API_SELF_CHECK
    if (test_node) get_node=test_node;
#endif
    if (get_node(&node,reinterpret_cast<void*>(tree.pointer),record.value.menu_id)!=&node) {
        record.lookup_status=ReturnMismatch; return;
    }
    if (!live(record.scope_call)) { record.lookup_status=Retired; return; }
    if (!node.found) { record.lookup_status=NodeMissing; return; }
    if (node.found!=1) { record.lookup_status=FoundFlagInvalid; return; }
    if (!loadout_current_node::inspect(graph,tree.pointer,node.pointer,record.value.menu_id,
            record.current_node,&loadout_attack_probe::copy)) {
        record.lookup_status=ReferenceInvalid; return;
    }
    record.lookup_status=CurrentNode;
}
inline bool prepare(bool opt_in,std::uintptr_t base,bool executable_hash_verified,
                    bool offline_unique_pins_verified,bool module_pinned) {
    if (!opt_in || !base || !executable_hash_verified || !offline_unique_pins_verified ||
        !module_pinned || enabled.load() || stopped.load()) return false;
    exe_base=base;
    if (!pins_match()) { exe_base=0; return false; }
    enabled.store(true); return true;
}
inline void stop() { stopped.store(true); enabled.store(false); }
} // namespace loadout_lookup_probe
#endif
