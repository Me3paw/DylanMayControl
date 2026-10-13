#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <xinput.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include "loadout_mod.h"
#include "native_ui_probe.h"
#include "native_ui_guard.h"
#include "native_ui_invoke_probe.h"
#include "native_ui_echo.h"
#include "loadout_attack_probe.h"
#include <loadout_assets.h>
#include "native_ui_transport.h"
#include "selection_observer.h"
#include "directional_input_log.h"

static HMODULE self_module, backend;
static INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;
static SRWLOCK input_lock = SRWLOCK_INIT;
static std::atomic<bool> stopping{false}, enabled{true};
#if CRMOD_DIAGNOSTICS
static std::atomic<unsigned long> polls{0}, poll_thread{0};
#endif
static bool controlled = false;
static DWORD selected = 0;
static bool automation_enabled = false;
struct PublishedState {
    DWORD number = 0, native_packet = 0, result = 0;
    XINPUT_GAMEPAD pad{};
    bool seen = false, synthetic = false;
};
static PublishedState published[XUSER_MAX_COUNT];
static void publish_state(DWORD index, DWORD result, XINPUT_STATE* state,
                          bool is_synthetic, DWORD native_packet) {
    auto& previous = published[index];
    if (!previous.seen) previous.number = native_packet ? native_packet : 1;
    else if (previous.result != result || memcmp(&previous.pad, &state->Gamepad, sizeof(previous.pad)) != 0 ||
             (!is_synthetic && !previous.synthetic && previous.native_packet != native_packet)) ++previous.number;
    previous.seen = true;
    previous.synthetic = is_synthetic;
    previous.native_packet = native_packet;
    previous.result = result;
    previous.pad = state->Gamepad;
    state->dwPacketNumber = previous.number;
}
// input_lock owns both masking and packet publication. Once a port has masked
// input, keep its logical packet sequence so the next physical change cannot
// collide with a packet number emitted for an overlay-only change.
static WORD menu_releases[XUSER_MAX_COUNT]{};
static bool menu_packets[XUSER_MAX_COUNT]{};
static void publish_menu_state(DWORD index,DWORD result,XINPUT_STATE* state,bool synthetic,
                               DWORD native_packet,bool modal) {
    if (!synthetic && result==ERROR_SUCCESS) {
        if (modal) {
            if (!menu_packets[index]) {
                publish_state(index,result,state,false,native_packet);
                menu_packets[index]=true;
            }
        }
        // Same filter as the plugin detour: mod-owned buttons and left stick are
        // masked before publication, so the logical packet reports the handoff.
        controller_menu_input::filter(state->Gamepad,modal,menu_releases[index]);
    }
    if (automation_enabled || menu_packets[index]) publish_state(index,result,state,synthetic,native_packet);
}
static ULONGLONG expires = 0;
static XINPUT_GAMEPAD pad{};
#if CRMOD_DIAGNOSTICS
static BYTE secret[16];
static wchar_t log_path[32768];
#endif

struct Command { uint32_t op, index, lease; XINPUT_GAMEPAD pad; };
static_assert(sizeof(Command) == 24);
static_assert(sizeof(XINPUT_GAMEPAD) == 12);

static void neutral() {
    AcquireSRWLockExclusive(&input_lock);
    if (controlled) { pad = {}; expires = 0; }
    ReleaseSRWLockExclusive(&input_lock);
}

