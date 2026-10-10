// Passive, build-pinned observations only. Native pointers never reach the writer.
#pragma once
#include <wincrypt.h>
#include <MinHook.h>
#include "loadout_selection_scope.h"
#include "loadout_lookup_probe.h"
#include "gameplay_runtime.h"
#include "loadout_player_catalogue.h"
#include "native_ui_transport.h"
#include "loadout_selection_bridge.h"
#include "loot_multiplier.h"
#include "controller_menu_input.h"
#include "split_form_retention.h"

namespace selection_observer {
using Commit = void (*)(uint32_t, void*, void*);
using Setter = void (*)(uint16_t, void*);
using FormLookup = uint8_t* (*)(uint8_t*, const uint32_t*, const void*);
using UISetup = void (*)(void*, void*);
static Commit original_commit;
static Setter original_setter;
static FormLookup original_form_lookup;
static UISetup original_ui_setup;
static SRWLOCK queue_lock = SRWLOCK_INIT;
static std::atomic<bool> recording{false};
static std::atomic<uint64_t> calls{0}, dropped{0};
static constexpr unsigned setter_record_limit=32768;
static std::atomic<unsigned> setter_admissions{0};
static std::atomic<unsigned> attempts{0};
#ifdef CRMOD_API_SELF_CHECK
static void (*drop_test_barrier)() = nullptr;
static void (*stop_test_barrier)() = nullptr;
#endif
struct Record {
    uint64_t call, qpc, first, second;
    DWORD thread;
    uint32_t id;
    uint16_t stage, flags;
    unsigned kind;
};
static Record queue[256];
static unsigned count;

static void record(unsigned kind, uint64_t call, uint32_t id, uint16_t stage,
                   void* first, void* second = nullptr) {
    if (!recording.load()) return;
    // Bound hot diagnostic traffic; native commit records remain available.
    if (kind==2 || kind==3) {
        if (setter_admissions.load(std::memory_order_relaxed)>=setter_record_limit) return;
        if (setter_admissions.fetch_add(1,std::memory_order_relaxed)>=setter_record_limit) return;
    }
    DWORD error = GetLastError();
    ++attempts;
    // Recheck after registration: a producer delayed across stop cannot add a late drop.
    if (recording.load()) {
        if (!TryAcquireSRWLockExclusive(&queue_lock)) {
#ifdef CRMOD_API_SELF_CHECK
            if (drop_test_barrier) drop_test_barrier();
#endif
            ++dropped;
        }
        else {
            if (recording.load()) {
                if (count == 256) ++dropped;
                else {
                    LARGE_INTEGER tick{};
                    QueryPerformanceCounter(&tick);
                    Record row{call, static_cast<uint64_t>(tick.QuadPart),
                               reinterpret_cast<uint64_t>(first), reinterpret_cast<uint64_t>(second),
                               GetCurrentThreadId(), id, stage, 0, kind};
                    if (kind >= 2) {
                        // These fields are copied only within the evidenced native setter call.
                        memcpy(&row.id, static_cast<const BYTE*>(first) + 0x1e8, sizeof(row.id));
                        memcpy(&row.flags, static_cast<const BYTE*>(first) + 0x1f4, sizeof(row.flags));
                    }
                    queue[count++] = row;
                }
            }
            ReleaseSRWLockExclusive(&queue_lock);
        }
    }
    --attempts;
    SetLastError(error);
}
static void commit(uint32_t id, void* graph, void* context) {
    uint64_t call = ++calls;
    record(0, call, id, 0, graph, context);
    loadout_selection_scope::observe_choice(id, graph, context, false);
    original_commit(id, graph, context);
    loadout_selection_scope::observe_choice(id, graph, context, true);
    record(1, call, id, 0, graph, context);
}
static void setter(uint16_t stage, void* node) {
    uint64_t call = ++calls;
    record(2, call, 0, stage, node);
    original_setter(stage, node);
    record(3, call, 0, stage, node);
}
static uint8_t* form_lookup(uint8_t* out, const uint32_t* id, const void* cfg) {
    uint8_t* result = original_form_lookup(out, id, cfg);
    loadout_mod::observe_form_result(out, id, cfg);
    return result;
}
static void ui_setup(void* object, void* view) {
    native_ui_probe::observe(view);
    native_ui::sdk::on_current_view(view);
    original_ui_setup(object, view);
}
static void stop() {
    AcquireSRWLockExclusive(&queue_lock);
    recording.store(false);
    ReleaseSRWLockExclusive(&queue_lock);
#ifdef CRMOD_API_SELF_CHECK
    if (stop_test_barrier) stop_test_barrier();
#endif
    // Only the stopping thread waits; record attempts never wait on this counter.
    while (attempts.load()) Sleep(1);
    // Hooks, trampolines and the pinned module survive until process exit.
}
static void flush(FILE* log) {
    Record rows[256];
    AcquireSRWLockExclusive(&queue_lock);
    unsigned size = count;
    memcpy(rows, queue, size * sizeof(Record));
    count = 0;
    ReleaseSRWLockExclusive(&queue_lock);
    for (unsigned i = 0; i < size; ++i) {
        const auto& r = rows[i];
        fprintf(log, "{\"event\":\"selection_observation\",\"kind\":%u,\"call\":%llu,\"qpc\":%llu,\"thread\":%lu,\"first\":%llu,\"second\":%llu,\"id\":%u,\"stage\":%u,\"raw_flags\":%u}\n",
                r.kind, r.call, r.qpc, r.thread, r.first, r.second, r.id, r.stage, r.flags);
    }
    uint64_t lost = dropped.exchange(0);
    if (lost) fprintf(log, "{\"event\":\"selection_observation_dropped\",\"count\":%llu}\n", lost);
    if (size || lost) fflush(log);
}

static bool main_exe_matches() {
    static const BYTE expected[32] = {
        0x2c,0x65,0x75,0xbe,0x23,0xea,0x9a,0x2d,0x31,0x6f,0xb5,0x30,0xd0,0x94,0x77,0x3b,
        0x37,0x1a,0xb1,0xda,0x63,0x44,0xaa,0x7a,0x97,0xb8,0xcc,0x2d,0xab,0xaf,0x1c,0xa0};
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart == 99490224 &&
              CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
              CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    BYTE buffer[65536], digest[32];
    DWORD bytes = 0, digest_size = sizeof(digest);
    while (ok) {
        if (!ReadFile(file, buffer, sizeof(buffer), &bytes, nullptr)) { ok = false; break; }
        if (!bytes) break;
        ok = CryptHashData(hash, buffer, bytes, 0) != FALSE;
    }
    ok = ok && CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0) &&
         digest_size == sizeof(digest) && memcmp(digest, expected, sizeof(digest)) == 0;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return ok;
}
static bool loaded_targets(void** commit_target, void** setter_target, void** form_target, void** ui_target = nullptr, void** actor_target = nullptr, void** scope_target = nullptr) {
    static const BYTE commit_bytes[] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x60,0x8b,0xd9,0x4d,0x8b,0xf0,0x49,0x8d,0x88,0x58,0x18,0x00,0x00};
    static const BYTE setter_bytes[] = {0x48,0x83,0xec,0x08,0x44,0x0f,0xb7,0x82,0xf4,0x01,0x00,0x00,0x4c,0x8b,0xda,0x44,0x0f,0xb7,0xd1,0x66,0x45,0x85,0xc2,0x75,0x6e};
    static const BYTE form_bytes[] = {0x49,0x8b,0x40,0x08,0x4c,0x8b,0xc9,0x45,0x8b,0x40,0x10,0x4f,0x8d,0x04,0x80,0x49,0xc1,0xe0,0x06,0x4c,0x03,0xc0,0x49,0x3b,0xc0};
    static const BYTE ui_bytes[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0xf1,0x48};
    static const BYTE actor_bytes[] = {0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xa8,0x48,0x81,0xec,0x58,0x01};
    static const BYTE scope_bytes[] = {0x40,0x53,0x48,0x83,0xec,0x40,0x49,0x8b,0x40,0x58,0x4c,0x8b,0x50,0x30,0x4c,0x8b,0x58,0x28,0x48,0x8b,0x58,0x20,0x4c,0x8b,0x48,0x18,0x4c,0x8b,0x40,0x10,0x48,0x8b,0x50,0x08,0x48,0x8b,0x08,0x4c,0x89,0x54,0x24,0x30,0x4c,0x89,0x5c,0x24,0x28,0x48,0x89,0x5c,0x24,0x20,0xe8,0x37};
    BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    SIZE_T read = 0;
    HANDLE process = GetCurrentProcess();
    if (!base || !ReadProcessMemory(process, base, &dos, sizeof(dos), &read) || read != sizeof(dos) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || dos.e_lfanew > 0x100000 ||
        !ReadProcessMemory(process, base + dos.e_lfanew, &nt, sizeof(nt), &read) || read != sizeof(nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.OptionalHeader.ImageBase != 0x140000000ULL ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) || nt.FileHeader.NumberOfSections > 96) return false;
    BYTE* headers = base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned index = 0; index < nt.FileHeader.NumberOfSections; ++index) {
        IMAGE_SECTION_HEADER section{};
        if (!ReadProcessMemory(process, headers + index * sizeof(section), &section, sizeof(section), &read) || read != sizeof(section)) return false;
        if (memcmp(section.Name, ".text\0\0\0", 8) != 0) continue;
        SIZE_T length = section.Misc.VirtualSize;
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) || length < sizeof(commit_bytes) ||
            section.VirtualAddress >= nt.OptionalHeader.SizeOfImage || length > nt.OptionalHeader.SizeOfImage - section.VirtualAddress) return false;
        BYTE* text = base + section.VirtualAddress;
        for (SIZE_T offset = 0; offset < length;) {
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(text + offset, &memory, sizeof(memory)) || memory.State != MEM_COMMIT || memory.AllocationBase != base ||
                (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                !(memory.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
            SIZE_T available = static_cast<BYTE*>(memory.BaseAddress) + memory.RegionSize - (text + offset);
            if (!available) return false;
            offset += available < length - offset ? available : length - offset;
        }
        unsigned commits = 0, setters = 0, forms = 0, ui = 0, actors = 0, scopes = 0;
        for (SIZE_T i = 0; i < length; ++i) {
            if (i + sizeof(commit_bytes) <= length && memcmp(text + i, commit_bytes, sizeof(commit_bytes)) == 0) {
                ++commits; *commit_target = text + i;
            }
            if (i + sizeof(setter_bytes) <= length && memcmp(text + i, setter_bytes, sizeof(setter_bytes)) == 0) {
                ++setters; *setter_target = text + i;
            }
            if (i + sizeof(form_bytes) <= length && memcmp(text + i, form_bytes, sizeof(form_bytes)) == 0) {
                ++forms; *form_target = text + i;
            }
            if (ui_target && i + sizeof(ui_bytes) <= length && memcmp(text + i, ui_bytes, sizeof(ui_bytes)) == 0) {
                ++ui; *ui_target = text + i;
            }
            if (actor_target && i + sizeof(actor_bytes) <= length && memcmp(text + i, actor_bytes, sizeof(actor_bytes)) == 0) {
                ++actors; *actor_target = text + i;
            }
            if (scope_target && i + sizeof(scope_bytes) <= length && memcmp(text + i, scope_bytes, sizeof(scope_bytes)) == 0) {
                ++scopes; *scope_target = text + i;
            }
        }
        return commits == 1 && setters == 1 && forms == 1 &&
               *commit_target == base + 0x1f08580 && *setter_target == base + 0x1ddc7e0 &&
               *form_target == base + 0x235cac0 &&
               (!ui_target || (ui == 1 && *ui_target == base + 0x17e6eb0)) &&
               (!actor_target || (actors == 1 && *actor_target == base + 0x234b100)) &&
               (!scope_target || (scopes == 1 && *scope_target == base + 0x1f0b4d0));
    }
    return false;
}
static void start(FILE* log, const std::atomic<bool>& stopping, bool manual_requested = false, HMODULE input_backend=nullptr) {
    char option[2]{};
    if (!manual_requested &&
        (GetEnvironmentVariableA("CRMOD_OBSERVE_SELECTION", option, sizeof(option)) != 1 || option[0] != '1')) return;
    void* commit_target = nullptr;
    void* setter_target = nullptr;
    void* form_target = nullptr;
    void* ui_target = nullptr;
    void* actor_target = nullptr;
    void* scope_target = nullptr;
    const bool scope_probe = true; // Current native selection execution loan.
    const bool lookup_probe = true; // Current native selection execution loan.
    if (lookup_probe && !scope_probe) {
        fprintf(log, "{\"event\":\"loadout_lookup_probe\",\"status\":\"scope_probe_required\"}\n");
        fflush(log); return;
    }
    char actor_option[2]{};
    const bool actor_probe = GetEnvironmentVariableA("CRMOD_OBSERVE_ACTOR", actor_option, sizeof(actor_option)) == 1 && actor_option[0] == '1';
    char ui_option[2]{};
    const bool passive_ui = GetEnvironmentVariableA("CRMOD_OBSERVE_UI", ui_option, sizeof(ui_option)) == 1 && ui_option[0] == '1';
    const bool bootstrap_ui = true; // Ordinary feature UI, guarded by exact native contracts.
    const bool invoke_probe = true; // Ordinary feature UI, guarded by exact native contracts.
    const bool echo_probe = true; // Ordinary feature UI, guarded by exact native contracts.
    if (echo_probe && !invoke_probe) {
        fprintf(log, "{\"event\":\"native_ui_echo\",\"status\":\"invoke_probe_required\"}\n");
        fflush(log);
        return;
    }
    if (invoke_probe && !bootstrap_ui) {
        fprintf(log, "{\"event\":\"native_ui_invoke_probe\",\"status\":\"bootstrap_required\"}\n");
        fflush(log);
        return;
    }
    const bool observe_ui = passive_ui || bootstrap_ui;
    const char* status = "executable_mismatch";
    MH_STATUS hook_status = MH_UNKNOWN;
    if (main_exe_matches()) {
        status = "loaded_bytes_mismatch";
        if (loaded_targets(&commit_target, &setter_target, &form_target, observe_ui ? &ui_target : nullptr,
                           actor_probe ? &actor_target : nullptr, scope_probe ? &scope_target : nullptr) && !stopping.load()) {
            if (bootstrap_ui) {
                const auto guard = native_ui_guard::verify(true);
                if (!native_ui::sdk::prepare(guard, cr_loadout_script, stopping)) {
                    fprintf(log, "{\"event\":\"native_ui_bootstrap\",\"status\":\"guard_rejected\"}\n");
                    fflush(log);
                    return;
                }
                if (invoke_probe) {
                    if (!native_ui_invoke_probe::prepare(true, native_ui_guard::verify_registration(guard))) {
                        native_ui::sdk::stop();
                        fprintf(log, "{\"event\":\"native_ui_invoke_probe\",\"status\":\"guard_rejected\"}\n");
                        fflush(log);
                        return;
                    }
                    native_ui::sdk::before_script = &native_ui_invoke_probe::register_current;
                    if (echo_probe) {
                        if (!native_ui_echo::prepare(true)) {
                            native_ui_invoke_probe::stop(); native_ui::sdk::stop();
                            fprintf(log, "{\"event\":\"native_ui_echo\",\"status\":\"guard_rejected\"}\n");
                            fflush(log);
                            return;
                        }
                        if (!native_ui_transport::prepare()) {
                            native_ui_transport::stop(); native_ui_echo::stop();
                            native_ui_invoke_probe::stop(); native_ui::sdk::stop();
                            fprintf(log, "{\"event\":\"native_ui_transport\",\"status\":\"guard_rejected\"}\n");
                            fflush(log); return;
                        }
                    }
                }
            }
            status = "module_pin_failed";
            HMODULE pinned = nullptr;
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                  reinterpret_cast<LPCWSTR>(&commit), &pinned)) {
                const auto game_base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
                const auto matches=[&](std::uint32_t rva,const char* hex) {
                    unsigned char bytes[64]{}; const std::size_t size=std::strlen(hex)/2;
                    if (size>sizeof(bytes) || !loadout_attack_probe::copy(game_base+rva,bytes,size)) return false;
                    for (std::size_t i=0;i<size;++i)
                        if (bytes[i]!=(native_pins::hexval(hex[i*2])*16+native_pins::hexval(hex[i*2+1]))) return false;
                    return true;
                };
                const bool catalogue_ready=matches(0x1f4f7a0,"488bc448895808488970184889782048895010554154415541564157488d6898") &&
                    matches(0x1f58410,"48895c241055565741544155415641574881ecb0000000488b01450fb788") &&
                    matches(0x1f5a2d0,"40574881ecb00000004c8b114d8bd80fb7c233ff4d8b44c250");
                const bool menu_ready=catalogue_ready && matches(0x1f636e0,
                    "4883ec488b05fe5cd3034c8bc925ffffff00448d04c5000000");
                // ponytail: one installation; even partial failures retain forwarding paths until exit.
                if (lookup_probe) {
                    if (!loadout_lookup_probe::prepare(true,
                            reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),true,true,true)) {
                        fprintf(log, "{\"event\":\"loadout_lookup_probe\",\"status\":\"guard_rejected\"}\n");
                        fflush(log); return;
                    }
                    loadout_selection_scope::lookup_observer=&loadout_lookup_probe::observe;
                }
                status = "hook_install_failed";
                const bool chooser_probe=gameplay_runtime::prepare(
                    reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),&stopping);
                const bool activation_ready=loadout_lua_activation::verify(game_base,true);
                const auto activation_hooks=loadout_lua_activation::hook_specs();
                const auto hold_hooks=[] {
                    const auto state=held_lock_native::hook_specs();
                    const auto mapping=held_lock_native::mapping_hook_specs();
                    std::array<held_lock_native::Hook,6> all{};
                    for (std::size_t i=0;i<3;++i) { all[i]=state[i]; all[i+3]=mapping[i]; }
                    return all;
                }();
                bool hold_ready=chooser_probe;
                for (const auto& hook:hold_hooks) hold_ready=hold_ready&&matches(hook.rva,hook.pin);
                const bool geometry_ready=chooser_probe && matches(0x22792a0,
                    "48895c240848896c2418565741564883ec40c5f82974243049");
                const bool finisher_commit_ready=chooser_probe &&
                    matches(0x235e6c0,"488bc44c89401848895010534881ec300100004c8b59484d8b") &&
                    matches(0x235d2b0,"48895c240848897c24108b4224488bd9486bc0384903008b50");
                const bool chain_ready=finisher_commit_ready && matches(0x22965b0,
                    "4c8b9100080000498bca4d8b4a08450fb659194584db7520498bc1");
                const bool finisher_test_ready=finisher_commit_ready && finisher_test_mode::unlocked_test_patch &&
                    matches(0x2339010,"4180783400448bc974708b02413b40087368488d048048c1e0");
                const bool attack_turn_ready=chooser_probe &&
                    matches(attack_turn_native::kUpdateRva,attack_turn_native::kUpdatePin);
                const bool split_ready=finisher_commit_ready && catalogue_ready &&
                    matches(split_form_retention::kPredicateRva,split_form_retention::kPredicatePin) &&
                    matches(0x2349936,split_form_retention::kSpawnCallPin);
                hook_status = MH_Initialize();
