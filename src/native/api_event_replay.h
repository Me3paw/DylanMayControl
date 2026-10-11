#pragma once
// Synthetic native events exercise production shims; this is not a frame-time benchmark.
namespace api_event_replay {
template<class T> inline void put(void* p,size_t offset,T value) {
    std::memcpy(static_cast<unsigned char*>(p)+offset,&value,sizeof(value));
}
inline void guarded_node_pages() {
    SYSTEM_INFO info{};GetSystemInfo(&info);const size_t page=info.dwPageSize;
    for(int denied=-1;denied<3;++denied) {
        unsigned char* blocks[3]{};
        for(auto& block:blocks) {block=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(block);}
        auto* graph=blocks[0]+page-12+(denied==0?4:0);
        auto* tree=blocks[1]+page-20+(denied==1?4:0);
        auto* node=blocks[2]+page-0x1f6+(denied==2?2:0);
        put(graph,0,reinterpret_cast<uintptr_t>(tree));put(graph,8,uint32_t{1});
        put(tree,0,loadout_current_node::kMeleeTree);put(tree,8,reinterpret_cast<uintptr_t>(node));put(tree,16,uint32_t{1});
        put(node,0x1e8,uint32_t{42});put(node,0x1ec,uint32_t{0});put(node,0x1f0,uint32_t{0});put(node,0x1f4,uint16_t{0x28});
        for(auto* block:blocks) {DWORD old=0;assert(VirtualProtect(block+page,page,PAGE_NOACCESS,&old));}
        loadout_current_node::Facts facts{};unsigned reads=0;
        const bool ok=loadout_current_node::inspect(reinterpret_cast<uintptr_t>(graph),reinterpret_cast<uintptr_t>(tree),
            reinterpret_cast<uintptr_t>(node),42,facts,[&](uintptr_t p,void* out,size_t n) {
                ++reads;return loadout_attack_probe::copy(p,out,n);
            });
        if(denied<0)assert(ok && reads==4 && facts.current_pointer_equal && facts.raw_flags==0x28);
        else assert(!ok && !facts.current_pointer_equal && facts.node_key==0 && facts.raw_flags==0);
        for(auto* block:blocks)assert(VirtualFree(block,0,MEM_RELEASE));
    }
    puts("PASS: real guarded node reads accept readable page edges and reject partial graph/tree/node spans at PAGE_NOACCESS");
}
struct Fixture {
    std::array<unsigned char,0x59000> world{};
    std::array<unsigned char,0x5000> chunk{};
    uint64_t generations[3]{0,1,1},locations[3]{0,0,uint64_t{1}<<32},archetypes[2]{0,1};
    uint32_t tags[1]{step_binding::kTagPlayer};
    uint32_t keys[14]{0x692a9998,0xfaf95524,0x760edfc8,0x3bfcf661,0x9988f341,0x546690f3,0x7703cd01,
        held_lock_native::kInput,held_lock_native::kLock,facing_lock_native::kSelected,facing_lock_native::kTransform,
        facing_lock_native::kPlane,0xf2358b43,0x4057d9a2};
    uint32_t offsets[14]{};
    uint64_t header[2]{},producer_columns[5]{},turn_columns[12]{};
    std::array<unsigned char,0x20> map_node{};
    std::array<unsigned char,0x140> native_input{};
    std::array<unsigned char,0x60> descriptor{};
    std::atomic<bool> stopped{false};
    uint16_t action=123;
    bool down=false,pressed=false,expect_changed=false;
    unsigned forwards=0,mapped=0,predicates=0,reads=0,mutation=0;
    uint64_t now=1000;
    float command[4]{};
    static constexpr uint64_t player=0x100000001ULL,target=0x100000002ULL,graph=0x12340000;
    uint64_t w() const {return reinterpret_cast<uintptr_t>(world.data());}
    unsigned char* column(unsigned i) {return chunk.data()+offsets[i];}
};
inline Fixture fixture{};
inline ULONGLONG clock() {return fixture.now;}
inline uint8_t predicate(void* descriptor,uint16_t key,void* args) {
    auto& f=fixture;assert(GetLastError()==81 && key==f.action && args);
    const auto* bytes=static_cast<const unsigned char*>(descriptor);assert(bytes[0x44]==5);
    ++f.predicates;SetLastError(82);return bytes[0x54]==0?f.down:f.pressed;
}
inline uint64_t mapped(void* bank,uint16_t key) {
    auto& f=fixture;assert(GetLastError()==81 && bank==f.native_input.data()+0x138 && key==f.action);
    ++f.mapped;uint64_t args=reinterpret_cast<uintptr_t>(bank);
    const auto result=held_lock_native::predicate_hook(f.descriptor.data(),key,&args);
    assert(GetLastError()==82);SetLastError(83);return result;
}
inline void producer(void* columns,void*,void* input) {
    auto& f=fixture;assert(GetLastError()==71 && columns==f.producer_columns && input==f.native_input.data());
    SetLastError(81);const auto edge=held_lock_native::mapped_hook(f.native_input.data()+0x138,f.action);
    assert(GetLastError()==83 && edge==f.pressed && f.descriptor[0x54]==2);
    f.map_node[0x1c]=f.down?(f.pressed?1:2):0;SetLastError(72);
}
inline void original_turn(void* columns,void* b,void* c,void* d,void* e) {
    auto& f=fixture;assert(GetLastError()==71 && !b && !c && !d && !e);++f.forwards;
    const auto* q=static_cast<const uint64_t*>(columns);
    assert((columns!=f.turn_columns)==f.expect_changed && q[0]==f.turn_columns[0] && q[1]==f.turn_columns[1] && q[11]==0);
    assert(loadout_attack_probe::copy(q[2],f.command,sizeof(f.command)));
    if(f.expect_changed)assert(f.command[0]==0 && f.command[1]==0 && f.command[2]==1);
    else assert(q[2]==f.turn_columns[2]);
    SetLastError(72);
}
inline void mutate_during_resolve() {
    auto& f=fixture;++f.reads;
    if(f.reads!=15)return;
    if(f.mutation==1)f.generations[1]=2;
    if(f.mutation==2)f.locations[1]=uint64_t{1}<<32;
    if(f.mutation==3)put(f.world.data(),0x18460,uint32_t{0});
}
inline void setup() {
    auto& f=fixture;
    constexpr unsigned strides[14]{0x10,0xa0,8,0x48,0x810,0xb0,0xc,0x40,0xf0,0x10,0x20,0x10,0x80,0xb0};
    uint32_t cursor=0x40;
    for(unsigned i=0;i<14;++i){f.offsets[i]=cursor;cursor+=(strides[i]*2+15)&~15u;}
    assert(cursor<f.chunk.size());
    put(f.world.data(),0x58510,uint64_t{3});put(f.world.data(),0x584e8,reinterpret_cast<uintptr_t>(f.generations));
    put(f.world.data(),0x58530,reinterpret_cast<uintptr_t>(f.locations));put(f.world.data(),0x58478,reinterpret_cast<uintptr_t>(f.archetypes));
    put(f.world.data(),0x50,reinterpret_cast<uintptr_t>(f.chunk.data()));put(f.world.data(),0x10448,uint32_t{2});
    put(f.world.data(),0x18448,reinterpret_cast<uintptr_t>(f.tags));put(f.world.data(),0x18450,reinterpret_cast<uintptr_t>(f.keys));
    put(f.world.data(),0x18458,reinterpret_cast<uintptr_t>(f.offsets));put(f.world.data(),0x18460,uint32_t{1});put(f.world.data(),0x18464,uint32_t{14});
    put(f.chunk.data(),0x10,Fixture::player);put(f.chunk.data(),0x18,Fixture::target);
    f.header[1]=reinterpret_cast<uintptr_t>(f.map_node.data());put(f.column(7),0,reinterpret_cast<uintptr_t>(f.header));
    put(f.map_node.data(),0x1a,f.action);f.column(8)[0]=1;put(f.column(9),0,Fixture::target);
    put(f.column(10),12,1.f);put(f.column(10)+0x20,12,1.f);put(f.column(10)+0x20,24,10.f);
    put(f.column(11),12,1.f);put(f.column(12),8,-1.f);put(f.column(12),0x24,1.f);
    put(f.column(3),0x20,uint32_t{76});f.column(4)[0x1c]=4;f.column(5)[0xa0]=1;
    f.producer_columns[0]=reinterpret_cast<uintptr_t>(f.column(7));f.producer_columns[3]=reinterpret_cast<uintptr_t>(f.chunk.data());
    f.turn_columns[0]=reinterpret_cast<uintptr_t>(f.column(13));f.turn_columns[1]=reinterpret_cast<uintptr_t>(f.column(5));f.turn_columns[2]=reinterpret_cast<uintptr_t>(f.column(12));
    f.descriptor[0x44]=5;f.descriptor[0x54]=2;
    step_binding::env().copy=&gameplay_runtime::copy;
    loadout_player_catalogue::shared_stopping=&f.stopped;loadout_player_catalogue::enabled.store(true);
    loadout_player_catalogue::observed_player_actor.store(Fixture::player);
    auto& pub=loadout_player_catalogue::published;pub={};pub.view_generation=77;pub.catalogue.valid=true;
    pub.catalogue.world=f.w();pub.catalogue.actor=Fixture::player;pub.catalogue.graph_key=Fixture::graph;native_ui::sdk::generation.store(77);
    held_lock_native::stopping=&f.stopped;held_lock_native::enabled.store(true);held_lock_native::stopped.store(false);
    held_lock_native::action_address=reinterpret_cast<uintptr_t>(&f.action);
    held_lock_native::originals[3]=reinterpret_cast<void*>(&producer);held_lock_native::originals[4]=reinterpret_cast<void*>(&mapped);
    held_lock_native::originals[5]=reinterpret_cast<void*>(&predicate);
    facing_lock_native::stopping=&f.stopped;facing_lock_native::enabled.store(true);
    attack_turn_native::stopping=&f.stopped;attack_turn_native::enabled.store(true);
    attack_turn_native::original_update=reinterpret_cast<void*>(&original_turn);attack_turn_native::update_clock_test=&clock;
}
inline void input(bool down,bool pressed) {
    auto& f=fixture;f.down=down;f.pressed=pressed;const auto before=f.mapped;
    SetLastError(71);held_lock_native::producer_hook(f.producer_columns,nullptr,f.native_input.data());
    assert(GetLastError()==72 && f.mapped==before+2 && held_lock_native::proof_valid.load());
    bool held=!down;assert(held_lock_native::held(f.w(),Fixture::player,reinterpret_cast<uintptr_t>(f.column(7)),held) && held==down);
}
inline void queue(bool sustained=true) {
    auto& f=fixture;auto& r=attack_turn_native::request;r={};
    r.world=f.w();r.actor=Fixture::player;r.graph=Fixture::graph;r.target=Fixture::target;r.node=66;
    r.reversed={0,0,1};r.pending=true;r.started_ms=static_cast<uint32_t>(f.now);r.ticket=++attack_turn_native::next_ticket;
    r.lifetime.sustained=sustained;
}
inline void frame(uint32_t node,bool window,bool changed) {
    auto& f=fixture;put(f.column(3),0x20,node);f.column(4)[0x1c]=window?4:0;f.column(5)[0xa0]=window?1:0;
    f.expect_changed=changed;unsigned char movement[0x80]{};std::memcpy(movement,f.column(12),sizeof movement);
    const auto before=f.forwards;SetLastError(71);
    attack_turn_native::update_hook(f.turn_columns,nullptr,nullptr,nullptr,nullptr);
    assert(GetLastError()==72 && f.forwards==before+1 && std::memcmp(movement,f.column(12),sizeof movement)==0);
}
inline void run() {
    assert(!diagnostics::enabled);guarded_node_pages();setup();auto& f=fixture;
    frame(76,false,false);input(true,true);queue();frame(76,false,false);
    assert(attack_turn_native::request.pending && !attack_turn_native::request.lifetime.started);
    f.now+=38;frame(66,true,true);assert(attack_turn_native::request.lifetime.started);
    input(true,false);f.now+=16;frame(66,false,true);f.now+=600;frame(66,true,true);
    f.now+=5000;frame(66,false,true); // Charge lifetime follows the node, not elapsed time or an animation window.
    frame(67,true,false);assert(!attack_turn_native::request.pending);frame(66,true,false);
    queue(false);frame(66,false,false);assert(attack_turn_native::request.pending);
    frame(66,true,true);assert(!attack_turn_native::request.pending);frame(66,true,false);
    queue();f.now+=251;frame(76,false,false);assert(!attack_turn_native::request.pending);
    queue();input(false,false);frame(66,true,false);assert(!attack_turn_native::request.pending);
    input(true,true);queue();frame(66,true,true);put(f.column(9),0,Fixture::player);
    frame(66,true,false);assert(!attack_turn_native::request.pending);put(f.column(9),0,Fixture::target);
    queue();native_ui::sdk::generation.store(78);frame(66,true,false);assert(!attack_turn_native::request.pending);
    native_ui::sdk::generation.store(77);
    for(unsigned mutation=1;mutation<=3;++mutation) {
        queue();f.reads=0;f.mutation=mutation;loadout_attack_probe::copy_test_barrier=&mutate_during_resolve;
        frame(66,true,false);loadout_attack_probe::copy_test_barrier=nullptr;
        assert(f.reads>=15 && !attack_turn_native::request.pending);
        f.generations[1]=1;f.locations[1]=0;put(f.world.data(),0x18460,uint32_t{1});
    }
    for(uint32_t tag:{step_binding::kTagDead,uint32_t{0}}) {
        queue();f.tags[0]=tag;frame(66,true,false);assert(!attack_turn_native::request.pending);f.tags[0]=step_binding::kTagPlayer;
    }
    queue();f.stopped.store(true);frame(66,true,false);assert(attack_turn_native::request.pending);
    attack_turn_native::stop();assert(!attack_turn_native::request.pending);
    f.stopped.store(false);attack_turn_native::enabled.store(true);input(true,true);queue();frame(66,true,true);
    assert(attack_turn_native::direction_count==0 && attack_turn_native::queued.load()==0 && attack_turn_native::applied.load()==0);
    puts("PASS: production input/mapping/attack event replay: hold/release, charge windows/cancel, one-shot, stale generation, relocated/dead/missing Player, retirement/resume, native forwarding and LastError");
    puts("Replay starts at the chooser's queued Request boundary; renderer, real native callbacks and frame-time improvement remain untested.");
}
}
