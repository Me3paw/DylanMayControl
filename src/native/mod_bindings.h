#pragma once
#include <windows.h>
#include <xinput.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
namespace mod_bindings {
using Assignments=std::array<std::array<std::uint32_t,3>,2>;
struct Settings { std::array<unsigned,2> key{VK_PRIOR,VK_NEXT},pad{4,8},key_alt{}; unsigned contact_margin_mm=35,back_pause_ms=50,loot_multiplier=3; Assignments loadout{};std::uint64_t revision=1; };
inline SRWLOCK lock=SRWLOCK_INIT;
inline Settings settings{};
inline std::atomic<std::uint64_t> saved_revision{0};
inline std::atomic<bool> save_error{false};
inline std::atomic<std::uint64_t> urgent_revision{0};
inline std::atomic<bool> editing{false},capturing{false},menu_owned{false};
inline std::atomic<bool> menu_active{false};
inline std::atomic<bool> capture_release_pending{false},capture_releasing{false};
inline void set_capture(bool open){
    const bool before=capturing.exchange(open);
    if(open){capture_release_pending.store(false);capture_releasing.store(false);}
    else if(before){capture_release_pending.store(true);capture_releasing.store(true);}
}
inline std::atomic<unsigned> menu_kind{0}; // Loadout0, Controls1, Gameplay2.
inline WORD menu_input_mask(){return capturing.load()||capture_releasing.load()?0xf31f:menu_owned.load()?0x100f:0;}
inline WORD filter_menu_buttons(WORD buttons,bool modal,WORD& releases){
    const WORD mask=modal?menu_input_mask():0;
    releases=static_cast<WORD>((releases&buttons)|(buttons&mask));
    return static_cast<WORD>(buttons&~(mask|releases)); // Keep old masked holds blocked when the menu mask narrows.
}
inline std::atomic<bool> panel_open{false};
inline std::atomic<unsigned> last_pad{0};
inline std::atomic<bool> pad_baseline{false};
struct PadPresses { std::array<unsigned,32> values{}; unsigned count=0; };
inline SRWLOCK pad_lock=SRWLOCK_INIT;
inline PadPresses pad_presses{};
inline PadPresses key_presses{};
inline std::array<bool,255> previous_keys{};
inline bool key_baseline=false;
inline std::atomic<unsigned> pad_dropped{0};
inline PadPresses take_pad_presses() {
    AcquireSRWLockExclusive(&pad_lock); const auto result=pad_presses; pad_presses={}; ReleaseSRWLockExclusive(&pad_lock); return result;
}
inline PadPresses take_key_presses(){
    AcquireSRWLockExclusive(&pad_lock);const auto result=key_presses;key_presses={};ReleaseSRWLockExclusive(&pad_lock);return result;
}
inline void remember_keys(const std::array<bool,255>& down){
    AcquireSRWLockExclusive(&pad_lock);
    if(key_baseline&&capturing.load())for(unsigned key=1;key<down.size();++key)if(down[key]&&!previous_keys[key]&&key_presses.count<key_presses.values.size())key_presses.values[key_presses.count++]=key;
    previous_keys=down;key_baseline=true;ReleaseSRWLockExclusive(&pad_lock);
}
inline void poll_keys(){
    const bool capture=capturing.load(),release=capture_release_pending.load();
    if(!capture&&!release){AcquireSRWLockExclusive(&pad_lock);key_baseline=false;key_presses={};ReleaseSRWLockExclusive(&pad_lock);return;}
    std::array<bool,255> previous{};
    AcquireSRWLockShared(&pad_lock);previous=previous_keys;ReleaseSRWLockShared(&pad_lock);
    std::array<bool,255> down{};
    bool held=false;
    for(unsigned key=1;key<down.size();++key)if(capture||previous[key]){down[key]=(GetAsyncKeyState(static_cast<int>(key))&0x8000)!=0;held=held||down[key];}
    remember_keys(down);
    if(!capture){
        const bool releasing=held||last_pad.load()!=0;
        capture_releasing.store(releasing);
        if(!releasing)capture_release_pending.store(false);
        AcquireSRWLockExclusive(&pad_lock);key_baseline=false;key_presses={};ReleaseSRWLockExclusive(&pad_lock);
    }
}
inline void set_panel_open(bool open) {
    if(!open){capturing.store(false);capture_release_pending.store(false);capture_releasing.store(false);menu_owned.store(false);}
    AcquireSRWLockExclusive(&pad_lock); pad_presses={};key_presses={};key_baseline=false; panel_open.store(open); ReleaseSRWLockExclusive(&pad_lock);
}
inline void reset_pad_input() {
    AcquireSRWLockExclusive(&pad_lock); pad_presses={};key_presses={};key_baseline=false;previous_keys={};capture_release_pending.store(false);capture_releasing.store(false); last_pad.store(0); pad_baseline.store(false); ReleaseSRWLockExclusive(&pad_lock);
}
inline void remember_pad(unsigned buttons) {
    AcquireSRWLockExclusive(&pad_lock);
    const unsigned previous=last_pad.exchange(buttons),pressed=buttons & ~previous;
    const bool baseline=pad_baseline.exchange(true);
    if (baseline && panel_open.load() && pressed) {
        if (pad_presses.count<pad_presses.values.size()) pad_presses.values[pad_presses.count++]=pressed;
        else ++pad_dropped;
    }
    ReleaseSRWLockExclusive(&pad_lock);
}
inline wchar_t path[MAX_PATH]{};
struct StartupPolicy { bool enabled=true,observe=false; };
inline StartupPolicy startup_policy(const wchar_t* config){
    return {GetPrivateProfileIntW(L"Mod",L"Enabled",1,config)!=0,
        GetPrivateProfileIntW(L"Diagnostics",L"Observe",0,config)!=0};
}
inline bool valid(const Settings& s) {
    constexpr unsigned allowed=0x3f3ffu;
    if(s.loot_multiplier<1 || s.loot_multiplier>10)return false;
    for(const auto& channel:s.loadout)for(auto id:channel)if(id==UINT32_MAX)return false;
    if(s.contact_margin_mm!=20 && s.contact_margin_mm!=35 && s.contact_margin_mm!=50) return false;
    if(s.back_pause_ms!=10 && s.back_pause_ms!=20 && s.back_pause_ms!=30 && s.back_pause_ms!=40 && s.back_pause_ms!=50 && s.back_pause_ms!=500) return false;
    for (unsigned i=0;i<2;++i) {
        if (s.key[i]>254 || s.key_alt[i]>254 || (s.pad[i] && ((s.pad[i]&(s.pad[i]-1)) || (s.pad[i]&~allowed)))) return false;
    }
    for(unsigned a:{s.key[0],s.key_alt[0]}) for(unsigned b:{s.key[1],s.key_alt[1]}) if(a && a==b) return false;
    return !s.pad[0] || s.pad[0]!=s.pad[1];
}
inline Settings snapshot() {
    AcquireSRWLockShared(&lock); const auto s=settings; ReleaseSRWLockShared(&lock); return s;
}
inline bool apply(Settings s) {
    if (!valid(s) || !TryAcquireSRWLockExclusive(&lock)) return false;
    if (settings.revision==UINT64_MAX) { ReleaseSRWLockExclusive(&lock); return false; }
    s.revision=settings.revision+1; settings=s; save_error.store(false); ReleaseSRWLockExclusive(&lock); return true;
}
inline bool set_loadout(const Assignments& ids) {
    for(const auto& channel:ids)for(auto id:channel)if(id==UINT32_MAX)return false;
    if(!TryAcquireSRWLockExclusive(&lock))return false;
    if(settings.loadout==ids){ReleaseSRWLockExclusive(&lock);return true;}
    if(settings.revision==UINT64_MAX){ReleaseSRWLockExclusive(&lock);return false;}
    settings.loadout=ids;urgent_revision.store(++settings.revision);save_error.store(false);
    ReleaseSRWLockExclusive(&lock);return true;
}
inline const wchar_t* const slot_names[2][3]={{L"PrimarySlot1",L"PrimarySlot2",L"PrimarySlot3"},{L"SecondarySlot1",L"SecondarySlot2",L"SecondarySlot3"}};
inline Settings read_file() {
    Settings s{};
    s.key[0]=GetPrivateProfileIntW(L"Controls",L"PrimaryKey",VK_PRIOR,path);
    s.key[1]=GetPrivateProfileIntW(L"Controls",L"SecondaryKey",VK_NEXT,path);
    s.pad[0]=GetPrivateProfileIntW(L"Controls",L"PrimaryPad",4,path);
    s.pad[1]=GetPrivateProfileIntW(L"Controls",L"SecondaryPad",8,path);
    if(!GetPrivateProfileIntW(L"Controls",L"PadDefaultsVersion",0,path) && !s.pad[0] && !s.pad[1])s.pad={4,8};
    s.key_alt[0]=GetPrivateProfileIntW(L"Controls",L"PrimaryAltKey",0,path);
    s.key_alt[1]=GetPrivateProfileIntW(L"Controls",L"SecondaryAltKey",0,path);
    s.contact_margin_mm=GetPrivateProfileIntW(L"Gameplay",L"ContactMarginMm",35,path);
    if(s.contact_margin_mm!=20 && s.contact_margin_mm!=35 && s.contact_margin_mm!=50) s.contact_margin_mm=35;
    s.back_pause_ms=GetPrivateProfileIntW(L"Gameplay",L"BackPauseMs",50,path);
    if(s.back_pause_ms!=10 && s.back_pause_ms!=20 && s.back_pause_ms!=30 && s.back_pause_ms!=40 && s.back_pause_ms!=50 && s.back_pause_ms!=500) s.back_pause_ms=50;
    s.loot_multiplier=GetPrivateProfileIntW(L"Gameplay",L"LootMultiplier",3,path);
    if(s.loot_multiplier<1 || s.loot_multiplier>10)s.loot_multiplier=3;
    for(unsigned c=0;c<2;++c)for(unsigned i=0;i<3;++i){const auto id=GetPrivateProfileIntW(L"Loadout",slot_names[c][i],0,path);s.loadout[c][i]=id==UINT32_MAX?0:id;}
    return s;
}
inline void start() {
    // Keep build93's settings beside the game when the DLL moves under crmods.
    if (!GetModuleFileNameW(nullptr,path,MAX_PATH)) return;
    wchar_t* tail=wcsrchr(path,L'\\'); if (!tail) { path[0]=0; return; }
    if (tail-path+20>=MAX_PATH) { path[0]=0; return; }
    wcscpy(tail+1,L"crmod-bindings.ini");
    const auto s=read_file();
    if (valid(s)) { AcquireSRWLockExclusive(&lock); settings=s; ReleaseSRWLockExclusive(&lock); }
    saved_revision.store(1);
}
inline void persist(bool force=false) {
    if (!path[0]) return;
    const auto s=snapshot(); if (saved_revision.load()==s.revision) return;
    static ULONGLONG next=0; const auto now=GetTickCount64();const auto urgent=urgent_revision.load();
    if (now<next && !force && !(urgent && urgent<=s.revision)) return; next=now+1000;
    const wchar_t* names[]={L"PrimaryKey",L"SecondaryKey",L"PrimaryPad",L"SecondaryPad",L"PrimaryAltKey",L"SecondaryAltKey",L"ContactMarginMm",L"BackPauseMs"};
    const unsigned values[]={s.key[0],s.key[1],s.pad[0],s.pad[1],s.key_alt[0],s.key_alt[1],s.contact_margin_mm,s.back_pause_ms};
    bool ok=true; wchar_t number[24]{};
    for (unsigned i=0;i<8;++i) { swprintf(number,24,L"%u",values[i]); ok=WritePrivateProfileStringW(i>=6?L"Gameplay":L"Controls",names[i],number,path)!=0 && ok; }
    swprintf(number,24,L"%u",s.loot_multiplier);ok=WritePrivateProfileStringW(L"Gameplay",L"LootMultiplier",number,path)!=0 && ok;
    ok=WritePrivateProfileStringW(L"Controls",L"PadDefaultsVersion",L"1",path)!=0 && ok;
    for(unsigned c=0;c<2;++c)for(unsigned i=0;i<3;++i){swprintf(number,24,L"%u",s.loadout[c][i]);ok=WritePrivateProfileStringW(L"Loadout",slot_names[c][i],number,path)!=0 && ok;}
    save_error.store(!ok); if (ok){saved_revision.store(s.revision);auto pending=urgent_revision.load();if(pending && pending<=s.revision)urgent_revision.compare_exchange_strong(pending,0);}
}
inline unsigned pad_buttons(const XINPUT_GAMEPAD& p) {
    return p.wButtons | (p.bLeftTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD?0x10000u:0u) |
        (p.bRightTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD?0x20000u:0u);
}
using GetState=DWORD (WINAPI*)(DWORD,XINPUT_STATE*);
inline std::atomic<GetState> raw_get{nullptr};
inline bool pad_down(HMODULE backend,unsigned mask) {
    if (!backend || !mask) return false;
    const auto raw=raw_get.load();
    const auto get=raw?raw:reinterpret_cast<DWORD (WINAPI*)(DWORD,XINPUT_STATE*)>(GetProcAddress(backend,"XInputGetState"));
    if (!get) return false;
    for (DWORD i=0;i<4;++i) { XINPUT_STATE state{}; if (get(i,&state)==ERROR_SUCCESS && (pad_buttons(state.Gamepad)&mask)) return true; }
    return false;
}
inline unsigned menu_directions(const XINPUT_GAMEPAD& p) {
    constexpr int threshold=16000;
    return (p.sThumbLY>threshold?1u:0u)|(p.sThumbLY<-threshold?2u:0u)|
        (p.sThumbLX<-threshold?4u:0u)|(p.sThumbLX>threshold?8u:0u);
}
inline unsigned poll_pad(HMODULE backend) {
    unsigned buttons=0;
    if (backend) {
        const auto raw=raw_get.load();
    const auto get=raw?raw:reinterpret_cast<DWORD (WINAPI*)(DWORD,XINPUT_STATE*)>(GetProcAddress(backend,"XInputGetState"));
        if (get) for (DWORD i=0;i<4;++i) { XINPUT_STATE state{}; if (get(i,&state)==ERROR_SUCCESS) buttons|=pad_buttons(state.Gamepad)|((panel_open.load()&&!capturing.load())?menu_directions(state.Gamepad):0); }
    }
    remember_pad(buttons); return buttons;
}
}