#ifdef CRMOD_API_SELF_CHECK
static void (*pass_test_barrier)() = nullptr;
#endif
[[maybe_unused]] static inline bool pass_through() {
#ifdef CRMOD_API_SELF_CHECK
    if (pass_test_barrier) pass_test_barrier();
#endif
    AcquireSRWLockExclusive(&input_lock);
    bool accepted = !stopping.load();
    if (accepted) {
        if (controlled) { pad = {}; expires = 0; }
        controlled = false;
    }
    ReleaseSRWLockExclusive(&input_lock);
    return accepted;
}
static std::uint32_t preview_cycle_sound(const loadout_key_requests::Context& pressed,loadout_control::Channel channel) {
    const unsigned which=static_cast<unsigned>(channel);if(which>=2)return 0;
    // A press may wait briefly for the native owner's snapshot; this never runs on idle polls.
    AcquireSRWLockShared(&loadout_player_catalogue::state_lock);
    const auto& p=loadout_player_catalogue::published;std::uint32_t form=0;
    if(loadout_player_catalogue::active() && p.catalogue.valid && p.menu.configuration_bound &&
       p.view_generation==native_ui::sdk::generation.load() && p.view_generation==pressed.view_generation &&
       p.catalogue.world==pressed.world && p.catalogue.actor==pressed.actor && p.catalogue.graph_key==pressed.graph) {
        const loadout_key_requests::Context current{p.catalogue.world,p.catalogue.actor,p.catalogue.graph_key,p.menu.world_epoch,p.view_generation};
        AcquireSRWLockShared(&loadout_key_requests::lock);unsigned presses=1;
        for(unsigned i=0;i<loadout_key_requests::count;++i) {
            const auto& r=loadout_key_requests::requests[(loadout_key_requests::head+i)%loadout_key_requests::requests.size()];
            if(r.channel==channel && r.focus==loadout_key_requests::focus_generation.load() && loadout_key_requests::same(r.context,current))++presses;
        }
        loadout_control::Adapter preview{};preview.catalogue_epoch=current.catalogue_epoch;
        preview.owner_scope_current=true;preview.context=const_cast<loadout_player_catalogue::Catalogue*>(&p.catalogue);
        preview.validate=&loadout_player_catalogue::validate;
        form=loadout_player_catalogue::control.preview_cycle(preview,channel,presses).choice.id;
#if CRMOD_AUDIO_TRACE
        const auto slots=loadout_player_catalogue::control.menu(preview,nullptr,0).channels[which].slots;
        loadout_audio_trace::row("audio_preview",",\"channel\":%u,\"presses\":%u,\"form\":%u,\"ids\":[%u,%u,%u],\"status\":[%u,%u,%u],\"selected\":[%u,%u,%u]",which,presses,form,
            slots[0].slot.id,slots[1].slot.id,slots[2].slot.id,
            static_cast<unsigned>(slots[0].validation.status),static_cast<unsigned>(slots[1].validation.status),static_cast<unsigned>(slots[2].validation.status),
            slots[0].validation.selected?1u:0u,slots[1].validation.selected?1u:0u,slots[2].validation.selected?1u:0u);
#endif
        ReleaseSRWLockShared(&loadout_key_requests::lock);
    } else {
        loadout_audio_trace::row("audio_preview_rejected",",\"channel\":%u,\"active\":%u,\"valid\":%u,\"configuration_bound\":%u,\"view_matches\":%u,\"owner_matches\":%u",which,
            loadout_player_catalogue::active()?1u:0u,p.catalogue.valid?1u:0u,p.menu.configuration_bound?1u:0u,
            p.view_generation==native_ui::sdk::generation.load() && p.view_generation==pressed.view_generation?1u:0u,
            p.catalogue.world==pressed.world && p.catalogue.actor==pressed.actor && p.catalogue.graph_key==pressed.graph?1u:0u);
    }
    ReleaseSRWLockShared(&loadout_player_catalogue::state_lock);return form;
}
static void sample_loadout_cycle_keys(FILE* log) {
    loadout_audio_trace::begin_sample();
    loadout_key_requests::Context context{}; bool available=false;
    unsigned snapshot_reason=loadout_player_catalogue::active()?1:0;
    if (loadout_player_catalogue::active() && TryAcquireSRWLockShared(&loadout_player_catalogue::state_lock)) {
        const auto& p=loadout_player_catalogue::published;
        available=p.catalogue.valid && p.menu.configuration_bound && p.view_generation==native_ui::sdk::generation.load();
        if (available) context={p.catalogue.world,p.catalogue.actor,p.catalogue.graph_key,p.menu.world_epoch,p.view_generation};
        snapshot_reason=!p.catalogue.valid?2:!p.menu.configuration_bound?3:p.view_generation!=native_ui::sdk::generation.load()?4:5;
        ReleaseSRWLockShared(&loadout_player_catalogue::state_lock);
    }
    if constexpr(CRMOD_AUDIO_TRACE) {
        static unsigned previous=UINT_MAX;
        if(previous!=snapshot_reason) {
            loadout_audio_trace::row("audio_snapshot",",\"reason\":%u,\"previous_reason\":%u",snapshot_reason,previous);previous=snapshot_reason;
        }
    }
    loadout_key_requests::sample(context,available,backend,&preview_cycle_sound);
#if CRMOD_DIAGNOSTICS
    const unsigned state=(available?1:0)|(mod_bindings::panel_open.load()?2:0)|(mod_bindings::menu_owned.load()?4:0)|(mod_bindings::capturing.load()?8:0);
    static unsigned previous=UINT_MAX,records=0;
    if(log && state!=previous && records<256){previous=state;++records;std::fprintf(log,"{\"event\":\"controller_menu_input\",\"catalogue_available\":%s,\"panel_open\":%s,\"mod_focus_owned\":%s,\"capturing\":%s,\"input_sampling_allowed\":%s,\"raw_buttons\":%u}\n",state&1?"true":"false",state&2?"true":"false",state&4?"true":"false",state&8?"true":"false",loadout_key_requests::input_available(context,available)?"true":"false",mod_bindings::last_pad.load());}
#else
    (void)log;
#endif
    mod_bindings::persist();
}
static void stop_bridge() {
    AcquireSRWLockExclusive(&input_lock);
    stopping.store(true);
    if (controlled) { pad = {}; expires = 0; }
    ReleaseSRWLockExclusive(&input_lock);
    loadout_lua_activation::stop();
    held_lock_native::stop();
    controller_menu_input::enabled.store(false);
    loadout_key_requests::stop();
    loadout_selection_bridge::stop();
    loadout_native_selection::stop();
    selection_observer::stop();
    loadout_mod::stop();
    loadout_attack_probe::stop();
    native_ui_probe::stop();
    native_ui_transport::stop();
    native_ui_echo::stop();
    native_ui::sdk::stop();
    native_ui_invoke_probe::stop();
}

