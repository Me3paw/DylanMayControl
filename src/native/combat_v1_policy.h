#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace combat_v1 {
struct Scale { float damage=1, falter=1; };
struct Value { float amount; std::uint32_t type; };
static_assert(sizeof(Value)==8);
struct Tuning { std::uint32_t id; Scale scale; };
// Player DamageComponent IDs: 20 physical-hit joins plus Split's explosion definition.
inline constexpr Tuning tuning[] = {
    {0x007580d6,{1,.5f}}, {0xa473f6cf,{1,.5f}}, // Beatdown: native damage; 10/10/20 Falter per definition event
    {0xb57abbd8,{1.4f,1.4f}}, // Wide: 350 / 14 per complete single-hit use
    {0xfa6b8baa,{1.5f,.5f}}, // Spin: 150 / 7.5 per event; actual spin/event multiplicity is separate
    {0xa9c8a6d4,{0.65f,0.65f}}, {0x378b8f5c,{0.8f,0.8f}}, // normal / charged Uppercut
    {0x3f3e63c2,{0.5f,0.5f}}, {0x31f6e1fb,{0.5f,0.5f}}, // Split initial / explosion
    {0x7be954f4,{0.5f,0.5f}}, {0xe9a22849,{0.5f,0.5f}}, // Orbit / Pivot ticks
    {0x84174b9e,{1,2}}, {0x443188a2,{1,2}}, {0x1eb506ba,{1,2}}, // Drill heavy / held / heavy-held
    {0x3b167833,{1,2}}, {0xf744bd6b,{1,2}}, {0x00b16a63,{1,2}}, // upgraded held variants / switch
    {0x3c3f5658,{1,1}}, {0xb18ef961,{1,1}}, {0x8bd2a9d6,{1,1}}, // Extend native normal / perfect / final perfect
    {0x36e4d908,{1,1}}, {0xe902208a,{1,1}} // Extend native mid / switch; Crush remains native
};
inline bool base_scale(std::uint32_t id,Scale& out) {
    for (const auto& t:tuning) if (t.id==id) { out=t.scale;return true; }
    return false;
}
// Validate the whole request before changing any value. No resource, tag or potency fields change.
inline bool scale_values(Value* values,std::size_t count,Scale scale) {
    if (!values || !count || count>12 || !std::isfinite(scale.damage) || !std::isfinite(scale.falter) ||
        scale.damage<0 || scale.falter<0) return false;
    for (std::size_t i=0;i<count;++i) {
        const auto& v=values[i];
        if (v.type<2 || v.type>9 || !std::isfinite(v.amount) || v.amount<0 ||
            !std::isfinite(v.amount*(v.type==9?scale.falter:scale.damage))) return false;
    }
    for (std::size_t i=0;i<count;++i) values[i].amount*=values[i].type==9?scale.falter:scale.damage;
    return true;
}

