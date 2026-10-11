#ifdef NDEBUG
#error Assertions are required
#endif
#include "loadout_current_node.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <utility>
int main() {
    using namespace loadout_current_node;
    std::array<unsigned char,16> graph{};
    std::array<unsigned char,0x50*2> trees{};
    std::array<unsigned char,0x268*2> nodes{};
    const auto put=[](auto& bytes,unsigned offset,auto value) {
        std::memcpy(bytes.data()+offset,&value,sizeof(value));
    };
    const auto g=reinterpret_cast<std::uintptr_t>(graph.data());
    const auto t=reinterpret_cast<std::uintptr_t>(trees.data())+0x50;
    const auto n=reinterpret_cast<std::uintptr_t>(nodes.data())+0x268;
    put(graph,0,std::uint64_t{t-0x50}); put(graph,8,std::uint32_t{2});
    put(trees,0x50,std::uint32_t{kMeleeTree});
    put(trees,0x58,std::uint64_t{n-0x268}); put(trees,0x60,std::uint32_t{2});
    put(nodes,0x268+0x1e8,std::uint32_t{42});
    put(nodes,0x268+0x1ec,std::uint32_t{1}); put(nodes,0x268+0x1f0,std::uint32_t{1});
    std::uintptr_t denied=0;
    unsigned reads=0;
    const auto read=[&](std::uintptr_t source,void* dest,std::size_t size) {
        ++reads;
        if (denied>=source && denied-source<size) { std::memset(dest,0xcc,size); return false; }
        for (const auto& span:std::array<std::pair<std::uintptr_t,std::size_t>,3>{{
                {g,graph.size()},{t-0x50,trees.size()},{n-0x268,nodes.size()}}})
            if (source>=span.first && source-span.first<=span.second &&
                size<=span.second-(source-span.first)) {
                std::memcpy(dest,reinterpret_cast<const void*>(source),size); return true;
            }
        std::memset(dest,0xcc,size); return false;
    };
    Facts out{};
    for (std::uint16_t flags:{0x48,0x60,0x44,0x18,0x09}) {
        put(nodes,0x268+0x1f4,flags);reads=0;
        assert(inspect(g,t,n,42,out,read) && out.current_pointer_equal);
        assert(reads==4); // Four gap-free guarded spans, without a pointer cache.
        assert(out.native_case2_selected==((flags&0x20)!=0));
        assert(out.native_case3_selectable==((flags&0x08)!=0 && !(flags&0x10)));
        assert(out.raw_low_six_bits==(flags&0x3f));
    }
    put(nodes,0x268+0x1f0,std::uint32_t{2});
    assert(!inspect(g,t,n,42,out,read) && out.node_key==0);
    put(nodes,0x268+0x1f0,std::uint32_t{1});
    assert(!inspect(g,t+1,n,42,out,read) && !inspect(g,t,n+1,42,out,read));
    assert(!inspect(g,t,n,43,out,read) && !inspect(g,t,n,0,out,read));
    for(auto span:std::array<std::pair<std::uintptr_t,std::size_t>,4>{{{g,12},{t,4},{t+8,12},{n+0x1e8,14}}})
        for(std::size_t byte=0;byte<span.second;++byte) {
            denied=span.first+byte;
            assert(!inspect(g,t,n,42,out,read) && out.raw_flags==0 && !out.current_pointer_equal);
        }
    denied=t+4; // Unused padding stays outside the requested spans.
    assert(inspect(g,t,n,42,out,read));
    denied=0; put(graph,0,std::uint64_t{UINT64_MAX-15});
    assert(!inspect(g,t,n,42,out,read) && out.graph_key==0);
    put(graph,0,std::uint64_t{t-0x50}); put(trees,0x58,std::uint64_t{UINT64_MAX-15});
    assert(!inspect(g,t,n,42,out,read));
    assert(!inspect(0,t,n,42,out,read));
    std::cout<<"loadout_current_node PASS\n";
}
