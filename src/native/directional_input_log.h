#pragma once
#include <windows.h>
#include <xinput.h>
#include <cstdint>
#include <cstdio>

namespace directional_input_log {
// State and FILE writes belong exclusively to the manual-session worker.
using GetState=DWORD (WINAPI*)(DWORD,XINPUT_STATE*);
inline GetState native_get_state=nullptr;
inline constexpr unsigned limit=32768;
inline unsigned records=0;
inline std::uint64_t sample_sequence=0;
inline bool started=false,limit_reported=false;
inline bool focus_known=false,focused=false,key_known=false;
inline unsigned previous_keys=0;
struct Pad {
    bool known=false;
    DWORD result=ERROR_DEVICE_NOT_CONNECTED;
    WORD buttons=0;
    int x=0,y=0;
    bool lt=false,rt=false;
};
inline Pad pads[XUSER_MAX_COUNT]{};
struct RestoreError {
    DWORD value=GetLastError();
    ~RestoreError() { SetLastError(value); }
};
inline unsigned long long qpc() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<unsigned long long>(value.QuadPart);
}
inline bool owns(HWND window) {
    DWORD pid=0;
    return window && GetForegroundWindow()==window &&
        GetWindowThreadProcessId(window,&pid)!=0 &&
        pid==GetCurrentProcessId();
}
inline bool admit(FILE* log) {
    if (records<limit) { ++records; return true; }
    if (!limit_reported) {
        limit_reported=true;
        std::fprintf(log,
            "{\"event\":\"directional_input_budget_exhausted\","
            "\"limit\":%u,\"sampling_stopped\":true}\n",limit);
        std::fflush(log);
    }
    return false;
}
inline void focus(FILE* log,bool value) {
    if (focus_known && focused==value) return;
    focus_known=true; focused=value;
    key_known=false;
    for (auto& pad:pads) pad.known=false;
    if (admit(log)) std::fprintf(log,
        "{\"event\":\"directional_input_focus\",\"source\":\"focus\","
        "\"sample_seq\":%llu,\"qpc\":%llu,\"foreground_game\":%s}\n",
        static_cast<unsigned long long>(sample_sequence),qpc(),
        value?"true":"false");
    std::fflush(log);
}
inline void start(FILE* log,HMODULE backend) {
    RestoreError restore;
    if (!log || started) return;
    started=true;
    native_get_state=backend
        ? reinterpret_cast<GetState>(GetProcAddress(backend,"XInputGetState"))
        : nullptr;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::fprintf(log,
        "{\"event\":\"directional_input_logging\","
        "\"nominal_sample_interval_ms\":10,\"record_limit\":%u,"
        "\"qpc_frequency\":%llu,\"exact_input_timeline\":false,"
        "\"short_taps_may_be_missed\":true,"
        "\"keyboard_mask\":\"1=W,2=A,4=S,8=D,16=LMB,32=RMB,64=Space,128=Shift,256=MMB,512=PageUp,1024=PageDown\","
        "\"float_active_verified\":false,"
        "\"stick_axis_deadzone\":%u,\"trigger_threshold\":%u,"
        "\"lock_relative_direction_verified\":false,"
        "\"native_xinput_available\":%s}\n",
        limit,static_cast<unsigned long long>(frequency.QuadPart),
        static_cast<unsigned>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
        static_cast<unsigned>(XINPUT_GAMEPAD_TRIGGER_THRESHOLD),
        native_get_state?"true":"false");
    std::fflush(log);
}
inline int axis(SHORT value) {
    const int deadzone=XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    return value>deadzone ? 1 : value<-deadzone ? -1 : 0;
}
inline void sample(FILE* log) {
    RestoreError restore;
    if (!log || !started) return;
    if (records>=limit) { admit(log); return; }
    ++sample_sequence;
    HWND window=GetForegroundWindow();
    if (!owns(window)) { focus(log,false); return; }
    focus(log,true);
    constexpr int keys[11]={'W','A','S','D',VK_LBUTTON,VK_RBUTTON,VK_SPACE,VK_SHIFT,VK_MBUTTON,VK_PRIOR,VK_NEXT};
    unsigned mask=0;
    const auto key_begin=qpc();
    for (unsigned i=0;i<11;++i) {
        if (!owns(window)) { focus(log,false); return; }
        if ((GetAsyncKeyState(keys[i])&0x8000)!=0) mask|=1u<<i;
    }
    const auto key_end=qpc();
    if (!owns(window)) { focus(log,false); return; }
    if (!key_known || mask!=previous_keys) {
        const bool baseline=!key_known;
        const unsigned changed=baseline ? 0u : mask^previous_keys;
        if (admit(log)) std::fprintf(log,
            "{\"event\":\"directional_input\",\"source\":\"keyboard_mouse\","
            "\"sample_seq\":%llu,\"qpc_begin\":%llu,\"qpc\":%llu,"
            "\"baseline\":%s,\"mask\":%u,\"down_mask\":%u,\"up_mask\":%u,"
            "\"w\":%u,\"a\":%u,\"s\":%u,\"d\":%u,\"lmb\":%u,\"rmb\":%u,\"jump\":%u,\"dash\":%u,\"mmb\":%u,\"page_up\":%u,\"page_down\":%u}\n",
            static_cast<unsigned long long>(sample_sequence),
            key_begin,key_end,baseline?"true":"false",mask,
            changed&mask,changed&previous_keys,
            (mask>>0)&1u,(mask>>1)&1u,(mask>>2)&1u,
            (mask>>3)&1u,(mask>>4)&1u,(mask>>5)&1u,(mask>>6)&1u,(mask>>7)&1u,(mask>>8)&1u,(mask>>9)&1u,(mask>>10)&1u);
        previous_keys=mask; key_known=true;
        std::fflush(log);
    }
    for (DWORD index=0;index<XUSER_MAX_COUNT && records<limit;++index) {
        if (!owns(window)) { focus(log,false); return; }
        XINPUT_STATE state{};
        const auto begin=qpc();
        const DWORD result=native_get_state
            ? native_get_state(index,&state) : ERROR_PROC_NOT_FOUND;
        const auto end=qpc();
        if (!owns(window)) { focus(log,false); return; }
        const bool connected=result==ERROR_SUCCESS;
        const WORD buttons=connected ? state.Gamepad.wButtons : 0;
        const int x=connected ? axis(state.Gamepad.sThumbLX) : 0;
        const int y=connected ? axis(state.Gamepad.sThumbLY) : 0;
        const bool lt=connected &&
            state.Gamepad.bLeftTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        const bool rt=connected &&
            state.Gamepad.bRightTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        auto& old=pads[index];
        if (old.known && old.result==result && old.buttons==buttons &&
            old.x==x && old.y==y && old.lt==lt && old.rt==rt) continue;
        const bool baseline=!old.known || old.result!=result;
        const WORD changed=baseline ? 0 : static_cast<WORD>(buttons^old.buttons);
        if (admit(log)) std::fprintf(log,
            "{\"event\":\"directional_input\",\"source\":\"controller_native\","
            "\"sample_seq\":%llu,\"qpc_begin\":%llu,\"qpc\":%llu,"
            "\"index\":%lu,\"poll_result\":%lu,\"connected\":%s,"
            "\"baseline\":%s,\"packet\":%lu,"
            "\"lx\":%d,\"ly\":%d,\"axis_x\":%d,\"axis_y\":%d,"
            "\"buttons\":%u,\"buttons_down\":%u,\"buttons_up\":%u,"
            "\"lt\":%u,\"rt\":%u,\"lt_pressed\":%s,\"rt_pressed\":%s}\n",
            static_cast<unsigned long long>(sample_sequence),begin,end,
            index,result,connected?"true":"false",baseline?"true":"false",
            connected ? state.dwPacketNumber : 0,
            connected ? static_cast<int>(state.Gamepad.sThumbLX) : 0,
            connected ? static_cast<int>(state.Gamepad.sThumbLY) : 0,x,y,
            static_cast<unsigned>(buttons),
            static_cast<unsigned>(changed&buttons),
            static_cast<unsigned>(changed&old.buttons),
            connected ? static_cast<unsigned>(state.Gamepad.bLeftTrigger) : 0u,
            connected ? static_cast<unsigned>(state.Gamepad.bRightTrigger) : 0u,
            lt?"true":"false",rt?"true":"false");
        old={true,result,buttons,x,y,lt,rt};
        std::fflush(log);
    }
}
}
