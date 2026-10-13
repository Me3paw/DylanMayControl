#pragma once
#include <array>
#include <cstdint>
namespace loadout_press_feedback {
// Worker-owned physical edges: weapon/catalogue changes never create or erase a press.
class Edges {
    std::array<std::uint64_t,5> scope_{};
    std::array<bool,2> previous_{};
    bool baseline_=false;
public:
    unsigned baseline_reason(const std::array<std::uint64_t,5>& scope) const {
        return !baseline_ ? 1 : scope!=scope_ ? 2 : 0;
    }
    void reset() { baseline_=false; }
    std::array<bool,2> update(const std::array<std::uint64_t,5>& scope,const std::array<bool,2>& down) {
        std::array<bool,2> edges{};
        if(baseline_ && scope==scope_)
            for(unsigned i=0;i<2;++i)edges[i]=down[i] && !previous_[i];
        scope_=scope;previous_=down;baseline_=true;return edges;
    }
};
}
