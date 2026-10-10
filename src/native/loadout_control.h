#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace loadout_control {
constexpr unsigned kSlotCount=3;
enum class Channel : std::uint8_t { Primary, Secondary };
enum class Status : std::uint8_t { Unknown, Available, Unavailable, WrongChannel, Stale };
enum class Boundary : std::uint8_t { Unknown, Attack, Charge, Release, Recovery, Legal };
enum class Submission : std::uint8_t { Submitted, Busy, Rejected };
enum class Terminal : std::uint8_t { Unknown, Pending, Applied, Aborted, EquippedObserved, CallbackDispatchObserved };
struct TerminalProof {
    Terminal state = Terminal::Unknown;
    Channel channel = Channel::Primary;
    std::uint32_t id = 0;
    std::uint64_t ticket = 0, world_epoch = 0;
};
enum class Result : std::uint8_t {
    Unbound, Rejected, Configured, Cleared, NoChange, Queued, Waiting, Submitted, Confirmed
};
enum class Action : std::uint8_t { Configure, Clear, SelectSlot };
struct Validation { Status status = Status::Unknown; bool selected = false; bool activation_matches = false; };

// Borrowed for one synchronous call on the verified native execution owner.
// validate proves CURRENT membership, channel and ownership. Legal means the
// native boundary permits selection with no new cancel/cadence change, move 0
// next. Submitted emits one native command; Busy/Rejected emit no side effect.
// Callbacks cannot reenter Control or retain argument pointers.
// Terminal proof is for the exact issued command: Applied includes native
// effects; Aborted guarantees it cannot still apply. EquippedObserved confirms
// current native equip state without exclusive cause/effect attribution.
// Never infer completion from a timeout.
struct Adapter {
    std::uint64_t world_epoch = 0, catalogue_epoch = 0;
    bool owner_scope_current = false;
    std::uint8_t selection_channels = 3;
    void* context = nullptr;
    Validation (*validate)(void*, Channel, std::uint32_t) = nullptr;
    Boundary (*boundary)(void*, Channel) = nullptr;
    Submission (*submit)(void*, Channel, std::uint32_t, std::uint64_t) = nullptr;
    TerminalProof (*terminal)(void*, Channel, std::uint32_t,
                             std::uint64_t, std::uint64_t) = nullptr;
    bool configuration_bound() const {
        return catalogue_epoch && owner_scope_current &&
               context && validate;
    }
    bool bound() const {
        return configuration_bound() && world_epoch && boundary && submit && terminal;
    }
    bool channel_bound(Channel channel) const {
        const auto index=static_cast<unsigned>(channel);
        return index<2 && bound() && (selection_channels&(1u<<index));
    }
};
struct Slot { bool assigned = false; std::uint32_t id = 0; };
struct DesiredChoice { bool assigned = false; unsigned slot = 0; std::uint32_t id = 0; };
struct MenuEvent {
    Action action = Action::SelectSlot;
    Channel channel = Channel::Primary;
    unsigned slot = 0;
    std::uint32_t id = 0;
    std::uint64_t world_epoch = 0;
};
struct Confirmation {
    Channel channel = Channel::Primary;
    std::uint32_t id = 0;
    std::uint64_t ticket = 0, world_epoch = 0;
    bool applied = false; // Native outcome/effects observed or proven abort, never timeout.
};
struct CatalogueItem { Channel channel = Channel::Primary; std::uint32_t id = 0; };
struct OptionModel { CatalogueItem item{}; Validation validation{}; bool enabled = false; };
struct SlotModel { Slot slot{}; Validation validation{}; bool enabled = false; };
struct ChannelModel {
    std::array<SlotModel, kSlotCount> slots{};
    DesiredChoice desired{};
    bool pending = false, in_flight = false;
    bool selection_bound = false;
    std::uint32_t pending_id = 0, in_flight_id = 0;
    std::uint64_t ticket = 0;
    Terminal terminal = Terminal::Unknown;
};
struct MenuModel {
    std::uint64_t world_epoch = 0;
    bool adapter_bound = false, configuration_bound = false;
    bool catalogue_incomplete = false;
    std::array<ChannelModel, 2> channels{};
    std::array<OptionModel, 64> options{};
    std::size_t option_count = 0;
};

