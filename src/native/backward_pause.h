#pragma once
#include <cstdint>
namespace backward_pause {
constexpr std::uint32_t kMilliseconds=50;
class Window {
    std::uint64_t world_=0,actor_=0,target_=0;
    std::uint32_t started_=0;
    bool backward_=false;
public:
    bool update(bool locked,bool backward,std::uint32_t now,std::uint64_t world,std::uint64_t actor,std::uint64_t target,std::uint32_t duration=kMilliseconds) {
        if(!locked) {backward_=false;world_=actor_=target_=0;return false;}
        if(world_!=world || actor_!=actor || target_!=target) {backward_=false;world_=world;actor_=actor;target_=target;}
        if(!backward) {backward_=false;return false;}
        if(!backward_) {backward_=true;started_=now;}
        return std::uint32_t(now-started_)<duration;
    }
};
}
