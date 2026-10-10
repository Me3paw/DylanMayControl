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
static void form_lookup_check() {
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
static void ui_probe_check() {
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
static void observer_check() {
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

int wmain() {
    observer_check();
    form_lookup_check();
    ui_probe_check();
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
