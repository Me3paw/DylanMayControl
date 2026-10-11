#ifdef NDEBUG
#error Assertions are required
#endif
#include "loadout_form_requirements.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <utility>
int main() {
    using namespace loadout_form_requirements;
    std::array<unsigned char,0x140> definition{};
    std::array<unsigned char,kMaxRequirements*4> records{};
    const auto put=[](auto& bytes,unsigned offset,auto value) {
        std::memcpy(bytes.data()+offset,&value,sizeof(value));
    };
    const auto def=reinterpret_cast<std::uintptr_t>(definition.data());
    const auto req=reinterpret_cast<std::uintptr_t>(records.data());
    std::uintptr_t denied=0;
    const auto read=[&](std::uintptr_t source,void* dest,std::size_t size) {
        if (source==denied) { std::memset(dest,0xcc,size); return false; }
        for (const auto& span:std::array<std::pair<std::uintptr_t,std::size_t>,2>{{
                {def,definition.size()},{req,records.size()}}}) {
            if (source>=span.first && source-span.first<=span.second &&
                size<=span.second-(source-span.first)) {
                std::memcpy(dest,reinterpret_cast<const void*>(source),size); return true;
            }
        }
        std::memset(dest,0xcc,size); return false;
    };
    put(definition,0,std::uint32_t{42}); put(definition,0x70,std::uint32_t{3});
    put(definition,0x78,std::uint64_t{req}); put(definition,0x80,std::uint32_t{2});
    Summary out{};
    records[0]=0; records[4]=1;
    assert(classify(def,42,out,read) && out.channels==0 && out.native_category==3 && out.input_kinds==3);
    records[0]=2; records[4]=3;
    assert(classify(def,42,out,read) && out.channels==0 && out.input_kinds==12);
    records[0]=0; records[4]=2;
    assert(classify(def,42,out,read) && out.channels==0 && out.input_kinds==5);
    // Native category selects a weapon slot independently of required attack inputs.
    put(definition,0x70,std::uint32_t{0});
    assert(classify(def,42,out,read) && out.channels==kPrimary && out.input_kinds==5);
    put(definition,0x70,std::uint32_t{1});
    assert(classify(def,42,out,read) && out.channels==kSecondary && out.input_kinds==5);
    put(definition,0x70,std::uint32_t{0x100});
    assert(!classify(def,42,out,read) && out.native_id==0 && out.native_category==UINT8_MAX);
    put(definition,0x70,std::uint32_t{2}); records[4]=4;
    assert(!classify(def,42,out,read) && out.channels==0);
    records[4]=2; denied=req;
    assert(!classify(def,42,out,read) && out.input_kinds==0 && out.native_id==0);
    denied=0;
    assert(!classify(def,43,out,read) && !classify(def,0,out,read));
    put(definition,0x80,std::uint32_t{kMaxRequirements+1});
    assert(!classify(def,42,out,read) && out.channels==0);
    put(definition,0x80,std::uint32_t{0}); put(definition,0x78,std::uint64_t{0});
    assert(classify(def,42,out,read) && out.native_id==42 && out.channels==0);
    put(definition,0x80,std::uint32_t{1}); put(definition,0x78,std::uint64_t{UINT64_MAX-1});
    assert(!classify(def,42,out,read));
    assert(!classify(0,42,out,read) && out.native_id==0);
    std::cout<<"loadout_form_requirements PASS\n";
}
