#include "../../src/native/controller_menu_input.h"
#include "../../src/native/mod_bindings.h"
#include "../../src/native/loadout_key_requests.h"
#include <cassert>
#include <cstdio>
int main(int argc,char** argv) {
    using namespace mod_bindings;
    if(argc==3 && strcmp(argv[1],"--read-loadout")==0){
        assert(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[2],-1,path,MAX_PATH));
        const auto saved=read_file();
        assert(saved.loadout[0][0]==0 && saved.loadout[0][1]==698021297u && saved.loadout[1][0]==2860349769u && saved.loadout[1][2]==4201352106u);
        assert(saved.key[0]==65 && saved.key_alt[0]==66 && saved.pad[0]==0x10000 && saved.loot_multiplier==3);
        return 0;
    }
    Settings s{};
    assert(valid(s) && s.pad[0]==4 && s.pad[1]==8 && s.loot_multiplier==3 && s.contact_margin_mm==300);
    for(unsigned cm=30;cm<=150;cm+=10){auto value=s;value.contact_margin_mm=cm*10;assert(valid(value));}
    for(unsigned mm:{0u,299u,350u,1501u,2000u}){auto value=s;value.contact_margin_mm=mm;assert(!valid(value));}
    assert(normalize_contact_margin_mm(-1)==300&&normalize_contact_margin_mm(0)==300);
    assert(normalize_contact_margin_mm(200)==300&&normalize_contact_margin_mm(2000)==1500&&normalize_contact_margin_mm(1250)==1200);
    s.loot_multiplier=0;assert(!valid(s));s.loot_multiplier=11;assert(!valid(s));s.loot_multiplier=3;
    s.key={65,65}; assert(!valid(s));
    s.key={65,34}; s.pad={4,8}; assert(valid(s));
    s.key_alt={66,67};assert(valid(s));
    s.key_alt[0]=34;assert(!valid(s));s.key_alt[0]=66;
    s.key_alt[1]=65;assert(!valid(s));s.key_alt[1]=67;
    s.pad={4,4}; assert(!valid(s));
    s.pad={3,0}; assert(!valid(s));
    s.pad={0x400,0}; assert(!valid(s));
    s.pad={0x10000,0x20000}; assert(valid(s));
    XINPUT_GAMEPAD pad{}; pad.wButtons=0x1000; pad.bLeftTrigger=255;
    assert(pad_buttons(pad)==0x11000);
    menu_kind.store(0);assert(menu_input_mask()==0);
    menu_owned.store(true);assert(menu_input_mask()==0x100f);menu_owned.store(false);
    pad.sThumbLX=20000;pad.sThumbLY=-20000;assert(menu_directions(pad)==10);
    pad.sThumbLX=3000;pad.sThumbLY=0;assert(menu_directions(pad)==0);
    menu_kind.store(1);assert(menu_input_mask()==0);
    capturing.store(true);assert((menu_input_mask()&0x1000)!=0);capturing.store(false);menu_kind.store(0);
    WORD release=0;capturing.store(true);assert(filter_menu_buttons(0x1000,true,release)==0);
    capturing.store(false);assert(filter_menu_buttons(0x1000,true,release)==0); // A held through capture is not a fresh confirmation.
    assert(filter_menu_buttons(0,true,release)==0 && release==0);
    assert(filter_menu_buttons(0x1000,true,release)==0x1000);
    assert(filter_menu_buttons(0x1004,false,release)==0x1004); // Gameplay dual-purpose buttons pass unchanged.
    XINPUT_GAMEPAD physical{};physical.wButtons=0x114f;physical.sThumbLX=21000;physical.sThumbLY=-21000;
    physical.sThumbRX=1234;physical.bLeftTrigger=255;
    auto filtered=physical;release=0;menu_owned.store(true);controller_menu_input::filter(filtered,true,release);
    assert(filtered.wButtons==(physical.wButtons&~0x100f)&&filtered.sThumbLX==0&&filtered.sThumbLY==0);
    assert(filtered.sThumbRX==physical.sThumbRX&&filtered.bLeftTrigger==physical.bLeftTrigger); // Native camera axis/lock button survive Mod navigation.
    menu_owned.store(false);filtered=physical;controller_menu_input::filter(filtered,false,release);
    assert(filtered.wButtons==(physical.wButtons&~0x100f)&&filtered.sThumbLX==physical.sThumbLX); // Held A quarantines through picker handoff.
    filtered={};controller_menu_input::filter(filtered,false,release);assert(!release);
    filtered=physical;controller_menu_input::filter(filtered,false,release);assert(memcmp(&physical,&filtered,sizeof physical)==0);
    controller_menu_input::Packet packet{};XINPUT_STATE state{};state.dwPacketNumber=10;packet.publish(state);assert(state.dwPacketNumber==10);
    state.Gamepad=physical;state.dwPacketNumber=10;packet.publish(state);assert(state.dwPacketNumber==11); // Ownership change without hardware change.
    state.dwPacketNumber=10;packet.publish(state);assert(state.dwPacketNumber==11); // Identical polls stay stable.
    reset_pad_input(); remember_pad(0); set_panel_open(true);
    loadout_key_requests::Context absent{};
    assert(loadout_key_requests::input_available(absent,false)); // Options does not require a loaded player.
    set_panel_open(false);assert(!loadout_key_requests::input_available(absent,false));
    menu_active.store(true);assert(loadout_key_requests::input_available(absent,false));menu_active.store(false);
    capturing.store(true);assert(loadout_key_requests::input_available(absent,false));capturing.store(false);
    loadout_key_requests::Context live{1,2,3,4,5};
    assert(loadout_key_requests::input_available(live,true) && !loadout_key_requests::input_available(live,false));
    set_panel_open(true);
    remember_pad(2); remember_pad(0); remember_pad(2); remember_pad(0);
    auto presses=take_pad_presses();
    assert(presses.count==2 && presses.values[0]==2 && presses.values[1]==2 && last_pad.load()==0);
    assert(take_pad_presses().count==0);
    reset_pad_input(); remember_pad(2); // A held button on focus return is not a new press.
    assert(take_pad_presses().count==0);
    remember_pad(0); remember_pad(2); assert(take_pad_presses().count==1);
    capturing.store(true);menu_owned.store(true);remember_pad(0); remember_pad(2); set_panel_open(false); assert(take_pad_presses().count==0 && !capturing.load() && !menu_owned.load());
    remember_pad(0); remember_pad(2); assert(take_pad_presses().count==0);
    std::array<bool,255> keys{};keys[13]=true;set_capture(true);remember_keys(keys);
    assert(take_key_presses().count==0); // Held Enter that opened capture is a baseline, not a binding.
    keys[13]=false;remember_keys(keys);keys[65]=true;remember_keys(keys);
    auto key_events=take_key_presses();assert(key_events.count==1&&key_events.values[0]==65);
    set_capture(false);assert(capture_release_pending.load()&&capture_releasing.load());
    reset_pad_input();assert(!capture_releasing.load()&&take_key_presses().count==0);
    wchar_t temp[MAX_PATH]{}; assert(GetTempPathW(MAX_PATH,temp));
    swprintf(path,MAX_PATH,L"%lscrmod-bindings-test-%lu.ini",temp,GetCurrentProcessId());
    (void)DeleteFileW(path); // A previous failed helper may have left this PID-named test file.
    assert(startup_policy(path).enabled && !startup_policy(path).observe);
    assert(WritePrivateProfileStringW(L"Diagnostics",L"Observe",L"1",path));
    assert(startup_policy(path).enabled && startup_policy(path).observe);
    assert(WritePrivateProfileStringW(L"Diagnostics",L"Observe",L"0",path));
    assert(startup_policy(path).enabled && !startup_policy(path).observe);
    assert(WritePrivateProfileStringW(L"Mod",L"Enabled",L"0",path));
    assert(!startup_policy(path).enabled); // Explicit service switch is independent of diagnostics.
    FILE* quiet=_wfopen(L"NUL",L"w");assert(quiet);fputs("quiet service sink\n",quiet);assert(fclose(quiet)==0);
    assert(apply(s)); persist(); assert(!save_error.load() && saved_revision.load()==snapshot().revision);
    assert(GetPrivateProfileIntW(L"Controls",L"PrimaryKey",0,path)==65);
    assert(GetPrivateProfileIntW(L"Controls",L"PrimaryPad",0,path)==0x10000);
    assert(GetPrivateProfileIntW(L"Controls",L"PrimaryAltKey",0,path)==66);
    assert(GetPrivateProfileIntW(L"Controls",L"SecondaryAltKey",0,path)==67);
    assert(GetPrivateProfileIntW(L"Gameplay",L"ContactMarginMm",0,path)==300);
    assert(GetPrivateProfileIntW(L"Gameplay",L"LootMultiplier",0,path)==3);
    Assignments ids{{{{3413371114u,698021297u,0}},{{2860349769u,0,4201352106u}}}};
    assert(WritePrivateProfileStringW(L"Other",L"Keep",L"untouched",path));
    assert(set_loadout(ids));persist();assert(!save_error.load() && read_file().loadout==ids);
    auto stored=read_file();assert(stored.key==s.key && stored.key_alt==s.key_alt && stored.pad==s.pad && stored.loot_multiplier==3);
    ids[0][0]=0;assert(set_loadout(ids));persist();assert(read_file().loadout==ids);
    (void)WritePrivateProfileStringW(nullptr,nullptr,nullptr,path); // Wine does not report success for the cache-flush form.
    wchar_t executable[MAX_PATH]{},command[MAX_PATH*2+64]{};assert(GetModuleFileNameW(nullptr,executable,MAX_PATH));
    swprintf(command,std::size(command),L"\"%ls\" --read-loadout \"%ls\"",executable,path);
    STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION child{};
    assert(CreateProcessW(executable,command,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child));
    auto waited=WaitForSingleObject(child.hProcess,5000);if(waited!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,1);WaitForSingleObject(child.hProcess,1000);}
    DWORD exit=1;assert(GetExitCodeProcess(child.hProcess,&exit));CloseHandle(child.hThread);CloseHandle(child.hProcess);
    assert(waited==WAIT_OBJECT_0 && exit==0); // A fresh process reads the persisted IDs and existing bindings.
    wchar_t kept[32]{};assert(GetPrivateProfileStringW(L"Other",L"Keep",L"",kept,32,path)>0 && wcscmp(kept,L"untouched")==0);
    assert(WritePrivateProfileStringW(L"Loadout",L"PrimarySlot3",L"-1",path));assert(read_file().loadout[0][2]==0);
    for(unsigned cm=30;cm<=150;cm+=10){auto next=snapshot();next.contact_margin_mm=cm*10;assert(apply(next));persist(true);assert(read_file().contact_margin_mm==cm*10);}
    assert(WritePrivateProfileStringW(L"Gameplay",L"ContactMarginMm",L"35",path));assert(read_file().contact_margin_mm==300);
    assert(WritePrivateProfileStringW(L"Gameplay",L"ContactMarginMm",L"200",path));assert(read_file().contact_margin_mm==300);
    assert(WritePrivateProfileStringW(L"Gameplay",L"ContactMarginMm",L"1250",path));assert(read_file().contact_margin_mm==1200);
    assert(WritePrivateProfileStringW(L"Gameplay",L"ContactMarginMm",L"0",path));assert(read_file().contact_margin_mm==300);
    assert(WritePrivateProfileStringW(L"Gameplay",L"ContactMarginMm",L"2000",path));assert(read_file().contact_margin_mm==1500);
    assert(WritePrivateProfileStringW(L"Gameplay",L"ContactMarginMm",L"-1",path));assert(read_file().contact_margin_mm==300);
    assert(WritePrivateProfileStringW(L"Gameplay",L"ContactMarginMm",nullptr,path));assert(read_file().contact_margin_mm==300);
    assert(DeleteFileW(path)); path[0]=0;
    puts("PASS: bindings, trigger masks, queued quick taps, focus/close baselines, native INI persistence and cleanup");
}
