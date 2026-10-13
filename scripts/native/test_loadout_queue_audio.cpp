#include "../../src/native/loadout_queue_audio.h"
#include <cassert>
#include <cstdio>
#include <cstring>
namespace audio=loadout_queue_audio;
static unsigned plays=0,stops=0,unloads=0;
static bool alive=true;
static unsigned loads=0;
[[maybe_unused]] static unsigned position_queries=0,event_queries=0;
static bool self_check_posts=false;
extern "C" int test_audio_position(std::uint32_t playing,std::int32_t* position,bool extrapolate) {
    assert(playing==77 && position && !extrapolate);
    *position=static_cast<std::int32_t>(position_queries++/16)*30;SetLastError(56);return 1;
}
extern "C" std::uint32_t test_audio_playing_event(std::uint32_t playing) {
    assert(playing==77);++event_queries;SetLastError(57);return audio::events[5];
}
static constexpr unsigned expected_indices[]={0,1,2,0,1,2,0,1,2,3,4,5,3,4,5,3,4,5};
static bool initialized() { SetLastError(51);return alive; }
static std::uint32_t post(std::uint32_t event,std::uint64_t object,std::uint32_t flags,
        void* callback,void* cookie,std::uint32_t count,void* external,std::uint32_t playing) {
    assert(plays<18 && event==audio::events[self_check_posts?plays:expected_indices[plays]] && object==audio::global_object);
    const auto expected_flags=CRMOD_AUDIO_TRACE && audio::source_position && loadout_audio_trace::file && !loadout_audio_trace::limited ? audio::source_position_flag : 0;
    assert(flags==expected_flags && !callback && !cookie && !count && !external && !playing);
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
    const auto* bytes=static_cast<const unsigned char*>(data);
    const auto read=[&](unsigned offset){assert(offset<=size-4);std::uint32_t n=0;std::memcpy(&n,bytes+offset,4);return n;};
    unsigned pos=0,sounds=0;std::uint32_t caches[6]{};
    unsigned index_offset=0,index_size=0,media_offset=0,media_size=0;
    while(pos<size) {
        const auto length=read(pos+4);assert(length<=size-pos-8);
        if(std::memcmp(bytes+pos,"DIDX",4)==0){index_offset=pos+8;index_size=length;}
        if(std::memcmp(bytes+pos,"DATA",4)==0){media_offset=pos+8;media_size=length;}
        if(std::memcmp(bytes+pos,"HIRC",4)==0) {
            const auto count=read(pos+8);unsigned cursor=pos+12;
            for(unsigned i=0;i<count;++i) {
                const auto kind=bytes[cursor];const auto length=read(cursor+1);cursor+=5;assert(length<=size-cursor);
                if(kind==2) {
                    assert(sounds<6 && length>=22);
                    const auto cache=read(cursor+13);assert(cache);
                    for(unsigned old=0;old<sounds;++old)assert(caches[old]!=cache);
                    caches[sounds++]=cache;
                }
                cursor+=length;
            }
            assert(cursor==pos+8+length);
        }
        pos+=8+length;
    }
    assert(pos==size && sounds==6);
    assert(index_size==6*12 && media_size && media_offset);
    for(unsigned i=0;i<6;++i) {
        const auto offset=read(index_offset+i*12+4),length=read(index_offset+i*12+8);
        assert(offset<=media_size && length<=media_size-offset && length>52);
        const auto base=media_offset+offset;
        assert(std::memcmp(bytes+base,"RIFF",4)==0 && read(base+4)==length-8);
        assert(std::memcmp(bytes+base+8,"WAVEfmt ",8)==0 && read(base+16)==24);
        assert(bytes[base+20]==0xfe && bytes[base+21]==0xff);
        const unsigned channels=bytes[base+22];assert(channels==1 || channels==2);
        assert(read(base+40)==(channels==1?0x4101u:0x3102u));
        assert(std::memcmp(bytes+base+44,"data",4)==0 && read(base+48)==length-52);
    }
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
#if CRMOD_AUDIO_TRACE
    SetLastError(50);loadout_audio_trace::start();
    assert(loadout_audio_trace::file && GetLastError()==50);
#endif
    std::atomic<bool> stopping{false};
    audio::shared_stopping=&stopping;audio::initialized=&initialized;audio::post=&post;
    audio::execute=&execute;audio::unload=&unload;
    audio::source_position=&test_audio_position;audio::playing_event=&test_audio_playing_event;
    SetLastError(50);audio::play(0);assert(plays==0 && GetLastError()==50);
    audio::ready.store(true);audio::loaded=true;
    for(unsigned i=0;i<18;++i) {audio::play(expected_indices[i]);assert(plays==i+1 && GetLastError()==50 && audio::last_playing_id.load()==77);}
#if CRMOD_AUDIO_TRACE
    loadout_audio_trace::flush();
    constexpr unsigned probe_ages[]={0,30,150,400,800,1800};
    for(unsigned stage=0;stage<6;++stage) {
        for(auto& p:audio::probes)if(p.playing)p.born=GetTickCount64()-probe_ages[stage];
        audio::probe_progress();loadout_audio_trace::flush();assert(GetLastError()==50);
    }
    assert(position_queries==96 && event_queries==96);
    for(const auto& p:audio::probes)assert(!p.playing);
    audio::source_position=nullptr;audio::playing_event=nullptr;
    audio::probes[0]={77,0,0,GetTickCount64(),false};
    audio::probe_progress();loadout_audio_trace::flush();
    assert(position_queries==96 && event_queries==96 && GetLastError()==50);audio::probes={};
#endif
    audio::play(6);assert(plays==18 && GetLastError()==50);
    stopping.store(true);audio::play(0);assert(plays==18 && GetLastError()==50);
    stopping.store(false);alive=false;audio::play(0);assert(plays==18 && GetLastError()==50);
    alive=true;audio::stop();assert(!audio::ready.load() && !audio::loaded);
    assert(stops==6 && unloads==1 && GetLastError()==50);
    audio::play(0);audio::stop();assert(plays==18 && stops==6 && unloads==1 && GetLastError()==50);
    audio::attempted=false;
    audio::prepare(GetModuleHandleW(nullptr),stopping);
    assert(loads==1 && !audio::ready.load() && !audio::attempted && GetLastError()==50);
    audio::prepare(GetModuleHandleW(nullptr),stopping);assert(loads==1 && GetLastError()==50);
    audio::next_attempt=0;
    audio::prepare(GetModuleHandleW(nullptr),stopping);
    assert(loads==2 && audio::loaded && audio::ready.load() && GetLastError()==50);
#if CRMOD_AUDIO_TRACE
    assert(audio::source_position && audio::playing_event);
#endif
    audio::prepare(GetModuleHandleW(nullptr),stopping);assert(loads==2);
    stops=0;audio::stop();assert(stops==6 && unloads==2 && GetLastError()==50);
#if CRMOD_AUDIO_TRACE
    SetEnvironmentVariableW(L"CRMOD_AUDIO_SELF_CHECK",nullptr);audio::configure_self_check();
    assert(!audio::self_check_requested);audio::run_self_check();assert(plays==18);
    SetEnvironmentVariableW(L"CRMOD_AUDIO_SELF_CHECK",L"1");audio::configure_self_check();
    assert(audio::self_check_requested);audio::run_self_check();assert(plays==18 && !audio::self_check_done);
    audio::ready.store(true);self_check_posts=true;plays=0;SetLastError(50);
    audio::run_self_check();assert(plays==6 && audio::self_check_done && GetLastError()==50);
    audio::run_self_check();assert(plays==6 && GetLastError()==50);
    audio::ready.store(false);SetEnvironmentVariableW(L"CRMOD_AUDIO_SELF_CHECK",nullptr);audio::configure_self_check();SetLastError(50);
    puts("PASS: audio-only self-check defaults off, waits for ready, posts each of six cues once");
    loadout_audio_trace::flush();assert(GetLastError()==50);
    for(unsigned i=0;i<67;++i)loadout_audio_trace::row("trace_test",",\"number\":%u",i);
    assert(loadout_audio_trace::count==64 && loadout_audio_trace::dropped==3);
    loadout_audio_trace::flush();assert(GetLastError()==50);
    loadout_audio_trace::records=8192;
    loadout_audio_trace::row("must_be_limited","");
    assert(loadout_audio_trace::limited && loadout_audio_trace::count==0);
    loadout_audio_trace::flush();assert(!loadout_audio_trace::file && GetLastError()==50);
    loadout_audio_trace::file=std::tmpfile();assert(loadout_audio_trace::file);
    SetLastError(50);
    loadout_audio_trace::limited=false;loadout_audio_trace::records=0;loadout_audio_trace::deadline=0;
    loadout_audio_trace::row("must_expire","");assert(loadout_audio_trace::limited);
    loadout_audio_trace::stop();assert(!loadout_audio_trace::file && GetLastError()==50);
    puts("PASS: focused trace file, buffer/record/time bounds and LastError preservation");
#endif
    puts("PASS: native audio ABI test doubles, fail-closed playback, error preservation, own-event stop/unload");
    puts("PASS: actual PE resource and exported API resolution, delayed Init.bnk retry, one successful bank load");
    puts("PASS: all six cues across three complete primary/secondary rotations; distinct nonzero embedded cache IDs");
}