#if CRMOD_DIAGNOSTICS
static bool receive(SOCKET s, void* data, int size, DWORD deadline_ms) {
    const ULONGLONG deadline = GetTickCount64() + deadline_ms;
    int done = 0;
    while (done < size && !stopping.load() && GetTickCount64() < deadline) {
        int n = recv(s, static_cast<char*>(data) + done, size - done, 0);
        if (n > 0) { done += n; continue; }
        if (n == 0) return false;
        int e = WSAGetLastError();
        if (e != WSAETIMEDOUT && e != WSAEWOULDBLOCK) return false;
    }
    return done == size;
}

static bool reply(SOCKET s, const char* text) {
    int length = static_cast<int>(strlen(text)), done = 0;
    while (done < length) {
        int n = send(s, text + done, length - done, 0);
        if (n <= 0) return false;
        done += n;
    }
    return true;
}

#endif

static void stop_services() {
    loadout_queue_audio::stop();
    loadout_player_catalogue::stop();
    loot_multiplier::enabled.store(false);
    gameplay_runtime::stop();
    loadout_lookup_probe::stop();
    loadout_selection_scope::stop();
    loadout_lua_activation::stop();
    loadout_selection_bridge::stop();
    loadout_native_selection::stop();
    selection_observer::stop();
    loadout_mod::stop();
    loadout_attack_probe::stop();
    native_ui_probe::stop();
    native_ui_transport::stop();
    native_ui_echo::stop();
    native_ui::sdk::stop();
    native_ui_invoke_probe::stop();
}

