// Executable finisher binding: chooser detour + REAL native sub-step calls (by pinned RVA) + evidence table loader + graph discovery
// logging + explicit feature status. Separate combat header; the loader (root) only registers the hook and calls init()/tick().
//
// A shortcut substitutes the first finisher entry only AFTER the original
// chooser accepts a real native attack. Its own native legality and window
// still apply. Direct direction reads borrow the actual current actor rows.
// Unselected forms queue native selection within the current verified graph
// loan, without a fixed wait. Graph/input hooks translate a genuine
// shortcut press in local copies and preserve the native authored commit.
#pragma once
#include "feature_status.h"
#include "graph_probe.h"
#include "native_pins.h"
#include "finisher_chooser_scope.h"
#include "loadout_chain_switch.h"
#include "step_binding.h"

namespace finisher_binding {
using combat_mod::Finisher;
using combat_native::binding;
using feature_status::Event;
using feature_status::Feature;
using feature_status::State;

constexpr uint32_t kRvaChooser = 0x239ec20;
using ChooserFn = uint32_t (*)(void*, uint8_t, void*, void*, void*, void*, void*, void*, void*, void*);
using InputFn = uint8_t* (*)(void*, void*, void*, void*);
using GraphFn = void (*)(void*, void*, void*, void*);
inline InputFn input_original=nullptr;
inline GraphFn graph_original=nullptr;
using CommitObserver=void (*)(void*,void*,void*,std::uint16_t,bool);
inline CommitObserver commit_observer=nullptr;
inline void set_input_original(InputFn f) { input_original=f; }
inline void set_graph_original(GraphFn f) { graph_original=f; }

inline uint64_t& image_base() { static uint64_t b = 0; return b; }
template <class F> inline F fn(uint32_t rva) { return reinterpret_cast<F>(static_cast<uintptr_t>(image_base() + rva)); }
#ifdef _WIN32
using UnlockFn=uint8_t (*)(int32_t,void*,void*);
inline UnlockFn original_unlock=nullptr;
inline uint8_t test_unlock_hook(int32_t id,void* mud,void* graph) {
    const DWORD incoming=GetLastError();
    if (finisher_test_mode::active() && finisher_chooser_scope::graph_live()) {
        const auto& a=finisher_chooser_scope::graph_current->args;
        if (mud==a.unlock_data && graph==a.graph) {
            auto& b=binding(); uint8_t c[combat_mod::rows::kMinCombo]{},cfg[0x30]{},node[combat_native::graph::kNode]{};
            if (combat_native::copy_checked(b.copy,c,reinterpret_cast<uintptr_t>(a.combo_data),sizeof c) &&
                combat_native::copy_checked(b.copy,cfg,reinterpret_cast<uintptr_t>(a.combo_cfg),sizeof cfg)) {
                const auto count=combat_native::graph::u32(c,8);
                if (count<=combat_native::graph::kMaxIndex) for (uint32_t i=0;i<count;++i) {
                    if (!combat_native::graph::node_at(b.copy,combat_native::graph::u64(c,0),i,node)) break;
                    if (combat_native::graph::u32(node,0x10)!=static_cast<uint32_t>(id)) continue;
                    uint32_t form=0,category=0;
                    if (combat_native::graph::form_def(b.copy,combat_native::graph::u64(c,0),combat_native::graph::u64(cfg,8),node,&form,&category) &&
                        category==2 && finisher_test_mode::known(form)) { SetLastError(incoming); return 1; }
                    break;
                }
            }
        }
    }
    SetLastError(incoming); return original_unlock ? original_unlock(id,mud,graph) : 0;
}
#endif

// ---- REAL native sub-steps (each verified by native_pins at init; only installed when every pin matches) ----
inline bool op_unlock_gate(uint32_t id, void* mud, void* g) { return fn<uint8_t (*)(int32_t, void*, void*)>(0x2379b30)(static_cast<int32_t>(id), mud, g) != 0; }
inline bool op_state_blocked(void* node, uint64_t mask, void* st, void* cond) { return fn<uint8_t (*)(void*, uint64_t, void*, void*)>(0x239e180)(node, mask, st, cond) != 0; }
inline bool op_condition(uint32_t id, void* node, void* cond) { return (fn<uint64_t (*)(uint32_t, uint8_t, uint8_t, void*, void*)>(0x239ce60)(id, 0, 1, node, cond) & 0xff) != 0; }
inline bool op_input_ok(void* node, void* combo, void* cfg) {
    alignas(16) uint8_t out[16] = {};
    const uint8_t* r = (input_original ? input_original : fn<InputFn>(0x239dcf0))(out, node, combo, cfg);
    uint8_t v = 0;
    return r && combat_native::copy_checked(binding().copy, &v, reinterpret_cast<uintptr_t>(r) + 1, 1) && v != 0;
}
inline bool op_post_ok(void* node, void* cond, void* cfg) { return (fn<uint8_t (*)(uint64_t, void*, void*, void*)>(0x239e030)(1, node, cond, cfg) & 0xff) != 0; }
inline bool op_anim_index(void* anim, uint32_t hash, uint16_t* index) {
    uint16_t out = 0xffff;
    const uint16_t* r = fn<uint16_t* (*)(void*, uint16_t*, uint32_t)>(0x2e4ab70)(anim, &out, hash);
    uint16_t v = 0xffff;
    if (!r || !combat_native::copy_checked(binding().copy, &v, reinterpret_cast<uintptr_t>(r), 2)) return false;
    *index = v;
    return true;
}
inline combat_native::NativeOps real_ops() {
    combat_native::NativeOps o;
    o.unlock_gate = op_unlock_gate; o.state_blocked = op_state_blocked; o.condition = op_condition;
    o.input_ok = op_input_ok; o.post_ok = op_post_ok; o.anim_index = op_anim_index;
    return o;
}

// ---- evidence table config (text; one entry per line): finisher=UppercutSurge form=4242 node_id=105 node_index=5 ----
inline bool name_to_finisher(const char* s, size_t n, Finisher* out) {
    struct E { const char* name; Finisher f; };
    static const E tab[] = {{"UppercutSurge", Finisher::UppercutSurge}, {"SplitPivot", Finisher::SplitPivot}, {"SplitOrbit", Finisher::SplitOrbit},
                            {"ConcussiveBeatdown", Finisher::ConcussiveBeatdown}, {"WideEdge", Finisher::WideEdge}, {"SpinEdge", Finisher::SpinEdge}};
    for (const E& e : tab) { size_t l = std::strlen(e.name); if (l == n && std::memcmp(e.name, s, n) == 0) { *out = e.f; return true; } }
    return false;
}
inline bool parse_u32(const char* s, size_t n, uint32_t* out) {
    if (n == 0 || n > 10) return false;
    uint64_t v = 0; size_t i = 0; int base = 10;
    if (n > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; i = 2; }
    for (; i < n; ++i) { const int d = native_pins::hexval(s[i]); if (d < 0 || d >= base) return false; v = v * base + d; if (v > 0xffffffffull) return false; }
    *out = static_cast<uint32_t>(v);
    return true;
}
struct ConfigResult { uint32_t accepted = 0, rejected = 0; };
// Bounded, allocation-free. Lines starting with '#' and blank lines are ignored; a malformed or duplicate line is rejected (counted).
inline ConfigResult parse_config(const char* text, size_t len, combat_native::NodeTable* table) {
    ConfigResult r;
    size_t pos = 0;
    while (pos < len) {
        size_t end = pos; while (end < len && text[end] != '\n') ++end;
        const char* line = text + pos; size_t n = end - pos; pos = end + 1;
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) --n;
        while (n && *line == ' ') { ++line; --n; }
        if (n == 0 || *line == '#') continue;
        combat_native::FinisherEntry e{0, Finisher::None, combat_native::graph::kNone, combat_native::graph::kNone};
        bool have_f = false, have_form = false, have_id = false, have_idx = false, ok = true;
        size_t i = 0;
        while (i < n && ok) {
            while (i < n && line[i] == ' ') ++i;
            size_t k0 = i; while (i < n && line[i] != '=' && line[i] != ' ') ++i;
            if (i >= n || line[i] != '=') { ok = false; break; }
            const char* key = line + k0; const size_t kl = i - k0; ++i;
            size_t v0 = i; while (i < n && line[i] != ' ') ++i;
            const char* val = line + v0; const size_t vl = i - v0;
            auto is = [&](const char* k) { return std::strlen(k) == kl && std::memcmp(k, key, kl) == 0; };
            if (is("finisher")) have_f = name_to_finisher(val, vl, &e.finisher);
            else if (is("form")) have_form = parse_u32(val, vl, &e.form_def_id);
            else if (is("node_id")) have_id = parse_u32(val, vl, &e.node_id);
            else if (is("node_index")) have_idx = parse_u32(val, vl, &e.node_index);
            else ok = false;
        }
        if (ok && have_f && have_form && have_id && have_idx && table->add(e)) ++r.accepted; else ++r.rejected;
    }
    return r;
}

