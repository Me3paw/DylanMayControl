#include "../../src/native/loadout_selection_scope.h"
#include <cassert>
#include <cstring>
#include <limits>

int main() {
    using namespace loadout_selection_scope;
    std::uint64_t outer=0x123456789abcdef0ULL;
    std::array<unsigned char,0x60> system{};
    std::array<unsigned char,0x40> menu{};
    std::array<unsigned char,0x270> input{};
    std::array<unsigned char,0x2060> lua{};
    const auto put=[](auto& bytes,unsigned offset,auto value) {
        std::memcpy(bytes.data()+offset,&value,sizeof(value));
    };
    std::array<std::uint64_t,7> args{
        reinterpret_cast<std::uintptr_t>(menu.data()),0x1111,
        reinterpret_cast<std::uintptr_t>(lua.data()),0x3333,0x4444,
        reinterpret_cast<std::uintptr_t>(input.data()),0x6666};
    put(system,0x58,reinterpret_cast<std::uintptr_t>(args.data()));
    put(menu,0x3c,std::uint32_t{0xfedcba98});
    put(input,0xa0,std::uint64_t{0x100000002});
    put(input,0x268,std::uint64_t{0x300000004});
    put(lua,0x2058,std::uint64_t{0x500000006});
    put(lua,0x1048,std::uint64_t{0x700000008});
    struct Range { std::uintptr_t start; std::size_t size; };
    const std::array<Range,6> ranges{{
        {reinterpret_cast<std::uintptr_t>(&outer),sizeof(outer)},
        {reinterpret_cast<std::uintptr_t>(system.data()),system.size()},
        {reinterpret_cast<std::uintptr_t>(args.data()),sizeof(args)},
        {reinterpret_cast<std::uintptr_t>(menu.data()),menu.size()},
        {reinterpret_cast<std::uintptr_t>(input.data()),input.size()},
        {reinterpret_cast<std::uintptr_t>(lua.data()),lua.size()}}};
    std::uintptr_t fail=0;
    unsigned auxiliary_reads=0;
    const auto read=[&](std::uintptr_t address,void* dst,std::size_t count) {
        if(address==ranges[0].start || address==args[5]+0xa0 || address==args[5]+0x268 || address==args[2]+0x2058 || address==args[2]+0x1048)++auxiliary_reads;
        if (address==fail) { std::memset(dst,0xcc,count); return false; }
        for (const auto& range:ranges)
            if (address>=range.start && address-range.start<=range.size &&
                count<=range.size-(address-range.start)) {
                std::memcpy(dst,reinterpret_cast<const void*>(address),count); return true;
            }
        std::memset(dst,0xcc,count); return false;
    };
    const auto capture_now=[&] { return capture(ranges[0].start,ranges[1].start,read); };
    const auto full=capture_now();
    assert(full.copied==(diagnostics::enabled?127u:kArguments|kMenuId) && full.arguments==args && full.menu_id==0xfedcba98);
    if constexpr(diagnostics::enabled) {
    assert(full.outer_first_word==outer && full.input_world_a0==0x100000002);
    assert(full.input_world_268==0x300000004 && full.arena_counter==0x500000006);
    assert(full.message_counter==0x700000008);
    } else assert(auxiliary_reads==0 && full.outer_first_word==0 && full.arena_counter==0 && full.input_world_a0==0);
    fail=ranges[2].start;
    const auto bad_array=capture_now();
    assert(bad_array.copied==(diagnostics::enabled?static_cast<unsigned>(kOuterWord):0u) && bad_array.argument_array==0);
    assert((bad_array.arguments==std::array<std::uint64_t,7>{}));
    fail=ranges[1].start+0x58;
    assert(capture_now().copied==(diagnostics::enabled?static_cast<unsigned>(kOuterWord):0u));
    fail=ranges[0].start;
    assert(capture_now().outer_first_word==0 && !(capture_now().copied&kOuterWord));
    fail=args[2]+0x2058;
    const auto bad_counter=capture_now();
    assert(bad_counter.arena_counter==0 && !(bad_counter.copied&kArenaCounter));
    assert(bad_counter.message_counter==full.message_counter);
    args[0]=(std::numeric_limits<std::uintptr_t>::max)()-0x20;
    args[5]=0; fail=0;
    const auto bad_fields=capture_now();
    assert(!(bad_fields.copied&(kMenuId|kInputA0|kInput268)));
    assert(bad_fields.menu_id==0 && bad_fields.input_world_a0==0 && bad_fields.input_world_268==0);
    assert(capture(0,0,read).copied==0);
}