#ifdef CRMOD_PLUGIN
                const bool menu_input_ready=hook_status==MH_OK&&controller_menu_input::prepare(input_backend);
                if(!menu_input_ready)hook_status=MH_ERROR_NOT_INITIALIZED;
#else
                (void)input_backend;
#endif
                void* unlock_original=nullptr;
                if (hook_status==MH_OK && finisher_test_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+0x2339010),
                        reinterpret_cast<void*>(&finisher_binding::test_unlock_hook),&unlock_original);
                finisher_binding::original_unlock=reinterpret_cast<finisher_binding::UnlockFn>(unlock_original);
                if (hook_status==MH_OK && split_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+split_form_retention::kPredicateRva),
                        reinterpret_cast<void*>(&split_form_retention::predicate),reinterpret_cast<void**>(&split_form_retention::original));
                void* graph_original=nullptr; void* input_original=nullptr;
                if (hook_status==MH_OK && finisher_commit_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+0x235e6c0),
                        reinterpret_cast<void*>(&finisher_binding::graph_detour),&graph_original);
                finisher_binding::set_graph_original(reinterpret_cast<finisher_binding::GraphFn>(graph_original));
                if (hook_status==MH_OK && finisher_commit_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+0x235d2b0),
                        reinterpret_cast<void*>(&finisher_binding::input_detour),&input_original);
                finisher_binding::set_input_original(reinterpret_cast<finisher_binding::InputFn>(input_original));
                if (hook_status==MH_OK && attack_turn_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+attack_turn_native::kUpdateRva),
                        reinterpret_cast<void*>(&attack_turn_native::update_hook),&attack_turn_native::original_update);
                if (hook_status==MH_OK && geometry_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+0x22792a0),
                        reinterpret_cast<void*>(&player_geometry_log::speed_hook),&player_geometry_log::original_speed);
                if (hook_status==MH_OK && hold_ready)
                    for (const auto& hook:hold_hooks)
                        if (hook_status==MH_OK) hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+hook.rva),hook.detour,hook.original);
                if (hook_status==MH_OK && activation_ready)
                    for (const auto& hook:activation_hooks)
                        if (hook_status==MH_OK) hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+hook.rva),hook.detour,hook.original);
                if (hook_status==MH_OK && catalogue_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+0x1f58410),
                        reinterpret_cast<void*>(&loadout_player_catalogue::dispatcher_a),&loadout_player_catalogue::original_a);
                if (hook_status==MH_OK && catalogue_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+0x1f5a2d0),
                        reinterpret_cast<void*>(&loadout_player_catalogue::dispatcher_b),&loadout_player_catalogue::original_b);
                if (hook_status==MH_OK && catalogue_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+0x1f4f7a0),
                        reinterpret_cast<void*>(&loadout_player_catalogue::body),&loadout_player_catalogue::original_body);
                const bool camera_probe=camera_lock_log::st().pins_ok;
                void* camera_original=nullptr;
                if (hook_status==MH_OK && camera_probe)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+camera_lock_log::kRvaLockOn),
                        reinterpret_cast<void*>(&camera_lock_log::lockon_detour),&camera_original);
                camera_lock_log::set_original(reinterpret_cast<camera_lock_log::LockFn>(camera_original));
                if (hook_status==MH_OK && gameplay_runtime::collision_ready) {
                    void* collision_original=nullptr;
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+0x277f170),
                        reinterpret_cast<void*>(&gameplay_runtime::collision_detour),&collision_original);
                    gameplay_runtime::original_collision=reinterpret_cast<collision_observer::Original4>(collision_original);
                }
                const bool step_observers_ready=false; // Entity stomp removed.
                void* loco_original=nullptr; void* jump_original=nullptr;
                if (hook_status==MH_OK && step_observers_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+step_binding::kRvaLoco),
                        reinterpret_cast<void*>(&step_binding::loco_detour),&loco_original);
                if (hook_status==MH_OK && step_observers_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+step_binding::kRvaJump),
                        reinterpret_cast<void*>(&step_binding::jump_detour),&jump_original);
                step_binding::set_originals(reinterpret_cast<step_binding::LocoFn>(loco_original),
                    reinterpret_cast<step_binding::JumpFn>(jump_original));
                void* menu_original=nullptr;
                if (hook_status==MH_OK && menu_ready)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(game_base+0x1f636e0),
                        reinterpret_cast<void*>(&native_ui_transport::menu_predicate),&menu_original);
                native_ui_transport::original_menu_predicate=reinterpret_cast<native_ui_transport::MenuPredicate>(menu_original);
                if (menu_ready && menu_original) loadout_selection_bridge::observe_menu=&native_ui_transport::observe_current_menu;
                void* chooser_original=nullptr;
                if (hook_status==MH_OK && chooser_probe)
                    hook_status=MH_CreateHook(reinterpret_cast<void*>(
                        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))+finisher_binding::kRvaChooser),
                        reinterpret_cast<void*>(&finisher_binding::chooser_detour),&chooser_original);
                if (chooser_original)
                    finisher_binding::set_original(reinterpret_cast<finisher_binding::ChooserFn>(chooser_original));
                void* commit_original = nullptr;
                void* setter_original = nullptr;
                void* form_original = nullptr;
                if (hook_status == MH_OK) hook_status = MH_CreateHook(commit_target, reinterpret_cast<void*>(&commit), &commit_original);
                original_commit = reinterpret_cast<Commit>(commit_original);
                const bool selection_kernel_ready=hook_status==MH_OK &&
                    loadout_native_selection::prepare(game_base,original_commit,true,true,true);
                if (selection_kernel_ready) {
                    loadout_selection_scope::after_original=&loadout_selection_bridge::after_original;
                    loadout_player_catalogue::adapter_factory=&loadout_selection_bridge::make_adapter;
                }
                if (hook_status == MH_OK) hook_status = MH_CreateHook(setter_target, reinterpret_cast<void*>(&setter), &setter_original);
                original_setter = reinterpret_cast<Setter>(setter_original);
                if (hook_status == MH_OK) hook_status = MH_CreateHook(form_target, reinterpret_cast<void*>(&form_lookup), &form_original);
                original_form_lookup = reinterpret_cast<FormLookup>(form_original);
                if (hook_status == MH_OK && actor_probe)
                    hook_status = MH_CreateHook(actor_target, reinterpret_cast<void*>(&loadout_attack_probe::forms),
                                                &loadout_attack_probe::original_forms);
                if (hook_status == MH_OK && scope_probe)
                    hook_status = MH_CreateHook(scope_target, reinterpret_cast<void*>(&loadout_selection_scope::shim),
                                                &loadout_selection_scope::original_shim);
                void* ui_original = nullptr;
                if (hook_status == MH_OK && observe_ui) hook_status = MH_CreateHook(ui_target, reinterpret_cast<void*>(&ui_setup), &ui_original);
                original_ui_setup = reinterpret_cast<UISetup>(ui_original);
                const auto ui_hooks = native_ui::sdk::hook_specs(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
                if (bootstrap_ui) for (const auto& hook : ui_hooks) {
                    if (hook_status == MH_OK) hook_status = MH_CreateHook(reinterpret_cast<void*>(hook.target), hook.detour, hook.original);
                }
                if (hook_status == MH_OK) hook_status = MH_QueueEnableHook(commit_target);
                if (hook_status==MH_OK && split_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+split_form_retention::kPredicateRva));
                if (hook_status==MH_OK && finisher_test_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+0x2339010));
                if (hook_status==MH_OK && catalogue_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+0x1f58410));
                if (hook_status==MH_OK && catalogue_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+0x1f5a2d0));
                if (hook_status==MH_OK && catalogue_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+0x1f4f7a0));
                if (hook_status==MH_OK && camera_probe)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+camera_lock_log::kRvaLockOn));
                if (hook_status==MH_OK && gameplay_runtime::collision_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+0x277f170));
                if (hook_status==MH_OK && step_observers_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+step_binding::kRvaLoco));
                if (hook_status==MH_OK && step_observers_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+step_binding::kRvaJump));
                if (hook_status==MH_OK && menu_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+0x1f636e0));
                if (hook_status==MH_OK && chooser_probe)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(
                        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))+finisher_binding::kRvaChooser));
                if (hook_status == MH_OK) hook_status = MH_QueueEnableHook(setter_target);
                if (hook_status == MH_OK) hook_status = MH_QueueEnableHook(form_target);
                if (hook_status == MH_OK && actor_probe) hook_status = MH_QueueEnableHook(actor_target);
                if (hook_status == MH_OK && scope_probe) hook_status = MH_QueueEnableHook(scope_target);
                if (hook_status == MH_OK && observe_ui) hook_status = MH_QueueEnableHook(ui_target);
                if (bootstrap_ui) for (const auto& hook : ui_hooks) {
                    if (hook_status == MH_OK) hook_status = MH_QueueEnableHook(reinterpret_cast<void*>(hook.target));
                }
                if (hook_status==MH_OK && activation_ready)
                    for (const auto& hook:activation_hooks)
                        if (hook_status==MH_OK) hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+hook.rva));
                if (hook_status==MH_OK && hold_ready)
                    for (const auto& hook:hold_hooks)
                        if (hook_status==MH_OK) hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+hook.rva));
                if (hook_status==MH_OK && geometry_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+0x22792a0));
                if (hook_status==MH_OK && finisher_commit_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+0x235e6c0));
                if (hook_status==MH_OK && finisher_commit_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+0x235d2b0));
                if (hook_status==MH_OK && attack_turn_ready)
                    hook_status=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+attack_turn_native::kUpdateRva));
                bool loot_ready=hook_status==MH_OK && loot_multiplier::prepare(game_base);
                if(loot_ready){
                    auto one=MH_CreateHook(reinterpret_cast<void*>(game_base+0x26f3380),reinterpret_cast<void*>(&loot_multiplier::initialize),reinterpret_cast<void**>(&loot_multiplier::original_init));
                    auto two=MH_CreateHook(reinterpret_cast<void*>(game_base+0x21a19e0),reinterpret_cast<void*>(&loot_multiplier::collector),reinterpret_cast<void**>(&loot_multiplier::original_collector));
                    auto three=MH_CreateHook(reinterpret_cast<void*>(game_base+0x219eb60),reinterpret_cast<void*>(&loot_multiplier::award),reinterpret_cast<void**>(&loot_multiplier::original_award));
                    loot_ready=one==MH_OK && two==MH_OK && three==MH_OK;
                    if(loot_ready)for(auto rva:{0x26f3380u,0x21a19e0u,0x219eb60u})loot_ready=MH_QueueEnableHook(reinterpret_cast<void*>(game_base+rva))==MH_OK && loot_ready;
                }
                if (hook_status == MH_OK) hook_status = MH_ApplyQueued();
                split_form_retention::executable_base=game_base;
                split_form_retention::stopping=&stopping;
                split_form_retention::enabled.store(hook_status==MH_OK && split_ready);
                std::fprintf(log,"{\"event\":\"split_form_retention\",\"armed\":%s,\"native_lifetime\":true}\n",
                    split_form_retention::enabled.load()?"true":"false");
                controller_menu_input::enabled.store(hook_status==MH_OK);
