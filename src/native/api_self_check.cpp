// Checks our XInput adapter contracts; mock events are not gameplay evidence.
#include "xinput_proxy.cpp"
#include <cassert>

static XINPUT_KEYSTROKE events[40];
static unsigned event_count, event_offset, event_calls;
static DWORD WINAPI mock_keystroke(DWORD, DWORD, XINPUT_KEYSTROKE* key) {
    ++event_calls;
    if (event_offset == event_count) return ERROR_EMPTY;
    *key = events[event_offset++];
    return ERROR_SUCCESS;
}
static void queue(unsigned count, BYTE user) {
    event_count = count; event_offset = 0; event_calls = 0;
    for (unsigned i = 0; i < count; ++i) events[i] = {VK_PAD_A, 0, XINPUT_KEYSTROKE_KEYDOWN, user, 0};
}

static HANDLE pass_entered, pass_continue;
static void gate_pass() {
    assert(SetEvent(pass_entered));
    assert(WaitForSingleObject(pass_continue, 2000) == WAIT_OBJECT_0);
}
static DWORD WINAPI invoke_pass(void* result) {
    *static_cast<bool*>(result) = pass_through();
    return 0;
}
static void shutdown_race_check() {
    stopping.store(false);
    AcquireSRWLockExclusive(&input_lock);
    controlled = true; selected = 1;
    pad = {}; pad.wButtons = XINPUT_GAMEPAD_A;
    expires = GetTickCount64() + 500;
    ReleaseSRWLockExclusive(&input_lock);
    pass_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    pass_continue = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(pass_entered && pass_continue);
    pass_test_barrier = gate_pass;
    bool accepted = true;
    HANDLE thread = CreateThread(nullptr, 0, invoke_pass, &accepted, 0, nullptr);
    assert(thread);
    assert(WaitForSingleObject(pass_entered, 2000) == WAIT_OBJECT_0);
    CrModStop();
    assert(stopping.load() && controlled && pad.wButtons == 0 && expires == 0);
    XINPUT_STATE state{};
    assert(XInputGetState(1, &state) == ERROR_SUCCESS && state.Gamepad.wButtons == 0);
    assert(SetEvent(pass_continue));
    assert(WaitForSingleObject(thread, 2000) == WAIT_OBJECT_0);
    assert(!accepted && controlled && pad.wButtons == 0 && expires == 0);
    pass_test_barrier = nullptr;
    CloseHandle(thread); CloseHandle(pass_entered); CloseHandle(pass_continue);
    stopping.store(false);
    assert(pass_through() && !controlled);
    CrModStop();
    assert(stopping.load() && !controlled);
    assert(!pass_through());
}