#if CRMOD_DIAGNOSTICS
static DWORD WINAPI service(void* reference) {
    FILE* log = _wfopen(log_path, L"w");
    if(!log && !automation_enabled)log=_wfopen(L"NUL",L"w");
    WSADATA wsa{};
    SOCKET listener = INVALID_SOCKET;
    bool network = false;
    if (!log) goto end;
    if (!automation_enabled) {
        performance_log::start(log,_wcsicmp(log_path,L"NUL")!=0);
        mod_bindings::start();
        directional_input_log::start(log,backend);
        fprintf(log, "{\"event\":\"manual_session\",\"pid\":%lu,\"combat_writes\":false,\"synthetic_input\":false,\"backend_verified\":%s}\n",
                GetCurrentProcessId(), backend && GetProcAddress(backend, "XInputGetState") ? "true" : "false");
        fflush(log);
        selection_observer::start(log, stopping, true,backend);
        while (!stopping.load()) {
            if(loadout_selection_bridge::enabled.load())loadout_queue_audio::prepare(self_module,stopping);
            { performance_log::Scope timing(performance_log::Phase::Tick); gameplay_runtime::tick(); }
            {
            performance_log::Scope timing(performance_log::Phase::Flush);
            gameplay_runtime::flush(log);
            loot_multiplier::flush(log);
            selection_observer::flush(log);
            loadout_lua_activation::flush(log);
            loadout_selection_bridge::flush(log);
            loadout_chain_switch::flush(log);
            loadout_mod::flush(log);
            loadout_attack_probe::flush(log);
            loadout_selection_scope::flush(log);
            native_ui_probe::flush(log);
            native_ui::sdk::flush(log);
            native_ui_invoke_probe::flush(log);
            native_ui_echo::flush(log);
            native_ui_transport::flush(log);
            }
            for (unsigned sample=0;sample<10 && !stopping.load();++sample) {
                {
                performance_log::Scope timing(performance_log::Phase::InputSample);
                sample_loadout_cycle_keys(log);
                directional_input_log::sample(log);
                }
                Sleep(10);
            }
            performance_log::report(log);
        }
        performance_log::report(log,true);
        mod_bindings::persist(true);
        goto end;
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) goto end;
    network = true;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) goto end;
    {
        BOOL exclusive = TRUE;
        setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener, 1) != 0) goto end;
        int len = sizeof(address);
        if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &len) != 0) goto end;
        char token[33]{};
        for (int i = 0; i < 16; ++i) sprintf(token + i * 2, "%02x", secret[i]);
        fprintf(log, "{\"event\":\"ready\",\"pid\":%lu,\"port\":%u,\"token\":\"%s\",\"protocol\":1,\"combat_writes\":false,\"backend_verified\":%s}\n",
                GetCurrentProcessId(), ntohs(address.sin_port), token,
                backend && GetProcAddress(backend, "XInputGetState") ? "true" : "false");
        fflush(log);
        selection_observer::start(log, stopping,false,backend);
        while (!stopping.load()) {
            if(loadout_selection_bridge::enabled.load())loadout_queue_audio::prepare(self_module,stopping);
            gameplay_runtime::tick();
            gameplay_runtime::flush(log);
            loot_multiplier::flush(log);
            selection_observer::flush(log);
            loadout_lua_activation::flush(log);
            loadout_selection_bridge::flush(log);
            loadout_chain_switch::flush(log);
            loadout_mod::flush(log);
            loadout_attack_probe::flush(log);
            loadout_selection_scope::flush(log);
            native_ui_probe::flush(log);
            native_ui::sdk::flush(log);
            native_ui_invoke_probe::flush(log);
            native_ui_echo::flush(log);
            native_ui_transport::flush(log);
            fd_set fds; FD_ZERO(&fds); FD_SET(listener, &fds);
            timeval timeout{0, 100000};
            int available = select(0, &fds, nullptr, nullptr, &timeout);
            if (available == SOCKET_ERROR) break;
            if (available == 0) continue;
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            DWORD milliseconds = 100;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&milliseconds), sizeof(milliseconds));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&milliseconds), sizeof(milliseconds));
            BYTE token_in[16]{};
            bool authenticated = receive(client, token_in, 16, 2000);
            BYTE difference = 0;
            for (int i = 0; i < 16; ++i) difference |= token_in[i] ^ secret[i];
            authenticated = authenticated && difference == 0;
            if (authenticated && reply(client, "{\"ok\":true,\"protocol\":1}\n")) {
                Command command{};
                while (receive(client, &command, sizeof(command), 2000)) {
                    selection_observer::flush(log);
                    loadout_lua_activation::flush(log);
                    loadout_selection_bridge::flush(log);
                    loadout_chain_switch::flush(log);
                    loadout_mod::flush(log);
                    loadout_attack_probe::flush(log);
                    loadout_selection_scope::flush(log);
                    native_ui_probe::flush(log);
                    native_ui::sdk::flush(log);
                    native_ui_invoke_probe::flush(log);
                    native_ui_echo::flush(log);
                    native_ui_transport::flush(log);
                    bool valid = true;
                    switch (command.op) {
                    case 1: break;
                    case 2:
                        if (command.index >= XUSER_MAX_COUNT || command.lease == 0 || command.lease > 500 ||
                            (command.pad.wButtons & 0x0c00) != 0) { valid = false; break; }
                        AcquireSRWLockExclusive(&input_lock);
                        if (stopping.load() || (controlled && selected != command.index)) { valid = false; }
                        else {
                            controlled = true; selected = command.index;
                            pad = command.pad;
                            expires = GetTickCount64() + command.lease;
                        }
                        ReleaseSRWLockExclusive(&input_lock);
                        break;
                    case 3: neutral(); break;
                    case 4: valid = pass_through(); break;
                    case 5: stop_bridge(); break;
                    default: valid = false; break;
                    }
                    char response[384];
                    AcquireSRWLockShared(&input_lock);
                    ULONGLONG now = GetTickCount64();
                    ULONGLONG lease = expires > now ? expires - now : 0;
                    sprintf(response, "{\"ok\":%s,\"pid\":%lu,\"polls\":%lu,\"poll_thread\":%lu,\"controlled\":%s,\"index\":%lu,\"packet\":%lu,\"lease_ms\":%llu,\"stopping\":%s}\n",
                            valid ? "true" : "false", GetCurrentProcessId(), polls.load(), poll_thread.load(),
                            controlled ? "true" : "false", selected, published[selected].number, lease, stopping.load() ? "true" : "false");
                    ReleaseSRWLockShared(&input_lock);
                    if (!reply(client, response)) break;
                    fprintf(log, "{\"event\":\"command\",\"op\":%u,\"tick_ms\":%llu,\"ok\":%s}\n",
                            command.op, GetTickCount64(), valid ? "true" : "false");
                    fflush(log);
                    if (stopping.load()) break;
                }
                neutral();
            }
            closesocket(client);
        }
    }
