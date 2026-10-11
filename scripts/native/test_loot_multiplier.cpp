#include "../../src/native/loot_multiplier.h"
#include <cassert>
#include <cstdio>
using namespace loot_multiplier;
static unsigned char currency[0x118]{},world[0x60]{},fact[0x40]{},definitions[0x120]{},column[0x20]{},pickup[0x10]{};
static uintptr_t db[4]{1,1,1,31},entity[3]{},collection[4]{};
static unsigned received=0;
static void* find_currency(void*,void* out,const void*,uint64_t){auto* result=static_cast<LookupResult*>(out);*result={0,reinterpret_cast<uintptr_t>(currency)};return out;}
static void* find_fact(void*,void* out,const void*,uint64_t){auto* result=static_cast<LookupResult*>(out);*result={0,reinterpret_cast<uintptr_t>(fact)};return out;}
static void init_original(void*,void*,void*,void*){SetLastError(42);}
static void give_original(void* item,void*,void*,void*,void*,void*,void*,void*,void*){received=static_cast<uint32_t*>(item)[1];SetLastError(43);}
static void collect_original(void*,void* reward,void* w,void* d,void* a,void* b,void* c,void* e,void* f){award(reinterpret_cast<void*>(collection[1]),reward,w,d,a,b,c,e,f);}
template<class T>static void put(void* buffer,size_t offset,T value){std::memcpy(static_cast<unsigned char*>(buffer)+offset,&value,sizeof value);}
int main(){
 enabled.store(true);currency_lookup=find_currency;fact_lookup=find_fact;original_init=init_original;original_award=give_original;original_collector=collect_original;
 entity[0]=reinterpret_cast<uintptr_t>(column);put(column,8,reinterpret_cast<uintptr_t>(definitions));put(column,0x10,uint32_t{1});
 put(definitions,8,uint32_t{42});put(definitions,0x2c,uint32_t{0x7f000002});std::strcpy(reinterpret_cast<char*>(definitions+0x34),"CURRENCY_CASH");
 put(currency,8,uint64_t{0});put(currency,0x10,uint64_t{123});put(currency,0x1c,int{100});put(world,0x18,uintptr_t{1});put(world,0x30,uintptr_t{31});put(fact,0x28,double{10});fact[0x30]=1;
 initialize(entity,db,world,nullptr);assert(entry_count==1&&entries[0].id==42&&GetLastError()==42);
 uint32_t item[3]{42,7,0};collection[0]=reinterpret_cast<uintptr_t>(pickup);collection[1]=reinterpret_cast<uintptr_t>(item);pickup[0xc]=1;
 collector(collection,nullptr,world,db,nullptr,nullptr,nullptr,nullptr,nullptr);assert(received==21&&item[1]==7&&current==nullptr&&GetLastError()==43);
 award(item,nullptr,world,db,nullptr,nullptr,nullptr,nullptr,nullptr);assert(received==7); // Quest/replay/shop have no live collection loan.
 item[0]=99;collector(collection,nullptr,world,db,nullptr,nullptr,nullptr,nullptr,nullptr);assert(received==7);
 item[0]=42;item[2]=1;collector(collection,nullptr,world,db,nullptr,nullptr,nullptr,nullptr,nullptr);assert(received==7);
 item[2]=0;mod_bindings::settings.loot_multiplier=1;collector(collection,nullptr,world,db,nullptr,nullptr,nullptr,nullptr,nullptr);assert(received==7);
 mod_bindings::settings.loot_multiplier=10;put(fact,0x28,double{95});collector(collection,nullptr,world,db,nullptr,nullptr,nullptr,nullptr,nullptr);assert(received==5);
 assert(!loot_policy::eligible_name("Currency_Talent_Points"));puts("PASS: live collection loan, unchanged source record, quest/replay exclusion, unknown/type exclusion,1x, cap and LastError");
}