static unsigned observed_commits, observed_setters;
static void* observed_graph;
static void* observed_context;
static void mock_commit(uint32_t id, void* graph, void* context) {
    assert(id == 0x12345678 && graph == observed_graph && context == observed_context);
    ++observed_commits;
    assert(GetLastError() == 71);
    SetLastError(72);
}
static void mock_setter(uint16_t stage, void* node) {
    assert(stage == 0x20);
    ++observed_setters;
    assert(GetLastError() == 81);
    uint16_t flags = 0x78;
    memcpy(static_cast<BYTE*>(node) + 0x1f4, &flags, sizeof(flags));
    SetLastError(82);
}
static HANDLE drop_entered, drop_continue, observer_disabled;
static void gate_observer_drop() {
    assert(SetEvent(drop_entered));
    assert(WaitForSingleObject(drop_continue, 2000) == WAIT_OBJECT_0);
}
static void notify_observer_disabled() { assert(SetEvent(observer_disabled)); }
static DWORD WINAPI invoke_observer_record(void*) {
    selection_observer::record(0, 1, 0, 0, nullptr);
    return 0;
}
static DWORD WINAPI invoke_observer_stop(void* stream) {
    CrModStop();
    FILE* log = static_cast<FILE*>(stream);
    selection_observer::flush(log);
    fprintf(log, "{\"event\":\"stopped\"}\n");
    fflush(log);
    return 0;
}
static void observer_drop_race_check() {
    using namespace selection_observer;
    assert(count == 0 && dropped.load() == 0 && attempts.load() == 0);
    drop_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    drop_continue = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    observer_disabled = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(drop_entered && drop_continue && observer_disabled);
    FILE* log = tmpfile();
    assert(log);
    drop_test_barrier = gate_observer_drop;
    stop_test_barrier = notify_observer_disabled;
    recording.store(true);
    AcquireSRWLockExclusive(&queue_lock);
    HANDLE producer = CreateThread(nullptr, 0, invoke_observer_record, nullptr, 0, nullptr);
    assert(producer);
    assert(WaitForSingleObject(drop_entered, 2000) == WAIT_OBJECT_0);
    assert(attempts.load() == 1 && dropped.load() == 0);
    ReleaseSRWLockExclusive(&queue_lock);
    HANDLE stopper = CreateThread(nullptr, 0, invoke_observer_stop, log, 0, nullptr);
    assert(stopper);
    assert(WaitForSingleObject(observer_disabled, 2000) == WAIT_OBJECT_0);
    assert(!recording.load() && attempts.load() == 1);
    assert(WaitForSingleObject(stopper, 0) == WAIT_TIMEOUT);
    assert(SetEvent(drop_continue));
    assert(WaitForSingleObject(producer, 2000) == WAIT_OBJECT_0);
    assert(WaitForSingleObject(stopper, 2000) == WAIT_OBJECT_0);
    assert(attempts.load() == 0 && dropped.load() == 0 && count == 0);
    rewind(log);
    char line[256]{};
    assert(fgets(line, sizeof(line), log) && strstr(line, "selection_observation_dropped") && strstr(line, "\"count\":1"));
    assert(fgets(line, sizeof(line), log) && strstr(line, "\"event\":\"stopped\""));
    assert(!fgets(line, sizeof(line), log));
    drop_test_barrier = nullptr;
    stop_test_barrier = nullptr;
    fclose(log);
    CloseHandle(producer); CloseHandle(stopper);
    CloseHandle(drop_entered); CloseHandle(drop_continue); CloseHandle(observer_disabled);
    stopping.store(false);
    puts("Observer failed-try/drop shutdown interleaving: final drop flushed before stopped, no late counters");
}
static uint8_t* lookup_fake(uint8_t* out, const uint32_t* id, const void* cfg) {
    if (cfg) { out[0] = 3; out[1] = 1; assert(id); }
    else { out[1] = 0; }
    SetLastError(0x12345678);
    return out;
}
[[maybe_unused]] static void form_lookup_check() {
    using namespace loadout_mod;
    std::atomic<bool> stopped{false};
    uint8_t out[2]{0xab, 0};
    // Disabled callbacks must not touch any supplied pointer.
    observe_form_result(nullptr, nullptr, nullptr);
    assert(!start(stopped));
    assert(start(stopped, true));
    selection_observer::original_form_lookup = lookup_fake;
    auto* invalid_id = reinterpret_cast<const uint32_t*>(1);
    assert(selection_observer::form_lookup(out, invalid_id, nullptr) == out);
    assert(GetLastError() == 0x12345678 && out[0] == 0xab && !out[1]);
    uint32_t id = 0x87654321;
    assert(selection_observer::form_lookup(out, &id, &id) == out);
    assert(GetLastError() == 0x12345678 && out[0] == 3 && out[1] == 1);
    FILE* log = tmpfile();
    assert(log);
    stop();
    flush(log);
    rewind(log);
    char line[512];
    assert(fgets(line, sizeof(line), log));
    assert(strstr(line, "\"id\":null,\"category\":null"));
    assert(fgets(line, sizeof(line), log));
    assert(strstr(line, "\"id\":2271560481,\"category\":3"));
    assert(!fgets(line, sizeof(line), log));
    assert(!start(stopped, true));
    observe_form_result(nullptr, invalid_id, nullptr);
    fclose(log);
    puts("Form lookup native return/LastError, empty-table no-ID-read, nullable failed fields and stop gate verified (mock callback)");
}
static unsigned ui_forwarded = 0;
static void* ui_expected_view;
static void ui_setup_fake(void* object, void* view) {
    assert(object == reinterpret_cast<void*>(0x1234) && view == ui_expected_view);
    assert(GetLastError() == 0x5678);
    ++ui_forwarded;
    SetLastError(0x9876);
}
[[maybe_unused]] static void ui_probe_check() {
    using namespace native_ui_probe;
    std::atomic<bool> stopped{false};
    observe(reinterpret_cast<void*>(1));
    assert(!start(stopped, false) && start(stopped, true));
    std::uint64_t table[kWords]{};
    for (unsigned i = 0; i < kWords; ++i) table[i] = 0x1000 + i;
    void* table_pointer = table;
    ui_expected_view = &table_pointer;
    selection_observer::original_ui_setup = ui_setup_fake;
    SetLastError(0x5678);
    selection_observer::ui_setup(reinterpret_cast<void*>(0x1234), ui_expected_view);
    assert(GetLastError() == 0x9876 && ui_forwarded == 1 && count == 1);
    assert(native_ui_probe::queue[0].readable && native_ui_probe::queue[0].words[75] == 0x104b);
    SetLastError(0x5678);
    selection_observer::ui_setup(reinterpret_cast<void*>(0x1234), ui_expected_view);
    assert(count == 1 && ui_forwarded == 2 && GetLastError() == 0x9876);
    observe(reinterpret_cast<void*>(1));
    assert(count == 2 && !native_ui_probe::queue[1].readable && native_ui_probe::queue[1].words[0] == 0);
    FILE* log = tmpfile(); assert(log);
    stop(); flush(log); rewind(log);
    char line[4096];
    assert(fgets(line, sizeof(line), log) && strstr(line, "\"readable\":true"));
    assert(fgets(line, sizeof(line), log) && strstr(line, "\"readable\":false,\"vtable_words\":[]"));
    assert(!fgets(line, sizeof(line), log));
    observe(reinterpret_cast<void*>(1));
    assert(count == 0 && !start(stopped, true));
    fclose(log);
    selection_observer::original_ui_setup = nullptr;
    puts("UI probe exact forwarding, LastError, scalar snapshots, duplicate gate, unreadable rejection and final stop verified (mock View)");
}
[[maybe_unused]] static void observer_check() {
    using namespace selection_observer;
    // This executable must never activate game hooks, even with the option set.
    assert(!main_exe_matches());
    assert(SetEnvironmentVariableA("CRMOD_OBSERVE_SELECTION", "1"));
    FILE* log = tmpfile();
    assert(log);
    start(log, stopping);
    assert(!recording.load() && original_commit == nullptr && original_setter == nullptr);
    rewind(log);
    char status[512]{};
    assert(fgets(status, sizeof(status), log) && strstr(status, "executable_mismatch"));
    fclose(log);
    assert(SetEnvironmentVariableA("CRMOD_OBSERVE_SELECTION", nullptr));
    original_commit = mock_commit;
    original_setter = mock_setter;
    observed_graph = reinterpret_cast<void*>(0x1110);
    observed_context = reinterpret_cast<void*>(0x2220);
    BYTE node[0x268]{};
    uint32_t id = 0xaabbccdd;
    uint16_t flags = 0x48;
    memcpy(node + 0x1e8, &id, sizeof(id));
    memcpy(node + 0x1f4, &flags, sizeof(flags));
    recording.store(true);
    SetLastError(71);
    commit(0x12345678, observed_graph, observed_context);
    assert(GetLastError() == 72 && observed_commits == 1 && count == 2);
    assert(selection_observer::queue[0].kind == 0 && selection_observer::queue[1].kind == 1 && selection_observer::queue[0].call == selection_observer::queue[1].call);
    assert(selection_observer::queue[0].id == 0x12345678 && selection_observer::queue[0].first == 0x1110 && selection_observer::queue[0].second == 0x2220);
    SetLastError(81);
    setter(0x20, node);
    assert(GetLastError() == 82 && observed_setters == 1 && count == 4);
    assert(selection_observer::queue[2].kind == 2 && selection_observer::queue[3].kind == 3 && selection_observer::queue[2].call == selection_observer::queue[3].call);
    assert(selection_observer::queue[2].id == id && selection_observer::queue[3].id == id && selection_observer::queue[2].flags == 0x48 && selection_observer::queue[3].flags == 0x78);
    assert(selection_observer::queue[2].thread == GetCurrentThreadId() && selection_observer::queue[3].qpc >= selection_observer::queue[2].qpc);
    count = 0;
    for (unsigned i = 0; i < 258; ++i) record(0, i, 0, 0, nullptr);
    assert(count == 256 && dropped.load() == 2);
    log = tmpfile();
    assert(log);
    stop();
    flush(log);
    assert(count == 0 && dropped.load() == 0);
    fclose(log);
    SetLastError(71);
    commit(0x12345678, observed_graph, observed_context);
    assert(observed_commits == 2 && count == 0 && GetLastError() == 72);
    original_commit = nullptr;
    original_setter = nullptr;
    observer_drop_race_check();
    puts("Observer helper rejection, exact forwarding, raw field snapshots, LastError, bounded queue/drop flush and recording stop verified (mock callbacks only)");
}

