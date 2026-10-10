#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cmath>
namespace nearby_candidates {
struct Entry { std::uint64_t actor=0;float distance_squared=0;std::uint32_t seen_ms=0;bool controller=false; };
constexpr unsigned kCapacity=128;
struct Snapshot {std::array<Entry,kCapacity> entries{};unsigned count=0;};
inline bool nearer(const Entry& a,const Entry& b) {return a.controller!=b.controller ? a.controller : a.distance_squared<b.distance_squared;}
class Cache {
    std::uint64_t world_=0,player_=0;
    Snapshot values_{};
public:
    void context(std::uint64_t world,std::uint64_t player) {if(world_!=world || player_!=player) {world_=world;player_=player;values_={};}}
    void offer(std::uint64_t actor,float distance_squared,bool controller,std::uint32_t now) {
        if(!actor || actor==player_ || !std::isfinite(distance_squared) || distance_squared<0 || distance_squared>144) return;
        for(unsigned i=0;i<values_.count;) {
            if(std::uint32_t(now-values_.entries[i].seen_ms)>5000) values_.entries[i]=values_.entries[--values_.count];else ++i;
        }
        Entry next{actor,distance_squared,now,controller};
        for(unsigned i=0;i<values_.count;++i) if(values_.entries[i].actor==actor) {values_.entries[i]=next;return;}
        if(values_.count<kCapacity) {values_.entries[values_.count++]=next;return;}
        unsigned worst=0;for(unsigned i=1;i<values_.count;++i) if(nearer(values_.entries[worst],values_.entries[i])) worst=i;
        if(nearer(next,values_.entries[worst])) values_.entries[worst]=next;
    }
    Snapshot snapshot(std::uint64_t world,std::uint64_t player,std::uint32_t now) const {
        Snapshot result{};if(world!=world_ || player!=player_) return result;
        for(unsigned i=0;i<values_.count;++i) if(std::uint32_t(now-values_.entries[i].seen_ms)<=5000) result.entries[result.count++]=values_.entries[i];
        std::sort(result.entries.begin(),result.entries.begin()+result.count,nearer);return result;
    }
};
}
