#pragma once
#include <cstdint>
#include <cstring>
#include <limits>
namespace loot_policy {
inline bool eligible_name(const char* name) {
    char key[128]{};unsigned n=0;
    for(unsigned i=0;name[i] && i<127;i++) {
        unsigned char c=static_cast<unsigned char>(name[i]);
        if(c>='A' && c<='Z')c+=32;
        if((c>='a' && c<='z') || (c>='0' && c<='9'))key[n++]=static_cast<char>(c);
    }
    const char* value=key;
    if(!std::strncmp(value,"currencies",10))value+=10;
    else if(!std::strncmp(value,"currency",8))value+=8;
    constexpr const char* names[]={"cash","source","livingshard","livingshards",
        "aberrantsliver","aberrantslivers","aberrantsplinter","aberrantsplinters",
        "aberrantfragment","aberrantfragments","aberrantshard","aberrantshards",
        "aberrantcuts","aberrantremnant","aberrantremnants","orderedimage","patternedthought",
        "corruptedelement","distortedform","warpedresonance","undefinedreading","edibleglob","fungalgrowth","thrivingspore"};
    for(auto allowed:names)if(!std::strcmp(value,allowed))return true;
    return false;
}
inline int scaled_amount(int amount,unsigned multiplier,int balance,int cap) {
    if(amount<=0 || multiplier<1 || multiplier>10 || balance<0 || cap<balance)return amount;
    const auto scaled=static_cast<std::int64_t>(amount)*multiplier;
    const auto room=static_cast<std::int64_t>(cap)-balance;
    return static_cast<int>(scaled<room?scaled:room);
}
}
