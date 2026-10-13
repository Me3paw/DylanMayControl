#include "loadout_control.h"
#include "loadout_queue_audio.h"
#include "loadout_press_feedback.h"
#include <cassert>
#include <cstdio>
#include <vector>
#ifdef NDEBUG
#error "Loadout checks require assertions enabled"
#endif
using namespace loadout_control;
namespace {
constexpr auto P = Channel::Primary;
constexpr auto S = Channel::Secondary;
struct Write { Channel channel; std::uint32_t id; std::uint64_t ticket; };
struct Native {
    struct Entry { Channel channel; std::uint32_t id; Status status; };
    std::array<Entry, 7> entries{{
        {P, 10, Status::Available}, {P, 20, Status::Available},
        {S, 30, Status::Available}, {S, 40, Status::Available}, {P, 0, Status::Available},
        {P, 50, Status::Available}, {S, 60, Status::Available}
    }};
    std::array<std::uint32_t, 2> selected{{10, 30}};
    std::array<Boundary, 2> gates{{Boundary::Legal, Boundary::Legal}};
    Submission next = Submission::Submitted;
    unsigned validations = 0, submissions = 0;
    std::vector<Write> writes;
    std::vector<Write> cues;
    static void queued(void* ctx,Channel channel,std::uint32_t id) {
        static_cast<Native*>(ctx)->cues.push_back({channel,id,0});
    }
    TerminalProof terminal_reply{};
    unsigned terminal_queries = 0;
    static Validation validate(void* ctx, Channel ch, std::uint32_t id) {
        auto& n = *static_cast<Native*>(ctx);
        ++n.validations;
        for (const auto& e : n.entries)
            if (e.id == id && e.channel == ch)
                return {e.status, n.selected[static_cast<unsigned>(ch)] == id,
                    n.selected[static_cast<unsigned>(ch)] == id};
        for (const auto& e : n.entries)
            if (e.id == id) return {Status::WrongChannel, false};
        return {Status::Stale, false};
    }
    static Boundary boundary(void* ctx, Channel ch) {
        return static_cast<Native*>(ctx)->gates[static_cast<unsigned>(ch)];
    }
    static Submission submit(void* ctx, Channel ch, std::uint32_t id, std::uint64_t ticket) {
        auto& n = *static_cast<Native*>(ctx);
        ++n.submissions;
        if (n.next == Submission::Submitted) n.writes.push_back({ch, id, ticket});
        return n.next;
    }
    Adapter adapter(std::uint64_t epoch = 1) {
        return {epoch, epoch, true, 3, this, validate, boundary, submit, terminal};
    }
    static TerminalProof terminal(void* ctx, Channel, std::uint32_t,
                                  std::uint64_t, std::uint64_t) {
        auto& n = *static_cast<Native*>(ctx);
        ++n.terminal_queries;
        return n.terminal_reply;
    }
};
Result configure(Control& c, const Adapter& a, Channel ch, unsigned slot, std::uint32_t id) {
    return c.on_menu_event(a, {Action::Configure, ch, slot, id, a.world_epoch});
}
void complete(Control& c, Native& n, const Adapter& a, const Write& w) {
    n.selected[static_cast<unsigned>(w.channel)] = w.id;
    assert(c.confirm(a, {w.channel, w.id, w.ticket, a.world_epoch, true}) == Result::Confirmed);
}
}
static void terminal_regressions() {
    Native n;
    auto a = n.adapter();
    Control c;
    assert(configure(c, a, P, 0, 20) == Result::Configured);
    assert(configure(c, a, P, 1, 0) == Result::Configured);
    assert(configure(c, a, S, 0, 40) == Result::Configured);
    assert(c.request_slot(a, P, 0) == Result::Queued);
    assert(c.pump(a, P) == Result::Submitted);
    const auto first = n.writes.back();
    assert(c.request_slot(a, P, 1) == Result::Queued);
    assert(c.request_slot(a, S, 0) == Result::Queued);
    const auto validations = n.validations, queries = n.terminal_queries;
    assert(c.pump({}, P) == Result::Unbound);
    assert(c.reconcile({}, P) == Result::Unbound);
    assert(c.confirm({}, {P, 20, first.ticket, 1, true}) == Result::Unbound);
    assert(c.on_menu_event({}, {Action::Clear, P, 0, 0, 1}) == Result::Unbound);
    auto m = c.menu({}, nullptr, 0);
    assert(!m.adapter_bound && m.world_epoch == 0);
    assert(m.channels[0].in_flight && m.channels[0].ticket == first.ticket);
    assert(m.channels[0].pending && m.channels[0].pending_id == 0);
    assert(m.channels[1].pending && m.channels[1].pending_id == 40);
    assert(!m.channels[0].slots[0].enabled && m.channels[0].terminal == Terminal::Unknown);
    assert(n.validations == validations && n.terminal_queries == queries);
    assert(c.pump(a, P) == Result::Waiting && n.writes.size() == 1);
    n.terminal_reply = {Terminal::Pending, P, 20, first.ticket, 1};
    assert(c.reconcile(a, P) == Result::Waiting);
    assert(c.menu(a, nullptr, 0).channels[0].terminal == Terminal::Pending);
    for (auto bad : {TerminalProof{Terminal::Applied, P, 20, first.ticket, 2},
                     TerminalProof{Terminal::Applied, P, 20, first.ticket + 1, 1},
                     TerminalProof{Terminal::Applied, S, 20, first.ticket, 1},
                     TerminalProof{Terminal::Applied, P, 0, first.ticket, 1}}) {
        n.terminal_reply = bad;
        assert(c.reconcile(a, P) == Result::Waiting && n.writes.size() == 1);
    }
    // Lost completion event: trusted native proof, not a retry timer.
    n.selected[0] = 20;
    n.terminal_reply = {Terminal::Applied, P, 20, first.ticket, 1};
    assert(c.pump(a, P) == Result::Submitted);
    assert(n.writes.size() == 2 && n.writes.back().id == 0);
    const auto second = n.writes.back();
    assert(c.pump(a, P) == Result::Waiting);
    auto missing = a;
    missing.terminal = nullptr;
    assert(c.reconcile(missing, P) == Result::Unbound);
    assert(c.menu(missing, nullptr, 0).channels[0].ticket == second.ticket);
    n.terminal_reply = {};
    assert(c.pump(a, P) == Result::Waiting && n.writes.size() == 2);
    n.terminal_reply = {Terminal::Aborted, P, 0, second.ticket, 1};
    assert(c.pump(a, P) == Result::Rejected);
    assert(c.pump(a, P) == Result::NoChange && n.writes.size() == 2);
    assert(c.request_slot(a, P, 1) == Result::Queued);
    assert(c.pump(a, P) == Result::Submitted);
    const auto third = n.writes.back();
    n.selected[0] = 0;
    n.terminal_reply = {Terminal::Applied, P, 0, third.ticket, 1};
    assert(c.pump(a, P) == Result::Confirmed);
    assert(c.pump(a, P) == Result::NoChange && n.writes.size() == 3);
    assert(c.request_slot(a, P, 0) == Result::Queued);
    assert(c.pump(a, P) == Result::Submitted);
    const auto fourth = n.writes.back();
    assert(c.request_slot(a, P, 1) == Result::Queued);
    auto unknown = a;
    unknown.world_epoch = 99;
    unknown.owner_scope_current = false;
    m = c.menu(unknown, nullptr, 0);
    assert(m.world_epoch == 0 && m.channels[0].ticket == fourth.ticket);
    auto fresh = n.adapter(2);
    assert(c.confirm(fresh, {P, 20, fourth.ticket, 1, true}) == Result::Rejected);
    m = c.menu(fresh, nullptr, 0);
    assert(m.world_epoch == 2 && !m.channels[0].in_flight);
    assert(!m.channels[0].pending && !m.channels[1].pending);
    assert(m.channels[0].slots[0].slot.id == 20);
    assert(c.pump(fresh, P) == Result::NoChange && n.writes.size() == 4);
    assert(c.request_slot(fresh, P, 0) == Result::Queued);
    assert(c.pump(fresh, P) == Result::Submitted);
    const auto fifth = n.writes.back();
    n.terminal_reply = {Terminal::Applied, P, 20, fourth.ticket, 1};
    assert(c.pump(fresh, P) == Result::Waiting && n.writes.size() == 5);
    n.terminal_reply = {Terminal::Aborted, P, 20, fifth.ticket, 2};
    assert(c.pump(fresh, P) == Result::Rejected);
    assert(c.pump(fresh, P) == Result::NoChange && n.writes.size() == 5);
}
static void queue_feedback_regressions() {
    Native n;auto a=n.adapter();a.queued=&Native::queued;Control c;
    assert(configure(c,a,P,0,20)==Result::Configured);
    assert(configure(c,a,P,1,10)==Result::Configured);
    assert(n.cues.empty());
    n.gates[0]=Boundary::Recovery;
    assert(c.cycle(a,P)==Result::Queued && n.cues.size()==1 && n.cues.back().id==20);
    assert(c.pump(a,P)==Result::Waiting && n.cues.size()==1 && n.writes.empty());
    assert(c.request_slot(a,P,1)==Result::NoChange && n.cues.size()==1);
    assert(c.request_slot(a,P,0)==Result::Queued && n.cues.size()==2);
    n.gates[0]=Boundary::Legal;
    assert(c.pump(a,P)==Result::Submitted && n.cues.size()==2);
    const auto first=n.writes.back();
    assert(c.request_slot(a,P,1)==Result::Queued && n.cues.size()==3 && n.cues.back().id==10);
    assert(c.pump(a,P)==Result::Waiting && n.cues.size()==3);
    complete(c,n,a,first);
    assert(c.pump(a,P)==Result::Submitted && n.cues.size()==3);
    complete(c,n,a,n.writes.back());
    assert(c.pump(a,P)==Result::NoChange && n.cues.size()==3);
    n.entries[1].status=Status::Unavailable;
    assert(c.request_slot(a,P,0)==Result::Rejected && n.cues.size()==3);
    assert(c.request_slot({},P,0)==Result::Unbound && n.cues.size()==3);
    assert(c.on_menu_event(a,{Action::SelectSlot,P,0,0,99})==Result::Rejected && n.cues.size()==3);
    constexpr std::uint32_t forms[]={698021297u,1737076817u,3413371114u,3298993364u,444122270u,2860349769u};
    for(unsigned i=0;i<6;++i) {
        const auto channel=i<3?P:S;
        assert(loadout_queue_audio::cue_index(channel,forms[i])==i);
        assert(loadout_queue_audio::cue_index(i<3?S:P,forms[i])==6);
    }
    assert(loadout_queue_audio::cue_index(P,42)==6);
    assert(loadout_queue_audio::cue_index(P,0)==6);
}
static void all_weapon_cycle_feedback() {
    constexpr std::uint32_t forms[]={698021297u,1737076817u,3413371114u,3298993364u,444122270u,2860349769u};
    Native n;
    for(unsigned i=0;i<6;++i)n.entries[i]={i<3?P:S,forms[i],Status::Available};
    n.entries[6]={P,0,Status::Available};n.selected={forms[0],forms[3]};
    auto a=n.adapter();a.queued=&Native::queued;Control c;
    for(unsigned i=0;i<6;++i)assert(configure(c,a,i<3?P:S,i%3,forms[i])==Result::Configured);
    unsigned count=0;
    for(unsigned channel=0;channel<2;++channel)for(unsigned step=0;step<9;++step) {
        const auto ch=channel?S:P;const unsigned index=channel*3+(step+1)%3;
        n.gates[channel]=Boundary::Recovery;
        assert(c.cycle(a,ch)==Result::Queued && n.cues.size()==++count);
        assert(n.cues.back().id==forms[index] && loadout_queue_audio::cue_index(ch,n.cues.back().id)==index);
        assert(c.pump(a,ch)==Result::Waiting && n.cues.size()==count);
        n.gates[channel]=Boundary::Legal;
        assert(c.pump(a,ch)==Result::Submitted && n.cues.size()==count);
        complete(c,n,a,n.writes.back());
        assert(c.pump(a,ch)==Result::NoChange && n.cues.size()==count);
    }
    assert(n.cues.size()==18);
}
static void physical_press_feedback() {
    using namespace loadout_press_feedback;
    Edges edges;std::array<std::uint64_t,5> scope{1,2,3,4,5};
    assert((edges.update(scope,{false,false})==std::array<bool,2>{false,false}));
    // Keyboard primary tap, held polls and release.
    assert((edges.update(scope,{true,false})==std::array<bool,2>{true,false}));
    assert((edges.update(scope,{true,false})==std::array<bool,2>{false,false}));
    assert((edges.update(scope,{false,false})==std::array<bool,2>{false,false}));
    // Controller secondary tap uses the same physical edge path.
    assert((edges.update(scope,{false,true})==std::array<bool,2>{false,true}));
    edges.update(scope,{false,false});
    // Catalogue epoch is intentionally absent: changing weapon cannot suppress a new tap.
    assert((edges.update(scope,{true,true})==std::array<bool,2>{true,true}));
    edges.reset();assert((edges.update(scope,{true,true})==std::array<bool,2>{false,false}));
    edges.update(scope,{false,false});scope[4]++;
    assert((edges.update(scope,{true,false})==std::array<bool,2>{false,false})); // Rebinding is a baseline.
    Native n;auto a=n.adapter();Control c;
    assert(configure(c,a,P,0,10)==Result::Configured);
    // Even a one-slot/no-change press has a known sound; no queued outcome is required.
    assert(c.preview_cycle(a,P).choice.id==10 && c.preview_cycle(a,P).no_change);
    assert(c.cycle(a,P)==Result::NoChange);
    assert(configure(c,a,P,1,20)==Result::Configured && configure(c,a,P,2,50)==Result::Configured);
    assert(c.preview_cycle(a,P,1).choice.id==20);
    assert(c.preview_cycle(a,P,2).choice.id==50);
    assert(c.preview_cycle(a,P,3).choice.id==10);
    assert(c.preview_cycle(a,P,4).choice.id==20); // Unconsumed rapid presses advance the preview.
    assert(c.desired_choice(a,P).assigned==false && n.writes.empty()); // Preview never changes native selection.
    auto readonly=a;readonly.world_epoch=0;readonly.submit=nullptr;readonly.terminal=nullptr;
    assert(c.preview_cycle(readonly,P).choice.id==20 && c.cycle(readonly,P)==Result::Unbound);
}
int main() {
    physical_press_feedback();
    queue_feedback_regressions();
    all_weapon_cycle_feedback();
    {
        Native n;auto a=n.adapter();Control first;
        assert(configure(first,a,P,0,20)==Result::Configured);
        assert(configure(first,a,S,2,40)==Result::Configured);
        auto saved=first.assignments();Control reboot;
        assert(reboot.restore_assignments(saved) && reboot.assignments()==saved && n.submissions==0);
        n.entries[1].status=Status::Unavailable;
        assert(reboot.request_slot(a,P,0)==Result::Rejected && n.submissions==0);
        assert(reboot.request_slot(a,S,2)==Result::Queued);
        assert(!reboot.restore_assignments(saved)); // Never overwrite a live request with disk data.
        Control invalid;saved[0][1]=UINT32_MAX;assert(!invalid.restore_assignments(saved));
    }
    {
        Native native; auto adapter=native.adapter(); Control queued;
        assert(configure(queued,adapter,P,0,10)==Result::Configured);
        assert(configure(queued,adapter,P,1,20)==Result::Configured);
        assert(configure(queued,adapter,P,2,0)==Result::Configured);
        native.gates[0]=Boundary::Attack;
        assert(queued.cycle(adapter,P)==Result::Queued);
        assert(queued.pending_choice(P).assigned && queued.pending_choice(P).id==20);
        assert(queued.cycle(adapter,P)==Result::Queued);
        assert(queued.pending_choice(P).assigned && queued.pending_choice(P).id==0);
        const auto preview=queued.menu(adapter,nullptr,0);
        assert(preview.channels[0].desired.slot==2 && preview.channels[0].pending_id==0);
        assert(native.selected[0]==10); // Preview changes; actual equip waits.
        for (unsigned i=0;i<3;++i) assert(queued.pump(adapter,P)==Result::Waiting);
        assert(native.writes.empty());
        native.gates[0]=Boundary::Legal;
        assert(queued.pump(adapter,P)==Result::Submitted);
        assert(native.writes.size()==1 && native.writes[0].id==0); // Skip intermediate weapon2.
        assert(!queued.pending_choice(P).assigned);
        const auto request=native.writes.back(); native.selected[0]=request.id;
        native.terminal_reply={Terminal::EquippedObserved,P,request.id,request.ticket,adapter.world_epoch};
        assert(queued.pump(adapter,P)==Result::Confirmed);
        assert(queued.pump(adapter,P)==Result::NoChange && native.writes.size()==1);
    }
    Native n;
    auto a = n.adapter();
    Control c;
    assert(configure(c, {}, P, 0, 20) == Result::Unbound);
    assert(n.validations == 0 && n.submissions == 0);
    for (unsigned i = 0; i < kSlotCount; ++i) {
        assert(configure(c, a, P, i, i % 2 ? 20 : 10) == Result::Configured);
        assert(configure(c, a, S, i, i % 2 ? 40 : 30) == Result::Configured);
    }
    assert(configure(c, a, P, 2, 0) == Result::Configured);
    assert(configure(c, a, P, 3, 20) == Result::Rejected);
    assert(configure(c, a, P, 0, 30) == Result::Rejected);
    auto m = c.menu(a, nullptr, 0);
    for (unsigned i = 0; i < kSlotCount; ++i) {
        assert(m.channels[0].slots[i].slot.assigned && m.channels[1].slots[i].slot.assigned);
    }
    assert(m.channels[0].slots[2].slot.id == 0);
    assert(c.request_slot(a, P, 1) == Result::Queued);
    assert(c.request_slot(a, S, 1) == Result::Queued);
    assert(c.request_slot(a, P, 0) == Result::NoChange);
    assert(c.pump(a, P) == Result::NoChange);
    assert(c.request_slot(a, P, 1) == Result::Queued);
    assert(c.request_slot(a, P, 2) == Result::Queued);
    n.gates = {Boundary::Attack, Boundary::Charge};
    for (unsigned i = 0; i < 20; ++i) {
        auto r = c.pump_all(a);
        assert(r[0] == Result::Waiting && r[1] == Result::Waiting);
    }
    assert(n.submissions == 0);
    n.gates = {Boundary::Legal, Boundary::Release};
    assert(c.pump(a, P) == Result::Submitted);
    assert(n.writes.size() == 1 && n.writes.back().id == 0);
    auto first = n.writes.back();
    assert(c.request_slot(a, P, 1) == Result::Queued);
    assert(c.pump(a, P) == Result::Waiting && n.writes.size() == 1);
    assert(c.confirm(a, {P, 0, first.ticket + 1, 1, true}) == Result::Rejected);
    complete(c, n, a, first);
    assert(c.pump(a, P) == Result::Submitted);
    complete(c, n, a, n.writes.back());
    n.gates[1] = Boundary::Recovery;
    assert(c.pump(a, S) == Result::Waiting);
    n.gates[1] = Boundary::Legal;
    assert(c.pump(a, S) == Result::Submitted);
    assert(n.writes.back().channel == S && n.writes.back().id == 40);
    complete(c, n, a, n.writes.back());
    Native revoked;
    auto live = revoked.adapter();
    Control other;
    assert(configure(other, live, P, 0, 20) == Result::Configured);
    assert(other.request_slot(live, P, 0) == Result::Queued);
    revoked.entries[1].status = Status::Unavailable;
    assert(other.pump(live, P) == Result::Rejected && revoked.writes.empty());
    m = other.menu(live, nullptr, 0);
    assert(m.channels[0].slots[0].slot.assigned && !m.channels[0].slots[0].enabled);
    revoked.entries[1].status = Status::Available;
    assert(other.request_slot(live, P, 0) == Result::Queued);
    revoked.entries[1].status = Status::Unknown;
    assert(other.pump(live, P) == Result::Waiting && revoked.submissions == 0);
    revoked.entries[1].status = Status::Available;
    revoked.next = Submission::Busy;
    assert(other.pump(live, P) == Result::Waiting && revoked.writes.empty());
    revoked.next = Submission::Submitted;
    assert(other.pump(live, P) == Result::Submitted);
    auto obsolete = revoked.writes.back();
    auto fresh = revoked.adapter(2);
    assert(other.confirm(fresh, {P, 20, obsolete.ticket, 1, true}) == Result::Rejected);
    assert(other.pump(fresh, P) == Result::NoChange);
    m = other.menu(fresh, nullptr, 0);
    assert(m.channels[0].slots[0].slot.id == 20);
    assert(other.on_menu_event(fresh, {Action::Clear, P, 0, 0, 1}) == Result::Rejected);
    revoked.entries[1].status = Status::Stale;
    assert(other.request_slot(fresh, P, 0) == Result::Rejected);
    auto absent = fresh;
    absent.owner_scope_current = false;
    auto before = revoked.validations;
    assert(other.pump(absent, P) == Result::Unbound && revoked.validations == before);
    absent = fresh;
    absent.catalogue_epoch = 0;
    assert(other.request_slot(absent, P, 0) == Result::Unbound);
    { // A different save preserves slot intent, but uses that save's ownership.
        Native reload;Control choices;auto original=reload.adapter();
        assert(configure(choices,original,P,0,10)==Result::Configured);
        assert(configure(choices,original,P,1,20)==Result::Configured);
        assert(choices.cycle(original,P)==Result::Queued);
        reload.entries[1].status=Status::Unavailable;
        auto loaded=reload.adapter(2);
        assert(choices.cycle(loaded,P)==Result::NoChange);
        auto view=choices.menu(loaded,nullptr,0);
        assert(view.channels[0].slots[1].slot.id==20 && !view.channels[0].slots[1].enabled);
        reload.entries[1].status=Status::Available;
        assert(choices.cycle(loaded,P)==Result::Queued);
        assert(choices.desired_choice(loaded,P).id==20);
    }
    terminal_regressions();
    puts("PASS: loadout control and terminal recovery; native adapter is a test double");
}
