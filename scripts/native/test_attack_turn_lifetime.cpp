#include "../../src/native/attack_turn_native.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){
    using attack_turn_native::Lifetime;
    Lifetime charge{true};
    assert(!charge.observe(0,true,false)); // Chooser return precedes native activation.
    assert(charge.observe(20,true,true));charge.applied();
    assert(charge.observe(1200,true,true));charge.applied();
    assert(charge.observe(8000,true,true)); // Duration is the native attack, not a fixed charge timer.
    assert(!charge.observe(8010,true,false) && charge.finished);
    assert(!charge.observe(8020,true,true)); // A cancelled charge cannot re-arm itself.
    Lifetime lost_lock{true};assert(lost_lock.observe(0,true,true));
    assert(!lost_lock.observe(10,false,true) && !lost_lock.observe(20,true,true));
    Lifetime stale{true};assert(!stale.observe(251,true,false) && stale.finished);
    Lifetime tap{};assert(tap.observe(0,true,true));tap.applied();assert(tap.finished);
    // Build101 human log: node66 stays current ~1.1s, but the melee window closes
    // on the call after the first correction. That must not retire the Uppercut.
    using attack_turn_native::attack_matches;
    Lifetime windows{true};
    assert(!windows.observe(0,true,attack_matches(true,false,false)));       // Node76 still current.
    assert(windows.observe(38,true,attack_matches(true,true,true)));windows.applied();
    assert(windows.observe(54,true,attack_matches(true,true,false)) && !windows.finished);windows.applied();
    assert(windows.observe(600,true,attack_matches(true,true,true)));windows.applied(); // Slot reactivated mid-charge.
    assert(windows.observe(1100,true,attack_matches(true,true,false)));
    assert(!windows.observe(1190,true,attack_matches(true,false,true)) && windows.finished); // Node left: retire.
    assert(!windows.observe(1200,true,attack_matches(true,true,true)));      // Never re-arms.
    Lifetime other{}; // Other Back attacks keep the native-window one-shot gate.
    assert(!attack_matches(false,true,false) && !attack_matches(false,false,true) && attack_matches(false,true,true));
    assert(!other.observe(10,true,attack_matches(false,true,false)) && !other.finished);
    assert(other.observe(20,true,attack_matches(false,true,true)));other.applied();assert(other.finished);
    using namespace facing_movement;
    Vec3 local{};const float upright[4]{0,0,0,1},wall[4]{0,0,std::sqrt(0.5f),std::sqrt(0.5f)},ceiling[4]{1,0,0,0},invalid[4]{0,0,0,0};
    assert(plane_local({0,0,1},upright,local) && std::abs(local.z-1)<1e-5f);
    assert(plane_local({0,1,0},wall,local) && std::abs(local.x-1)<1e-5f && std::abs(local.y)<1e-5f);
    assert(plane_local({0,0,-1},ceiling,local) && std::abs(local.z-1)<1e-5f);
    assert(!plane_local({0,0,0},upright,local) && !plane_local({0,0,1},invalid,local));
    assert(!plane_local({NAN,0,1},upright,local));
    puts("PASS: sustained native attack lifetime, cancellation, one-shot preservation and gravity-plane targeting");
}