end:
    stop_services();
    if (log) selection_observer::flush(log);
    loadout_lua_activation::flush(log);
    loadout_selection_bridge::flush(log);
    loadout_chain_switch::flush(log);
    if (log) gameplay_runtime::flush(log);
    if (log) loadout_mod::flush(log);
    if (log) {
        const bool scope_drained = loadout_selection_scope::flush(log);
        if (!scope_drained || loadout_selection_scope::stop_incomplete.load()) {
            fprintf(log, "{\"event\":\"loadout_selection_scope_shutdown\",\"telemetry_partial\":true}\n");
            fflush(log);
        }
        const bool actor_drained = loadout_attack_probe::flush(log);
        if (!actor_drained || loadout_attack_probe::stop_incomplete.load()) {
            fprintf(log, "{\"event\":\"loadout_actor_shutdown\",\"telemetry_partial\":true}\n");
            fflush(log);
        }
    }
    if (log) native_ui_probe::flush(log);
    if (log) native_ui::sdk::flush(log);
    if (log) native_ui_invoke_probe::flush(log);
    if (log) native_ui_echo::flush(log);
    native_ui_transport::flush(log);
    neutral();
    if (listener != INVALID_SOCKET) closesocket(listener);
    if (network) WSACleanup();
    if (log) { fprintf(log, "{\"event\":\"stopped\",\"polls\":%lu}\n", polls.load()); fclose(log); }
    FreeLibraryAndExitThread(static_cast<HMODULE>(reference), 0);
}
#else
static DWORD WINAPI service(void* reference) {
    mod_bindings::start();
    loadout_audio_trace::start();
    loadout_queue_audio::configure_self_check();
    selection_observer::start(nullptr,stopping,true,backend);
    while(!stopping.load()) {
        if(loadout_selection_bridge::enabled.load())loadout_queue_audio::prepare(self_module,stopping);
        loadout_queue_audio::run_self_check();
        sample_loadout_cycle_keys(nullptr);
        loadout_queue_audio::probe_progress();
        loadout_audio_trace::flush();
        Sleep(10);
    }
    mod_bindings::persist(true);
    stop_services();
    loadout_audio_trace::stop();
    neutral();
    FreeLibraryAndExitThread(static_cast<HMODULE>(reference),0);
}
#endif