#ifdef CRMOD_PLUGIN
                std::fprintf(log,"{\"event\":\"controller_menu_filter\",\"armed\":%s,\"raw_sampler_uses_trampoline\":true}\n",controller_menu_input::enabled.load()?"true":"false");
#endif
                loot_multiplier::enabled.store(hook_status==MH_OK && loot_ready);
                std::fprintf(log,"{\"event\":\"loot_binding\",\"armed\":%s}\n",loot_multiplier::enabled.load()?"true":"false");
                finisher_test_mode::enabled.store(hook_status==MH_OK && finisher_test_ready && unlock_original);
                std::fprintf(log,"{\"event\":\"finisher_test_mode\",\"enabled\":%s,\"scope\":\"six_finishers_only\",\"save_unlock_writes\":false,\"normal_release_must_disable\":true}\n",
                    finisher_test_mode::active()?"true":"false");
                const bool geometry_armed=hook_status==MH_OK && geometry_ready && player_geometry_log::arm(true,stopping);
                std::fprintf(log,"{\"event\":\"player_geometry_binding\",\"guard_verified\":%s,\"armed\":%s,\"writes\":false}\n",geometry_ready?"true":"false",geometry_armed?"true":"false");
                const bool hold_armed=hook_status==MH_OK && hold_ready &&
                    held_lock_native::arm(game_base,true,stopping);
                std::fprintf(log,"{\"event\":\"shared_input_reader\",\"initialized\":%s,\"stomp_writes_enabled\":%s,\"stomp_hooks_installed\":%s}\n",
                    step_binding::env().copy && step_binding::env().base==game_base?"true":"false",
                    step_binding::env().write?"true":"false",step_observers_ready?"true":"false");
                const bool direction_armed=hook_status==MH_OK && hold_armed && facing_lock_native::arm(game_base,true,stopping);
                const bool turn_armed=hook_status==MH_OK && direction_armed && attack_turn_ready &&
                    attack_turn_native::arm(game_base,true,stopping);
                finisher_binding::set_direction_reader(direction_armed ? &facing_lock_native::direction_sample : nullptr);
                finisher_binding::set_attack_start_observer(turn_armed ? &attack_turn_native::accepted : nullptr);
                finisher_binding::set_form_activator(hook_status==MH_OK && loadout_native_selection::enabled.load()?&loadout_native_selection::activate_finisher:nullptr);
                finisher_binding::set_commit_hooks_ready(hook_status==MH_OK && finisher_commit_ready);
                loadout_chain_switch::active=reinterpret_cast<loadout_chain_switch::Active>(game_base+0x22965b0);
                loadout_chain_switch::enabled=hook_status==MH_OK && chain_ready;
                std::fprintf(log,"{\"event\":\"loadout_chain_binding\",\"armed\":%s,\"handoff\":\"next_native_attack_to_new_first_entry\",\"synthetic_input\":false}\n",
                    loadout_chain_switch::enabled?"true":"false");
                std::fprintf(log,"{\"event\":\"finisher_dispatch_binding\",\"graph_input_guards\":%s,\"armed\":%s,\"back_turn_armed\":%s,\"unselected_same_press_verified\":false,\"movement_writes\":false}\n",
                    finisher_commit_ready?"true":"false",(direction_armed&&finisher_commit_ready)?"true":"false",turn_armed?"true":"false");
                std::fprintf(log,"{\"event\":\"target_direction_binding\",\"armed\":%s,\"movement_writes\":false,\"continuous_facing_override\":false}\n",direction_armed?"true":"false");
                std::fprintf(log,"{\"event\":\"held_lock_binding\",\"guard_verified\":%s,\"armed\":%s,\"native_input_action\":true,\"direct_locked_byte_writes\":false}\n",hold_ready?"true":"false",hold_armed?"true":"false");
                const bool activation_armed=hook_status==MH_OK && activation_ready && loadout_lua_activation::arm(stopping,true);
                std::fprintf(log,"{\"event\":\"loadout_activation_binding\",\"guard_verified\":%s,\"armed\":%s,\"callback_chain_limit\":32,\"completion_contract\":\"native_dispatch_and_matching_preset\",\"active_weapon_verified\":false}\n",
                    activation_ready?"true":"false",activation_armed?"true":"false");
                camera_lock_log::set_hooked(hook_status==MH_OK && camera_probe && camera_original);
                fprintf(log,"{\"event\":\"camera_lock_logging\",\"pin_verified\":%s,\"hooked\":%s,\"reason\":\"%s\",\"record_limit\":%u,\"player_ownership_verified\":false}\n",
                    camera_probe?"true":"false",camera_lock_log::st().hooked?"true":"false",
                    camera_lock_log::missing(),camera_lock_log::kBudget);
                if (hook_status==MH_OK && gameplay_runtime::collision_ready)
                    gameplay_runtime::collision.enable(true);
                if (hook_status==MH_OK && step_observers_ready) {
                    step_binding::set_hooked(true,true);
                    step_binding::refresh_status();
                }
                if (hook_status==MH_OK && catalogue_ready &&
                    !loadout_player_catalogue::arm(game_base,true,true,true,stopping))
                    hook_status=MH_ERROR_NOT_INITIALIZED;
                if (hook_status == MH_OK && bootstrap_ui && !native_ui::sdk::arm(true)) {
                    native_ui::sdk::stop();
                    hook_status = MH_ERROR_NOT_INITIALIZED;
                }
                if (hook_status == MH_OK && actor_probe &&
                    !loadout_attack_probe::arm(true, true, true, stopping))
                    hook_status = MH_ERROR_NOT_INITIALIZED;
                if (hook_status == MH_OK && scope_probe &&
                    !loadout_selection_scope::arm(true, true, true, stopping))
                    hook_status = MH_ERROR_NOT_INITIALIZED;
                if (hook_status==MH_OK && selection_kernel_ready && catalogue_ready)
                    loadout_selection_bridge::arm();
                if (hook_status == MH_OK) {
                    AcquireSRWLockExclusive(&queue_lock);
                    recording.store(!stopping.load());
                    ReleaseSRWLockExclusive(&queue_lock);
                    loadout_mod::start(stopping, true);
                    if (observe_ui) native_ui_probe::start(stopping, true);
                    status = "installed";
                }
            }
        }
    }
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    fprintf(log, "{\"event\":\"selection_observer\",\"status\":\"%s\",\"minhook_status\":%d,\"recording\":%s,\"qpc_frequency\":%lld,\"kinds\":\"0=commit_entry,1=commit_exit,2=setter_entry,3=setter_exit\"}\n",
            status, static_cast<int>(hook_status), recording.load() ? "true" : "false", frequency.QuadPart);
    fflush(log);
}
} // namespace selection_observer