// Single execution owner. UI/input events must be marshalled to it.
// IDs and command state only: never weapon stats, upgrades or native adapters.
class Control {
    struct Pending { bool valid = false; unsigned slot = 0; std::uint32_t id = 0; };
    struct Flight {
        bool valid = false;
        std::uint32_t id = 0;
        std::uint64_t ticket = 0;
        Terminal terminal = Terminal::Unknown;
    };
    struct State { Pending pending{}; Flight flight{}; DesiredChoice desired{}; };
    std::array<std::array<Slot, kSlotCount>, 2> slots_{};
    std::array<State, 2> states_{};
    std::uint64_t epoch_ = 0, next_ticket_ = 0;
    static bool valid(Channel channel) { return static_cast<unsigned>(channel) < 2; }
    static unsigned index(Channel channel) { return static_cast<unsigned>(channel); }
    void synchronize(const Adapter& adapter) {
        if (!adapter.bound()) return;
        if (epoch_ != adapter.world_epoch) {
            epoch_ = adapter.world_epoch;
            for (auto& state:states_) { state.pending={}; state.flight={}; }
            // Desired IDs survive; native activation always revalidates them.
        }
    }
public:
    using Assignments=std::array<std::array<std::uint32_t,kSlotCount>,2>;
    Control() = default;
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;
    Assignments assignments() const {
        Assignments out{};
        for(unsigned c=0;c<2;++c)for(unsigned s=0;s<kSlotCount;++s)if(slots_[c][s].assigned)out[c][s]=slots_[c][s].id;
        return out;
    }
    bool restore_assignments(const Assignments& saved) {
        if(epoch_ || has_in_flight())return false; // Startup import, never replacement of a live request.
        for(const auto& channel:saved)for(auto id:channel)if(id==UINT32_MAX)return false;
        for(unsigned c=0;c<2;++c)for(unsigned s=0;s<kSlotCount;++s)slots_[c][s]={saved[c][s]!=0,saved[c][s]};
        return true; // Current adapter validation still controls every equip/cycle.
    }
    bool has_in_flight() const { return states_[0].flight.valid || states_[1].flight.valid; }
    DesiredChoice pending_choice(Channel channel) const {
        if (!valid(channel)) return {};
        const auto& pending=states_[index(channel)].pending;
        if (!pending.valid || pending.slot>=kSlotCount) return {};
        const auto& slot=slots_[index(channel)][pending.slot];
        return slot.assigned && slot.id==pending.id ? DesiredChoice{true,pending.slot,pending.id} : DesiredChoice{};
    }
    Result on_menu_event(const Adapter& adapter, const MenuEvent& event) {
        if (!adapter.configuration_bound()) return Result::Unbound;
        if (!valid(event.channel) || event.slot >= kSlotCount || event.world_epoch != adapter.catalogue_epoch)
            return Result::Rejected;
        auto& slot = slots_[index(event.channel)][event.slot];
        auto& state = states_[index(event.channel)];
        switch (event.action) {
        case Action::Configure: {
            auto checked = adapter.validate(adapter.context, event.channel, event.id);
            if (checked.status != Status::Available) return Result::Rejected;
            if (slot.assigned && slot.id == event.id) return Result::NoChange;
            slot = {true, event.id};
            if (state.pending.valid && state.pending.slot == event.slot) state.pending = {};
            if (state.desired.assigned && state.desired.slot == event.slot) state.desired = {};
            return Result::Configured;
        }
        case Action::Clear:
            slot = {};
            if (state.pending.valid && state.pending.slot == event.slot) state.pending = {};
            if (state.desired.assigned && state.desired.slot == event.slot) state.desired = {};
            return Result::Cleared;
        case Action::SelectSlot: {
            if (!adapter.channel_bound(event.channel)) return Result::Unbound;
            synchronize(adapter);
            if (!slot.assigned) return Result::Rejected;
            auto checked = adapter.validate(adapter.context, event.channel, slot.id);
            if (checked.status != Status::Available) return Result::Rejected;
            state.desired={true,event.slot,slot.id};
            if (checked.selected && checked.activation_matches && !state.flight.valid) {
                state.pending = {};
                return Result::NoChange;
            }
            state.pending = {true, event.slot, slot.id};
            return Result::Queued;
        }
        }
        return Result::Rejected;
    }
    Result request_slot(const Adapter& adapter, Channel channel, unsigned slot) {
        return on_menu_event(adapter,
            {Action::SelectSlot, channel, slot, 0, adapter.catalogue_epoch});
    }
    Result cycle(const Adapter& adapter,Channel channel) {
        if (!adapter.channel_bound(channel)) return Result::Unbound;
        synchronize(adapter);
        if (!valid(channel)) return Result::Rejected;
        const unsigned c=index(channel); unsigned anchor=kSlotCount;
        const auto& desired=states_[c].desired;
        if (desired.assigned && desired.slot<kSlotCount && slots_[c][desired.slot].assigned &&
            slots_[c][desired.slot].id==desired.id) anchor=desired.slot;
        if (anchor==kSlotCount) {
            for (unsigned i=0;i<kSlotCount;++i) {
                const auto& slot=slots_[c][i]; if (!slot.assigned) continue;
                const auto checked=adapter.validate(adapter.context,channel,slot.id);
                if (checked.status==Status::Available && checked.selected) { anchor=i; break; }
            }
        }
        const unsigned start=anchor<kSlotCount ? anchor : kSlotCount-1;
        for (unsigned offset=1;offset<=kSlotCount;++offset) {
            const unsigned next=(start+offset)%kSlotCount; const auto& slot=slots_[c][next];
            if (!slot.assigned) continue;
            const auto checked=adapter.validate(adapter.context,channel,slot.id);
            if (checked.status!=Status::Available) continue;
            if (anchor<kSlotCount && next==anchor && checked.selected) return Result::NoChange;
            return request_slot(adapter,channel,next);
        }
        return Result::Rejected;
    }
    DesiredChoice desired_choice(const Adapter& adapter,Channel channel) const {
        if (!adapter.configuration_bound() || !valid(channel)) return {};
        const auto& desired=states_[index(channel)].desired;
        if (!desired.assigned || desired.slot>=kSlotCount) return {};
        const auto& slot=slots_[index(channel)][desired.slot];
        if (!slot.assigned || slot.id!=desired.id ||
            adapter.validate(adapter.context,channel,desired.id).status!=Status::Available) return {};
        return desired;
    }
    Result reconcile(const Adapter& adapter, Channel channel) {
        synchronize(adapter);
        if (!adapter.bound()) return Result::Unbound;
        if (!valid(channel)) return Result::Rejected;
        auto& flight = states_[index(channel)].flight;
        if (!flight.valid) return Result::NoChange;
        flight.terminal = Terminal::Unknown;
        const auto proof = adapter.terminal(
            adapter.context, channel, flight.id, flight.ticket, epoch_);
        if (proof.channel != channel || proof.id != flight.id ||
            proof.ticket != flight.ticket || proof.world_epoch != epoch_) return Result::Waiting;
        switch (proof.state) {
        case Terminal::Pending:
            flight.terminal = Terminal::Pending;
            return Result::Waiting;
        case Terminal::Applied:
        case Terminal::EquippedObserved:
        case Terminal::CallbackDispatchObserved:
            return confirm(adapter, {channel, flight.id, flight.ticket, epoch_, true});
        case Terminal::Aborted:
            return confirm(adapter, {channel, flight.id, flight.ticket, epoch_, false});
        default:
            return Result::Waiting;
        }
    }
    Result pump(const Adapter& adapter, Channel channel) {
        synchronize(adapter);
        if (!adapter.channel_bound(channel)) return Result::Unbound;
        if (!valid(channel)) return Result::Rejected;
        auto& state = states_[index(channel)];
        if (state.flight.valid) {
            const auto outcome = reconcile(adapter, channel);
            if (state.flight.valid || !state.pending.valid) return outcome;
        }
        if (!state.pending.valid) return Result::NoChange;
        const auto checked = adapter.validate(adapter.context, channel, state.pending.id);
        if (checked.status == Status::Unknown) return Result::Waiting;
        if (checked.status != Status::Available) {
            state.pending = {};
            return Result::Rejected;
        }
        if (checked.selected && checked.activation_matches) { state.pending = {}; return Result::NoChange; }
        if (adapter.boundary(adapter.context, channel) != Boundary::Legal) return Result::Waiting;
        if (next_ticket_ == std::numeric_limits<std::uint64_t>::max()) {
            state.pending = {};
            return Result::Rejected;
        }
        const auto ticket = ++next_ticket_;
        const auto outcome = adapter.submit(adapter.context, channel, state.pending.id, ticket);
        if (outcome == Submission::Busy) return Result::Waiting;
        if (outcome != Submission::Submitted) { state.pending = {}; return Result::Rejected; }
        state.flight = {true, state.pending.id, ticket, Terminal::Pending};
        state.pending = {};
        return Result::Submitted;
    }
    std::array<Result, 2> pump_all(const Adapter& adapter) {
        const auto primary = pump(adapter, Channel::Primary);
        const auto secondary = pump(adapter, Channel::Secondary);
        return {primary, secondary};
    }
    Result confirm(const Adapter& adapter, const Confirmation& confirmation) {
        synchronize(adapter);
        if (!adapter.bound()) return Result::Unbound;
        if (!valid(confirmation.channel) || confirmation.world_epoch != epoch_)
            return Result::Rejected;
        auto& flight = states_[index(confirmation.channel)].flight;
        if (!flight.valid || confirmation.ticket != flight.ticket || confirmation.id != flight.id)
            return Result::Rejected;
        flight = {};
        return confirmation.applied ? Result::Confirmed : Result::Rejected;
    }
    MenuModel menu(const Adapter& adapter, const CatalogueItem* catalogue, std::size_t count) {
        synchronize(adapter);
        MenuModel model{};
        model.configuration_bound = adapter.configuration_bound();
        model.world_epoch = model.configuration_bound ? adapter.catalogue_epoch : 0;
        model.adapter_bound = adapter.bound();
        model.catalogue_incomplete = count > model.options.size() || (count && !catalogue);
        for (unsigned channel = 0; channel < 2; ++channel) {
            auto& view = model.channels[channel];
            view.selection_bound=adapter.channel_bound(static_cast<Channel>(channel));
            const auto& state = states_[channel];
            view.desired=state.desired;
            view.pending = state.pending.valid;
            view.pending_id = state.pending.id;
            view.in_flight = state.flight.valid;
            view.in_flight_id = state.flight.id;
            view.ticket = state.flight.ticket;
            view.terminal = model.adapter_bound ? state.flight.terminal : Terminal::Unknown;
            for (unsigned slot = 0; slot < kSlotCount; ++slot) {
                auto& item = view.slots[slot];
                item.slot = slots_[channel][slot];
                if (model.configuration_bound && item.slot.assigned) {
                    item.validation = adapter.validate(adapter.context, static_cast<Channel>(channel), item.slot.id);
                    item.enabled = item.validation.status == Status::Available;
                }
            }
        }
        if (!model.configuration_bound || !catalogue) return model;
        const auto limit = count < model.options.size() ? count : model.options.size();
        for (std::size_t i = 0; i < limit; ++i) {
            if (!valid(catalogue[i].channel)) { model.catalogue_incomplete = true; continue; }
            auto& option = model.options[model.option_count++];
            option.item = catalogue[i];
            option.validation = adapter.validate(adapter.context, option.item.channel, option.item.id);
            option.enabled = option.validation.status == Status::Available;
        }
        return model;
    }
};
} // namespace loadout_control