static BOOL CALLBACK initialize(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t directory[MAX_PATH];
    UINT size = GetSystemDirectoryW(directory, MAX_PATH);
    if (size && size < MAX_PATH - 20) {
        wcscat(directory, L"\\xinput1_4.dll");
        backend = LoadLibraryW(directory);
        if (backend == self_module) { FreeLibrary(backend); backend = nullptr; }
    }
#if CRMOD_DIAGNOSTICS
    char token[33]{};
    SetLastError(ERROR_SUCCESS);
    DWORD token_size = GetEnvironmentVariableA("CRMOD_TOKEN", token, sizeof(token));
    bool token_absent = !token_size && GetLastError() == ERROR_ENVVAR_NOT_FOUND;
    SetLastError(ERROR_SUCCESS);
    DWORD path_size = GetEnvironmentVariableW(L"CRMOD_LOG", log_path, 32768);
    bool path_absent = !path_size && GetLastError() == ERROR_ENVVAR_NOT_FOUND;
    if (token_absent && path_absent) {
        wchar_t config[32768];
        DWORD length = GetModuleFileNameW(nullptr, config, 32768);
        if (!length || length >= 32768) return TRUE;
        wchar_t* separator = wcsrchr(config, L'\\');
        if (!separator || separator - config > 32768 - 32) return TRUE;
        separator[1] = L'\0';
        wcscpy(log_path, config);
        wcscat(config, L"crmod.ini");
        const auto policy=mod_bindings::startup_policy(config);
        if(!policy.enabled)return TRUE;
        if(policy.observe)wcscat(log_path,L"crmod-manual.jsonl");
        else wcscpy(log_path,L"NUL"); // Logging verbosity never disables the gameplay service.
    } else {
        if (token_size != 32 || !path_size || path_size >= 32768) return TRUE;
        for (int i = 0; i < 32; ++i) {
            char c = token[i];
            int nibble = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (nibble < 0) return TRUE;
            if (i % 2 == 0) secret[i / 2] = static_cast<BYTE>(nibble << 4);
            else secret[i / 2] |= static_cast<BYTE>(nibble);
        }
        automation_enabled = true;
    }
#else
    wchar_t config[32768];
    const DWORD length=GetModuleFileNameW(nullptr,config,32768);
    if(!length || length>=32768)return TRUE;
    wchar_t* separator=wcsrchr(config,L'\\');
    if(!separator || separator-config>32768-32)return TRUE;
    separator[1]=L'\0';wcscat(config,L"crmod.ini");
    if(!GetPrivateProfileIntW(L"Mod",L"Enabled",1,config))return TRUE;
#endif
    HMODULE reference = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(&initialize), &reference)) return TRUE;
    HANDLE thread = CreateThread(nullptr, 0, service, reference, 0, nullptr);
    if (thread) CloseHandle(thread); else FreeLibrary(reference);
    return TRUE;
}

template<class T> static T real(const char* name) {
    InitOnceExecuteOnce(&init_once, initialize, nullptr, nullptr);
    return backend ? reinterpret_cast<T>(GetProcAddress(backend, name)) : nullptr;
}

static bool synthetic(DWORD index) {
    AcquireSRWLockShared(&input_lock);
    bool result = controlled && selected == index;
    ReleaseSRWLockShared(&input_lock);
    return result;
}