[[maybe_unused]] static void performance_check() {
    using namespace performance_log;
    FILE* log=tmpfile(); assert(log);
    start(log,false);
    SetLastError(71);
    { Scope timing(Phase::Tick); SetLastError(72); }
    assert(GetLastError()==72 && metrics[0].calls==0);
    start(log,true);
    SetLastError(81);
    { Scope timing(Phase::Tick); assert(GetLastError()==81); Sleep(2); SetLastError(82); }
    assert(GetLastError()==82);
    const auto& metric=metrics[static_cast<unsigned>(Phase::Tick)];
    assert(metric.calls==1 && metric.samples==1 && metric.ticks>0);
    SetLastError(91); report(log,true); assert(GetLastError()==91);
    rewind(log); char text[4096]{}; const auto size=fread(text,1,sizeof(text)-1,log);
    assert(size && strstr(text,"mod_performance") && strstr(text,"sampled_ms"));
    performance_log::enabled.store(false); fclose(log);
    puts("Performance sampling, disabled path, measured duration, report and LastError verified");
}

static HANDLE attack_log_entered,attack_log_continue;
static void block_attack_log() {
    assert(SetEvent(attack_log_entered));
    assert(WaitForSingleObject(attack_log_continue,2000)==WAIT_OBJECT_0);
}
static DWORD WINAPI flush_attack_log(void* file) {
    attack_turn_native::flush(static_cast<FILE*>(file));return 0;
}
static void attack_log_lock_check() {
    using namespace attack_turn_native;
    attack_log_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    attack_log_continue=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    assert(attack_log_entered && attack_log_continue);
    FILE* file=tmpfile();assert(file);
    direction_count=1;direction_probes[0]={};direction_probes[0].ticket=11;
    request={};request.ticket=50;request.pending=true;
    flush_test_barrier=&block_attack_log;
    HANDLE writer=CreateThread(nullptr,0,&flush_attack_log,file,0,nullptr);assert(writer);
    assert(WaitForSingleObject(attack_log_entered,2000)==WAIT_OBJECT_0);
    assert(TryAcquireSRWLockExclusive(&lock)); // A blocked diagnostic write must not deny the gameplay request lock.
    assert(request.pending && request.ticket==50);
    request.ticket=51;direction_probes[0]={};direction_probes[0].ticket=12;direction_count=1;
    ReleaseSRWLockExclusive(&lock);
    assert(SetEvent(attack_log_continue));assert(WaitForSingleObject(writer,2000)==WAIT_OBJECT_0);
    flush_test_barrier=nullptr;
    assert(direction_count==1 && request.ticket==51);
    rewind(file);char line[1024]{};
    assert(fgets(line,sizeof line,file) && strstr(line,"\"ticket\":11"));
    assert(!fgets(line,sizeof line,file));
    fseek(file,0,SEEK_END);flush(file);assert(direction_count==0);
    fclose(file);request={};CloseHandle(writer);CloseHandle(attack_log_entered);CloseHandle(attack_log_continue);
    puts("PASS: blocked attack-log output leaves gameplay request lock available and concurrent records intact");
}
static unsigned forwarded_collisions;
static void forward_collision(void*,void*,void*,void*) {
    assert(GetLastError()==71);++forwarded_collisions;SetLastError(72);
}
static void world_after_budget_check() {
    using namespace gameplay_runtime;
    const auto old_copy=step_binding::env().copy;
    const auto old_original=original_collision;const auto old_token=shared_stopping;
    const auto old_world=step_binding::mail().world.load();
    const auto old_actor=loadout_player_catalogue::observed_player_actor.load();
    const auto old_calls=collision_calls.load();
    std::atomic<bool> stop_token{false};shared_stopping=&stop_token;original_collision=&forward_collision;
    step_binding::env().copy=&copy;
    loadout_player_catalogue::observed_player_actor.store(0x100000001ULL);
    static std::array<unsigned char,0x58600> world_a{},world_b{};
    const uint64_t entity_count=1;std::memcpy(world_a.data()+0x58510,&entity_count,8);std::memcpy(world_b.data()+0x58510,&entity_count,8);
    uint64_t cols[0x1d]{},control[4]{},container[4]{};
    cols[0]=reinterpret_cast<uintptr_t>(control);control[1]=reinterpret_cast<uintptr_t>(container);container[3]=0x100000001ULL;
    uint64_t context=reinterpret_cast<uintptr_t>(world_a.data());
    collision_calls.store(kCollisionCalls);forwarded_collisions=0;
    SetLastError(71);collision_detour(cols,&context,nullptr,nullptr);
    assert(GetLastError()==72 && step_binding::mail().world.load()==context);
    context=reinterpret_cast<uintptr_t>(world_b.data());
    SetLastError(71);collision_detour(cols,&context,nullptr,nullptr);
    assert(GetLastError()==72 && step_binding::mail().world.load()==context);
    assert(collision_calls.load()==kCollisionCalls && forwarded_collisions==2);
    container[3]=2;context=reinterpret_cast<uintptr_t>(world_a.data());
    SetLastError(71);collision_detour(cols,&context,nullptr,nullptr);
    assert(GetLastError()==72 && step_binding::mail().world.load()==reinterpret_cast<uintptr_t>(world_b.data()));
    container[3]=0x100000001ULL;stop_token.store(true);
    SetLastError(71);collision_detour(cols,&context,nullptr,nullptr);
    assert(GetLastError()==72 && step_binding::mail().world.load()==reinterpret_cast<uintptr_t>(world_b.data()));
    assert(forwarded_collisions==4);
    step_binding::env().copy=old_copy;original_collision=old_original;shared_stopping=old_token;
    step_binding::mail().world.store(old_world);loadout_player_catalogue::observed_player_actor.store(old_actor);collision_calls.store(old_calls);
    puts("PASS: current Player world refresh survives exhausted collision telemetry budget; ownership, stop and forwarding guards preserved");
}
static ULONGLONG persist_test_now;
static unsigned persist_test_writes;
static bool persist_test_success;
static bool persist_test_new_revision;
static mod_bindings::Assignments persist_test_new_loadout{};
static ULONGLONG WINAPI persist_fake_clock() {return persist_test_now;}
static BOOL WINAPI persist_fake_write(LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR) {
    ++persist_test_writes;
    if(persist_test_new_revision){persist_test_new_revision=false;assert(mod_bindings::set_loadout(persist_test_new_loadout));}
    return persist_test_success?TRUE:FALSE;
}
static void failed_save_retry_check() {
    using namespace mod_bindings;
    const auto old_settings=snapshot();const auto old_saved=saved_revision.load();
    const auto old_urgent=urgent_revision.load();const auto old_error=save_error.load();
    wchar_t old_path[MAX_PATH]{};wcscpy(old_path,path);wcscpy(path,L"mock-settings-write.ini");
    persist_clock_test=&persist_fake_clock;persist_write_test=&persist_fake_write;
    persist_test_now=GetTickCount64()+1000;persist_test_writes=0;persist_test_success=false;
    saved_revision.store(old_settings.revision);urgent_revision.store(0);
    auto ids=old_settings.loadout;ids[0][0]=ids[0][0]==111?112:111;assert(set_loadout(ids));
    persist();assert(persist_test_writes==16 && save_error.load() && saved_revision.load()==old_settings.revision);
    for(unsigned i=0;i<100;++i){++persist_test_now;persist();}
    assert(persist_test_writes==16); // Failed urgent saves retry on the normal delay, never at input-poll frequency.
    ids[0][0]=113;assert(set_loadout(ids));persist();assert(persist_test_writes==32);
    persist_test_now+=1000;persist();assert(persist_test_writes==48 && save_error.load());
    persist_test_success=true;persist(true);
    assert(persist_test_writes==64 && !save_error.load() && saved_revision.load()==snapshot().revision && urgent_revision.load()==0);
    persist(true);assert(persist_test_writes==64);
    ids[0][1]=114;assert(set_loadout(ids));const auto written_revision=snapshot().revision;
    persist_test_new_loadout=ids;persist_test_new_loadout[0][1]=115;persist_test_new_revision=true;
    persist();
    assert(persist_test_writes==80 && saved_revision.load()==written_revision && snapshot().revision>written_revision);
    assert(urgent_revision.load()==snapshot().revision);
    persist();assert(persist_test_writes==96 && saved_revision.load()==snapshot().revision && urgent_revision.load()==0);
    persist_clock_test=nullptr;persist_write_test=nullptr;
    settings=old_settings;saved_revision.store(old_saved);urgent_revision.store(old_urgent);save_error.store(old_error);wcscpy(path,old_path);
    puts("PASS: failed save retries throttled, urgent/forced recovery and a new revision arriving during a save preserved");
}
static unsigned camera_test_reads,camera_test_forwards;
static bool refuse_camera_read(void*,const void*,size_t) {++camera_test_reads;return false;}
static void forward_camera(void** cols,void*,void*) {
    assert(cols==reinterpret_cast<void**>(0x10000) && GetLastError()==71);
    ++camera_test_forwards;SetLastError(72);
}
static void camera_budget_check() {
    auto& state=camera_lock_log::st();auto& env=step_binding::env();auto& binding=combat_native::binding();
    const auto old_copy=env.copy;const auto old_original=state.orig;const auto old_pins=state.pins_ok;
    const auto old_budget=state.pushed.load(),old_calls=state.calls.load(),old_invalid=state.invalid.load();
    const auto old_get=binding.get_last_error;const auto old_set=binding.set_last_error;
    env.copy=&refuse_camera_read;state.orig=&forward_camera;state.pins_ok=true;
    binding.get_last_error=&gameplay_runtime::get_error;binding.set_last_error=&gameplay_runtime::set_error;
    state.pushed.store(camera_lock_log::kBudget);camera_test_reads=0;camera_test_forwards=0;
    SetLastError(71);camera_lock_log::lockon_detour(reinterpret_cast<void**>(0x10000),nullptr,nullptr);
    assert(camera_test_reads==0 && camera_test_forwards==1 && GetLastError()==72);
    state.pushed.store(camera_lock_log::kBudget-1);camera_lock_log::observe(reinterpret_cast<void**>(0x10000));
    assert(camera_test_reads==(diagnostics::enabled?1u:0u));
    env.copy=old_copy;state.orig=old_original;state.pins_ok=old_pins;
    state.pushed.store(old_budget);state.calls.store(old_calls);state.invalid.store(old_invalid);
    binding.get_last_error=old_get;binding.set_last_error=old_set;
    puts("PASS: camera telemetry stops guarded reads at its budget while native forwarding and LastError remain intact");
}
static unsigned quiet_dispatches,quiet_original_calls,quiet_after_calls,quiet_trace_calls;
static void quiet_trace_sink(const char*,size_t){++quiet_trace_calls;}
static void quiet_dispatch(native_ui_invoke_probe::Handler*,void*){++quiet_dispatches;assert(native_ui::sdk::callbacks.load()==(diagnostics::enabled?1u:0u));}
static void quiet_selection_original(void*,void*,void*){++quiet_original_calls;assert(loadout_selection_scope::borrowed);SetLastError(72);}
static void quiet_selection_after() {
    ++quiet_after_calls;const auto* loan=loadout_selection_scope::borrowed;assert(loan && loadout_selection_scope::active());
    loadout_selection_scope::Record row{};row.scope_call=loan->call;
    row.value=loadout_selection_scope::capture(loan->outer,loan->system,&loadout_attack_probe::copy);
    assert(row.value.copied==(loadout_selection_scope::kArguments|loadout_selection_scope::kMenuId));
    loadout_lookup_probe::observe(row);assert(row.lookup_status==loadout_lookup_probe::EmptyGraph);
}
[[maybe_unused]] static void quiet_runtime_check() {
    assert(!diagnostics::enabled);
    feature_status::Registry registry;quiet_trace_calls=0;registry.set_sink(&quiet_trace_sink);
    registry.set_state(feature_status::Feature::Finisher,feature_status::State::Active,"ignored");
    registry.event(feature_status::Feature::Finisher,feature_status::Event::Applied,1,2,3,4,"ignored");
    assert(registry.active(feature_status::Feature::Finisher) && quiet_trace_calls==0);
    std::atomic<bool> token{false};
    native_ui::sdk::shared_stopping=&token;native_ui::sdk::prepared.store(true);native_ui::sdk::enabled.store(true);
    native_ui::sdk::generation.store(77);native_ui::sdk::ui_thread.store(GetCurrentThreadId());
    native_ui_invoke_probe::recording.store(true);native_ui_invoke_probe::after_capture=&quiet_dispatch;
    native_ui_invoke_probe::Handler handler(native_ui_invoke_probe::methods,77);
    SetLastError(71);native_ui_invoke_probe::invoke(&handler,reinterpret_cast<void*>(0x10000));
    assert(GetLastError()==71 && quiet_dispatches==1 && native_ui_invoke_probe::count==0);
    native_ui_invoke_probe::recording.store(false);native_ui_invoke_probe::after_capture=nullptr;
    native_ui::sdk::prepared.store(false);native_ui::sdk::enabled.store(false);native_ui::sdk::shared_stopping=nullptr;
    native_ui::sdk::generation.store(0);native_ui::sdk::ui_thread.store(0);
    std::array<unsigned char,0x60> system{},menu{};std::uint64_t graph[2]{};
    std::uint64_t args[7]{reinterpret_cast<uintptr_t>(menu.data()),reinterpret_cast<uintptr_t>(graph),0x10000,0,0,0x10000,0};
    const auto array=reinterpret_cast<uintptr_t>(args);std::memcpy(system.data()+0x58,&array,8);
    const uint32_t menu_id=42;std::memcpy(menu.data()+0x3c,&menu_id,4);
    loadout_selection_scope::shared_stopping=&token;loadout_selection_scope::recording.store(true);
    loadout_selection_scope::original_shim=reinterpret_cast<void*>(&quiet_selection_original);loadout_selection_scope::after_original=&quiet_selection_after;
    loadout_lookup_probe::enabled.store(true);
    SetLastError(71);loadout_selection_scope::shim(reinterpret_cast<void*>(0x10000),nullptr,system.data());
    assert(GetLastError()==72 && quiet_original_calls==1 && quiet_after_calls==1 && !loadout_selection_scope::borrowed && loadout_selection_scope::count==0);
    loadout_selection_scope::recording.store(false);loadout_selection_scope::shared_stopping=nullptr;
    loadout_selection_scope::original_shim=nullptr;loadout_selection_scope::after_original=nullptr;loadout_lookup_probe::enabled.store(false);
    assert(performance_log::enabled.load()==false && !directional_input_log::started && attack_turn_native::direction_count==0);
    puts("PASS: quiet feature state, native UI dispatch and selection loan/lookup work without diagnostic queues or samples");
}
static int lua_test_result;
static bool lua_test_nested;
static unsigned lua_test_invocations;
static int lua_fake_pcall(uintptr_t L,int nargs,int nresults,int errfunc) {
    assert(loadout_lua_activation::current_frame && loadout_lua_activation::current_frame->in_pcall);
    ++lua_test_invocations;
    if(lua_test_nested && nargs==1){lua_test_nested=false;assert(loadout_lua_activation::pcall_hook(L,0,0,0)==0);}
    if(nargs==1)assert(nresults==0 && errfunc==1);
    SetLastError(72);return nargs==1?lua_test_result:0;
}
static uint32_t lua_missing_lookup(uintptr_t,uint64_t){SetLastError(72);return UINT32_MAX;}
static void lua_completion_check() {
    using namespace loadout_lua_activation;
    const auto old_pcall=originals[4],old_lookup=originals[1];const auto old_enabled=loadout_lua_activation::enabled.load();
    const auto old_stopping=shared_stopping;const auto old_frame=current_frame;const auto old_watch=watch;
    const auto old_actor=loadout_player_catalogue::observed_player_actor.load();
    std::atomic<bool> token{false};loadout_lua_activation::enabled.store(true);shared_stopping=&token;
    static std::array<unsigned char,0x58600> world{};uint64_t gens[2]{0,1},locations[2]{},chunk[3]{0,0,0x100000001ULL},archetypes[2]{0,1};
    const auto put=[&](size_t offset,auto value){std::memcpy(world.data()+offset,&value,sizeof value);};
    put(0x58478,reinterpret_cast<uintptr_t>(archetypes));put(0x58510,uint64_t{2});put(0x584e8,reinterpret_cast<uintptr_t>(gens));put(0x58530,reinterpret_cast<uintptr_t>(locations));
    put(0x50,reinterpret_cast<uintptr_t>(chunk));put(0x10448,uint32_t{1});
    std::array<unsigned char,0x80> L{};uint64_t context=reinterpret_cast<uintptr_t>(world.data());
    const auto context_pointer=reinterpret_cast<uintptr_t>(&context);std::memcpy(L.data()+0x78,&context_pointer,8);
    uint64_t state[2]{0,reinterpret_cast<uintptr_t>(L.data())},events[2]{0,1234};
    loadout_player_catalogue::observed_player_actor.store(chunk[2]);
    originals[4]=reinterpret_cast<void*>(&lua_fake_pcall);originals[1]=reinterpret_cast<void*>(&lua_missing_lookup);
    for(int result:{1,0}) {
        Frame f{};f.state=reinterpret_cast<uintptr_t>(state);f.events=reinterpret_cast<uintptr_t>(events);f.registry=events[1];
        f.L=reinterpret_cast<uintptr_t>(L.data());f.world=context;f.actor=chunk[2];f.serial=5;
        f.pending.valid=true;f.pending.matched=true;f.pending.serial=5;
        f.chain.valid=true;f.chain.count=1;f.chain.seen=1;f.chain.nonzero=1;f.chain.argument_seen=true;f.chain.id=42;f.chain.watch_token=77;
        watch={true,false,false,false,77,f.world,f.actor,99,f.events,42,0};current_frame=&f;
        lua_test_result=result;lua_test_nested=result==0;lua_test_invocations=0;
        SetLastError(71);assert(pcall_hook(f.L,1,0,1)==result && GetLastError()==72 && !f.in_pcall);
        if(result)assert(failed(77) && !watch.dispatched);
        else assert(observed(77,f.world,f.actor,99,f.events,42) && watch.callbacks==1 && lua_test_invocations==2);
        watch={true,false,false,false,78,f.world,f.actor,99,f.events,42,0};
        SetLastError(71);assert(lookup_hook(f.registry,kEvent)==UINT32_MAX && GetLastError()==72);
        assert(notification_consumed_without_handler(78,f.world,f.actor,99,f.events,42));
    }
    if constexpr(!diagnostics::enabled)assert(log_count==0 && logged==0 && protected_calls.load()==0);
    originals[4]=old_pcall;originals[1]=old_lookup;loadout_lua_activation::enabled.store(old_enabled);shared_stopping=old_stopping;current_frame=old_frame;watch=old_watch;
    loadout_player_catalogue::observed_player_actor.store(old_actor);
    puts("PASS: Lua protected-call failure token, successful nested callback completion and missing-listener state survive quiet mode");
}

