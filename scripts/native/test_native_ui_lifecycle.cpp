#include "../../src/native/native_ui.h"
#include "../../src/native/native_ui_invoke_probe.h"
#include "../../src/native/native_ui_echo.h"
#include "../../src/native/loadout_attack_probe.h"
#include "../../src/native/loadout_selection_scope.h"
#include "../../src/native/loadout_lookup_probe.h"
#include <cassert>
#include <cstdio>
using namespace native_ui::sdk;
static unsigned dom_calls, release_calls, destroy_calls;
static unsigned argument_calls;
static unsigned after_capture_calls;
static unsigned forms_calls;
namespace lookup_fixture {
namespace p=loadout_lookup_probe;
namespace s=loadout_selection_scope;
enum Mode { Valid, TreeMiss, TreeFlag, TreeReturn, NodeMiss, NodeFlag, NodeReturn, Retire };
static Mode mode;
static std::uintptr_t expected_graph,expected_tree,expected_node;
static unsigned tree_calls,node_calls;
static p::Lookup wrong;
static bool pins() { return true; }
static p::Lookup* tree(p::Lookup* out,void* graph,std::uint32_t key) {
    assert(out->pointer==0 && out->found==0);
    assert(reinterpret_cast<std::uintptr_t>(graph)==expected_graph && key==loadout_current_node::kMeleeTree);
    ++tree_calls; out->pointer=expected_tree; out->found=1;
    if (mode==TreeMiss) { out->pointer=UINTPTR_MAX; out->found=0; }
    if (mode==TreeFlag) out->found=2;
    if (mode==Retire) p::stop();
    SetLastError(93);
    return mode==TreeReturn ? &wrong : out;
}
static p::Lookup* node(p::Lookup* out,void* tree,std::uint32_t id) {
    assert(out->pointer==0 && out->found==0);
    assert(reinterpret_cast<std::uintptr_t>(tree)==expected_tree && id==42);
    ++node_calls; out->pointer=expected_node; out->found=1;
    if (mode==NodeMiss) { out->pointer=UINTPTR_MAX; out->found=0; }
    if (mode==NodeFlag) out->found=2;
    SetLastError(94);
    return mode==NodeReturn ? &wrong : out;
}
static void run() {
    std::array<unsigned char,16> graph{};
    std::array<unsigned char,0x50*2> trees{};
    std::array<unsigned char,0x278*2> nodes{};
    std::array<unsigned char,0x28> head{},first{};
    const auto put=[](auto& bytes,unsigned offset,auto value) {
        std::memcpy(bytes.data()+offset,&value,sizeof(value));
    };
    expected_graph=reinterpret_cast<std::uintptr_t>(graph.data());
    expected_tree=reinterpret_cast<std::uintptr_t>(trees.data())+0x50;
    expected_node=reinterpret_cast<std::uintptr_t>(nodes.data())+0x278;
    put(graph,0,std::uint64_t{expected_tree-0x50}); put(graph,8,std::uint32_t{2});
    put(trees,0x50,std::uint32_t{loadout_current_node::kMeleeTree});
    put(trees,0x58,std::uint64_t{expected_node-0x278}); put(trees,0x60,std::uint32_t{2});
    put(trees,0x68,std::uint64_t{reinterpret_cast<std::uintptr_t>(head.data())});
    put(head,0,std::uint64_t{reinterpret_cast<std::uintptr_t>(first.data())});
    put(nodes,0x278+0x1f8,std::uint32_t{42});
    put(nodes,0x278+0x1fc,std::uint32_t{1}); put(nodes,0x278+0x200,std::uint32_t{1});
    put(nodes,0x278+0x204,std::uint16_t{0x09});
    std::atomic<bool> stopping{false};
    s::shared_stopping=&stopping; s::recording.store(true);
    const s::Borrowed loan{900,0,0};
    s::Record record{}; record.kind=0; record.scope_call=loan.call;
    record.value.copied=s::kArguments|s::kMenuId;
    record.value.arguments[1]=expected_graph; record.value.menu_id=42;
    assert(!p::prepare(false,0x100000000,true,true,true));
    assert(!p::prepare(true,0x100000000,false,true,true));
    SetLastError(92); p::observe(record);
    assert(GetLastError()==92 && record.lookup_status==p::Disabled && tree_calls==0);
    p::test_pins=&pins; p::test_tree=&tree; p::test_node=&node;
    assert(p::prepare(true,0x100000000,true,true,true));
    SetLastError(92); p::observe(record);
    assert(GetLastError()==92 && record.lookup_status==p::InvalidLoan && tree_calls==0);
    s::borrowed=&loan;
    for (Mode m:{Valid,TreeMiss,TreeFlag,TreeReturn,NodeMiss,NodeFlag,NodeReturn}) {
        mode=m; SetLastError(92); p::observe(record); assert(GetLastError()==92);
        if (m==Valid) assert(record.lookup_status==p::CurrentNode && record.current_node.raw_flags==0x09);
        if (m==TreeMiss) assert(record.lookup_status==p::TreeMissing && !record.node_called);
        if (m==TreeFlag || m==NodeFlag) assert(record.lookup_status==p::FoundFlagInvalid);
        if (m==TreeReturn || m==NodeReturn) assert(record.lookup_status==p::ReturnMismatch);
        if (m==NodeMiss) assert(record.lookup_status==p::NodeMissing && record.current_node.node_key==0);
    }
    assert(tree_calls==7 && node_calls==4);
    const auto before=tree_calls;
    put(graph,8,std::uint32_t{0}); p::observe(record);
    assert(record.lookup_status==p::EmptyGraph && tree_calls==before && !record.tree_called);
    put(graph,8,std::uint32_t{129}); p::observe(record);
    assert(record.lookup_status==p::InvalidGraph && tree_calls==before);
    put(graph,8,std::uint32_t{2}); put(graph,0,std::uint64_t{UINT64_MAX-15}); p::observe(record);
    assert(record.lookup_status==p::InvalidGraph && tree_calls==before);
    put(graph,0,std::uint64_t{expected_tree-0x50});
    mode=Retire; SetLastError(92); p::observe(record);
    assert(GetLastError()==92 && record.lookup_status==p::Retired && !record.node_called);
    assert(tree_calls==8 && node_calls==4);
    s::borrowed=nullptr; s::recording.store(false); s::shared_stopping=nullptr;
    p::test_pins=nullptr; p::test_tree=nullptr; p::test_node=nullptr;
    std::puts("Native lookup guard/out/found/return/loan/retirement checks PASS (mock getters)");
}
}
namespace selection_scope_fixture {
namespace p=loadout_selection_scope;
static void* expected_system;
static void* expected_graph;
static void* expected_lua;
static unsigned depth,forwarded;
static bool nested;
static void original(void* first,void* middle,void* third) {
    assert(first==reinterpret_cast<void*>(std::uintptr_t{1}+depth));
    assert(middle==reinterpret_cast<void*>(3) && third==expected_system && GetLastError()==81);
    assert(p::borrowed && p::borrowed->outer==reinterpret_cast<std::uintptr_t>(first));
    ++forwarded;
    SetLastError(81); // Mock-native TLS accesses may themselves alter LastError.
    p::observe_choice(42,expected_graph,expected_lua,false);
    assert(GetLastError()==81);
    if (nested && depth==0) {
        const auto* outer=p::borrowed;
        ++depth; SetLastError(81); p::shim(reinterpret_cast<void*>(2),middle,third); --depth;
        assert(GetLastError()==82);
        assert(p::borrowed==outer);
    }
    SetLastError(82);
    p::observe_choice(42,expected_graph,expected_lua,true);
    assert(GetLastError()==82);
}
static DWORD WINAPI fresh_thread(void*) {
    SetLastError(81);
    p::observe_choice(42,expected_graph,expected_lua,false);
    assert(GetLastError()==81);
    p::shim(reinterpret_cast<void*>(1),reinterpret_cast<void*>(3),expected_system);
    assert(GetLastError()==82);
    assert(!p::borrowed);
    return 0;
}
static void call_fresh_thread() {
    HANDLE thread=CreateThread(nullptr,0,&fresh_thread,nullptr,0,nullptr);
    assert(thread && WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    DWORD result=1; assert(GetExitCodeThread(thread,&result) && result==0);
    CloseHandle(thread);
}
static void run() {
    std::array<unsigned char,0x60> system{};
    std::array<unsigned char,0x40> menu{};
    std::array<unsigned char,0x270> input{};
    std::array<unsigned char,0x2060> lua{};
    std::array<std::uint64_t,7> args{reinterpret_cast<std::uintptr_t>(menu.data()),0x1110,
        reinterpret_cast<std::uintptr_t>(lua.data()),0x3330,0x4440,
        reinterpret_cast<std::uintptr_t>(input.data()),0x6660};
    const auto address=reinterpret_cast<std::uintptr_t>(args.data());
    std::memcpy(system.data()+0x58,&address,sizeof(address));
    expected_system=system.data(); expected_graph=reinterpret_cast<void*>(args[1]); expected_lua=lua.data();
    p::original_shim=reinterpret_cast<void*>(&original);
    std::atomic<bool> stopping{false};
    assert(!p::arm(false,true,true,stopping));
    call_fresh_thread();
    assert(forwarded==1 && p::count==0 && !p::borrowed);
    assert(p::arm(true,true,true,stopping));
    call_fresh_thread();
    assert(forwarded==2 && p::count==(diagnostics::enabled?4u:0u));
    FILE* log=tmpfile(); assert(log);
    assert(p::flush(log) && p::count==0);
    nested=true;
    SetLastError(81); p::shim(reinterpret_cast<void*>(1),reinterpret_cast<void*>(3),expected_system);
    assert(forwarded==4 && GetLastError()==82 && p::count==(diagnostics::enabled?8u:0u) && !p::borrowed);
#if CRMOD_DIAGNOSTICS
    const auto outer_call=p::queue[0].scope_call,inner_call=p::queue[2].scope_call;
    assert(outer_call!=inner_call && p::queue[4].scope_call==inner_call);
    assert(p::queue[6].scope_call==outer_call && p::queue[6].kind==3);
    assert(p::queue[1].arguments_match && p::queue[3].arguments_match);

#endif
    AcquireSRWLockExclusive(&p::queue_lock);
    assert(p::flush(log)==!diagnostics::enabled && p::count==(diagnostics::enabled?8u:0u));
    ReleaseSRWLockExclusive(&p::queue_lock);
    assert(p::flush(log) && p::count==0);
    assert(p::stop() && p::attempts.load()==0);
    nested=false;
    SetLastError(81); p::shim(reinterpret_cast<void*>(1),reinterpret_cast<void*>(3),expected_system);
    assert(forwarded==5 && GetLastError()==82 && p::count==0 && !p::borrowed);
    assert(!p::arm(true,true,true,stopping));
    fclose(log); p::shared_stopping=nullptr; p::original_shim=nullptr;
}
}
static void fake_forms(void* columns, void* query, void* command_buffer) {
    assert(columns == reinterpret_cast<void*>(1) && query == reinterpret_cast<void*>(2) &&
           command_buffer == reinterpret_cast<void*>(3) && GetLastError() == 61);
    ++forms_calls;
    SetLastError(62);
}
static void check_actor_forwarding() {
    namespace actor = loadout_attack_probe;
    std::atomic<bool> stopping{false};
    actor::original_forms = reinterpret_cast<void*>(&fake_forms);
    assert(!actor::arm(false, true, true, stopping));
    for (unsigned i = 0; i < 3; ++i) {
        if (i == 1) assert(actor::arm(true, true, true, stopping));
        if (i == 2) actor::stop();
        SetLastError(61);
        actor::forms(reinterpret_cast<void*>(1), reinterpret_cast<void*>(2),
                     reinterpret_cast<void*>(3));
        assert(forms_calls == i + 1 && GetLastError() == 62 && actor::attempts.load() == 0);
    }
    assert(!actor::active());
}
static void fake_argument_method() { ++argument_calls; }
namespace attack_probe_fixture {
namespace p = loadout_attack_probe;
inline constexpr DWORD incoming_error=71, outgoing_error=72;
inline void* expected_columns=nullptr;
inline void* expected_query=nullptr;
inline void* expected_commands=nullptr;
inline std::atomic<unsigned> forwarded{0};
inline std::atomic<DWORD> thread_error{0};
inline std::atomic<bool> pause_once{false};
inline HANDLE entered=nullptr, release=nullptr;
inline void original(void* columns,void* query,void* commands) {
    assert(columns==expected_columns && query==expected_query && commands==expected_commands);
    assert(GetLastError()==incoming_error);
    ++forwarded; SetLastError(outgoing_error);
}
inline void barrier() {
    if (!pause_once.exchange(false)) return;
    assert(SetEvent(entered));
    assert(WaitForSingleObject(release,5000)==WAIT_OBJECT_0);
}
inline DWORD WINAPI producer(void*) {
    SetLastError(incoming_error);
    p::forms(expected_columns,expected_query,expected_commands);
    thread_error.store(GetLastError());
    return 0;
}
inline DWORD WINAPI hold_queue(void*) {
    AcquireSRWLockExclusive(&p::queue_lock);
    assert(SetEvent(entered));
    assert(WaitForSingleObject(release,5000)==WAIT_OBJECT_0);
    ReleaseSRWLockExclusive(&p::queue_lock);
    return 0;
}
inline void run() {
    std::atomic<bool> stopping{false};
    std::uint64_t columns[11]{},query[4]{0x12345678,0,0,0},commands[1]{};
    unsigned char chunk[0x20]{},forms_data[0x78]{},attack[0xb0]{};
    const std::uint64_t actor=0x5566778800000042ULL;
    std::memcpy(chunk+0x10,&actor,sizeof(actor));
    columns[0]=reinterpret_cast<std::uintptr_t>(forms_data);
    columns[2]=UINT64_MAX;
    columns[5]=reinterpret_cast<std::uintptr_t>(attack);
    columns[9]=reinterpret_cast<std::uintptr_t>(chunk);
    expected_columns=columns; expected_query=query; expected_commands=commands;
    p::original_forms=reinterpret_cast<void*>(&original);
    p::shared_stopping=&stopping;
    p::recording.store(false); p::stopped.store(false); p::stop_incomplete.store(false);
    p::count=0; forwarded.store(0);
    const auto call=[&] {
        const auto before=forwarded.load();
        SetLastError(incoming_error);
        p::forms(columns,query,commands);
        assert(forwarded.load()==before+1 && GetLastError()==outgoing_error);
    };
    call(); assert(p::count==0);
    p::recording.store(true);
    call(); assert(p::count==2);
    assert(p::queue[0].phase==0 && p::queue[1].phase==1 &&
           p::queue[0].call==p::queue[1].call);
    assert((p::queue[0].value.copied&p::kActor) && p::queue[0].value.actor==actor);
    FILE* log=std::tmpfile(); assert(log);
    entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    assert(entered && release);
    HANDLE holder=CreateThread(nullptr,0,&hold_queue,nullptr,0,nullptr); assert(holder);
    assert(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
    const auto flush_begin=GetTickCount64();
    assert(!p::flush(log));
    assert(GetTickCount64()-flush_begin<1000 && p::count==2);
    assert(SetEvent(release));
    assert(WaitForSingleObject(holder,5000)==WAIT_OBJECT_0);
    assert(CloseHandle(holder)); assert(CloseHandle(entered)); assert(CloseHandle(release));
    entered=nullptr; release=nullptr;
    assert(p::flush(log) && p::count==0);
    stopping.store(true);
    call(); assert(p::count==0);
    stopping.store(false);
    assert(p::stop() && !p::stop_incomplete.load());
    call(); assert(p::count==0);
    assert(p::attempts.load()==0);
    p::stopped.store(false); p::recording.store(true);
    p::stop_incomplete.store(false); forwarded.store(0);
    entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    assert(entered && release);
    p::copy_test_barrier=&barrier; pause_once.store(true);
    HANDLE thread=CreateThread(nullptr,0,&producer,nullptr,0,nullptr); assert(thread);
    assert(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
    assert(p::attempts.load()!=0 && forwarded.load()==0);
    const auto stop_begin=GetTickCount64();
    assert(!p::stop());
    assert(GetTickCount64()-stop_begin<1000 && p::stop_incomplete.load());
    assert(forwarded.load()==0);
    assert(SetEvent(release));
    assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    assert(p::attempts.load()==0 && forwarded.load()==1 &&
           thread_error.load()==outgoing_error && p::count==0);
    p::copy_test_barrier=nullptr;
    assert(CloseHandle(thread)); assert(CloseHandle(entered)); assert(CloseHandle(release));
    entered=nullptr; release=nullptr;
    assert(p::stop() && !p::stop_incomplete.load());
    assert(p::flush(log)); std::fclose(log);
    p::shared_stopping=nullptr; p::original_forms=nullptr;
}
} // namespace attack_probe_fixture
static void fake_after_capture(native_ui_invoke_probe::Handler*, void*) {
    assert(TryAcquireSRWLockExclusive(&native_ui_invoke_probe::lock));
    ReleaseSRWLockExclusive(&native_ui_invoke_probe::lock);
    ++after_capture_calls;
    SetLastError(99);
}
static void check_passive_context_copy() {
    namespace probe = native_ui_invoke_probe;
    std::atomic<bool> stopping{false};
    shared_stopping = &stopping;
    prepared.store(true); enabled.store(true);
    generation.store(9); ui_thread.store(GetCurrentThreadId());
    probe::recording.store(true);
    std::uintptr_t table[probe::kWords]{};
    for (auto& word : table) word = reinterpret_cast<std::uintptr_t>(&fake_argument_method);
    struct Context { const std::uintptr_t* table; std::uint64_t untouched; } context{table, 42};
    probe::Handler handler(probe::methods, 9);
    probe::after_capture = &fake_after_capture;
    SetLastError(51); probe::invoke(&handler, &context);
    assert(GetLastError() == 51 && callbacks.load() == 0 && argument_calls == 0 && after_capture_calls == 1);
    assert(context.table == table && context.untouched == 42 && probe::count == (diagnostics::enabled?1u:0u));
#if CRMOD_DIAGNOSTICS
    assert(probe::queue[0].readable && probe::queue[0].generation == 9 &&
           probe::queue[0].thread == GetCurrentThreadId());
    assert(std::memcmp(probe::queue[0].words, table, sizeof(table)) == 0);
#endif
    SetLastError(52); probe::invoke(&handler, reinterpret_cast<void*>(1));
    assert(GetLastError() == 52 && probe::count == (diagnostics::enabled?2u:0u) && (!diagnostics::enabled || !probe::queue[1].readable));
    generation.store(10);
    probe::invoke(&handler, &context);
    assert(probe::count == (diagnostics::enabled?2u:0u));
    generation.store(9); ui_thread.store(GetCurrentThreadId() + 1);
    probe::invoke(&handler, &context);
    assert(probe::count == (diagnostics::enabled?2u:0u));
    ui_thread.store(GetCurrentThreadId()); probe::stop();
    probe::invoke(&handler, &context);
    assert(probe::count == (diagnostics::enabled?2u:0u) && argument_calls == 0 && callbacks.load() == 0);
    probe::after_capture = nullptr;
    probe::count = 0;
    prepared.store(false); enabled.store(false);
    generation.store(0); ui_thread.store(0); shared_stopping = nullptr;
}
static void fake_dom(void* object) {
    assert(object == reinterpret_cast<void*>(1));
    assert(GetLastError() == 17 && callbacks.load() == (diagnostics::enabled?1u:0u));
    stop(); // Reentrant stop must not wait for this callback.
    ++dom_calls;
    SetLastError(18);
}
static void fake_release(void* object) {
    assert(object == reinterpret_cast<void*>(1));
    assert(GetLastError() == 27 && callbacks.load() == (diagnostics::enabled?1u:0u));
    ++release_calls;
    SetLastError(28);
}
static std::uintptr_t fake_destroy(void* object, std::uint32_t flags) {
    assert(object == reinterpret_cast<void*>(1) && flags == 0x12345678);
    assert(GetLastError() == 37 && callbacks.load() == (diagnostics::enabled?1u:0u));
    ++destroy_calls;
    SetLastError(38);
    return 0xabcdef;
}
int main() {
    lookup_fixture::run();
    selection_scope_fixture::run();
    check_actor_forwarding();
#if CRMOD_DIAGNOSTICS
    attack_probe_fixture::run();
#endif
    char copied[513]; std::memset(copied, 'x', sizeof(copied));
    std::size_t copied_size = 999;
    assert(!native_ui_echo::read_one(reinterpret_cast<void*>(1), 1, copied, copied_size));
    assert(copied_size == 0 && copied[0] == '\0' && !native_ui_echo::prepare(false));
    check_passive_context_copy();
    // Unprepared lifecycle callbacks forward without following the test pointer.
    auto specs = hook_specs(0x140000000);
    *specs[0].original = reinterpret_cast<void*>(&fake_dom);
    *specs[1].original = reinterpret_cast<void*>(&fake_release);
    *specs[2].original = reinterpret_cast<void*>(&fake_destroy);
    assert(!arm() && !prepared.load());
    SetLastError(17); dom_built(reinterpret_cast<void*>(1));
    assert(dom_calls == 1 && GetLastError() == 18 && callbacks.load() == 0);
    assert(stopped.load() && !enabled.load());
    SetLastError(27); bindings_released(reinterpret_cast<void*>(1));
    assert(release_calls == 1 && GetLastError() == 28 && callbacks.load() == 0);
    SetLastError(37);
    assert(listener_destroyed(reinterpret_cast<void*>(1), 0x12345678) == 0xabcdef);
    assert(destroy_calls == 1 && GetLastError() == 38 && callbacks.load() == 0);
    std::atomic<bool> stopping{false};
    assert(!prepare({}, "void 0;", stopping));
    assert(!arm(true));
    puts("PASS: passive context copy/read failure/scope/stop, callback forwarding, LastError and reentrant stop (no SDK calls)");
}
