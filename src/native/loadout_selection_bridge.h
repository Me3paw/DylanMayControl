#pragma once
#include "loadout_actor_resolver.h"
#include "loadout_native_selection.h"
#include "loadout_player_catalogue.h"
#include "loadout_lua_activation.h"
#include "loadout_chain_switch.h"
#ifdef _WIN32
namespace loadout_selection_bridge {
struct Owner {
    std::uint64_t world=0,actor=0,graph=0;
    std::uint32_t revision=0;
};
inline Owner owner{};
inline std::uint64_t lifetime=0;
inline bool ready=false;
inline std::atomic<bool> enabled{false};
inline std::atomic<std::uint64_t> issued{0},waiting{0},invalid{0};
enum class Stage : unsigned { Off,NoCatalogue,ScopeMismatch,ActorUnavailable,CatalogueUnavailable,Busy,SourceChanged,OwnerFrozen,Ready,QueuedWaiting };
inline std::atomic<Stage> stage{Stage::Off};
inline std::atomic<std::uint64_t> sampled_world{0},sampled_input_a0{0},sampled_input_268{0};
inline std::atomic<std::uint64_t> sampled_outer_word{0},sampled_ui_stacks{0};
struct Frame {
    loadout_actor_resolver::Rows rows{};
    loadout_player_catalogue::Catalogue* source=nullptr;
    const loadout_selection_scope::Borrowed* loan=nullptr;
    std::uint64_t call=0;
    std::uint64_t argument_array=0;
    std::array<std::uint64_t,7> resources{};
    bool chain_permission=false;
    std::uint32_t chain_channel=0,chain_id=0;
};
inline thread_local Frame* current_frame=nullptr;
inline void (*observe_menu)(std::uint64_t)=nullptr;
struct Journal {
    bool valid=false;
    Owner owner{};
    std::uint32_t id=0;
    std::uint8_t category=UINT8_MAX;
    std::uint64_t ticket=0,lifetime=0;
    std::uint64_t dispatch_token=0,lua_arena=0;
    loadout_control::Channel channel=loadout_control::Channel::Primary;
    std::uint32_t other_category_id=0;bool owned_selection=false;
};
inline Journal journal{};
enum class LogKind : unsigned { Submitted,EquipObserved,Rejected,ActorResolution,DispatchObserved,WaitingState,NativePresetConfirmed };
struct LogRecord {
    LogKind kind=LogKind::Rejected;
    Owner owner{};
    std::uint32_t id=0;
    std::uint8_t category=UINT8_MAX;
    std::uint64_t ticket=0,lifetime=0;
    unsigned outcome=0;
    loadout_actor_resolver::Diagnostic diagnostic{};
    loadout_control::Channel channel=loadout_control::Channel::Primary;
    std::uint32_t preset=0,other_preset=0;
    std::uint16_t raw_flags=0;
    bool dispatch_seen=false;
};
inline SRWLOCK log_lock=SRWLOCK_INIT;
inline std::array<LogRecord,64> log_queue{};
inline unsigned log_count=0;
inline std::atomic<std::uint64_t> logs_dropped{0};
inline void record(LogKind kind,std::uint32_t id,std::uint64_t ticket,std::uint8_t category,unsigned outcome=0,
                   loadout_control::Channel channel=loadout_control::Channel::Primary) {
    if constexpr(!diagnostics::enabled)return;
    if (!TryAcquireSRWLockExclusive(&log_lock)) { ++logs_dropped; return; }
    if (log_count==log_queue.size()) ++logs_dropped;
    else {
        LogRecord row{kind,owner,id,category,ticket,lifetime,outcome};
        row.channel=channel; log_queue[log_count++]=row;
    }
    ReleaseSRWLockExclusive(&log_lock);
}
inline void record_wait(const loadout_player_catalogue::Catalogue& source,const Journal& j) {
    if constexpr(!diagnostics::enabled)return;
    if (j.category>=2) return;
    std::uint16_t flags=0;
    for (std::size_t i=0;i<source.count;++i) if (source.items[i].id==j.id) flags=source.items[i].raw_flags;
    const bool dispatched=loadout_lua_activation::observed(j.dispatch_token,source.world,source.actor,source.graph_key,j.lua_arena,j.id);
    static std::uint64_t last_ticket=0; static std::uint32_t last_preset=0,last_other=0;
    static std::uint16_t last_flags=0; static bool last_dispatch=false; static unsigned count=0;
    if (count>=64 || (last_ticket==j.ticket && last_preset==source.loadout[j.category] &&
            last_other==source.loadout[1u-j.category] && last_flags==flags && last_dispatch==dispatched)) return;
    if (!TryAcquireSRWLockExclusive(&log_lock)) return;
    if (log_count<log_queue.size()) {
        LogRecord r{LogKind::WaitingState,owner,j.id,j.category,j.ticket,lifetime,0};
        r.channel=j.channel; r.preset=source.loadout[j.category]; r.other_preset=source.loadout[1u-j.category];
        r.raw_flags=flags; r.dispatch_seen=dispatched; log_queue[log_count++]=r;
        last_ticket=j.ticket; last_preset=r.preset; last_other=r.other_preset; last_flags=flags; last_dispatch=dispatched; ++count;
    }
    ReleaseSRWLockExclusive(&log_lock);
}
inline std::array<loadout_actor_resolver::Diagnostic,32> actor_failures{};
inline unsigned actor_failure_count=0;
inline void record_actor_failure(const loadout_actor_resolver::Diagnostic& d) {
    if constexpr(!diagnostics::enabled)return;
    const DWORD saved=GetLastError();
    if (!TryAcquireSRWLockExclusive(&log_lock)) { SetLastError(saved); return; }
    bool seen=false;
    for (unsigned i=0;i<actor_failure_count;++i) {
        const auto& old=actor_failures[i];
        if (old.reason==d.reason && old.world==d.world && old.actor==d.actor && old.hash==d.hash &&
            old.actual_actor==d.actual_actor && old.entity_count==d.entity_count &&
            old.generation_entry==d.generation_entry && old.tag_count==d.tag_count && old.key_count==d.key_count)
            seen=true;
    }
    if (!seen && actor_failure_count<actor_failures.size() && log_count<log_queue.size()) {
        actor_failures[actor_failure_count++]=d;
        LogRecord r{}; r.kind=LogKind::ActorResolution; r.diagnostic=d; log_queue[log_count++]=r;
    }
    ReleaseSRWLockExclusive(&log_lock); SetLastError(saved);
}
inline bool same(const Owner& a,const Owner& b) {
    return a.world==b.world && a.actor==b.actor && a.graph==b.graph && a.revision==b.revision;
}
inline Owner identity(const loadout_player_catalogue::Catalogue& source) {
    return {source.world,source.actor,source.graph_key,source.revision};
}
inline bool live(Frame& frame) {
    const auto* loan=loadout_selection_scope::borrowed;
    if (!enabled.load() || !loadout_selection_scope::active() ||
        !loan || loan!=frame.loan || loan->call!=frame.call ||
        loadout_player_catalogue::observed_player_actor.load()!=frame.rows.location.actor ||
        !loadout_actor_resolver::current(frame.rows)) return false;
    const auto now=loadout_selection_scope::capture(
        loan->outer,loan->system,&loadout_attack_probe::copy);
    return (now.copied&loadout_selection_scope::kArguments) &&
        now.argument_array==frame.argument_array && now.arguments==frame.resources;
}
inline bool before_commit(void* context) {
    auto& frame=*static_cast<Frame*>(context);
    return live(frame) && loadout_actor_resolver::idle(frame.rows);
}
inline bool weapon_before_commit(void* context) {
    auto& frame=*static_cast<Frame*>(context);
    return live(frame) && (loadout_actor_resolver::idle(frame.rows) ||
        (frame.chain_permission && frame.source && loadout_chain_switch::permit(frame.rows,*frame.source,frame.resources[1],frame.chain_channel,frame.chain_id)));
}
inline loadout_control::Boundary boundary(void* context,loadout_control::Channel channel) {
    if (static_cast<unsigned>(channel)>=2 || !current_frame || current_frame->source!=context ||
        !live(*current_frame)) return loadout_control::Boundary::Unknown;
    current_frame->chain_permission=false;
    if (loadout_actor_resolver::idle(current_frame->rows)) return loadout_control::Boundary::Legal;
    const auto pending=loadout_player_catalogue::control.pending_choice(channel);
    if (pending.assigned && loadout_chain_switch::permit(current_frame->rows,*current_frame->source,current_frame->resources[1],static_cast<unsigned>(channel),pending.id)) {
        current_frame->chain_permission=true;
        current_frame->chain_channel=static_cast<unsigned>(channel); current_frame->chain_id=pending.id;
        return loadout_control::Boundary::Legal;
    }
    return loadout_control::Boundary::Unknown;
}
inline loadout_control::Submission submit(void* context,loadout_control::Channel channel,std::uint32_t id,std::uint64_t ticket) {
    const unsigned category=static_cast<unsigned>(channel);
    if (category>=2 || journal.valid || !current_frame || current_frame->source!=context ||
        !weapon_before_commit(current_frame)) return loadout_control::Submission::Busy;
    const auto& source=*current_frame->source;
    const loadout_player_catalogue::Item* item=nullptr;
    for (std::size_t i=0;i<source.count;++i) if (source.items[i].id==id) { item=&source.items[i]; break; }
    if (!item || !item->known || !item->acquired || item->category!=category ||
        !(item->channels&(1u<<category))) {
        record(LogKind::Rejected,id,ticket,UINT8_MAX,0,channel); return loadout_control::Submission::Rejected;
    }
    const auto lua_arena=current_frame->resources[2];
    if (!lua_arena || current_frame->resources[1]!=source.graph_key || !weapon_before_commit(current_frame))
        return loadout_control::Submission::Busy;
    const std::uint64_t token=0;
    const auto result=loadout_native_selection::submit_current(id,&weapon_before_commit,current_frame,source.loadout[category],true);
    if (result.outcome==loadout_native_selection::Outcome::Submitted || result.outcome==loadout_native_selection::Outcome::AlreadySelected) {
        journal={true,owner,id,item->category,ticket,lifetime,token,lua_arena,
                 channel,source.loadout[1u-category],true}; CRMOD_DIAGNOSTIC(++issued);
        record(LogKind::Submitted,id,ticket,item->category,0,channel); return loadout_control::Submission::Submitted;
    }
    loadout_lua_activation::cancel(token);
    if (result.outcome==loadout_native_selection::Outcome::NoCurrentLoan) return loadout_control::Submission::Busy;
    record(LogKind::Rejected,id,ticket,item->category,static_cast<unsigned>(result.outcome),channel);
    return loadout_control::Submission::Rejected;
}
inline loadout_control::TerminalProof terminal(void* context,loadout_control::Channel channel,
        std::uint32_t id,std::uint64_t ticket,std::uint64_t epoch) {
    loadout_control::TerminalProof proof{loadout_control::Terminal::Unknown,channel,id,ticket,epoch};
    if (static_cast<unsigned>(channel)>=2 || channel!=journal.channel || !journal.valid || journal.id!=id || journal.ticket!=ticket ||
        journal.lifetime!=epoch || lifetime!=epoch || !current_frame || current_frame->source!=context ||
        !live(*current_frame)) return proof;
    const auto& source=*current_frame->source;
    record_wait(source,journal);
    if (!same(journal.owner,owner) || !same(journal.owner,identity(source))) return proof;
    if (loadout_lua_activation::failed(journal.dispatch_token)) {
        loadout_lua_activation::cancel(journal.dispatch_token);
        journal.valid=false; proof.state=loadout_control::Terminal::Aborted;
        record(LogKind::Rejected,id,ticket,journal.category,8,channel); return proof;
    }
    if (source.loadout[1u-static_cast<unsigned>(channel)]!=journal.other_category_id) return proof;
    for (std::size_t i=0;i<source.count;++i) {
        const auto& item=source.items[i];
        if (item.id!=id) continue;
        if (item.category!=journal.category || item.category>=4 || !item.known || !item.acquired || !item.selected ||
            (item.raw_flags&0x200) || (item.raw_flags&0x3f)!=0x20 || source.loadout[item.category]!=id) return proof;
        if (current_frame->resources[1]!=source.graph_key || current_frame->resources[2]!=journal.lua_arena) return proof;
        const bool callback=loadout_lua_activation::observed(journal.dispatch_token,source.world,source.actor,source.graph_key,journal.lua_arena,id);
        const bool no_listener=loadout_lua_activation::notification_consumed_without_handler(
            journal.dispatch_token,source.world,source.actor,source.graph_key,journal.lua_arena,id);
        if (!callback && !no_listener && !journal.owned_selection) return proof;
        proof.state=callback ? loadout_control::Terminal::CallbackDispatchObserved : loadout_control::Terminal::EquippedObserved;
        record(callback ? LogKind::DispatchObserved : LogKind::NativePresetConfirmed,id,ticket,item.category,0,channel);
        loadout_lua_activation::cancel(journal.dispatch_token);
        journal.valid=false; return proof;
    }
    return proof;
}
inline loadout_control::Adapter make_adapter(loadout_player_catalogue::Catalogue& source,std::uint64_t token) {
    loadout_control::Adapter adapter{};
    adapter.catalogue_epoch=token; adapter.owner_scope_current=source.valid;
    adapter.context=&source; adapter.validate=&loadout_player_catalogue::validate;
    if (enabled.load() && loadout_lua_activation::active() && ready && lifetime && same(owner,identity(source))) {
        adapter.world_epoch=lifetime; // Mod identity token, never a simulation frame.
        adapter.selection_channels=3;
        adapter.boundary=&boundary; adapter.submit=&submit; adapter.terminal=&terminal;
    }
    return adapter;
}

// Native addresses are freshly derived inside the current mutable-G callback.
inline void after_original() {
    const DWORD saved=GetLastError();
    struct Restore { DWORD value; ~Restore() { SetLastError(value); } } restore{saved};
    if (!enabled.load() || !loadout_selection_scope::active()) return;
    const auto* loan=loadout_selection_scope::borrowed;
    if (!loan) return;
    struct MenuGuard {
        bool observed=false;
        ~MenuGuard() { if (!observed && observe_menu) observe_menu(0); }
    } menu_guard;
    loadout_player_catalogue::Published previous{};
    const auto generation=native_ui::sdk::generation.load();
    if (!loadout_player_catalogue::copy_published(generation,previous)) { CRMOD_DIAGNOSTIC(stage.store(Stage::NoCatalogue)); return; }
    const auto arguments=loadout_selection_scope::capture(loan->outer,loan->system,&loadout_attack_probe::copy);
    CRMOD_DIAGNOSTIC(sampled_world.store(previous.catalogue.world));
    CRMOD_DIAGNOSTIC(sampled_outer_word.store(arguments.outer_first_word));
    CRMOD_DIAGNOSTIC(sampled_input_a0.store(arguments.input_world_a0)); CRMOD_DIAGNOSTIC(sampled_input_268.store(arguments.input_world_268));
    CRMOD_DIAGNOSTIC(sampled_ui_stacks.store(arguments.arguments[4]));
    if (!(arguments.copied&loadout_selection_scope::kArguments) || arguments.arguments[1]!=previous.catalogue.graph_key ||
        !arguments.arguments[2] || !previous.catalogue.ui_stacks_key ||
        arguments.arguments[4]!=previous.catalogue.ui_stacks_key) {
        CRMOD_DIAGNOSTIC(stage.store(Stage::ScopeMismatch)); CRMOD_DIAGNOSTIC(++invalid); return;
    }
    Frame frame{}; frame.loan=loan; frame.call=loan->call;
    frame.argument_array=arguments.argument_array; frame.resources=arguments.arguments;
    loadout_actor_resolver::Diagnostic diagnostic{};
    if (!loadout_actor_resolver::resolve(static_cast<std::uintptr_t>(previous.catalogue.world),previous.catalogue.actor,frame.rows,diagnostics::enabled?&diagnostic:nullptr)) {
        record_actor_failure(diagnostic);
        CRMOD_DIAGNOSTIC(stage.store(Stage::ActorUnavailable)); CRMOD_DIAGNOSTIC(++invalid); return;
    }
    const auto& rows=frame.rows;
    std::uint64_t columns[5]{rows.loadout_base,rows.cfg_base,rows.mud_base,rows.location.chunk,rows.location.row};
    loadout_player_catalogue::Catalogue source{};
    if (!loadout_player_catalogue::capture(static_cast<std::uintptr_t>(rows.location.world),
        reinterpret_cast<std::uintptr_t>(columns),static_cast<std::uintptr_t>(arguments.arguments[1]),source) ||
        source.actor!=rows.location.actor || !loadout_actor_resolver::current(rows)) { CRMOD_DIAGNOSTIC(stage.store(Stage::CatalogueUnavailable)); CRMOD_DIAGNOSTIC(++invalid); return; }
    source.ui_stacks_key=arguments.arguments[4];
    if (source.world!=previous.catalogue.world || source.actor!=previous.catalogue.actor ||
        source.graph_key!=previous.catalogue.graph_key ||
        source.ui_stacks_key!=previous.catalogue.ui_stacks_key || !live(frame)) {
        CRMOD_DIAGNOSTIC(stage.store(Stage::SourceChanged)); CRMOD_DIAGNOSTIC(++invalid); return;
    }
    if (observe_menu) observe_menu(source.ui_stacks_key);
    menu_guard.observed=true;
    if (!TryAcquireSRWLockExclusive(&loadout_player_catalogue::state_lock)) { CRMOD_DIAGNOSTIC(stage.store(Stage::Busy)); return; }
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&loadout_player_catalogue::state_lock); } } unlock;
    if (!enabled.load() || !live(frame)) { CRMOD_DIAGNOSTIC(stage.store(Stage::SourceChanged)); return; }
    const Owner next=identity(source);
    if (!same(owner,next)) {
        ready=false;
        if (loadout_player_catalogue::control.has_in_flight() || lifetime==UINT64_MAX) { CRMOD_DIAGNOSTIC(stage.store(Stage::OwnerFrozen)); return; }
        owner=next; ++lifetime;
    }
    if (!loadout_player_catalogue::same_source(source,loadout_player_catalogue::published.catalogue)) {
        if (loadout_player_catalogue::catalogue_token==UINT64_MAX) return;
        ++loadout_player_catalogue::catalogue_token;
    }
    ready=true; frame.source=&source;
    struct RestoreFrame { Frame* previous; ~RestoreFrame() { current_frame=previous; } } restore_frame{current_frame};
    current_frame=&frame;
    const auto adapter=make_adapter(source,loadout_player_catalogue::catalogue_token);
    const auto outcomes=loadout_player_catalogue::control.pump_all(adapter);
    
    const bool queued=outcomes[0]==loadout_control::Result::Waiting ||
                      outcomes[1]==loadout_control::Result::Waiting;
    CRMOD_DIAGNOSTIC(stage.store(queued ? Stage::QueuedWaiting : Stage::Ready));
    if (queued) CRMOD_DIAGNOSTIC(++waiting);
    std::array<loadout_control::CatalogueItem,64> options{};
    std::size_t count=0; bool incomplete=source.incomplete;
    for (std::size_t i=0;i<source.count;++i) for (unsigned channel=0;channel<2;++channel) {
        if (!(source.items[i].channels&(1u<<channel))) continue;
        if (count==options.size()) { incomplete=true; continue; }
        options[count++]={static_cast<loadout_control::Channel>(channel),source.items[i].id};
    }
    auto& published=loadout_player_catalogue::published;
    published.catalogue=source;
    published.menu=loadout_player_catalogue::control.menu(adapter,options.data(),count);
    published.menu.catalogue_incomplete|=incomplete; published.view_generation=generation;
}
inline bool arm() {
    if (!loadout_lua_activation::active() || !loadout_native_selection::enabled.load() || !loadout_player_catalogue::active() ||
        !loadout_selection_scope::active() || enabled.load()) return false;
    enabled.store(true); return true;
}
inline void stop() { enabled.store(false); CRMOD_DIAGNOSTIC(stage.store(Stage::Off)); }
inline void flush(FILE* log) {
    if (!log) return;
    static unsigned previous_stage=UINT32_MAX;
    const auto current_stage=static_cast<unsigned>(stage.load());
    if (previous_stage!=current_stage) {
        previous_stage=current_stage;
        constexpr const char* labels[]={"off","awaiting_player_catalogue","input_scope_mismatch","actor_rows_unavailable","current_catalogue_unavailable","busy","source_changed","owner_frozen","ready","queued_waiting"};
        std::fprintf(log,"{\"event\":\"loadout_binding_status\",\"kernel_enabled\":%s,\"bridge_enabled\":%s,\"stage\":\"%s\",\"submitted\":%llu,\"waiting\":%llu,\"invalid\":%llu,\"sampled_world\":%llu,\"input_world_a0\":%llu,\"input_world_268\":%llu,\"outer_first_word\":%llu,\"ui_stacks_key\":%llu,\"input_words_world_verified\":false}\n",
            loadout_native_selection::enabled.load()?"true":"false",enabled.load()?"true":"false",labels[current_stage],
            static_cast<unsigned long long>(issued.load()),static_cast<unsigned long long>(waiting.load()),
            static_cast<unsigned long long>(invalid.load()),static_cast<unsigned long long>(sampled_world.load()),
            static_cast<unsigned long long>(sampled_input_a0.load()),static_cast<unsigned long long>(sampled_input_268.load()),
            static_cast<unsigned long long>(sampled_outer_word.load()),static_cast<unsigned long long>(sampled_ui_stacks.load()));
    }
    if (!TryAcquireSRWLockExclusive(&log_lock)) return;
    std::array<LogRecord,64> copied{}; const unsigned count=log_count;
    std::memcpy(copied.data(),log_queue.data(),count*sizeof(LogRecord)); log_count=0;
    ReleaseSRWLockExclusive(&log_lock);
    for (unsigned i=0;i<count;++i) {
        const auto& r=copied[i];
        if (r.kind==LogKind::WaitingState) {
            std::fprintf(log,"{\"event\":\"loadout_completion_wait\",\"channel\":%u,\"id\":%u,\"ticket\":\"%llu\","
                "\"native_preset\":%u,\"other_native_preset\":%u,\"raw_flags\":%u,\"native_dispatch_observed\":%s}\n",
                static_cast<unsigned>(r.channel),r.id,static_cast<unsigned long long>(r.ticket),r.preset,r.other_preset,
                static_cast<unsigned>(r.raw_flags),r.dispatch_seen?"true":"false");
            continue;
        }
        if (r.kind==LogKind::ActorResolution) {
            const auto& d=r.diagnostic;
            std::fprintf(log,"{\"event\":\"loadout_actor_resolution_failure\",\"reason\":%u,\"reason_name\":\"%s\","
                "\"world\":%llu,\"actor\":\"%llu\",\"entity_count\":%llu,\"expected_generation\":%u,"
                "\"generation_entry\":\"%llu\",\"packed_location\":\"%llu\",\"archetype\":%u,\"archetype_count\":%llu,"
                "\"row\":%u,\"row_count\":%u,\"actual_actor\":\"%llu\",\"tag_count\":%u,\"key_count\":%u,"
                "\"player_tag\":%s,\"dead_tag\":%s,\"component_hash\":%u,\"column_offset\":%u,\"stride\":%u,"
                "\"column_row\":%llu,\"distinct_failure_limit\":32}\n",
                static_cast<unsigned>(d.reason),loadout_actor_resolver::reason_name(d.reason),
                static_cast<unsigned long long>(d.world),static_cast<unsigned long long>(d.actor),
                static_cast<unsigned long long>(d.entity_count),static_cast<unsigned>(d.actor>>32),
                static_cast<unsigned long long>(d.generation_entry),static_cast<unsigned long long>(d.packed),
                static_cast<unsigned>(d.archetype),static_cast<unsigned long long>(d.archetype_count),d.row,d.row_count,
                static_cast<unsigned long long>(d.actual_actor),d.tag_count,d.key_count,d.player?"true":"false",d.dead?"true":"false",
                d.hash,d.column_offset,d.stride,static_cast<unsigned long long>(d.column_row));
            continue;
        }
        const char* event=r.kind==LogKind::Submitted ? "loadout_submitted" :
            r.kind==LogKind::NativePresetConfirmed ? "loadout_native_preset_confirmed" :
            r.kind==LogKind::DispatchObserved ? "loadout_callback_dispatch_observed" :
            r.kind==LogKind::EquipObserved ? "loadout_equip_observed" : "loadout_rejected";
        std::fprintf(log,"{\"event\":\"%s\",\"channel\":%u,\"id\":%u,\"category\":%u,\"ticket\":\"%llu\",\"mod_lifetime\":\"%llu\",\"world_key\":%llu,\"actor\":\"%llu\",\"graph_key\":%llu,\"catalogue_revision\":%u,\"outcome\":%u,\"active_weapon_verified\":false,\"causal_attribution\":\"not_claimed\",\"effect_attribution\":\"not_claimed\"}\n",
            event,static_cast<unsigned>(r.channel),r.id,static_cast<unsigned>(r.category),static_cast<unsigned long long>(r.ticket),
            static_cast<unsigned long long>(r.lifetime),static_cast<unsigned long long>(r.owner.world),
            static_cast<unsigned long long>(r.owner.actor),static_cast<unsigned long long>(r.owner.graph),r.owner.revision,r.outcome);
    }
    const auto dropped=logs_dropped.exchange(0);
    if (dropped) std::fprintf(log,"{\"event\":\"loadout_log_dropped\",\"count\":%llu}\n",static_cast<unsigned long long>(dropped));
}
}
#endif
