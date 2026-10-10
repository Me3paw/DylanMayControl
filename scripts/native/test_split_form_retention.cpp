#include "../../src/native/split_form_retention.h"
#include <cassert>
#include <cstdio>
#include <map>

// Contract replay of native spawning (142349850), detached form update
// (142349ee0), and map-miss destruction (14234b100). Not a gameplay test.
struct Form { bool detached=false; float remaining=0; };
struct NativeForms {
    std::map<std::uint32_t,std::uint16_t> flags;
    std::map<std::uint32_t,Form> forms;
    bool patch=true;
    void equip(std::uint32_t id) {
        for (auto& [key,value]:flags) value=key==id ? 0x20 : 0x08;
        for (const auto& [key,value]:flags) {
            const bool selected=(value&0x60)!=0;
            if (selected || (patch && split_form_retention::retain(key,value))) forms.try_emplace(key);
            else forms.erase(key);
        }
    }
    void attack(std::uint32_t id) {
        // Native same-form attack reattaches the existing detached entity.
        // Its next authored detach event starts a new native duration.
        auto found=forms.find(id);
        if (found!=forms.end()) found->second.detached=false;
    }
    void detach(std::uint32_t id,float native_duration) { forms.at(id)={true,native_duration}; }
    void tick(float dt) {
        for (auto& [id,form]:forms) {
            (void)id;
            if (form.detached) {
                form.remaining-=dt;
                if (form.remaining<=0) form={}; // Native animation requests reattachment on expiry.
            }
        }
    }
};

int main() {
    using namespace split_form_retention;
    constexpr std::uint32_t other=0xa9c8aed4;
    for (auto first:{kPivot,kOrbit}) {
        NativeForms baseline{{{kPivot,8},{kOrbit,8},{other,8}},{},false};
        baseline.equip(first);baseline.detach(first,10);baseline.equip(other);
        assert(!baseline.forms.contains(first)); // Reproduces the destructive selection boundary.
        NativeForms fixed{{{kPivot,8},{kOrbit,8},{other,8}},{},true};
        fixed.equip(first);fixed.detach(first,10);fixed.tick(2);fixed.equip(other);
        assert(fixed.forms.at(first).detached && fixed.forms.at(first).remaining==8);
        auto second=first==kPivot?kOrbit:kPivot;
        fixed.equip(second);fixed.detach(second,4);fixed.equip(other);
        assert(fixed.forms.at(first).detached && fixed.forms.at(second).detached);
        fixed.tick(3);
        assert(fixed.forms.at(first).remaining==5 && fixed.forms.at(second).remaining==1);
        for (int i=0;i<20;++i) {
            fixed.equip(second);fixed.attack(second);
            assert(!fixed.forms.at(second).detached && fixed.forms.at(first).remaining==5);
            fixed.detach(second,4);fixed.equip(other);
            assert(fixed.forms.count(first)==1 && fixed.forms.count(second)==1);
        }
        fixed.tick(4);
        assert(fixed.forms.at(first).detached && !fixed.forms.at(second).detached);
        fixed.tick(1);assert(!fixed.forms.at(first).detached);
        fixed.detach(first,10);fixed.forms.clear(); // Native actor/world cleanup is untouched.
        assert(fixed.forms.empty());
    }
    for (auto id:{kPivot,kOrbit}) {
        assert(retain(id,0x08) && retain(id,0x108));
        for (auto flags:{0,1,2,4,0x10,0x18,0x20,0x40,0x208}) assert(!retain(id,flags));
    }
    assert(!retain(other,8) && !retain(0,8));
    puts("PASS: native-contract replay: both orderings, other finishers, independent timers, same-form retrigger, per-ID cap, cleanup and ownership rejection");
}