// ---- state ----
struct State_ {
    std::atomic<bool> installed{false}, streams{false}, discovery{true}, commit_hooks_ready{false};
    ChooserFn orig = nullptr;
    combat_mod::Module module;
    feature_status::LogFn raw_sink = nullptr;
    bool (*accept)(const char*, size_t) = nullptr;       // optional: returns whether the sink queued the line (drops are then retried)
    std::atomic<uint32_t> discovery_left{300};
    graph_probe::Dedupe dedupe;
    std::atomic_flag dedupe_lock;
    std::atomic_flag line_lock;
    std::atomic<uint32_t> seq{0};
    using DirectionReader = bool (*)(const combat_mod::ChooserArgs&, combat_mod::DirectionSample*);
    std::atomic<DirectionReader> direction_reader{nullptr}, applied_reader{nullptr};
};
inline State_& st() { static State_ s; return s; }
#ifdef _WIN32
inline uint32_t chain_choose(const combat_mod::ChooserArgs& a) {
    const auto f=st().orig;
    return f ? f(a.node,a.phase_is_2,a.combo_data,a.conditions,a.loadout,a.unlock_data,a.state_set,a.combo_cfg,a.graph,a.anim_out) : UINT32_MAX;
}
inline uint16_t chain_input(void* node,void* combo,void* cfg) {
    if (!input_original) return 0;
    alignas(16) uint8_t out[16]{};
    const auto* result=input_original(out,node,combo,cfg);
    uint16_t word=0;
    return result && combat_native::copy_checked(binding().copy,&word,reinterpret_cast<uintptr_t>(result),2) ? word : 0;
}
#endif
inline void set_original(ChooserFn f) {
    st().orig = f;
#ifdef _WIN32
    loadout_chain_switch::choose=&chain_choose;
    loadout_chain_switch::input=&chain_input;
#endif
}