static unsigned turn_calls,turn_clock_calls;
static ULONGLONG turn_clock(){++turn_clock_calls;SetLastError(99);return 1000;}
static void turn_forward(void* a,void* b,void* c,void* d,void* e) {
    assert(a==reinterpret_cast<void*>(1) && b==reinterpret_cast<void*>(2) && c==reinterpret_cast<void*>(3) &&
        d==reinterpret_cast<void*>(4) && e==reinterpret_cast<void*>(5));
    assert(GetLastError()==71);++turn_calls;SetLastError(72);
}
static DWORD WINAPI turn_invoke(void*) {
    SetLastError(71);attack_turn_native::update_hook(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),
        reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),reinterpret_cast<void*>(5));
    assert(GetLastError()==72);return 0;
}
static DWORD WINAPI graph_contended(void* result) {
    *static_cast<bool*>(result)=attack_turn_native::current_graph(attack_turn_native::request);return 0;
}
static void attack_fast_path_check() {
    namespace turn=attack_turn_native;
    namespace cat=loadout_player_catalogue;
    const auto old_request=turn::request;const auto old_original=turn::original_update;
    const auto old_enabled=turn::enabled.load();const auto old_stop=turn::stopping;
    const auto old_cat_enabled=cat::enabled.load();const auto old_cat_stop=cat::shared_stopping;
    const auto old_generation=native_ui::sdk::generation.load();static cat::Published old_published{};old_published=cat::published;
    std::atomic<bool> token{false};turn::stopping=&token;turn::enabled.store(true);
    turn::original_update=reinterpret_cast<void*>(&turn_forward);turn::update_clock_test=&turn_clock;
    turn_calls=turn_clock_calls=0;turn::request={};
    turn_invoke(nullptr);assert(turn_calls==1 && turn_clock_calls==0);
    turn::request.pending=true;turn::request.ticket=5;turn::enabled.store(false);
    turn_invoke(nullptr);assert(turn_calls==2 && turn_clock_calls==0 && turn::request.pending);
    turn::enabled.store(true);token.store(true);
    turn_invoke(nullptr);assert(turn_calls==3 && turn_clock_calls==0 && turn::request.pending);
    token.store(false);AcquireSRWLockExclusive(&turn::lock);
    HANDLE thread=CreateThread(nullptr,0,&turn_invoke,nullptr,0,nullptr);assert(thread);
    assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    ReleaseSRWLockExclusive(&turn::lock);CloseHandle(thread);
    assert(turn_calls==4 && turn_clock_calls==0 && turn::request.pending);
    // A real pending request still validates and retires an invalid owner.
    turn_invoke(nullptr);assert(turn_calls==5 && turn_clock_calls==1 && !turn::request.pending);
    turn::request.world=100;turn::request.actor=200;turn::request.graph=300;
    cat::enabled.store(true);cat::shared_stopping=&token;cat::published={};
    auto& pub=cat::published;pub.view_generation=77;pub.catalogue.valid=true;
    pub.catalogue.world=100;pub.catalogue.actor=200;pub.catalogue.graph_key=300;
    native_ui::sdk::generation.store(77);assert(turn::current_graph(turn::request));
    for(auto* identity:{&pub.catalogue.world,&pub.catalogue.actor,&pub.catalogue.graph_key}) {
        ++*identity;assert(!turn::current_graph(turn::request));--*identity;
    }
    native_ui::sdk::generation.store(78);assert(!turn::current_graph(turn::request));
    native_ui::sdk::generation.store(0);assert(!turn::current_graph(turn::request));
    native_ui::sdk::generation.store(77);pub.catalogue.valid=false;assert(!turn::current_graph(turn::request));
    pub.catalogue.valid=true;token.store(true);assert(!turn::current_graph(turn::request));token.store(false);
    cat::enabled.store(false);assert(!turn::current_graph(turn::request));cat::enabled.store(true);
    bool accepted=true;AcquireSRWLockExclusive(&cat::state_lock);
    thread=CreateThread(nullptr,0,&graph_contended,&accepted,0,nullptr);assert(thread);
    assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    ReleaseSRWLockExclusive(&cat::state_lock);CloseHandle(thread);assert(!accepted);
    assert(turn::current_graph(turn::request));
    turn::request=old_request;turn::original_update=old_original;turn::enabled.store(old_enabled);turn::stopping=old_stop;
    turn::update_clock_test=nullptr;cat::published=old_published;cat::enabled.store(old_cat_enabled);cat::shared_stopping=old_cat_stop;
    native_ui::sdk::generation.store(old_generation);
    puts("PASS: idle/inactive/contended attack forwarding skips clock; pending validation, LastError and publication ownership/generation remain intact");
}

