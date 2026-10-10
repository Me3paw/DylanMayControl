#pragma once
#include "loadout_attack_probe.h"
#include <array>
namespace loadout_form_requirements {
inline constexpr std::size_t kMaxRequirements=64;
// Verified native input kinds: PRIMARY, PRIMARY_HOLD, SECONDARY, SECONDARY_HOLD.
enum : std::uint8_t { kPrimary=1, kSecondary=2 };
struct Summary {
    std::uint32_t native_id=0, requirement_count=0;
    std::uint8_t native_category=UINT8_MAX, input_kinds=0, channels=0;
};
// Weapon slots use native form category, not attack-input membership. Finisher
// and attack-variant definitions can use primary input without being weapons.
template<class Reader>
inline bool classify(std::uintptr_t definition,std::uint32_t expected_id,
                     Summary& out,Reader read) {
    out={};
    if (!definition || !expected_id) return false;
    Summary result{}; std::uintptr_t address=0;
    std::uint32_t id=0,count=0,category=UINT32_MAX;
    std::uint64_t requirements=0;
    if (!read(definition,&id,sizeof(id)) || id!=expected_id ||
        !loadout_attack_probe::add(definition,0x70,address) || !read(address,&category,sizeof(category)) ||
        category>=4 ||
        !loadout_attack_probe::add(definition,0x78,address) ||
        !read(address,&requirements,sizeof(requirements)) ||
        !loadout_attack_probe::add(definition,0x80,address) || !read(address,&count,sizeof(count)) ||
        count>kMaxRequirements) return false;
    result.native_id=id; result.native_category=static_cast<std::uint8_t>(category); result.requirement_count=count;
    result.channels=category<2 ? static_cast<std::uint8_t>(1u<<category) : 0;
    if (!count) { out=result; return true; }
    const std::size_t bytes=static_cast<std::size_t>(count)*4;
    std::uintptr_t last=0;
    if (!requirements || requirements>(std::numeric_limits<std::uintptr_t>::max)() ||
        !loadout_attack_probe::add(static_cast<std::uintptr_t>(requirements),bytes-1,last))
        return false;
    std::array<unsigned char,kMaxRequirements*4> records{};
    if (!read(static_cast<std::uintptr_t>(requirements),records.data(),bytes)) return false;
    for (std::size_t i=0;i<count;++i) {
        const unsigned kind=records[i*4];
        if (kind>3) return false;
        result.input_kinds=static_cast<std::uint8_t>(result.input_kinds|(1u<<kind));
    }
    out=result; return true;
}
} // namespace loadout_form_requirements