// A genuine native chooser acceptance in this invocation, never an update/frame
// stamp. Nested calls restore their own acceptance when they return.
inline thread_local bool native_attack_accepted=false;
inline thread_local bool native_finisher_continuation=false;
using FormActivator=bool (*)(const combat_mod::ChooserArgs&,uint32_t);
inline FormActivator activate_form=nullptr;
inline void set_form_activator(FormActivator fn){activate_form=fn;}
inline bool chooser_live(const combat_mod::ChooserArgs& a) {
#ifdef _WIN32
    return finisher_chooser_scope::live(a);
#else
    (void)a; return false;
#endif
}
using AttackStartObserver = void (*)(const combat_mod::ChooserArgs&,uint32_t,const combat_mod::DirectionSample&);
inline std::atomic<AttackStartObserver> attack_start_observer{nullptr};
inline void set_attack_start_observer(AttackStartObserver observer) { attack_start_observer.store(observer); }
inline thread_local combat_mod::DirectionSample before_turn_direction{};
inline thread_local bool have_before_turn_direction=false;
inline bool read_current_direction(const combat_mod::ChooserArgs& a, combat_mod::DirectionSample* out) {
    const auto reader=st().direction_reader.load();
    if (reader && chooser_live(a) && have_before_turn_direction) { *out=before_turn_direction; return true; }
    if (!reader || !chooser_live(a) || !reader(a,out) || !chooser_live(a)) return false;
    before_turn_direction=*out; have_before_turn_direction=true;
    return true;
}
inline bool read_shortcut_attack(const combat_mod::ChooserArgs& a, combat_mod::Attack* out) {
    if (!native_attack_accepted || !chooser_live(a)) return false;
    uint8_t combo[combat_mod::rows::kMinCombo], node[combat_native::graph::kNode], cfg[0x30];
    auto& b=binding();
    if (!combat_native::copy_checked(b.copy,combo,reinterpret_cast<uintptr_t>(a.combo_data),sizeof combo) ||
        !combat_native::copy_checked(b.copy,cfg,reinterpret_cast<uintptr_t>(a.combo_cfg),sizeof cfg)) return false;
    const auto current=combat_mod::rows::combo_current_node(combo);
    if (current!=combat_native::graph::kNone) {
        uint32_t form=0,category=0;
        if (!combat_native::graph::node_at(b.copy,combat_native::graph::u64(combo,0),current,node)) return false;
        if (combat_native::graph::u32(node,0x24)!=combat_native::graph::kNone) {
            if (!combat_native::graph::form_def(b.copy,combat_native::graph::u64(combo,0),combat_native::graph::u64(cfg,8),node,&form,&category)) return false;
            if (category==2 && native_finisher_continuation) return false;
        }
    }
    const bool primary=combat_mod::rows::combo_input_kind(combo,0),secondary=combat_mod::rows::combo_input_kind(combo,2);
    if (primary==secondary) return false;
    *out=primary ? combat_mod::Attack::Primary : combat_mod::Attack::Secondary;
    return chooser_live(a);
}
inline std::array<std::atomic<uint64_t>,7> shortcut_rejections{};
inline std::atomic<uint32_t> shortcut_last_node{UINT32_MAX},shortcut_last_arg{0};
inline bool shortcut_reject([[maybe_unused]] unsigned stage) { CRMOD_DIAGNOSTIC(++shortcut_rejections[stage]); return false; }
inline void flush_shortcut_diagnostics(FILE* log) {
    if (!log) return;
    static uint32_t next=0; static unsigned budget=256;
    const auto now=binding().clock ? binding().clock() : 0;
    if (!budget || static_cast<int32_t>(now-next)<0) return;
    next=now+100;
    std::array<uint64_t,7> counts{}; bool any=false;
    for (unsigned i=0;i<counts.size();++i) { counts[i]=shortcut_rejections[i].exchange(0); any=any || counts[i]!=0; }
    if (!any) return;
    --budget;
    std::fprintf(log,"{\"event\":\"finisher_shortcut_rejected\",\"lookup\":%llu,\"locked\":%llu,\"not_selected\":%llu,\"input\":%llu,\"entry\":%llu,\"window\":%llu,\"native_legal\":%llu,\"node\":%u,\"entry_arg\":%u}\n",
        static_cast<unsigned long long>(counts[0]),static_cast<unsigned long long>(counts[1]),static_cast<unsigned long long>(counts[2]),
        static_cast<unsigned long long>(counts[3]),static_cast<unsigned long long>(counts[4]),static_cast<unsigned long long>(counts[5]),
        static_cast<unsigned long long>(counts[6]),shortcut_last_node.load(),shortcut_last_arg.load());
}
inline bool read_shortcut_node(const combat_mod::ChooserArgs& a, Finisher f, uint32_t* out) {
    uint32_t form=0,idx=combat_native::graph::kNone;
#ifdef _WIN32
    uint64_t airborne_row=0;uint8_t airborne=0;
    if(!chooser_live(a) || !finisher_chooser_scope::live() ||
        !step_binding::resolve_player_column(finisher_chooser_scope::current->world,finisher_chooser_scope::current->actor,
            step_binding::kColAirborne,0xc,&airborne_row) ||
        !combat_native::copy_checked(binding().copy,&airborne,airborne_row+8,1) || airborne>1)return shortcut_reject(0);
#else
    const uint8_t airborne=0;
#endif
    if (!native_attack_accepted || !chooser_live(a) ||
        !combat_native::find_finisher_node(a,f,&idx,&form,false,airborne!=0)) return shortcut_reject(0);
#ifdef _WIN32
    if (!finisher_test_mode::active() && !finisher_chooser_scope::acquired(form)) return shortcut_reject(1);
    if (!finisher_chooser_scope::graph_live() || finisher_chooser_scope::loadout_borrow.local!=a.loadout) return shortcut_reject(2);
#else
    return shortcut_reject(1);
#endif
    *out=idx; return chooser_live(a);
}
// Translate only the genuine fresh press into the finisher form's authored
// native press kind, in a stack copy. No game input flags are changed.
inline bool shortcut_input(const combat_mod::ChooserArgs& a,uint32_t node_idx,uint8_t* combo,uint8_t& authored,bool& mapped) {
    auto& b=binding(); uint8_t cfg[0x30],node[combat_native::graph::kNode],form[combat_native::graph::kNode],def[combat_native::graph::kDef];
    if (!combat_native::copy_checked(b.copy,combo,reinterpret_cast<uintptr_t>(a.combo_data),combat_mod::rows::kMinCombo) ||
        !combat_native::copy_checked(b.copy,cfg,reinterpret_cast<uintptr_t>(a.combo_cfg),sizeof cfg)) return false;
    const auto nodes=combat_native::graph::u64(combo,0),defs=combat_native::graph::u64(cfg,8);
    if (!combat_native::graph::node_at(b.copy,nodes,node_idx,node) ||
        !combat_native::graph::node_at(b.copy,nodes,combat_native::graph::u32(node,0x24),form) ||
        !combat_native::copy_checked(b.copy,def,combat_native::at(defs,combat_native::graph::u32(form,0x2c),combat_native::graph::kDef),sizeof def) ||
        combat_native::graph::u32(def,0x70)!=2) return false;
    const bool primary=combo[0x28]!=0,secondary=combo[0x2a]!=0;
    if (primary==secondary) return false;
    const uint8_t source=primary ? 0 : 2;
    const uint32_t count=combat_native::graph::u32(def,0x80);
    uint8_t requirements[combat_native::graph::kMaxList*4]{};
    if (!count || count>combat_native::graph::kMaxList ||
        !combat_native::copy_checked(b.copy,requirements,combat_native::graph::u64(def,0x78),count*4)) return false;
    authored=0xff;
    for (uint32_t i=0;i<count;++i) {
        const auto kind=requirements[i*4];
        if (kind>3) return false;
        if (kind==source) { authored=source; break; }
        if ((kind==0 || kind==2) && authored==0xff) authored=kind;
    }
    if (authored==0xff) return false;
    mapped=authored!=source;
    if (mapped) {
#ifdef _WIN32
        if (!st().commit_hooks_ready.load() || !input_original || !graph_original || !finisher_chooser_scope::graph_live() ||
            finisher_chooser_scope::graph_current->args.combo_data!=a.combo_data ||
            finisher_chooser_scope::graph_current->args.combo_cfg!=a.combo_cfg ||
            finisher_chooser_scope::graph_current->args.graph!=a.graph) return false;
#else
        return false;
#endif
        const auto pressed=combo[0x28+source]; combo[0x28]=0; combo[0x2a]=0; combo[0x28+authored]=pressed;
    }
    return true;
}
inline bool read_shortcut_legal(const combat_mod::ChooserArgs& a,uint32_t node) {
    auto& b=binding();
    uint8_t combo[combat_mod::rows::kMinCombo]; uint8_t authored=0; bool mapped=false;
    CRMOD_DIAGNOSTIC(shortcut_last_node.store(node));
    if (!native_attack_accepted || !chooser_live(a) || !shortcut_input(a,node,combo,authored,mapped)) return shortcut_reject(3);
    auto local=a; if (mapped) local.combo_data=combo;
    uint32_t loadout[4]{},form=0,category=0;uint8_t cfg[0x30]{},candidate[combat_native::graph::kNode]{};
    if(!combat_native::copy_checked(b.copy,loadout,reinterpret_cast<uintptr_t>(a.loadout),sizeof loadout) ||
        !combat_native::copy_checked(b.copy,cfg,reinterpret_cast<uintptr_t>(a.combo_cfg),sizeof cfg) ||
        !combat_native::graph::node_at(b.copy,combat_native::graph::u64(combo,0),node,candidate) ||
        !combat_native::graph::form_def(b.copy,combat_native::graph::u64(combo,0),combat_native::graph::u64(cfg,8),candidate,&form,&category) || category!=2) return shortcut_reject(2);
    loadout[2]=form;local.loadout=loadout;
    uint32_t connection_arg=0;
    if (!combat_native::shortcut_entry_arg(local,node,b.copy,connection_arg)) return shortcut_reject(4);
    CRMOD_DIAGNOSTIC(shortcut_last_arg.store(connection_arg));
    if (!combat_native::window_open(local,node,b.copy,b.ops)) return shortcut_reject(5);
    if (!combat_native::legal_node(local,node,b.copy,b.ops,combat_native::SkipRequirement{},connection_arg) || !chooser_live(a)) return shortcut_reject(6);
#ifdef _WIN32
    if (!activate_form || !activate_form(a,form) || !finisher_chooser_scope::select_form(a,form)) return shortcut_reject(2);
    if (mapped) {
        auto& loan=*finisher_chooser_scope::graph_current;
        loan.chosen=node; loan.authored_press=authored; loan.remap=true;
    }
#endif
    return true;
}

