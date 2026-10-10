#pragma once
#include <cstdint>
#include <atomic>
namespace finisher_test_mode {
// HUMAN REQUEST: next test patch only. Restore false for the normal release.
inline constexpr bool unlocked_test_patch=false;
inline std::atomic<bool> enabled{false};
inline bool active() { return unlocked_test_patch && enabled.load(); }
inline bool known(std::uint32_t id) {
    switch(id) {
    case 2848499412u: case 2276994531u: case 3260945180u:
    case 2922085471u: case 3896273609u: case 4201352106u: return true;
    default: return false;
    }
}
}
