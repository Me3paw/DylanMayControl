#include "facing_movement.h"
#include <cassert>
#include <limits>
int main() {
    assert(!facing_movement::back_turn_ready(1,0));
    assert(!facing_movement::back_turn_ready(0.7f,30));
    assert(facing_movement::back_turn_ready(-1,40));
    assert(facing_movement::back_turn_ready(-0.1f,250));
    assert(!facing_movement::back_turn_ready(-1,251));
    assert(!facing_movement::back_turn_ready(std::numeric_limits<float>::quiet_NaN(),30));
}