inline bool current_finisher(const combat_mod::ChooserArgs& a,uint32_t& form,uint32_t& current) {
    auto& b=binding();uint8_t combo[combat_mod::rows::kMinCombo]{},cfg[0x30]{},node[combat_native::graph::kNode]{};uint32_t category=0;
    if(!chooser_live(a) || !combat_native::copy_checked(b.copy,combo,reinterpret_cast<uintptr_t>(a.combo_data),sizeof combo) ||
        !combat_native::copy_checked(b.copy,cfg,reinterpret_cast<uintptr_t>(a.combo_cfg),sizeof cfg)) return false;
    current=combat_mod::rows::combo_current_node(combo);
    return combat_native::graph::node_at(b.copy,combat_native::graph::u64(combo,0),current,node) &&
        combat_native::graph::form_def(b.copy,combat_native::graph::u64(combo,0),combat_native::graph::u64(cfg,8),node,&form,&category) && category==2;
}
inline uint32_t choose_with_finisher_chain(const combat_mod::ChooserArgs& a,ChooserFn original,uint8_t* decision_combo,bool& copied) {
    copied=false;uint32_t form=0,current=UINT32_MAX;
    if(!current_finisher(a,form,current)) {
        uint32_t local_loadout[4]{};
        if(combat_native::copy_checked(binding().copy,local_loadout,reinterpret_cast<uintptr_t>(a.loadout),sizeof local_loadout)) {
            local_loadout[2]=0;
            return original(a.node,a.phase_is_2,a.combo_data,a.conditions,local_loadout,a.unlock_data,a.state_set,a.combo_cfg,a.graph,a.anim_out);
        }
        return original(a.node,a.phase_is_2,a.combo_data,a.conditions,a.loadout,a.unlock_data,a.state_set,a.combo_cfg,a.graph,a.anim_out);
    }
    auto& b=binding();uint8_t candidate[combat_native::graph::kNode]{};
    combat_native::graph::Conn follow[combat_native::graph::kMaxConn]{};size_t count=0;uint32_t first=UINT32_MAX,air_first=UINT32_MAX;
    for(size_t i=0;i<b.table.size();++i) {const auto* entry=b.table.at(i);if(entry->form_def_id==form){first=entry->node_index;air_first=entry->air_node_index;}}
    if(first!=UINT32_MAX && combat_native::finisher_followups(a,form,first,b.copy,follow,count,air_first)) {
        uint8_t authored=0;bool mapped=false;
        if(count && shortcut_input(a,follow[0].target,decision_combo,authored,mapped) &&
            combat_native::copy_checked(b.copy,candidate,reinterpret_cast<uintptr_t>(a.node),sizeof candidate)) {
            const auto pointer=reinterpret_cast<uintptr_t>(follow);const auto length=static_cast<uint32_t>(count);
            std::memcpy(candidate,&pointer,8);std::memcpy(candidate+8,&length,4);
            const auto chosen=original(candidate,a.phase_is_2,decision_combo,a.conditions,a.loadout,a.unlock_data,a.state_set,a.combo_cfg,a.graph,a.anim_out);
            if(chosen!=UINT32_MAX && chooser_live(a)) {
                native_finisher_continuation=true;copied=true;
                feature_status::registry().event(feature_status::Feature::Finisher,feature_status::Event::Applied,reinterpret_cast<uintptr_t>(a.graph),reinterpret_cast<uintptr_t>(a.combo_data),chosen,kRvaChooser,"native finisher follow-up");
#ifdef _WIN32
                if(mapped && finisher_chooser_scope::graph_live()) {auto& loan=*finisher_chooser_scope::graph_current;loan.chosen=chosen;loan.authored_press=authored;loan.remap=true;}
#endif
                return chosen;
            }
        }
    }
    // No legal native follow-up: return to the physical button's weapon chain.
    // A fresh directional gesture may select another unlocked finisher afterward.
    uint32_t loadout[4]{};
    if(!combat_native::copy_checked(b.copy,loadout,reinterpret_cast<uintptr_t>(a.loadout),sizeof loadout))
        return original(a.node,a.phase_is_2,a.combo_data,a.conditions,a.loadout,a.unlock_data,a.state_set,a.combo_cfg,a.graph,a.anim_out);
    loadout[2]=0;
    return original(a.node,a.phase_is_2,a.combo_data,a.conditions,loadout,a.unlock_data,a.state_set,a.combo_cfg,a.graph,a.anim_out);
}