#include "api_event_replay.h"

int wmain(int argc,wchar_t** argv) {
    if(argc==2 && wcscmp(argv[1],L"--event-replay")==0){
        world_after_budget_check();failed_save_retry_check();lua_completion_check();attack_fast_path_check();api_event_replay::run();return 0;
    }
    if(argc==2 && wcscmp(argv[1],L"--attack-fast-path")==0){attack_fast_path_check();return 0;}
    if(argc==2 && wcscmp(argv[1],L"--attack-log-lock")==0){attack_log_lock_check();return 0;}
    if(argc==2 && wcscmp(argv[1],L"--world-after-budget")==0){world_after_budget_check();return 0;}
    if(argc==2 && wcscmp(argv[1],L"--failed-save-retry")==0){failed_save_retry_check();return 0;}
    if(argc==2 && wcscmp(argv[1],L"--camera-budget")==0){camera_budget_check();return 0;}
#if CRMOD_DIAGNOSTICS
    attack_log_lock_check();
#else
    quiet_runtime_check();
#endif
    world_after_budget_check();
    failed_save_retry_check();
    camera_budget_check();
    lua_completion_check();
    attack_fast_path_check();
#if CRMOD_DIAGNOSTICS
    performance_check();observer_check();form_lookup_check();ui_probe_check();
#endif
    SetEnvironmentVariableW(L"CRMOD_TOKEN", nullptr);
    SetEnvironmentVariableW(L"CRMOD_LOG", nullptr);
    controlled = true; selected = 1;
    XINPUT_KEYSTROKE key{};
    queue(1, 1);
    assert(filter_keystroke(mock_keystroke, 1, 0, &key) == ERROR_EMPTY);
    assert(event_calls == 0 && key.VirtualKey == 0);
    queue(3, 1); events[1].UserIndex = 2; events[2].UserIndex = 3;
    assert(filter_keystroke(mock_keystroke, XUSER_INDEX_ANY, 0, &key) == ERROR_SUCCESS);
    assert(key.UserIndex == 2 && event_offset == 2);
    assert(filter_keystroke(mock_keystroke, XUSER_INDEX_ANY, 0, &key) == ERROR_SUCCESS);
    assert(key.UserIndex == 3 && event_offset == 3);
    queue(40, 1);
    assert(filter_keystroke(mock_keystroke, XUSER_INDEX_ANY, 0, &key) == ERROR_EMPTY);
    assert(event_calls == 32 && event_offset == 32 && key.VirtualKey == 0);
    queue(1, 2);
    assert(filter_keystroke(mock_keystroke, 2, 0, &key) == ERROR_SUCCESS && key.UserIndex == 2);
    controlled = false;
    queue(1, 1);
    assert(filter_keystroke(mock_keystroke, XUSER_INDEX_ANY, 0, &key) == ERROR_SUCCESS && key.UserIndex == 1);
    controlled = true;
    XINPUT_CAPABILITIES caps{};
    assert(XInputGetCapabilities(1, 0, &caps) == ERROR_SUCCESS);
    assert(caps.Type == XINPUT_DEVTYPE_GAMEPAD && caps.SubType == XINPUT_DEVSUBTYPE_GAMEPAD);
    assert(caps.Gamepad.wButtons == 0xf3ff && caps.Gamepad.bLeftTrigger == 255 && caps.Gamepad.bRightTrigger == 255);
    assert(caps.Gamepad.sThumbLX == -1 && caps.Gamepad.sThumbLY == -1 && caps.Gamepad.sThumbRX == -1 && caps.Gamepad.sThumbRY == -1);
    assert(caps.Flags == 0 && caps.Vibration.wLeftMotorSpeed == 0 && caps.Vibration.wRightMotorSpeed == 0);
    assert(XInputGetCapabilities(1, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS);
    assert(XInputGetCapabilities(1, 2, &caps) == ERROR_BAD_ARGUMENTS);
    assert(XInputGetCapabilities(4, 0, &caps) == ERROR_BAD_ARGUMENTS);
    auto native_caps = real<decltype(&XInputGetCapabilities)>("XInputGetCapabilities");
    assert(native_caps && backend != self_module);
    XINPUT_CAPABILITIES native{};
    DWORD actual = XInputGetCapabilities(2, 0, &caps);
    DWORD expected = native_caps(2, 0, &native);
    assert(actual == expected);
    if (actual == ERROR_SUCCESS) assert(memcmp(&caps, &native, sizeof(caps)) == 0);
    published[0] = {};
    XINPUT_STATE value{};
    value.dwPacketNumber = 1;
    publish_state(0, ERROR_SUCCESS, &value, false, 1);
    assert(value.dwPacketNumber == 1);
    value.Gamepad.wButtons = XINPUT_GAMEPAD_A;
    publish_state(0, ERROR_SUCCESS, &value, true, 1);
    assert(value.dwPacketNumber == 2);
    publish_state(0, ERROR_SUCCESS, &value, true, 2);
    assert(value.dwPacketNumber == 2);
    value.Gamepad = {};
    publish_state(0, ERROR_SUCCESS, &value, false, 1);
    assert(value.dwPacketNumber == 3);
    publish_state(0, ERROR_SUCCESS, &value, false, 1);
    assert(value.dwPacketNumber == 3);
    publish_state(0, ERROR_SUCCESS, &value, false, 2);
    assert(value.dwPacketNumber == 4);
    publish_state(0, ERROR_SUCCESS, &value, true, 2);
    assert(value.dwPacketNumber == 4);
    publish_state(0, ERROR_SUCCESS, &value, false, 999);
    assert(value.dwPacketNumber == 4);
    publish_state(0, ERROR_SUCCESS, &value, false, 1000);
    assert(value.dwPacketNumber == 5);
    publish_state(0, ERROR_DEVICE_NOT_CONNECTED, &value, false, 0);
    assert(value.dwPacketNumber == 6);
    publish_state(0, ERROR_SUCCESS, &value, false, 0);
    assert(value.dwPacketNumber == 7);
    XINPUT_STATE other{};
    publish_state(3, ERROR_SUCCESS, &other, false, 10);
    assert(other.dwPacketNumber == 10 && published[0].number == 7);
    published[0].number = 0xffffffff;
    value.Gamepad.wButtons = XINPUT_GAMEPAD_B;
    publish_state(0, ERROR_SUCCESS, &value, false, 0);
    assert(value.dwPacketNumber == 0);
    published[2]={}; menu_packets[2]=false; menu_releases[2]=0;
    mod_bindings::capturing.store(true); // The mask is ownership-based since build101; capture owns confirm here.
    XINPUT_STATE modal{}; modal.dwPacketNumber=41;
    modal.Gamepad.wButtons=XINPUT_GAMEPAD_A; modal.Gamepad.bLeftTrigger=90;
    publish_menu_state(2,ERROR_SUCCESS,&modal,false,41,true);
    assert(!modal.Gamepad.wButtons && modal.Gamepad.bLeftTrigger==90 && modal.dwPacketNumber==42);
    modal.Gamepad.wButtons=XINPUT_GAMEPAD_A;
    publish_menu_state(2,ERROR_SUCCESS,&modal,false,41,true);
    assert(!modal.Gamepad.wButtons && modal.dwPacketNumber==42);
    modal.Gamepad.wButtons=XINPUT_GAMEPAD_A;
    mod_bindings::capturing.store(false);
    publish_menu_state(2,ERROR_SUCCESS,&modal,false,41,false);
    assert(!modal.Gamepad.wButtons && modal.dwPacketNumber==42); // Close cannot replay a held confirm.
    publish_menu_state(2,ERROR_SUCCESS,&modal,false,42,false);
    assert(modal.dwPacketNumber==43); // The physical release remains observable.
    modal.Gamepad.wButtons=XINPUT_GAMEPAD_A;
    publish_menu_state(2,ERROR_SUCCESS,&modal,false,43,false);
    assert(modal.Gamepad.wButtons==XINPUT_GAMEPAD_A && modal.dwPacketNumber==44);
    publish_menu_state(2,ERROR_SUCCESS,&modal,false,43,false);
    assert(modal.dwPacketNumber==44); // Stable physical state keeps a stable logical packet.
    // Root proxy route: Mod-owned navigation hides Dpad/A/left stick, as the plugin detour does.
    const bool was_capturing=mod_bindings::capturing.exchange(false),was_owned=mod_bindings::menu_owned.exchange(true);
    published[3]={}; menu_packets[3]=false; menu_releases[3]=0;
    XINPUT_STATE owned{}; owned.dwPacketNumber=7;
    const auto hold=[&]{owned.Gamepad={};owned.Gamepad.wButtons=XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_RIGHT_SHOULDER;
        owned.Gamepad.sThumbLX=21000;owned.Gamepad.sThumbLY=-21000;owned.Gamepad.sThumbRX=1234;owned.Gamepad.bLeftTrigger=255;};
    hold(); publish_menu_state(3,ERROR_SUCCESS,&owned,false,7,true);
    assert(owned.Gamepad.wButtons==XINPUT_GAMEPAD_RIGHT_SHOULDER && !owned.Gamepad.sThumbLX && !owned.Gamepad.sThumbLY);
    assert(owned.Gamepad.sThumbRX==1234 && owned.Gamepad.bLeftTrigger==255); // Camera stick and lock trigger pass.
    const DWORD masked_packet=owned.dwPacketNumber;
    mod_bindings::menu_owned.store(false);
    hold(); publish_menu_state(3,ERROR_SUCCESS,&owned,false,7,true);
    assert(owned.Gamepad.sThumbLX==21000 && owned.Gamepad.sThumbLY==-21000 && owned.dwPacketNumber==masked_packet+1); // Handoff is a new packet.
    assert(owned.Gamepad.wButtons==XINPUT_GAMEPAD_RIGHT_SHOULDER); // The held Dpad stays quarantined until released.
    mod_bindings::capturing.store(was_capturing); mod_bindings::menu_owned.store(was_owned);
    puts("Controller overlay packet changes, held-confirm release and next gameplay press verified (test double)");
    shutdown_race_check();
    puts("ANY filtering, selected/nonselected/pass, bounded draining, capability resolutions/flags, real backend forwarding, packet continuity/renewal/connection/wrap and shutdown-interleaving verified");
    return 0;
}
