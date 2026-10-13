#include "../../src/native/combat_v1_policy.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace combat_v1;
static void close(float a,float b) { assert(std::fabs(a-b)<0.0001f); }
int main() {
    Repetition repeat;
    const Selection crush1{1,1,Category::Secondary},crush2{1,2,Category::Secondary},crush3{1,3,Category::Secondary};
    const Selection beatdown{2,1,Category::Finisher},spin{3,1,Category::Finisher};
    const Selection primary{4,1,Category::Primary},upper{5,1,Category::NormalUppercut},charged{5,2,Category::Finisher};
    close(repeat.select(crush1).repeat.damage,1);close(repeat.select(crush2).repeat.damage,1);
    close(repeat.select(crush3).repeat.damage,1);
    const auto again=repeat.select(crush1);close(again.repeat.damage,.75f);close(again.repeat.falter,1);
    repeat.select(primary);close(repeat.select(crush1).repeat.damage,1);
    const float expected[]={1,.75f,.5625f,.421875f,.31640625f};
    for (float value:expected) { const auto t=repeat.select(beatdown);close(t.repeat.damage,value);close(t.repeat.falter,value); }
    // A canceled selection has no refund. Automatic nodes/ticks retain the copied token.
    const auto selected=repeat.select(spin);
    for (int i=0;i<4;++i) { const Token automatic=selected;close(automatic.repeat.damage,1);assert(automatic.serial==selected.serial); }
    close(repeat.select(spin).repeat.damage,.75f);
    const auto persistent=repeat.select(spin);repeat.select(primary);
    close(persistent.repeat.damage,.5625f);close(repeat.select(spin).repeat.damage,1);
    repeat.different_weapon();close(repeat.select(spin).repeat.damage,1);
    close(repeat.select(charged).repeat.falter,1);close(repeat.select(charged).repeat.falter,.75f);
    for (int i=0;i<20;++i) { auto t=repeat.select(upper);close(t.repeat.damage,1);close(t.repeat.falter,1); }
    close(repeat.select(charged).repeat.damage,1);
    const auto before_owner_change=repeat.reserve();repeat.different_weapon();
    assert(repeat.reserve().serial>before_owner_change.serial); // retained observers cannot alias a newer use
    repeat.different_weapon();for(int i=0;i<150;++i)repeat.select(spin);
    assert(repeat.select(spin).repeat.damage<1e-18f); // no artificial floor
    Scale scale;assert(!base_scale(0x12345678,scale));
    assert(base_scale(0x443188a2,scale));Value drill[]={{15,4},{2,9}};
    assert(scale_values(drill,2,scale));close(drill[0].amount,15);close(drill[1].amount,4);
    assert(base_scale(0x31f6e1fb,scale));Value split[]={{20,4},{20,6},{5,9}};
    assert(scale_values(split,3,scale));close(split[0].amount+split[1].amount,20);close(split[2].amount,2.5f);
    Value invalid[]={{10,2},{-1,9}};assert(!scale_values(invalid,2,{.5f,.5f}));close(invalid[0].amount,10);
    invalid[1]={1,10};assert(!scale_values(invalid,2,{.5f,.5f}));close(invalid[0].amount,10);
    invalid[1]={std::numeric_limits<float>::infinity(),9};assert(!scale_values(invalid,2,{.5f,.5f}));
    invalid[1]={1,9};assert(!scale_values(invalid,2,{-1,1}));assert(!scale_values(invalid,0,{1,1}));
    assert(!scale_values(invalid,13,{1,1}));assert(!scale_values(nullptr,1,{1,1}));
    // Native clips/cache prove four eligible episodes; target contact remains unmeasured.
    assert(base_scale(0x007580d6,scale));const auto beat=(90+90+90+180)*scale.damage;
    close(beat,450);close(beat/210,15.f/7);
    assert(base_scale(0xb57abbd8,scale));close(250*scale.damage/210,5.f/3);
    assert(base_scale(0xfa6b8baa,scale));close(4*100*scale.damage/210,20.f/7);
    assert(base_scale(0xb18ef961,scale));close(210/(120*scale.damage),1.75f);
    assert(base_scale(0x007580d6,scale));close(100*scale.falter,50);
    assert(base_scale(0xb57abbd8,scale));close(10*scale.falter,14);
    assert(base_scale(0xfa6b8baa,scale));close(15*scale.falter,7.5f);close(4*15*scale.falter,30); // four-event reference, not full upgraded total
    assert(base_scale(0xa9c8a6d4,scale));close(100*scale.damage,65);close(30*scale.falter,19.5f);
    assert(base_scale(0x378b8f5c,scale));close(160*scale.damage,128);close(50*scale.falter,40);
    assert(sequence_stage(2922085471u,0xa4d85a9a)==1);assert(sequence_stage(2922085471u,0x62a5fc15)==3);
    assert(sequence_stage(4201352106u,0xbce2c77f)==1);assert(sequence_stage(4201352106u,0xb60ee137)==4);
    assert(!sequence_stage(2922085471u,0xbce2c77f));
    // Match immutable hit identity, refusing indistinguishable hits from
    // different input generations instead of applying the latest counter.
    HitIdentity hits[]={{77,{123,456},17,8},{78,{123,456},17,9},{77,{124,456},17,10}};
    HitIdentity request{77,{123,456},17,0};std::uint32_t generation=0;
    assert(hit_generation(hits,3,request,generation) && generation==8);
    hits[2]=hits[0];assert(hit_generation(hits,3,request,generation) && generation==8);
    hits[2].generation=10;assert(!hit_generation(hits,3,request,generation));
    request.id=18;assert(!hit_generation(hits,3,request,generation));
    Repetition elevation;
    close(elevation.select({2922085471u,logical_node(0xa4d85a9a),Category::Finisher}).repeat.damage,1);
    close(elevation.select({2922085471u,logical_node(0x60a5f8ef),Category::Finisher}).repeat.damage,.75f);
    assert(logical_node(0xd1819d75)==logical_node(0x1a720ffe));
    assert(logical_node(0xce8198bc)!=logical_node(0xd1819d75));
    HitTokens ledger;Repetition input;
    const auto first=input.select(spin),second=input.select(spin);
    assert(ledger.save(7,first,nullptr,0));assert(ledger.save(8,second,nullptr,0));
    // Native detachment renumbers the same group; old queued contacts and new
    // detached ticks share their original selection even after a primary reset.
    assert(ledger.save(9,ledger.find(8),nullptr,0));input.select(primary);
    close(ledger.find(8).repeat.damage,.75f);close(ledger.find(9).repeat.damage,.75f);
    assert(ledger.find(9).serial==second.serial);assert(!ledger.find(999).serial);
    // Exhaustion must never evict a still-live old effect to admit a new attack.
    ledger={};std::array<std::uint32_t,128> live{};
    for (unsigned i=0;i<live.size();++i) {live[i]=i;assert(ledger.save(i,first,nullptr,0));}
    assert(!ledger.save(500,second,live.data(),live.size()));assert(ledger.find(0).serial==first.serial);
    live[0]=500;assert(ledger.save(500,second,live.data(),live.size()));
    assert(!ledger.find(0).serial && ledger.find(500).serial==second.serial);
    // Reused native generation from an unrelated source invalidates the old token.
    assert(ledger.save(500,{},nullptr,0));assert(!ledger.find(500).serial);
    // Native Beat second input can own multiple following nodes; held Spin is one input.
    Repetition manual;
    auto beat1=manual.select({2922085471u,logical_node(0xa4d85a9a),Category::Finisher});
    assert(!inherit_native_input(beat1.serial,2922085471u,2922085471u,0x100,true,0,1));
    auto beat2=manual.select({2922085471u,logical_node(0xa3d85907),Category::Finisher});
    assert(inherit_native_input(beat2.serial,2922085471u,2922085471u,0,false,1,2));
    assert(inherit_native_input(beat2.serial,2922085471u,2922085471u,0,false,1,1));
    close(beat2.repeat.damage,1);
    auto beat1_again=manual.select({2922085471u,logical_node(0x60a5f8ef),Category::Finisher});
    close(beat1_again.repeat.damage,.75f);
    auto beat2_again=manual.select({2922085471u,logical_node(0xa3d85907),Category::Finisher});
    close(beat2_again.repeat.damage,.75f);
    assert(inherit_native_input(beat2_again.serial,2922085471u,2922085471u,0,false,3,4));
    auto spin_hold=manual.select({4201352106u,logical_node(0xbce2c77f),Category::Finisher});
    for(unsigned node=2;node<50;++node) {
        assert(inherit_native_input(spin_hold.serial,4201352106u,4201352106u,0x101,false,7,7));
        assert(inherit_native_input(spin_hold.serial,4201352106u,4201352106u,0x100,false,7,7));
        close(spin_hold.repeat.damage,1);
    }
    assert(!inherit_native_input(spin_hold.serial,4201352106u,4201352106u,0x101,false,7,8));
    assert(!inherit_native_input(spin_hold.serial,4201352106u,4201352106u,0x100,true,7,7));
    assert(!inherit_native_input(spin_hold.serial,4201352106u,2922085471u,0,false,7,7));
    assert(!inherit_native_input(0,4201352106u,4201352106u,0,false,7,7));
    auto spin_new=manual.select({4201352106u,logical_node(0xb10ed958),Category::Finisher});
    close(spin_new.repeat.damage,.75f); // canceled accepted direct uses still count
    WaveRows winners;std::array<std::uint8_t,0x70> wave_a{},wave_b{};
    wave_a[0x30]=1;wave_b[0x30]=2;
    winners.save(123,0,wave_a,first);
    assert(winners.find(123,0,wave_a).serial==first.serial); // native weaker/tied wave does not replace row
    assert(!winners.find(123,0,wave_b).serial);
    winners.save(123,0,wave_b,second); // native stronger wave becomes the actual winner
    assert(winners.find(123,0,wave_b).serial==second.serial);
    winners.save(123,0,wave_a,{});assert(!winners.find(123,0,wave_a).serial); // unrelated producer invalidates
    winners.save(123,1,wave_b,second);winners.clear(123);
    assert(!winners.find(123,1,wave_b).serial); // queue index reuse cannot inherit old authority
    // Base tuning precedes native upgrades; a later flat bonus is not retuned.
    Value upgraded[]={{100,3},{30,9}};
    assert(scale_values(upgraded,2,{.65f,.65f}));
    upgraded[0].amount=upgraded[0].amount*1.5f+20;
    upgraded[1].amount+=5;
    Value final[] = {upgraded[0],upgraded[1],{12,6}}; // upgrade-added Fire channel
    assert(scale_values(final,3,{.75f,.75f}));
    close(final[0].amount,88.125f);close(final[1].amount,18.375f);close(final[2].amount,9);
    Value secondary_final[]={{60,4},{8,9},{10,6}};
    assert(scale_values(secondary_final,3,{.5625f,1}));
    close(secondary_final[0].amount,33.75f);close(secondary_final[1].amount,8);close(secondary_final[2].amount,5.625f);
    Repetition drill_repeat;
    const Selection drill_input{444122270u,logical_node(0xc19456d3),Category::Drill};
    close(drill_repeat.select(drill_input).repeat.falter,1);
    auto drill_second=drill_repeat.select(drill_input);close(drill_second.repeat.damage,.75f);close(drill_second.repeat.falter,.75f);
    Value drill_upgraded[]={{15,4},{2,9}};
    assert(scale_values(drill_upgraded,2,{1,2})); // tune base before native upgrades
    drill_upgraded[1].amount+=2; // illustrative native flat upgrade
    assert(scale_values(drill_upgraded,2,drill_second.repeat));close(drill_upgraded[1].amount,4.5f);
    drill_repeat.select(primary);close(drill_repeat.select(drill_input).repeat.falter,1);
    // Native minimum/flat additions run first; repeat adds no artificial floor.
    Value native_minimum[]={{40,2},{20,9}};
    assert(scale_values(native_minimum,2,{.1f,.1f}));close(native_minimum[0].amount,4);
    std::puts("PASS: repetition/cancel/reset/immutable provenance, base-before-upgrade and final repeat channels, tuning projections");
}
