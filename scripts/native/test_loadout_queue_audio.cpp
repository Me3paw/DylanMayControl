#include "../../src/native/loadout_queue_audio.h"
#include <cassert>
#include <cstdio>
namespace audio=loadout_queue_audio;
static unsigned plays=0,stops=0,unloads=0;
static bool alive=true;
static unsigned loads=0;
static bool initialized() { SetLastError(51);return alive; }
static std::uint32_t post(std::uint32_t event,std::uint64_t object,std::uint32_t flags,
        void* callback,void* cookie,std::uint32_t count,void* external,std::uint32_t playing) {
    assert(event==audio::events[plays==0?0:5] && object==audio::global_object);
    assert(!flags && !callback && !cookie && !count && !external && !playing);
    ++plays;SetLastError(52);return 77;
}
static int execute(std::uint32_t event,int action,std::uint64_t object,int duration,int curve,std::uint32_t playing) {
    assert(event==audio::events[stops] && action==0 && object==audio::global_object);
    assert(!duration && curve==4 && !playing);++stops;SetLastError(53);return 1;
}
static int unload(std::uint32_t bank,const void* data,std::uint32_t type) {
    assert(bank==audio::bank_id && !data && !type);++unloads;SetLastError(54);return 1;
}
extern "C" bool test_audio_initialized() {return initialized();}
extern "C" int test_audio_load(const void* data,std::uint32_t size,std::uint32_t* bank) {
    assert(data && size>1000000 && bank && std::memcmp(data,"BKHD",4)==0);
    ++loads;SetLastError(55);
    *bank=loads==1?0:audio::bank_id;return loads==1?92:1;
}
extern "C" std::uint32_t test_audio_post(std::uint32_t e,std::uint64_t o,std::uint32_t f,
        void* cb,void* cookie,std::uint32_t count,void* external,std::uint32_t p) {
    return post(e,o,f,cb,cookie,count,external,p);
}
extern "C" int test_audio_execute(std::uint32_t e,int a,std::uint64_t o,int t,int c,std::uint32_t p) {
    return execute(e,a,o,t,c,p);
}
extern "C" int test_audio_unload(std::uint32_t b,const void* p,std::uint32_t t) {return unload(b,p,t);}
int main() {
    std::atomic<bool> stopping{false};
    audio::shared_stopping=&stopping;audio::initialized=&initialized;audio::post=&post;
    audio::execute=&execute;audio::unload=&unload;
    SetLastError(50);audio::play(0);assert(plays==0 && GetLastError()==50);
    audio::ready.store(true);audio::loaded=true;
    audio::play(0);assert(plays==1 && GetLastError()==50 && audio::last_playing_id.load()==77);
    audio::play(5);assert(plays==2 && GetLastError()==50);
    audio::play(6);assert(plays==2 && GetLastError()==50);
    stopping.store(true);audio::play(0);assert(plays==2 && GetLastError()==50);
    stopping.store(false);alive=false;audio::play(0);assert(plays==2 && GetLastError()==50);
    alive=true;audio::stop();assert(!audio::ready.load() && !audio::loaded);
    assert(stops==6 && unloads==1 && GetLastError()==50);
    audio::play(0);audio::stop();assert(plays==2 && stops==6 && unloads==1 && GetLastError()==50);
    audio::attempted=false;
    audio::prepare(GetModuleHandleW(nullptr),stopping);
    assert(loads==1 && !audio::ready.load() && !audio::attempted && GetLastError()==50);
    audio::prepare(GetModuleHandleW(nullptr),stopping);assert(loads==1 && GetLastError()==50);
    audio::next_attempt=0;
    audio::prepare(GetModuleHandleW(nullptr),stopping);
    assert(loads==2 && audio::loaded && audio::ready.load() && GetLastError()==50);
    audio::prepare(GetModuleHandleW(nullptr),stopping);assert(loads==2);
    stops=0;audio::stop();assert(stops==6 && unloads==2 && GetLastError()==50);
    puts("PASS: native audio ABI test doubles, fail-closed playback, error preservation, own-event stop/unload");
    puts("PASS: actual PE resource and exported API resolution, delayed Init.bnk retry, one successful bank load");
}
