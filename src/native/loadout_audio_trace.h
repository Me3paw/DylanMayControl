#pragma once
#ifndef CRMOD_AUDIO_TRACE
#define CRMOD_AUDIO_TRACE 0
#endif
#include <windows.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace loadout_audio_trace {
// Worker only. Formatting is bounded; disk writes happen after sampler locks are released.
inline FILE* file=nullptr;
inline std::array<std::array<char,1024>,64> rows{};
inline unsigned count=0,records=0,dropped=0;
inline ULONGLONG deadline=0;
inline unsigned long long qpc_frequency=0;
inline bool limited=false;
inline unsigned long long previous_sample=0,poll_gap_us=0,pad_poll_us=0,bindings_us=0;
inline unsigned long long qpc_now() {
    if(!file || limited)return 0;
    LARGE_INTEGER tick{};QueryPerformanceCounter(&tick);return tick.QuadPart;
}
inline unsigned long long elapsed_us(unsigned long long first,unsigned long long last) {
    return qpc_frequency && last>=first ? (last-first)*1000000/qpc_frequency : 0;
}
inline void begin_sample() {
    if constexpr(!CRMOD_AUDIO_TRACE)return;
    if(!file || limited)return;
    const DWORD saved=GetLastError();const auto current=qpc_now();
    poll_gap_us=elapsed_us(previous_sample?previous_sample:current,current);previous_sample=current;
    SetLastError(saved);
}
inline void row(const char* event,const char* format,...) {
    if constexpr(!CRMOD_AUDIO_TRACE)return;
    if(!file || limited)return;
    const DWORD saved=GetLastError();
    if(records>=8192 || GetTickCount64()>=deadline)limited=true;
    else if(count==rows.size())++dropped;
    else {
        auto& out=rows[count];
        LARGE_INTEGER tick{};QueryPerformanceCounter(&tick);
        const int prefix=std::snprintf(out.data(),out.size(),"{\"event\":\"%s\",\"tick_ms\":%llu,\"qpc\":%llu",event,GetTickCount64(),static_cast<unsigned long long>(tick.QuadPart));
        va_list args;va_start(args,format);
        const int length=std::vsnprintf(out.data()+prefix,out.size()-prefix,format,args);va_end(args);
        if(length>=0 && static_cast<unsigned>(prefix+length+3)<out.size()) {
            out[prefix+length]='}';out[prefix+length+1]='\n';out[prefix+length+2]=0;++count;++records;
        } else ++dropped;
    }
    SetLastError(saved);
}
inline void start() {
    if constexpr(!CRMOD_AUDIO_TRACE)return;
    const DWORD saved=GetLastError();
    wchar_t path[32768]{};const DWORD n=GetModuleFileNameW(nullptr,path,32768);
    if(n && n<32700) {
        wchar_t* slash=wcsrchr(path,L'\\');
        if(slash) {
            FILETIME wall{};GetSystemTimeAsFileTime(&wall);
            const auto timestamp=(static_cast<unsigned long long>(wall.dwHighDateTime)<<32)|wall.dwLowDateTime;
            std::swprintf(slash+1,32768-(slash+1-path),L"crmod-audio-%lu-%llu.jsonl",GetCurrentProcessId(),timestamp);
            file=_wfopen(path,L"wx");
            if(file) {
                deadline=GetTickCount64()+10*60*1000;
                LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);qpc_frequency=frequency.QuadPart;
                row("audio_trace_start",",\"pid\":%lu,\"qpc_frequency\":%llu,\"max_records\":8192,\"max_seconds\":600,\"general_diagnostics\":false",GetCurrentProcessId(),qpc_frequency);
            }
        }
    }
    SetLastError(saved);
}
inline void flush() {
    if constexpr(!CRMOD_AUDIO_TRACE)return;
    if(!file)return;
    const DWORD saved=GetLastError();
    for(unsigned i=0;i<count;++i)std::fputs(rows[i].data(),file);
    if(count)std::fflush(file);
    count=0;
    if(limited) {
        std::fprintf(file,"{\"event\":\"audio_trace_limit\",\"records\":%u,\"dropped\":%u}\n",records,dropped);
        std::fclose(file);file=nullptr;
    }
    SetLastError(saved);
}
inline void stop() {
    if constexpr(!CRMOD_AUDIO_TRACE)return;
    const DWORD saved=GetLastError();
    row("audio_trace_stop",",\"records\":%u,\"dropped\":%u",records,dropped);flush();
    if(file){std::fclose(file);file=nullptr;}
    SetLastError(saved);
}
}
