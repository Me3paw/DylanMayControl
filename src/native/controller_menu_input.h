#pragma once
#include <MinHook.h>
#include "mod_bindings.h"
namespace controller_menu_input {
inline mod_bindings::GetState original=nullptr;
inline std::atomic<bool> enabled{false};
inline WORD releases[4]{};
inline SRWLOCK lock=SRWLOCK_INIT;
struct Packet {
    bool seen=false;
    DWORD number=0,native=0;
    XINPUT_GAMEPAD pad{};
    void publish(XINPUT_STATE& state) {
        const DWORD incoming=state.dwPacketNumber;
        if(!seen)number=incoming?incoming:1;
        else if(native!=incoming||memcmp(&pad,&state.Gamepad,sizeof pad)!=0)++number;
        seen=true;native=incoming;pad=state.Gamepad;state.dwPacketNumber=number;
    }
};
inline Packet packets[4]{};
inline void filter(XINPUT_GAMEPAD& pad,bool modal,WORD& release) {
    pad.wButtons=mod_bindings::filter_menu_buttons(pad.wButtons,modal,release);
    if(modal&&(mod_bindings::menu_owned.load()||mod_bindings::capturing.load()||mod_bindings::capture_releasing.load())) {
        pad.sThumbLX=0;pad.sThumbLY=0;
    }
}
inline DWORD WINAPI get_state(DWORD port,XINPUT_STATE* state) {
    const auto result=original(port,state);
    const DWORD error=GetLastError();
    DWORD pid=0;
    const HWND window=GetForegroundWindow();
    const bool foreground=window&&GetWindowThreadProcessId(window,&pid)&&pid==GetCurrentProcessId();
    if(result==ERROR_SUCCESS&&state&&port<4&&enabled.load()&&foreground) {
        // Only our visible menu/capture owns these inputs. The sampler uses the
        // original trampoline, so physical presses reach the UI exactly once.
        AcquireSRWLockExclusive(&lock);
        const bool modal=mod_bindings::panel_open.load();
        filter(state->Gamepad,modal,releases[port]);
        // Changes in UI ownership can unmask a held stick without a hardware
        // packet change. Publish that change so native input sees the handoff.
        packets[port].publish(*state);
        ReleaseSRWLockExclusive(&lock);
    }
    SetLastError(error);return result;
}
inline bool prepare(HMODULE backend) {
    void* target=backend?reinterpret_cast<void*>(GetProcAddress(backend,"XInputGetState")):nullptr;
    if(!target||MH_CreateHook(target,reinterpret_cast<void*>(&get_state),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    mod_bindings::raw_get.store(original);
    return MH_QueueEnableHook(target)==MH_OK;
}
}