inline void graph_detour(void* columns,void* graph,void* context,void* delta) {
    if (!graph_original) return;
#ifdef _WIN32
    const DWORD incoming=GetLastError();
    finisher_chooser_scope::GraphScope scope(columns,graph);
    SetLastError(incoming);
    graph_original(scope.forwarded,graph,context,delta);
#else
    graph_original(columns,graph,context,delta);
#endif
}
__attribute__((noinline)) inline uint8_t* input_detour(void* output,void* node,void* combo,void* cfg) {
    if (!input_original) return nullptr;
#ifdef _WIN32
    const auto caller=reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const DWORD incoming=GetLastError();
    const auto forward=[&](void* input_combo) {
        SetLastError(incoming);
        auto* result=input_original(output,node,input_combo,cfg);
        const DWORD forwarded=GetLastError();
        // Only this graph call occurs AFTER current-node assignment. Chooser
        // probes and phase checks call the same helper but must never count.
        if (commit_observer && caller==image_base()+0x239f5c8) {
            std::uint16_t accepted=0;std::uint8_t physical[combat_mod::rows::kMinCombo]{};
            if(result&&combat_native::copy_checked(binding().copy,&accepted,reinterpret_cast<uintptr_t>(result),2)&&
                combat_native::copy_checked(binding().copy,physical,reinterpret_cast<uintptr_t>(combo),sizeof physical)) {
                const auto kind=accepted&0xff;
                const bool mapped=input_combo!=combo;
                const bool direct=(accepted>>8)&&((kind==0||kind==2)&&
                    (mapped?(bool(physical[0x28])!=bool(physical[0x2a])):physical[0x28+kind]!=0));
                commit_observer(node,combo,cfg,accepted,direct);
            }
        }
        SetLastError(forwarded);return result;
    };
    auto* loan=finisher_chooser_scope::graph_current;
    uint8_t local[combat_mod::rows::kMinCombo]; uint8_t authored=0; bool mapped=false;
    if (loan && loan->remap && finisher_chooser_scope::graph_live() && combo==loan->args.combo_data && cfg==loan->args.combo_cfg) {
        auto a=loan->args; a.combo_data=combo; a.combo_cfg=cfg;
        if (shortcut_input(a,loan->chosen,local,authored,mapped) && mapped && authored==loan->authored_press &&
            combat_mod::rows::combo_current_node(local)==loan->chosen &&
            reinterpret_cast<uintptr_t>(node)==combat_native::at(combat_native::graph::u64(local,0),loan->chosen,combat_native::graph::kNode) &&
            finisher_chooser_scope::graph_live()) {
            loan->remap=false;
            SetLastError(incoming);
            // Native d2b0 writes the caller's REAL output, including the
            // authored action kind. Native graph code copies that u16 to C+24.
            return forward(local);
        }
    }
    return forward(combo);
#else
    return input_original(output,node,combo,cfg);
#endif
}

// Recomputes the prerequisite list and the feature state; installs/clears the Module readers accordingly. Call after any change and
// from a slow tick. Returns true when ACTIVE.
inline bool refresh_status() {
    auto& b = binding(); auto& s = st(); auto& reg = feature_status::registry();
    char miss[160]{};bool complete=true;
    auto add = [&](const char* m) { complete=false;(void)m;CRMOD_DIAGNOSTIC(std::snprintf(miss + std::strlen(miss), sizeof miss - std::strlen(miss), "%s%s", miss[0] ? "," : "", m)); };
    if (!s.installed.load()) add("not_initialised");
    if (b.table.size() == 0) add("node_table_empty");
    const auto reader=s.direction_reader.load();
    if (!reader) add("current_chooser_direction_reader_missing");
    if (!s.commit_hooks_ready.load() || !input_original || !graph_original) add("graph_input_commit_hooks_missing");
    if (!b.ops.complete()) add("native_legality_missing");
    if (!b.get_last_error || !b.set_last_error) add("lasterror_pair_missing");
    if (!b.clock) add("clock_missing");
    if (complete) {
        b.skip = combat_native::SkipRequirement{};               // exact native legality
        if (s.applied_reader.load()!=reader || !s.module.finishers_enabled()) {
            combat_mod::Readers readers;
            readers.direction_sample=read_current_direction; readers.attack_press=read_shortcut_attack;
            readers.finisher_node=read_shortcut_node; readers.legal_without_step_requirement=read_shortcut_legal;
            s.module.set_readers(readers); s.applied_reader.store(reader);
            s.module.enable_finishers(true);
        }
        reg.set_state(Feature::Finisher, State::Active, "");
        return true;
    }
    if (s.module.finishers_enabled()) s.module.enable_finishers(false);
    reg.set_state(Feature::Finisher, s.installed.load() ? State::Unbound : State::Disabled, miss);
    return false;
}
inline void set_direction_reader(State_::DirectionReader reader) {
    st().direction_reader.store(reader); refresh_status();
}
inline void set_commit_hooks_ready(bool ready) {
    st().commit_hooks_ready.store(ready); refresh_status();
}

// Verifies every pin the finisher path calls or hooks, installs the real native ops, publishes the status. Returns false (feature
// Disabled with the failing pin name) on any mismatch: the detour then never runs.
inline bool init(uint64_t base, combat_native::CopyFn guarded_copy, combat_native::ClockFn clock, feature_status::LogFn sink) {
    auto& reg = feature_status::registry(); auto& s = st(); auto& b = binding();
    reg.set_sink(sink); s.raw_sink = sink;
    image_base() = base; b.copy = guarded_copy; b.clock = clock;
    static const char* const names[] = {"chooser", "legality", "unlock_gate", "state_blocked", "condition", "input_ok", "post_ok", "anim_index"};
    const char* bad = nullptr;
    if (!native_pins::verify_set(guarded_copy, base, names, sizeof names / sizeof names[0], &bad)) {
        const char* reason=nullptr;
#if CRMOD_DIAGNOSTICS
        char why[96]; std::snprintf(why, sizeof why, "pin_mismatch:%s", bad ? bad : "?"); reason=why;
#endif
        reg.set_state(Feature::Finisher, State::Disabled, reason);
        return false;
    }
    b.ops = real_ops();
    // Human-labelled native first entries, cb40-cb46. Node and definition IDs
    // are checked against the current native graph before every dispatch.
    // Follow-up finisher entries remain native, driven by further clicks.
    if (b.table.size()==0) {
        b.table.add({2848499412u,Finisher::UppercutSurge,53223285u,66u,3856553470u,65u});
        b.table.add({2276994531u,Finisher::SplitPivot,3901327294u,69u,728398563u,71u});
        b.table.add({3260945180u,Finisher::SplitOrbit,4128024157u,75u,2797882438u,73u});
        b.table.add({2922085471u,Finisher::ConcussiveBeatdown,2765642394u,64u,1621489903u,63u});
        b.table.add({3896273609u,Finisher::WideEdge,3019650184u,67u,210243573u,68u});
        b.table.add({4201352106u,Finisher::SpinEdge,3168978815u,56u,2970540376u,55u});
    }
    s.installed.store(true);
    refresh_status();
    return true;
}
inline combat_native::NodeTable& table() { return binding().table; }
inline void set_streams_hooked(bool on) { st().streams.store(on); refresh_status(); }

