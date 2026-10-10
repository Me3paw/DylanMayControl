#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3) { fprintf(stderr, "Usage: diagnostic_fixture.exe <absolute proxy DLL path> [absolute trace path]\n"); return 2; }
    FILE* trace = argc == 3 ? _wfopen(argv[2], L"w") : stdout;
    if (!trace) { fprintf(stderr, "Cannot open fixture trace file\n"); return 5; }
    fprintf(trace, "{\"event\":\"fixture_started\",\"pid\":%lu,\"trace_schema\":1,\"file_output\":%s}\n",
            GetCurrentProcessId(), argc == 3 ? "true" : "false");
    fflush(trace);
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) {
        fprintf(trace, "{\"event\":\"fixture_error\",\"stage\":\"load_dll\",\"win_error\":%lu}\n", GetLastError());
        fflush(trace);
        if (trace != stdout) fclose(trace);
        return 3;
    }
    using Poll = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
    using Stop = void (WINAPI*)();
    auto poll = reinterpret_cast<Poll>(GetProcAddress(dll, MAKEINTRESOURCEA(2)));
    auto by_name = reinterpret_cast<Poll>(GetProcAddress(dll, "XInputGetState"));
    auto stop = reinterpret_cast<Stop>(GetProcAddress(dll, "CrModStop"));
    if (!poll || poll != by_name || !stop) {
        fprintf(trace, "{\"event\":\"fixture_error\",\"stage\":\"exports\"}\n");
        fflush(trace);
        FreeLibrary(dll);
        if (trace != stdout) fclose(trace);
        return 4;
    }
    ULONGLONG end = GetTickCount64() + 20000;
    while (GetTickCount64() < end) {
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            XINPUT_STATE state{};
            DWORD result = poll(i, &state);
            fprintf(trace, "{\"tick_ms\":%llu,\"index\":%lu,\"result\":%lu,\"packet\":%lu,\"buttons\":%u,\"lt\":%u,\"rt\":%u,\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d}\n",
                   GetTickCount64(), i, result, state.dwPacketNumber, state.Gamepad.wButtons,
                   state.Gamepad.bLeftTrigger, state.Gamepad.bRightTrigger, state.Gamepad.sThumbLX,
                   state.Gamepad.sThumbLY, state.Gamepad.sThumbRX, state.Gamepad.sThumbRY);
        }
        fflush(trace);
        Sleep(100);
    }
    stop();
    fprintf(trace, "{\"event\":\"fixture_finished\",\"pid\":%lu}\n", GetCurrentProcessId());
    fflush(trace);
    if (trace != stdout) fclose(trace);
    FreeLibrary(dll);
    return 0;
}
