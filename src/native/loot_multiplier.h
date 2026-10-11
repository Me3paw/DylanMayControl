#pragma once
#include "loot_policy.h"
#include "mod_bindings.h"
#include "loadout_attack_probe.h"
#include <cmath>
namespace loot_multiplier {
using Init=void(*)(void*,void*,void*,void*);
using Collect=void(*)(void*,void*,void*,void*,void*,void*,void*,void*,void*);
using Lookup=void*(*)(void*,void*,const void*,std::uint64_t);
inline Init original_init=nullptr;
inline Collect original_collector=nullptr,original_award=nullptr;
inline Lookup currency_lookup=nullptr,fact_lookup=nullptr;
inline std::atomic<bool> enabled{false};
inline std::uintptr_t base=0;
struct Entry {std::uint32_t id;std::uint64_t fact[2];};
inline SRWLOCK cache_lock=SRWLOCK_INIT;
inline Entry entries[128]{};inline unsigned entry_count=0;
inline void* cache_db=nullptr;inline void* cache_world=nullptr;
struct Scope {void* record;void* reward;void* world;void* db;std::uint32_t values[3];};
inline thread_local const Scope* current=nullptr;
struct LookupResult {std::uintptr_t cursor,entry;};
struct Log {unsigned kind,id,amount,scaled,multiplier;char name[128];};
inline SRWLOCK log_lock=SRWLOCK_INIT;inline Log logs[256]{};inline unsigned log_count=0;
inline std::atomic<unsigned> admissions{0};
inline bool read(std::uintptr_t address,void* out,std::size_t size){return loadout_attack_probe::copy(address,out,size);}
inline std::uint64_t hash(std::uint64_t value){auto p=static_cast<unsigned __int128>(0xde5fb9d2630458e9ull)*value;return static_cast<std::uint64_t>(p)+static_cast<std::uint64_t>(p>>64);}
inline void record(Log row){if constexpr(!diagnostics::enabled)return;if(admissions.fetch_add(1)>=512)return;if(TryAcquireSRWLockExclusive(&log_lock)){if(log_count<256)logs[log_count++]=row;ReleaseSRWLockExclusive(&log_lock);}}
inline bool lookup(void* db,std::uint32_t id,LookupResult& out){
    std::uintptr_t header[4]{};if(!read(reinterpret_cast<std::uintptr_t>(db),header,sizeof header) || !header[0] || header[3]>65535)return false;
    currency_lookup(db,&out,&id,hash(id));return out.cursor!=header[0]+header[3] && out.entry;
}
inline void initialize(void* columns,void* db,void* world,void* metadata){
    const DWORD incoming=GetLastError();Entry found[128]{};unsigned count=0;
    std::uintptr_t entity[3]{},row=0,list=0;std::uint32_t size=0;
    if(enabled.load() && read(reinterpret_cast<std::uintptr_t>(columns),entity,sizeof entity) && entity[2]<1048576 &&
        loadout_attack_probe::row_address(entity[0],entity[2],0x20,row) && read(row+8,&list,8) && read(row+0x10,&size,4) && size<=128){
        for(unsigned i=0;i<size;i++){
            unsigned char data[0x120]{};auto address=list+static_cast<std::uintptr_t>(i)*sizeof data;
            if(!read(address,data,sizeof data))break;
            std::uint32_t id=0,packed=0;std::memcpy(&id,data+8,4);std::memcpy(&packed,data+0x2c,4);
            unsigned capacity=packed>>24;capacity=capacity<128?capacity*8:capacity*256-0x7c00;
            std::uintptr_t name=address+0x34;if(capacity<(packed&0xffffffu)*8ull)std::memcpy(&name,data+0x34,8);
            char currency_name[128]{};
            bool terminated=false;
            for(unsigned j=0;j<127;j++){if(!read(name+j,currency_name+j,1))break;if(!currency_name[j]){terminated=true;break;}if(static_cast<unsigned char>(currency_name[j])<32 || static_cast<unsigned char>(currency_name[j])>126)break;}
            if(!terminated)continue;
            const bool eligible=loot_policy::eligible_name(currency_name);
            if(eligible)found[count++].id=id;
            if constexpr(diagnostics::enabled){Log log{};log.id=id;log.kind=eligible?1u:0u;std::memcpy(log.name,currency_name,sizeof currency_name);record(log);}
        }
    }
    SetLastError(incoming);original_init(columns,db,world,metadata);const DWORD forwarded=GetLastError();
    unsigned valid=0;
    for(unsigned i=0;i<count;i++){LookupResult result{};if(lookup(db,found[i].id,result) && read(result.entry+8,found[i].fact,16))found[valid++]=found[i];}
    AcquireSRWLockExclusive(&cache_lock);cache_db=db;cache_world=world;entry_count=valid;std::memcpy(entries,found,valid*sizeof(Entry));ReleaseSRWLockExclusive(&cache_lock);
    SetLastError(forwarded);
}
inline void collector(void* columns,void* reward,void* world,void* db,void* p5,void* p6,void* p7,void* p8,void* p9){
    const DWORD incoming=GetLastError();Scope scope{};std::uintptr_t entity[4]{};unsigned char collected=0;
    bool valid=enabled.load() && read(reinterpret_cast<std::uintptr_t>(columns),entity,sizeof entity) && entity[3]<1048576 &&
        read(entity[0]+entity[3]*0x10+0xc,&collected,1) && collected;
    if(valid){scope.record=reinterpret_cast<void*>(entity[1]+entity[3]*12);scope.reward=reward;scope.world=world;scope.db=db;valid=read(reinterpret_cast<std::uintptr_t>(scope.record),scope.values,12);}
    auto previous=current;current=valid?&scope:nullptr;SetLastError(incoming);
    original_collector(columns,reward,world,db,p5,p6,p7,p8,p9);
    const DWORD forwarded=GetLastError();current=previous;SetLastError(forwarded);
}
inline void award(void* item,void* reward,void* world,void* db,void* p5,void* p6,void* p7,void* p8,void* p9){
    const DWORD incoming=GetLastError();std::uint32_t values[3]{};unsigned multiplier=mod_bindings::snapshot().loot_multiplier;Entry match{};bool allowed=false;
    if(enabled.load() && multiplier>1 && current && item==current->record && reward==current->reward && world==current->world && db==current->db &&
        read(reinterpret_cast<std::uintptr_t>(item),values,12) && !std::memcmp(values,current->values,12) && values[2]==0 && values[1]>0 && values[1]<=INT_MAX){
        if(TryAcquireSRWLockShared(&cache_lock)){if(db==cache_db && world==cache_world)for(unsigned i=0;i<entry_count;i++)if(entries[i].id==values[0]){match=entries[i];allowed=true;break;}ReleaseSRWLockShared(&cache_lock);}
    }
    if(allowed){
        LookupResult currency{},fact{};std::uint64_t key[2]{};int cap=0;std::uintptr_t header[4]{};double balance=0;unsigned char tag=0;
        auto w=reinterpret_cast<std::uintptr_t>(world);
        allowed=lookup(db,values[0],currency) && read(currency.entry+8,key,16) && !std::memcmp(key,match.fact,16) && read(currency.entry+0x1c,&cap,4) && read(w+0x18,header,sizeof header) && header[0] && header[3]<=1048575;
        if(allowed){fact_lookup(reinterpret_cast<void*>(w+0x18),&fact,key,hash(key[0]^key[1]));allowed=fact.cursor!=header[0]+header[3] && fact.entry && read(fact.entry+0x28,&balance,8) && read(fact.entry+0x30,&tag,1) && tag==1 && std::isfinite(balance) && balance>=0 && balance<=INT_MAX;}
        if(allowed){auto scaled=loot_policy::scaled_amount(static_cast<int>(values[1]),multiplier,static_cast<int>(balance),cap);CRMOD_DIAGNOSTIC(Log row{};row.kind=2;row.id=values[0];row.amount=values[1];row.scaled=scaled;row.multiplier=multiplier;record(row));values[1]=static_cast<unsigned>(scaled);item=values;}
    }
    SetLastError(incoming);original_award(item,reward,world,db,p5,p6,p7,p8,p9);
}
inline bool prepare(std::uintptr_t executable){
    base=executable;currency_lookup=reinterpret_cast<Lookup>(base+0x20ec8b0);fact_lookup=reinterpret_cast<Lookup>(base+0x1dc4020);
    constexpr unsigned char init[]={0x4c,0x8b,0xdc,0x4d,0x89,0x4b,0x20};
    constexpr unsigned char collect[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
    constexpr unsigned char give[]={0x40,0x53,0x57,0x41,0x56,0x48,0x83,0xec,0x50,0x44,0x8b,0x51,0x08};
    constexpr unsigned char getter[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
    unsigned char actual[16]{};
    return read(base+0x2737b00,actual,sizeof init) && !std::memcmp(actual,init,sizeof init) && read(base+0x21cf140,actual,sizeof collect) && !std::memcmp(actual,collect,sizeof collect) && read(base+0x21cc390,actual,sizeof give) && !std::memcmp(actual,give,sizeof give) && read(base+0x20ec8b0,actual,sizeof getter) && !std::memcmp(actual,getter,sizeof getter) && read(base+0x1dc4020,actual,sizeof getter) && !std::memcmp(actual,getter,sizeof getter);
}
inline void flush(FILE* output){if(!output)return;AcquireSRWLockExclusive(&log_lock);for(unsigned i=0;i<log_count;i++){auto& r=logs[i];for(char& c:r.name)if(c=='"'||c=='\\')c='_';std::fprintf(output,"{\"event\":\"loot_multiplier\",\"kind\":%u,\"id\":%u,\"name\":\"%s\",\"base\":%u,\"scaled\":%u,\"multiplier\":%u}\n",r.kind,r.id,r.name,r.amount,r.scaled,r.multiplier);}log_count=0;ReleaseSRWLockExclusive(&log_lock);}
}