inline void set_raw_accept(bool (*accept)(const char*, size_t)) { st().accept = accept; }
inline bool emit_raw(State_& s, const char* line, size_t n) {
    if (s.accept) return s.accept(line, n);
    s.raw_sink(line, n);                                                                   // legacy void sink: acceptance unknown, assumed
    return true;
}
// One G record per chooser call at most. The dedupe entry and the budget are consumed only after the sink ACCEPTED the line, so a
// full queue (root keeps 16 slots) never loses a record silently: the same state is simply captured again on the next call.
inline void discover(const combat_mod::ChooserArgs& a, uint32_t ret) {
    State_& s = st();
    if (!s.discovery.load(std::memory_order_relaxed) || s.discovery_left.load(std::memory_order_relaxed) == 0 || !s.raw_sink) return;
    graph_probe::Record r;
    if (!graph_probe::capture(binding().copy, a, ret, combat_native::Phase::Post, s.seq.fetch_add(1) + 1, &r)) return;
    if (s.dedupe_lock.test_and_set(std::memory_order_acquire)) return;                      // contended: skip, never wait
    const bool seen = s.dedupe.seen(r);
    s.dedupe_lock.clear(std::memory_order_release);
    if (seen) return;
    if (s.line_lock.test_and_set(std::memory_order_acquire)) return;
    static char line[graph_probe::kLineMax];
    const size_t n = graph_probe::format_line(r, line, sizeof line);
    if (n) {
        uint32_t lo[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};              // the equipped-definition ints: ties each G line to a weapon
        combat_native::copy_checked(binding().copy, lo, reinterpret_cast<uintptr_t>(a.loadout), sizeof lo);
        char pre[96];
        const int pn = std::snprintf(pre, sizeof pre, "L %u %u %u %u %u\n", r.seq, lo[0], lo[1], lo[2], lo[3]);   // companion line, same seq as the G line
        if (pn > 0 && emit_raw(s, pre, static_cast<size_t>(pn)) && emit_raw(s, line, n)) {
            s.discovery_left.fetch_sub(1, std::memory_order_relaxed);
            if (!s.dedupe_lock.test_and_set(std::memory_order_acquire)) {                   // try once, never spin: a busy lock only
                s.dedupe.mark(r);                                                           // means a duplicate line may be logged later
                s.dedupe_lock.clear(std::memory_order_release);
            }
        }
    }
    s.line_lock.clear(std::memory_order_release);
}