extern "C" DWORD WINAPI XInputGetState(DWORD index, XINPUT_STATE* state) {
    if (!state || index >= XUSER_MAX_COUNT) return ERROR_BAD_ARGUMENTS;
    auto fn = real<decltype(&XInputGetState)>("XInputGetState");
    CRMOD_DIAGNOSTIC(++polls;poll_thread.store(GetCurrentThreadId()));
    XINPUT_STATE native{};
    DWORD result = fn ? fn(index, &native) : ERROR_DEVICE_NOT_CONNECTED;
#if CRMOD_DIAGNOSTICS
    performance_log::Scope timing(performance_log::Phase::InputFilter);
#endif
    DWORD native_packet = native.dwPacketNumber;
    *state = native;
    const bool modal=mod_bindings::panel_open.load() &&
        loadout_key_requests::game_foreground(GetForegroundWindow());
    AcquireSRWLockExclusive(&input_lock);
    bool is_synthetic = controlled && selected == index;
    if (is_synthetic) {
        if (expires && expires <= GetTickCount64()) { pad = {}; expires = 0; }
        state->Gamepad = enabled.load() && !stopping.load() ? pad : XINPUT_GAMEPAD{};
        result = ERROR_SUCCESS;
    }
    publish_menu_state(index,result,state,is_synthetic,native_packet,modal);
    ReleaseSRWLockExclusive(&input_lock);
    return result;
}
extern "C" DWORD WINAPI XInputSetState(DWORD i, XINPUT_VIBRATION* v) {
    auto fn = real<decltype(&XInputSetState)>("XInputSetState");
    if (!v || i >= XUSER_MAX_COUNT) return ERROR_BAD_ARGUMENTS;
    if (synthetic(i)) return ERROR_SUCCESS;
    return fn ? fn(i, v) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI XInputGetCapabilities(DWORD i, DWORD flags, XINPUT_CAPABILITIES* caps) {
    auto fn = real<decltype(&XInputGetCapabilities)>("XInputGetCapabilities");
    if (!caps || i >= XUSER_MAX_COUNT || (flags != 0 && flags != XINPUT_FLAG_GAMEPAD)) return ERROR_BAD_ARGUMENTS;
    if (synthetic(i)) {
        *caps = {};
        caps->Type = XINPUT_DEVTYPE_GAMEPAD;
        caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        caps->Gamepad = {0xf3ff, 0xff, 0xff, -1, -1, -1, -1};
        return ERROR_SUCCESS;
    }
    return fn ? fn(i, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" void WINAPI XInputEnable(BOOL value) {
    auto fn = real<decltype(&XInputEnable)>("XInputEnable");
    enabled.store(value != FALSE); if (fn) fn(value);
}
extern "C" DWORD WINAPI XInputGetBatteryInformation(DWORD i, BYTE type, XINPUT_BATTERY_INFORMATION* info) {
    auto fn = real<decltype(&XInputGetBatteryInformation)>("XInputGetBatteryInformation");
    if (!info || i >= XUSER_MAX_COUNT) return ERROR_BAD_ARGUMENTS;
    if (synthetic(i)) { *info = {BATTERY_TYPE_WIRED, BATTERY_LEVEL_FULL}; return ERROR_SUCCESS; }
    return fn ? fn(i, type, info) : ERROR_DEVICE_NOT_CONNECTED;
}
using KeystrokeFn = DWORD (WINAPI*)(DWORD, DWORD, XINPUT_KEYSTROKE*);
static DWORD filter_keystroke(KeystrokeFn fn, DWORD index, DWORD flags, XINPUT_KEYSTROKE* key) {
    if (index < XUSER_MAX_COUNT && synthetic(index)) { *key = {}; return ERROR_EMPTY; }
    if (!fn) return ERROR_DEVICE_NOT_CONNECTED;
    for (unsigned attempts = 0; attempts < 32; ++attempts) {
        DWORD result = fn(index, flags, key);
        if (result != ERROR_SUCCESS) return result;
        if (!synthetic(key->UserIndex)) return ERROR_SUCCESS;
        *key = {};
    }
    return ERROR_EMPTY;
}
extern "C" DWORD WINAPI XInputGetKeystroke(DWORD i, DWORD flags, XINPUT_KEYSTROKE* key) {
    auto fn = real<KeystrokeFn>("XInputGetKeystroke");
    if (!key || (i >= XUSER_MAX_COUNT && i != XUSER_INDEX_ANY) || flags != 0) return ERROR_BAD_ARGUMENTS;
    return filter_keystroke(fn, i, flags, key);
}
extern "C" DWORD WINAPI XInputGetAudioDeviceIds(DWORD i, LPWSTR render, UINT* nr, LPWSTR capture, UINT* nc) {
    using Fn = DWORD (WINAPI*)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);
    auto fn = real<Fn>("XInputGetAudioDeviceIds");
    return fn ? fn(i, render, nr, capture, nc) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" void WINAPI CrModStop() { stop_bridge(); }
#ifdef CRMOD_PLUGIN
static DWORD WINAPI plugin_start(void*) {
    // Runs after the loader lock is released; native feature guards are unchanged.
    InitOnceExecuteOnce(&init_once,initialize,nullptr,nullptr);
    return 0;
}
#endif
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self_module = module; DisableThreadLibraryCalls(module);
#ifdef CRMOD_PLUGIN
        HANDLE thread=CreateThread(nullptr,0,plugin_start,nullptr,0,nullptr);
        if(!thread)return FALSE;
        CloseHandle(thread);
#endif
    }
    return TRUE;
}
