#ifdef NDEBUG
#error Assertions are required
#endif
#include "loadout_attack_probe.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace loadout_attack_probe;
struct Span { std::uintptr_t base; std::size_t size; };
template<class T> void put(unsigned char* bytes,std::size_t offset,T value) {
    std::memcpy(bytes+offset,&value,sizeof(value));
}
int main() {
    std::uint64_t columns[11]{}, query[4]{0x12345678,0,0,0};
    unsigned char chunk[0x30]{},forms_data[0x78*2]{},attack[0xb0*2]{},
        cfg[0xa0*2]{},definitions[0x140*2]{};
    columns[0]=reinterpret_cast<std::uintptr_t>(forms_data);
    columns[2]=UINT64_MAX;
    columns[3]=reinterpret_cast<std::uintptr_t>(cfg);
    columns[5]=reinterpret_cast<std::uintptr_t>(attack);
    columns[9]=reinterpret_cast<std::uintptr_t>(chunk); columns[10]=1;
    const std::uint64_t actor=0x5566778800000042ULL;
    put(chunk,0x18,actor);
    put(forms_data,0x78+0x70,std::uint32_t{101});
    put(forms_data,0x78+0x74,std::uint32_t{202});
    put(attack,0xb0+8,std::uint32_t{7});
    put(attack,0xb0+0xc,std::uint32_t{1});
    put(attack,0xb0+0x10,std::uint32_t{2});
    attack[0xb0+0xa0]=9; attack[0xb0+0xae]=1;
    put(cfg,0xa0+8,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(definitions)));
    put(cfg,0xa0+0x10,std::uint32_t{2});
    put(definitions,0x140,std::uint32_t{UINT32_MAX}); definitions[0x140+0x70]=3;
    const std::vector<Span> spans{
        {reinterpret_cast<std::uintptr_t>(columns),sizeof(columns)},
        {reinterpret_cast<std::uintptr_t>(query),sizeof(query)},
        {reinterpret_cast<std::uintptr_t>(chunk),sizeof(chunk)},
        {reinterpret_cast<std::uintptr_t>(forms_data),sizeof(forms_data)},
        {reinterpret_cast<std::uintptr_t>(attack),sizeof(attack)},
        {reinterpret_cast<std::uintptr_t>(cfg),sizeof(cfg)},
        {reinterpret_cast<std::uintptr_t>(definitions),sizeof(definitions)}};
    std::uintptr_t deny=0;
    const auto reader=[&](std::uintptr_t source,void* dest,std::size_t size) {
        for (const auto& span:spans) {
            if (source<span.base || source-span.base>span.size ||
                size>span.size-(source-span.base)) continue;
            if (deny && source<=deny && deny-source<size) {
                std::memset(dest,0xaa,size); return false;
            }
            std::memcpy(dest,reinterpret_cast<const void*>(source),size); return true;
        }
        return false;
    };
    const auto snapshot=[&] { return capture(reinterpret_cast<std::uintptr_t>(columns),
        reinterpret_cast<std::uintptr_t>(query),reader); };
    auto s=snapshot();
    assert(s.copied==255 && s.world==query[0] && s.row==1 && s.actor==actor);
    assert(s.form70==101 && s.form74==202 && s.attack_index==7 &&
           s.form_index==1 && s.variant==2 && s.raw_a0==9 && s.raw_ae==1);
    assert(s.referenced_form_id==UINT32_MAX && s.category==3);
    put(definitions,0x140+0x70,std::uint32_t{0x100});
    s=snapshot(); assert(!(s.copied&kDefinition) && s.category==UINT8_MAX);
    put(definitions,0x140+0x70,std::uint32_t{3});
    put(chunk,0x18,actor-(1ULL<<32));
    assert(snapshot().actor!=s.actor);
    put(chunk,0x18,actor);
    deny=reinterpret_cast<std::uintptr_t>(query);
    s=snapshot(); assert(!(s.copied&kWorld) && s.world==0 && (s.copied&kActor));
    deny=reinterpret_cast<std::uintptr_t>(chunk)+0x18;
    s=snapshot(); assert(!(s.copied&kActor) && s.actor==0 && (s.copied&kAttackIndices));
    deny=reinterpret_cast<std::uintptr_t>(attack)+0xb0+8;
    s=snapshot(); assert(!(s.copied&kAttackIndices) && !(s.copied&kDefinition) &&
                        s.attack_index==UINT32_MAX && (s.copied&kFlagA0));
    deny=0; put(attack,0xb0+0xc,std::uint32_t{2});
    s=snapshot(); assert(!(s.copied&kDefinition) && (s.copied&kAttackIndices));
    columns[10]=UINT64_MAX;
    s=snapshot(); assert(s.copied==(kBundle|kWorld));
    columns[10]=1; columns[5]=UINT64_MAX-8;
    s=snapshot(); assert(!(s.copied&kAttackIndices) && (s.copied&kActor));
    std::cout<<"loadout_attack_probe PASS\n";
}