// ---- chronological chooser history (naga #30862) ----
// discover() only logs NEW graph neighbourhoods, so repeated nodes and the order of real actions are lost. This ring records EVERY
// accepted chooser result in order, plus sampled refused context, with
// QPC, call number, current node, phase, candidate node (+ index), the native result and the result node. It reuses the worker flush
// (no new hook, no sink queue): the detour only pushes into a bounded ring under a try-lock; `flush_history` is called by the worker.
// ret != 0xffffffff is native chooser acceptance, not proof an attack executed.
using QpcFn = uint64_t (*)();
using PlayerRowFn = int (*)(uint64_t combo_row);              // 0 = unknown, 1 = the Player's ComboData row, 2 = another entity
constexpr size_t kHistCap=512;
constexpr uint32_t kHistAcceptedBudget=65536, kHistRefusedBudget=8192;
constexpr uint32_t kHistRefusedIntervalMs=250;
struct RefusedRow {
    uint64_t row=0;
    uint32_t last_ms=0,current=0,phase=0;
    uint8_t have_c=0,p2=0;
    bool used=false;
};
struct HistRec {
    uint32_t seq = 0, ms = 0, ret = 0, node_idx = 0xffffffffu, repeats = 0; uint64_t qpc = 0, last_qpc = 0, node_ptr = 0, c_ptr = 0, nodes_base = 0;
    uint8_t p2 = 0, player = 0, have_c = 0, have_node = 0, have_ret = 0;
    uint8_t c[0x48] = {}, node[0x38] = {}, retnode[0x38] = {}; uint32_t lo[4] = {};
};
struct History {
    HistRec ring[kHistCap]; uint32_t head = 0, tail = 0, last_index = 0xffffffffu;
    std::atomic<uint32_t> dropped{0}, pushed{0};
    std::atomic<uint32_t> accepted_pushed{0},refused_pushed{0};
    std::atomic<uint32_t> accepted_dropped{0},refused_sampled_out{0};
    RefusedRow refused_rows[4]{};
    unsigned refused_cursor=0;
    std::atomic_flag lock; std::atomic<uint32_t> call{0};
    QpcFn qpc = nullptr; PlayerRowFn player = nullptr;
};
inline History& history() { static History h; return h; }
inline void set_history_env(QpcFn qpc, PlayerRowFn player) { history().qpc = qpc; history().player = player; }
inline bool hist_same(const HistRec& a, const HistRec& b) {
    return a.c_ptr == b.c_ptr && a.node_ptr == b.node_ptr && a.ret == b.ret && a.p2 == b.p2 && a.have_c == b.have_c &&
           std::memcmp(a.c + 0x20, b.c + 0x20, 0x14) == 0;                     // current node, u16, input flags, phase, timer
}
inline void history_push(const combat_mod::ChooserArgs& a, uint32_t ret) {
    if constexpr(!diagnostics::enabled)return;
    History& h = history(); auto& b = binding();
    const bool accepted=ret!=0xffffffffu;
    if (!b.copy || (accepted ? h.accepted_pushed.load(std::memory_order_relaxed)>=kHistAcceptedBudget
                            : h.refused_pushed.load(std::memory_order_relaxed)>=kHistRefusedBudget)) return;
    HistRec r;
    r.seq = h.call.fetch_add(1) + 1; r.ms = b.clock ? b.clock() : 0; r.qpc = h.qpc ? h.qpc() : 0; r.last_qpc = r.qpc;
    r.c_ptr = reinterpret_cast<uintptr_t>(a.combo_data); r.node_ptr = reinterpret_cast<uintptr_t>(a.node); r.p2 = a.phase_is_2; r.ret = ret; r.repeats = 1;
    r.player = h.player ? static_cast<uint8_t>(h.player(r.c_ptr)) : 0;
    if (r.player == 2) return;                                                   // another entity's combo row
    r.have_c = combat_native::copy_checked(b.copy, r.c, r.c_ptr, sizeof r.c) ? 1 : 0;
    if (r.have_c) {
        std::memcpy(&r.nodes_base, r.c, 8);
        if (r.nodes_base && r.node_ptr >= r.nodes_base && (r.node_ptr - r.nodes_base) % 0x38 == 0 && (r.node_ptr - r.nodes_base) / 0x38 < 4096)
            r.node_idx = static_cast<uint32_t>((r.node_ptr - r.nodes_base) / 0x38);
        if (r.node_idx != 0xffffffffu) r.have_node = combat_native::copy_checked(b.copy, r.node, r.node_ptr, sizeof r.node) ? 1 : 0;
        if (ret < 4096) r.have_ret = combat_native::copy_checked(b.copy, r.retnode, combat_native::at(r.nodes_base, ret, 0x38), sizeof r.retnode) ? 1 : 0;
    }
    combat_native::copy_checked(b.copy, r.lo, reinterpret_cast<uintptr_t>(a.loadout), sizeof r.lo);
    if (h.lock.test_and_set(std::memory_order_acquire)) {
        ++h.dropped; if (accepted) ++h.accepted_dropped; return;
    }
    auto& counter=accepted ? h.accepted_pushed : h.refused_pushed;
    const uint32_t budget=accepted ? kHistAcceptedBudget : kHistRefusedBudget;
    if (counter.load(std::memory_order_relaxed)>=budget) {
        h.lock.clear(std::memory_order_release); return;
    }
    RefusedRow* sampled=nullptr;
    uint32_t current=0,phase=0;
    if (!accepted) {
        if (r.have_c) { std::memcpy(&current,r.c+0x20,4); std::memcpy(&phase,r.c+0x2c,4); }
        for (auto& row:h.refused_rows)
            if (row.used && row.row==r.c_ptr) { sampled=&row; break; }
        if (!sampled) sampled=&h.refused_rows[h.refused_cursor&3u];
        const bool changed=!sampled->used || sampled->row!=r.c_ptr || sampled->have_c!=r.have_c ||
            sampled->p2!=r.p2 || (r.have_c && (sampled->current!=current || sampled->phase!=phase));
        if (!changed && static_cast<uint32_t>(r.ms-sampled->last_ms)<kHistRefusedIntervalMs) {
            ++h.refused_sampled_out; h.lock.clear(std::memory_order_release); return;
        }
    }
    const uint32_t occupied=h.head-h.tail;
    if (occupied>=kHistCap || (!accepted && occupied>=kHistCap/2)) {
        ++h.dropped; if (accepted) ++h.accepted_dropped;
    } else {
        h.ring[h.head % kHistCap] = r; h.last_index = h.head; ++h.head; ++h.pushed; ++counter;
        if (sampled) {
            if (!sampled->used || sampled->row!=r.c_ptr) ++h.refused_cursor;
            *sampled={r.c_ptr,r.ms,current,phase,r.have_c,r.p2,true};
        }
    }
    h.lock.clear(std::memory_order_release);
}
inline size_t hist_hex(const uint8_t* p, size_t n, char* dst) { static const char d[] = "0123456789abcdef"; for (size_t i = 0; i < n; ++i) { dst[2 * i] = d[p[i] >> 4]; dst[2 * i + 1] = d[p[i] & 15]; } return 2 * n; }
// Worker side: formats and writes up to `max_lines` pending records ("H ..." lines); returns how many were written. A busy lock skips the call.
inline unsigned flush_history(bool (*write)(const char*, size_t, void*), void* ctx, unsigned max_lines) {
    History& h = history(); static HistRec batch[16]; static char line[1024];
    if (max_lines > 16) max_lines = 16;
    if (h.lock.test_and_set(std::memory_order_acquire)) return 0;
    unsigned n = 0, lost = 0;
    while (n < max_lines && h.tail != h.head) { batch[n++] = h.ring[h.tail % kHistCap]; ++h.tail; }
    lost = h.dropped.exchange(0,std::memory_order_relaxed);
    h.lock.clear(std::memory_order_release);
    static bool policy_written=false;
    static unsigned previous_budget_state=0xffffffffu;
    static uint32_t previous_budget_ms=0;
    if (!policy_written) {
        const int o=std::snprintf(line,sizeof line,
            "{\"event\":\"chooser_history_policy\",\"accepted_budget\":%u,\"rejected_budget\":%u,"
            "\"rejected_interval_ms\":%u,\"rejected_row_cache\":4,\"rejected_state_changes_bypass_interval\":true,"
            "\"accepted_records_collapsed\":false,\"ring_capacity\":%u,\"rejected_ring_limit\":%u,\"ring_drops_possible\":true}\n",
            kHistAcceptedBudget,kHistRefusedBudget,kHistRefusedIntervalMs,
            static_cast<unsigned>(kHistCap),static_cast<unsigned>(kHistCap/2));
        if (o>0 && write(line,static_cast<size_t>(o),ctx)) policy_written=true;
    }
    const uint32_t ap=h.accepted_pushed.load(std::memory_order_relaxed),rp=h.refused_pushed.load(std::memory_order_relaxed);
    const bool ae=ap>=kHistAcceptedBudget,re=rp>=kHistRefusedBudget;
    const unsigned budget_state=(ae?1u:0u)|(re?2u:0u);
    const uint32_t now=binding().clock ? binding().clock() : 0;
    if (budget_state!=previous_budget_state || static_cast<uint32_t>(now-previous_budget_ms)>=5000) {
        const int o=std::snprintf(line,sizeof line,
            "{\"event\":\"chooser_history_budget\",\"accepted_remaining\":%u,\"rejected_remaining\":%u,"
            "\"accepted_exhausted\":%s,\"rejected_exhausted\":%s,\"accepted_pushed\":%u,\"rejected_pushed\":%u,"
            "\"accepted_ring_dropped\":%u,\"rejected_sampled_out\":%u}\n",
            ae?0u:kHistAcceptedBudget-ap,re?0u:kHistRefusedBudget-rp,
            ae?"true":"false",re?"true":"false",ap,rp,
            h.accepted_dropped.load(std::memory_order_relaxed),h.refused_sampled_out.load(std::memory_order_relaxed));
        if (o>0 && write(line,static_cast<size_t>(o),ctx)) { previous_budget_state=budget_state; previous_budget_ms=now; }
    }
    for (unsigned i = 0; i < n; ++i) {
        const HistRec& r = batch[i];
        uint32_t cur = 0, st = 0; std::memcpy(&cur, r.c + 0x20, 4); std::memcpy(&st, r.c + 0x2c, 4);
        int o = std::snprintf(line, sizeof line, "H %u qpc=%llu last=%llu ms=%u rep=%u pl=%u p2=%u cur=%x st=%x idx=%x ret=%x node=%llx c=%llx lo=%x,%x,%x,%x C:",
            r.seq, static_cast<unsigned long long>(r.qpc), static_cast<unsigned long long>(r.last_qpc), r.ms, r.repeats, r.player, r.p2, cur, st, r.node_idx, r.ret,
            static_cast<unsigned long long>(r.node_ptr), static_cast<unsigned long long>(r.c_ptr), r.lo[0], r.lo[1], r.lo[2], r.lo[3]);
        if (o <= 0) continue;
        size_t k = static_cast<size_t>(o);
        if (r.have_c) k += hist_hex(r.c, sizeof r.c, line + k); else line[k++] = '-';
        k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " N:"));
        if (r.have_node) k += hist_hex(r.node, sizeof r.node, line + k); else line[k++] = '-';
        k += static_cast<size_t>(std::snprintf(line + k, sizeof line - k, " R:"));
        if (r.have_ret) k += hist_hex(r.retnode, sizeof r.retnode, line + k); else line[k++] = '-';
        line[k++] = '\n'; line[k] = 0;
        if (!write(line, k, ctx)) break;
    }
    if (lost) { const int o = std::snprintf(line, sizeof line, "H dropped=%u\n", lost); if (o > 0) (void)write(line, static_cast<size_t>(o), ctx); }
    return n;
}

