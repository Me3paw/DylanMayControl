#ifdef NDEBUG
#error Assertions are required
#endif
#include "loadout_catalogue_join.h"
#include <cassert>
#include <iostream>
#include <utility>
int main() {
    using namespace loadout_catalogue_join;
    std::array<unsigned char,0x40> graph{};
    std::array<unsigned char,0x50*2> trees{};
    std::uint32_t row[2]{1,7};
    const auto g=reinterpret_cast<std::uintptr_t>(graph.data());
    const auto t=reinterpret_cast<std::uintptr_t>(trees.data());
    const auto r=reinterpret_cast<std::uintptr_t>(row);
    const auto put=[](auto& bytes,unsigned offset,auto value) {
        std::memcpy(bytes.data()+offset,&value,sizeof(value));
    };
    put(graph,0,std::uint64_t{t}); put(graph,8,std::uint32_t{2});
    put(graph,0x30,std::uint32_t{7}); graph[0x34]=1;
    put(trees,0x50,std::uint32_t{loadout_current_node::kMeleeTree});
    put(trees,0x50+0x48,std::uint32_t{1});
    std::uintptr_t deny=0; unsigned revision_reads=0; bool change=false;
    const auto read=[&](std::uintptr_t address,void* dest,std::size_t bytes) {
        if (address==deny) { std::memset(dest,0xcc,bytes); return false; }
        if (address==g+0x30 && ++revision_reads==2 && change) {
            const std::uint32_t newer=8; std::memcpy(dest,&newer,bytes); return true;
        }
        for (const auto& span:std::array<std::pair<std::uintptr_t,std::size_t>,3>{{
                {g,graph.size()},{t,trees.size()},{r,sizeof(row)}}})
            if (address>=span.first && address-span.first<=span.second &&
                bytes<=span.second-(address-span.first)) {
                std::memcpy(dest,reinterpret_cast<const void*>(address),bytes); return true;
            }
        return false;
    };
    Facts out{};
    const auto get=[&] { revision_reads=0; return inspect(g,r,out,read); };
    assert(get() && out.revision==7 && out.tree_index==1 && out.tree_key_address==t+0x50);
    row[1]=6; assert(!get() && out.graph_key==0); row[1]=7;
    row[0]=0; assert(!get()); row[0]=1;
    graph[0x34]=0; assert(!get()); graph[0x34]=1;
    put(graph,0x30,std::uint32_t{UINT32_MAX}); assert(!get()); put(graph,0x30,std::uint32_t{7});
    deny=r; assert(!get() && out.row_key==0); deny=0;
    change=true; assert(!get() && out.revision==0); change=false;
    put(trees,0x50+0x48,std::uint32_t{0}); assert(!get()); put(trees,0x50+0x48,std::uint32_t{1});
    put(graph,0,std::uint64_t{UINT64_MAX-15}); assert(!get());
    assert(!inspect(0,r,out,read) && out.graph_key==0);
    std::cout<<"loadout_catalogue_join PASS\n";
}