// Runtime selections produce immutable tokens; never use a current counter for old hits.
enum class Category { Primary, Secondary, Drill, Finisher, NormalUppercut };
struct Selection { std::uint32_t move=0,node=0; Category category=Category::Primary; };
struct Token { Scale repeat{}; std::uint64_t serial=0; std::uint32_t node=UINT32_MAX; };
// Native automatic nodes and held input keep their original immutable use.
inline bool inherit_native_input(std::uint64_t serial,std::uint32_t previous_form,std::uint32_t form,
    std::uint16_t input,bool direct,std::uint32_t previous_epoch,std::uint32_t epoch) {
    return serial&&previous_form==form&&(!(input>>8)||(!direct&&previous_epoch==epoch));
}
// Native impulse waves merge requests before damage emission. The retained
// bytes belong to the winning wave; an unchanged row keeps its original token.
class WaveRows {
    struct Row { std::uintptr_t queue=0;std::uint32_t index=0;std::array<std::uint8_t,0x70> bytes{};Token token{}; };
    std::array<Row,128> rows_{};
public:
    void save(std::uintptr_t queue,std::uint32_t index,const std::array<std::uint8_t,0x70>& bytes,Token token) {
        Row* empty=nullptr;
        for(auto& row:rows_) {
            if(row.queue==queue&&row.index==index){row=token.serial?Row{queue,index,bytes,token}:Row{};return;}
            if(!row.queue&&!empty)empty=&row;
        }
        if(empty&&token.serial)*empty={queue,index,bytes,token};
    }
    Token find(std::uintptr_t queue,std::uint32_t index,const std::array<std::uint8_t,0x70>& bytes) const {
        for(const auto& row:rows_)if(row.queue==queue&&row.index==index&&row.bytes==bytes)return row.token;
        return {};
    }
    void clear(std::uintptr_t queue) {for(auto& row:rows_)if(row.queue==queue)row={};}
    bool has(std::uintptr_t queue) const {for(const auto& row:rows_)if(row.queue==queue&&row.token.serial)return true;return false;}
};
struct GroupToken { std::uint32_t generation=0; bool valid=false; Token token{}; };
class HitTokens {
    std::array<GroupToken,128> groups_{};
public:
    Token find(std::uint32_t generation) const {
        for (const auto& group:groups_) if (group.valid && group.generation==generation) return group.token;
        return {};
    }
    bool save(std::uint32_t generation,Token token,const std::uint32_t* live,std::size_t count) {
        GroupToken* slot=nullptr;
        for (auto& group:groups_) {
            if (group.generation==generation && group.valid) {slot=&group;break;}
            if (!group.valid && !slot) slot=&group;
        }
        // Old Split groups remain owned by their original input. Only retire
        // generations absent from the current native group storage.
        if (!slot && live) for (auto& group:groups_) {
            bool present=false;for (std::size_t i=0;i<count;++i) present|=live[i]==group.generation;
            if (!present) {slot=&group;break;}
        }
        if (!slot) return false;
        *slot={generation,token.serial!=0,token};return true;
    }
};
struct HitIdentity { std::uint64_t victim=0,origin[2]{};std::uint32_t id=0,generation=0; };
inline bool hit_generation(const HitIdentity* hits,std::size_t count,const HitIdentity& request,std::uint32_t& generation) {
    bool found=false;
    for (std::size_t i=0;i<count;++i) if (hits[i].victim==request.victim && hits[i].id==request.id &&
        !std::memcmp(hits[i].origin,request.origin,sizeof request.origin)) {
        if (found && generation!=hits[i].generation) return false;
        found=true;generation=hits[i].generation;
    }
    return found;
}
// Ground/air presentations of the same authored input stage share a counter;
// Step changes elevation, not the logical input selected by the player.
inline std::uint32_t logical_node(std::uint32_t node_id) {
    constexpr std::uint32_t pairs[][2]={
        {0xc19456d3,0xceb15b8c}, // drill_attack_ground_1
        {0xc2945866,0xd1b16045}, // drill_attack_ground_2
        {0xc39459f9,0xd0b15eb2}, // drill_attack_ground_3
        {0x66961948,0x2199567}, // drill_attack_switch
        {0x899f6bf3,0x9b1c21d0}, // extend_attack_switch
        {0x375de308,0x23518b75}, // extend_attack_ground_1
        {0x3a5de7c1,0x205186bc}, // extend_attack_ground_2
        {0x395de62e,0x2151884f}, // extend_attack_ground_3
        {0x539946ca,0x302293b1}, // crush_attack_switch
        {0xd1819d75,0x1a720ffe}, // crush_attack_ground_1
        {0xce8198bc,0x19720e6b}, // crush_attack_ground_2
        {0xcf819a4f,0x18720cd8}, // crush_attack_ground_3
        {0xbde2c912,0xb40ede11}, // cleave_spin_attack_ground_2
        {0xbee2caa5,0xb30edc7e}, // cleave_spin_attack_ground_3
        {0xbce2c77f,0xb10ed958}, // cleave_spin_attack_ground_1
        {0xb7e2bfa0,0xb60ee137}, // cleave_spin_attack_ground_4
        {0xa2d85774,0x62a5fc15}, // bludgeon_attack_ground_3
        {0xa3d85907,0x61a5fa82}, // bludgeon_attack_ground_2
        {0xa4d85a9a,0x60a5f8ef}, // bludgeon_attack_ground_1
        {0x32c1f75,0xe5de55fe}, // bludgeon_uppercut_attack_ground_1
        {0xb3fc3488,0xc880ff5}, // cleave_attack_ground_1
        {0xe88987be,0x2b6a7ae3}, // split_attack_ground_1
        {0xe789862b,0x2c6a7c76}, // split_attack_ground_2
        {0xf60ca65d,0xa6c44c46}, // split_triple_attack_ground_1
        {0xf30ca1a4,0xa5c44ab3}, // split_triple_attack_ground_2
    };
    for (const auto& pair:pairs) if (node_id==pair[0] || node_id==pair[1]) return pair[0];
    return node_id;
}
inline unsigned sequence_stage(std::uint32_t form,std::uint32_t node_id) {
    constexpr std::uint32_t beat[3][2]={{0xa4d85a9a,0x60a5f8ef},{0xa3d85907,0x61a5fa82},{0xa2d85774,0x62a5fc15}};
    constexpr std::uint32_t spin[4][2]={{0xbce2c77f,0xb10ed958},{0xbde2c912,0xb40ede11},{0xbee2caa5,0xb30edc7e},{0xb7e2bfa0,0xb60ee137}};
    if (form==2922085471u) for (unsigned i=0;i<3;++i) if (beat[i][0]==node_id || beat[i][1]==node_id) return i+1;
    if (form==4201352106u) for (unsigned i=0;i<4;++i) if (spin[i][0]==node_id || spin[i][1]==node_id) return i+1;
    return 0;
}
class Repetition {
    std::uint32_t move_=0;
    std::uint64_t serial_=0;
    struct Node { std::uint32_t id=0; float next=1; bool used=false; };
    std::array<Node,128> nodes_{};
public:
    Token reserve() { return {{},++serial_}; }
    void different_weapon() { move_=0;nodes_={}; }
    Token select(Selection selection) {
        Token token{{},++serial_};
        if (selection.category==Category::Primary || selection.category==Category::NormalUppercut) {
            different_weapon();return token;
        }
        if (move_!=selection.move) { different_weapon();move_=selection.move; }
        Node* free=nullptr;
        for (auto& node:nodes_) {
            if (!node.used && !free) free=&node;
            if (!node.used || node.id!=selection.node) continue;
            token.repeat.damage=node.next;node.next*=0.75f;
            token.repeat.falter=selection.category==Category::Finisher||selection.category==Category::Drill?token.repeat.damage:1;
            return token;
        }
        if (free) *free={selection.node,0.75f,true};
        return token;
    }
    // Cancellation/Enemy Step have no operation: the selection was already counted.
    // Automatic nodes and persistent ticks receive a COPY of Token, never select().
};
}
