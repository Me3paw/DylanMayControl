// Pinned native function table for build 0.563.737.9 (PE image base 0x140000000). RVA = VA - 0x140000000.
// Every function the combat bindings CALL or HOOK is verified at load against its unique prologue bytes (sigpack.py, cb11/cb31);
// any mismatch (other game build, patched image, wrong base) DISABLES the dependent feature with an explicit status line.
// No RIP-relative instruction exists in any of these prologues; `covered` = whole-instruction bytes available for a 14-byte detour.
#pragma once
#include "combat_native.h"

namespace native_pins {
using combat_native::CopyFn;

struct Pin { const char* name; uint32_t rva; uint8_t covered; const char* hex; };

// ABIs (x64 Microsoft; evidence cb1/cb12/cb15/cb29/cb31):
//  chooser      u32 f(Node*, u8 phase2, ComboData*, Conditions*, Loadout*, MeleeUnlockData*, StateSet*, MeleeComboCfg*, G*, AnimOut*)  [10 args, HOOK]
//  legality     u8  f(Node*, ComboData*, Conditions*, Loadout*, MeleeUnlockData*, StateSet*, Cfg*, i32 connArg, G*)                       [9 args, CALL-only reference]
//  unlock_gate  u8  f(i32 nodeId, MeleeUnlockData*, G*)
//  state_blocked u8 f(Node*, u64 mask, StateSet*, Conditions*)
//  condition    u64 f(u32 id, u8 0, u8 1, Node*, Conditions*)  (low byte = bool)
//  input_ok     u8* f(u8 out[16], Node*, ComboData*, Cfg*)      (result byte at ptr+1)
//  post_ok      u8  f(u64 1, Node*, Conditions*, Cfg*)
//  anim_index   u16* f(AnimOut*, u16* out, u32 hash)            (result index at *ret, 0xffff = none)
//  input flag A u8  f(InputRow*, u16 action) / flag B u8 f(InputTree*, u16 action)
//  wrapper      void f(cols*, ctx*, p3*, p4*)                   [4 args, HOOK, collision observer]
//  loco_state   void f(cols*, x)         [2 args, HOOK, player_locomotion::update_locomotion_state body: DodgeData/Jump/Airborne/LogicData/CharacterCollisions, cb33]
//  jump_input   void f(cols*, p2, p3, float* dt) [4 args, HOOK, jump::handle_input body: requests a jump through NextJumpRequest, cb33]
//  map_find     i32  f(u64 map, u32 count, u32 hash)            [CALL: archetype component-offset lookup, -1 = absent]
//  lockon/movement bodies: void f(cols*, ctx*, dt*)             [3 args, HOOK, passive observers]
inline const Pin* pins(size_t* n) {
    static const Pin kPins[] = {
        {"chooser",        0x235e1e0, 14, "48895c24184c894c2420885424105556574154415541564157"},
        {"legality",       0x235dee0, 15, "48895c241048896c241848897424205741544155415641574883ec308b"},
        {"unlock_gate",    0x2339010, 16, "4180783400448bc974708b02413b40087368488d048048c1e0"},
        {"state_blocked",  0x235d740, 14, "48895c2408574883ec300fb64118498bf8c0e803488bdaa801"},
        {"condition",      0x235c420, 15, "4154415541574883ec4048895c246048b8e9580463d2b95fde"},
        {"input_ok",       0x235d2b0, 16, "48895c240848897c24108b4224488bd9486bc0384903008b50"},
        {"post_ok",        0x235d5f0, 15, "48895c240848896c2410488974241857415641574883ec308b42"},
        {"anim_index",     0x2dfecb0, 16, "40534883ec20488bda8b5130488b4928e8ab0b480066890348"},
        {"input_flag_a",   0x228d1f0, 14, "6689542410534883ec30488d591049b8b3010000000100000fb6ca48b825232284e49cf2cb4833c80fb7c2490fafc848c1e8084833c848b8e9580463d2b95fde490fafc84c8d44244848f7e1488bcb4c8d0c02488d542420e88364"},
        {"input_flag_b",   0x228d190, 15, "4883ec284c8b01498b40088078190075186639501a7306488b4010eb064c8bc0488b008078190074e84180781900751466413b501a720d4180781c00"},
        {"lockon_body",    0x27e6cb0, 14, "4c894424185356574154415541574881ecd8000000c5fc1009"},
        {"movement_body",  0x228ec70, 15, "488bc448895808488970204c8940185741544155415641574881ecc0"},
        {"collision_wrap", 0x277f170, 15, "48895c2408488974241048897c24184c894c2420554154415541564157488dac2470"},
        {"loco_state",     0x2278f90, 14, "4c8bdc53565741544155415641574881ec30010000c5fc1009"},
        {"jump_input",     0x23f9e80, 14, "4c89442418488954241053564154415541574881ec90000000"},
        {"map_find",       0x327f790, 17, "488bc24c8bd241b900000000c4c1796ed0c4e27d58d24883e0"},
    };
    if (n) *n = sizeof kPins / sizeof kPins[0];
    return kPins;
}
inline const Pin* find(const char* name) {
    size_t n = 0; const Pin* p = pins(&n);
    for (size_t i = 0; i < n; ++i) { const char* a = p[i].name; const char* b = name; while (*a && *a == *b) { ++a; ++b; } if (!*a && !*b) return &p[i]; }
    return nullptr;
}
inline int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
// Compares the live bytes at base+rva with the pin through the guarded copier. false = mismatch OR unreadable (never assume).
inline bool verify(CopyFn cp, uint64_t base, const Pin& p) {
    uint8_t live[160];
    size_t n = 0; while (p.hex[2 * n] && p.hex[2 * n + 1]) ++n;
    if (n == 0 || n > sizeof live) return false;
    const uint64_t at = combat_native::at(base, 0, 0, p.rva);
    if (!at || !combat_native::copy_checked(cp, live, at, n)) return false;
    for (size_t i = 0; i < n; ++i) {
        const int hi = hexval(p.hex[2 * i]), lo = hexval(p.hex[2 * i + 1]);
        if (hi < 0 || lo < 0 || live[i] != static_cast<uint8_t>(hi * 16 + lo)) return false;
    }
    return true;
}
// Verifies the listed names; `bad` receives the first failing pin name (or nullptr). true only if every name verifies.
inline bool verify_set(CopyFn cp, uint64_t base, const char* const* names, size_t count, const char** bad) {
    if (bad) *bad = nullptr;
    for (size_t i = 0; i < count; ++i) {
        const Pin* p = find(names[i]);
        if (!p || !verify(cp, base, *p)) { if (bad) *bad = names[i]; return false; }
    }
    return true;
}
}  // namespace native_pins