// The chooser detour (HOOK RVA 0x239ec20). Exact forward of all ten arguments; fail-open: whenever anything is missing the
// original decides. Never throws (built -fno-exceptions), never blocks.
inline uint32_t chooser_detour(void* node, uint8_t p2, void* c, void* cond, void* load, void* mud, void* state, void* cfg, void* g, void* anim) {
    State_& s = st();
    const ChooserFn orig = s.orig;
    if (!orig) return 0xffffffffu;                                                       // cannot happen once the loader sets the trampoline first
    if (!s.installed.load(std::memory_order_relaxed)) return orig(node, p2, c, cond, load, mud, state, cfg, g, anim);
    const combat_mod::ChooserArgs a{node, p2, c, cond, load, mud, state, cfg, g, anim};
    const auto incoming=binding().get_last_error ? binding().get_last_error() : 0;
#ifdef _WIN32
    finisher_chooser_scope::Scope scope(a);
#endif
    struct RestoreCall {
        bool accepted,have,continuation; combat_mod::DirectionSample direction;
        ~RestoreCall() { native_attack_accepted=accepted; have_before_turn_direction=have; before_turn_direction=direction;native_finisher_continuation=continuation; }
    } restore{native_attack_accepted,have_before_turn_direction,native_finisher_continuation,before_turn_direction};
    native_attack_accepted=false; have_before_turn_direction=false;native_finisher_continuation=false;
    const bool owner_current=chooser_live(a);
    if (owner_current) {
        combat_mod::DirectionSample direction{};
        (void)read_current_direction(a,&direction); // Before native attack-turn/commit, under this real actor loan.
    }
    if (binding().set_last_error) binding().set_last_error(incoming);
    uint8_t decision_combo[combat_mod::rows::kMinCombo]{};bool decision_copied=false;
    const uint32_t native_result=owner_current ? choose_with_finisher_chain(a,orig,decision_combo,decision_copied) : orig(node,p2,c,cond,load,mud,state,cfg,g,anim);
    const auto err=binding().get_last_error ? binding().get_last_error() : 0;
    uint32_t result=native_result;
    uint8_t combo[combat_mod::rows::kMinCombo],accepted_node[combat_native::graph::kNode];
    if (owner_current && chooser_live(a) && native_result!=combat_native::graph::kNone &&
        combat_native::copy_checked(binding().copy,combo,reinterpret_cast<uintptr_t>(c),sizeof combo) &&
        combat_native::graph::node_at(binding().copy,combat_native::graph::u64(combo,0),native_result,accepted_node) &&
        combat_native::graph::u32(accepted_node,0x20)==1 && binding().ops.input_ok &&
        binding().ops.input_ok(reinterpret_cast<void*>(static_cast<uintptr_t>(combat_native::at(combat_native::graph::u64(combo,0),native_result,combat_native::graph::kNode))),decision_copied?decision_combo:c,cfg))
        native_attack_accepted=chooser_live(a);
    auto& reg = feature_status::registry();
    bool chain_handled=false;
#ifdef _WIN32
    if (owner_current) {
        chain_handled=loadout_chain_switch::intercept(a,native_result,native_attack_accepted,result);
        if (chain_handled) native_attack_accepted=result!=UINT32_MAX;
    }
#endif
    if (!chain_handled && owner_current && feature_status::registry().active(Feature::Finisher)) {
        uint32_t chosen = 0xffffffffu;
        const uint32_t now = binding().clock ? binding().clock() : 0;
        if (s.module.on_chooser(a, now, &chosen) && chosen != 0xffffffffu) {
            reg.event(Feature::Finisher, Event::Applied, reinterpret_cast<uintptr_t>(g), reinterpret_cast<uintptr_t>(c), chosen, kRvaChooser, "finisher node returned from chooser");
            result=chosen;
        }
        combat_mod::Attack atk = combat_mod::Attack::None;
        if (diagnostics::enabled && result==native_result && combat_native::read_attack(a, &atk) && atk != combat_mod::Attack::None)
            reg.event(Feature::Finisher, Event::Reject, reinterpret_cast<uintptr_t>(g), reinterpret_cast<uintptr_t>(c), 0, kRvaChooser, "attack pressed: no finisher applicable (direction/node/legality)");
    }
    combat_mod::Attack attack=combat_mod::Attack::None;
    const auto observer=attack_start_observer.load();
    if (observer && native_attack_accepted && have_before_turn_direction && chooser_live(a) &&
        combat_native::read_attack(a,&attack) && attack!=combat_mod::Attack::None)
        observer(a,result,before_turn_direction);
    if constexpr(diagnostics::enabled) { history_push(a,result); discover(a,result); }
    if (binding().set_last_error) binding().set_last_error(err);
    return result;
}
}  // namespace finisher_binding
