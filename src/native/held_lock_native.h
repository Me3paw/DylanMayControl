#pragma once
#include "loadout_player_catalogue.h"
#include "step_binding.h"
#ifdef _WIN32
namespace held_lock_native {
constexpr uint64_t kMask=0x10000000000ull;
constexpr uint32_t kInput=step_binding::fnv1a("heron::player_input::component::Input");
static_assert(kInput==0x49458a06,"Native87 Input hash");
constexpr uint32_t kLock=0xdf50365a;
using ActiveFn=void (*)(void*,void*);
using FlagFn=uint64_t (*)(void*,uint16_t);
using ClearFn=void (*)(void*,uint64_t,uint8_t);
using ProducerFn=void (*)(void*,void*,void*);
using MappedFn=uint64_t (*)(void*,uint16_t);
using PredicateFn=uint8_t (*)(void*,uint16_t,void*);
inline void* originals[6]{};
inline std::atomic<bool> enabled{false},stopped{false};
inline const std::atomic<bool>* stopping=nullptr;
inline std::atomic<uint16_t> action{0xffff};
inline uint64_t action_address=0;
inline std::atomic<uint64_t> transitions{0},release_cleanups{0};
inline std::atomic<uint64_t> mapped_evaluations{0},edge_overrides{0},unsupported_mappings{0};
inline std::atomic<uint32_t> unsupported_kinds{0},unsupported_modes{0};
inline bool active() { return enabled.load()&&!stopped.load()&&stopping&&!stopping->load(); }

// Native88: RB byte1c is 0 inactive, 1 first mapped tick, 2 sustained.
// Swiss Input+10 is buffered availability and must not supply held state.
inline bool mapped_count(uint64_t input,uint8_t& count) {
    auto copy=step_binding::env().copy;
    uint64_t header=0,root=0;
    if (!input||action==0xffff||!combat_native::copy_checked(copy,&header,input,8)||!header||
        !combat_native::copy_checked(copy,&root,combat_native::at(header,0,0,8),8)) return false;
    uint64_t node=root;
    for (unsigned depth=0;node&&depth<64;++depth) {
        uint8_t sentinel=0;
        uint16_t key=0;
        if (!combat_native::copy_checked(copy,&sentinel,combat_native::at(node,0,0,0x19),1)||sentinel||
            !combat_native::copy_checked(copy,&key,combat_native::at(node,0,0,0x1a),2)) return false;
        if (key==action) {
            uint8_t value=0,again=0; uint64_t header2=0,root2=0; uint16_t key2=0;
            if (!combat_native::copy_checked(copy,&value,combat_native::at(node,0,0,0x1c),1)||value>2||
                !combat_native::copy_checked(copy,&header2,input,8)||header2!=header||
                !combat_native::copy_checked(copy,&root2,combat_native::at(header,0,0,8),8)||root2!=root||
                !combat_native::copy_checked(copy,&key2,combat_native::at(node,0,0,0x1a),2)||key2!=key||
                !combat_native::copy_checked(copy,&again,combat_native::at(node,0,0,0x1c),1)||again!=value) return false;
            count=value; return true;
        }
        uint64_t next=0;
        if (!combat_native::copy_checked(copy,&next,combat_native::at(node,0,0,key<action?0x10:0),8)||next==node) return false;
        node=next;
    }
    return false;
}
struct MappedProof { uint64_t world=0,actor=0,input=0,serial=0; uint8_t count=0; bool down=false; };
inline SRWLOCK proof_lock=SRWLOCK_INIT;
inline MappedProof proof{};
inline std::atomic<uint64_t> producer_serial{0};
inline std::atomic<bool> proof_valid{false};
inline bool refresh_action() {
    uint16_t registered=0xffff;
    const bool known=active()&&combat_native::copy_checked(step_binding::env().copy,&registered,action_address,2);
    if(!known)registered=0xffff;
    if(action.exchange(registered)!=registered)proof_valid.store(false);
    return known&&registered!=0xffff;
}
inline bool held(uint64_t world,uint64_t actor,uint64_t input,bool& down) {
    if (!proof_valid.load()||!TryAcquireSRWLockShared(&proof_lock)) return false;
    const MappedProof p=proof;
    const bool known=proof_valid.load()&&p.world==world&&p.actor==actor&&p.input==input&&
        p.serial==producer_serial.load();
    ReleaseSRWLockShared(&proof_lock);
    uint8_t count=0;
    if (!known||!mapped_count(input,count)||count!=p.count||!proof_valid.load()||
        p.serial!=producer_serial.load()) return false;
    down=p.down; return true;
}
struct Loan { uint64_t world=0,actor=0,input=0,logic=0,status=0; bool down=false,active_state=false,valid=false; };
inline thread_local Loan* current=nullptr;
inline bool identity(uint64_t& world,uint64_t& actor) {
    if (!loadout_player_catalogue::active()||
        !TryAcquireSRWLockShared(&loadout_player_catalogue::state_lock)) return false;
    const auto& s=loadout_player_catalogue::published.catalogue;
    const bool ok=s.valid&&s.actor==loadout_player_catalogue::observed_player_actor.load();
    if (ok) { world=s.world; actor=s.actor; }
    ReleaseSRWLockShared(&loadout_player_catalogue::state_lock); return ok;
}
struct ProducerLoan {
    uint64_t world=0,actor=0,input=0,bank=0,serial=0;
    bool camera_seen=false,supported=true,level_known=false,down=false;
};
inline thread_local ProducerLoan* producer=nullptr;
inline thread_local ProducerLoan* camera_mapping=nullptr;
inline thread_local unsigned mapping_depth=0;
inline bool qualify_producer(const ProducerLoan& s) {
    uint64_t row=0;
    return active()&&s.world&&s.actor&&s.input&&s.bank&&
        loadout_player_catalogue::observed_player_actor.load()==s.actor&&
        step_binding::resolve_player_column(s.world,s.actor,kInput,0x40,&row)&&row==s.input;
}
// Native87/88 exact fixed-input bundle and its current env::Input+138 bank.
// These loans never escape their native producer invocation.
inline void producer_hook(void* columns,void* time,void* native_input) {
    const DWORD incoming=GetLastError();
    // f2g starts before native action registration. Refresh on the native
    // producer thread, after player identity is available, rather than freezing
    // the zero-initialized registry value at DLL load time.
    refresh_action();
    ProducerLoan loan{}; uint64_t q[5]{};
    const bool candidate=active()&&identity(loan.world,loan.actor)&&
        combat_native::copy_checked(step_binding::env().copy,q,reinterpret_cast<uintptr_t>(columns),sizeof q)&&
        q[4]<combat_native::cols::kMaxRow;
    if (candidate) {
        loan.input=combat_native::at(q[0],q[4],0x40);
        loan.bank=combat_native::at(reinterpret_cast<uintptr_t>(native_input),0,0,0x138);
        uint64_t actual=0;
        if (!qualify_producer(loan)||
            !combat_native::copy_checked(step_binding::env().copy,&actual,combat_native::at(q[3],q[4],8,0x10),8)||
            actual!=loan.actor) loan.input=0;
    }
    ProducerLoan* previous=producer;
    if (loan.input) {
        loan.serial=producer_serial.fetch_add(1)+1;
        proof_valid.store(false);
        producer=&loan;
    } else producer=nullptr;
    SetLastError(incoming);
    reinterpret_cast<ProducerFn>(originals[3])(columns,time,native_input);
    const DWORD outgoing=GetLastError();
    producer=previous;
    uint8_t count=0;
    if (loan.input&&loan.camera_seen&&loan.supported&&loan.level_known&&qualify_producer(loan)&&
        loan.serial==producer_serial.load()&&mapped_count(loan.input,count)&&
        TryAcquireSRWLockExclusive(&proof_lock)) {
        proof={loan.world,loan.actor,loan.input,loan.serial,count,loan.down};
        proof_valid.store(active()&&loan.serial==producer_serial.load());
        ReleaseSRWLockExclusive(&proof_lock);
    }
    if (loan.camera_seen&&!loan.supported) ++unsupported_mappings;
    SetLastError(outgoing);
}
inline uint64_t mapped_hook(void* bank,uint16_t key) {
    const DWORD incoming=GetLastError();
    ProducerLoan* s=producer;
    ProducerLoan* previous=camera_mapping;
    const bool ours=mapping_depth==0&&s&&key==action&&reinterpret_cast<uintptr_t>(bank)==s->bank&&qualify_producer(*s);
    // First evaluation remains stock, including edge predicates. In particular
    // unsupported mappings cannot partially rewrite native Input production.
    camera_mapping=nullptr;
    ++mapping_depth;
    SetLastError(incoming);
    const uint64_t result=reinterpret_cast<MappedFn>(originals[4])(bank,key);
    const DWORD outgoing=GetLastError();
    if (ours&&qualify_producer(*s)) {
        s->camera_seen=true; ++mapped_evaluations;
        camera_mapping=s;
        SetLastError(incoming);
        const uint64_t level=reinterpret_cast<MappedFn>(originals[4])(bank,key);
        s->level_known=s->supported&&qualify_producer(*s);
        s->down=static_cast<uint8_t>(level)!=0;
    }
    --mapping_depth;
    camera_mapping=previous;
    SetLastError(outgoing); return result;
}
inline uint8_t predicate_hook(void* descriptor,uint16_t key,void* args) {
    const DWORD incoming=GetLastError();
    ProducerLoan* s=camera_mapping;
    alignas(16) uint8_t local[0x60]{};
    void* evaluated=descriptor;
    if (s&&qualify_producer(*s)) {
        uint64_t bank=0;
        if (!combat_native::copy_checked(step_binding::env().copy,&bank,reinterpret_cast<uintptr_t>(args),8)||
            bank!=s->bank||!combat_native::copy_checked(step_binding::env().copy,local,
                reinterpret_cast<uintptr_t>(descriptor),sizeof local)) s->supported=false;
        else {
            const uint8_t kind=local[0x44];
            if (kind==5) {
                const uint8_t mode=local[0x54];
                if (mode>4) {
                    s->supported=false;
                    unsupported_modes.fetch_or(uint32_t{1}<<(mode<31?mode:31));
                }
                else if (mode>=2) {
                    // Native90: 0=current, 2=pressed, 3=released, 4=repeat.
                    // Native91 refresh mutates only cache48 and binding50/52.
                    // This plain descriptor copy retains native gate38 and IDs;
                    // the original case5 does not retain the descriptor address.
                    local[0x54]=0; evaluated=local; ++edge_overrides;
                }
            } else if (kind!=1&&kind!=2&&kind!=3&&kind!=4&&kind!=10) {
                // Preserve logical composition; do not reinterpret unknown
                // stateful gestures or device-specific predicate tables.
                s->supported=false;
                unsupported_kinds.fetch_or(uint32_t{1}<<(kind<31?kind:31));
            }
        }
    }
    SetLastError(incoming);
    const uint8_t result=reinterpret_cast<PredicateFn>(originals[5])(evaluated,key,args);
    const DWORD outgoing=GetLastError();
    SetLastError(outgoing); return result;
}
inline bool qualify(Loan& s) {
    uint64_t input=0,logic=0,status=0;
    return active()&&s.valid&&
        loadout_player_catalogue::observed_player_actor.load()==s.actor&&
        step_binding::resolve_player_column(s.world,s.actor,kInput,0x40,&input)&&input==s.input&&
        step_binding::resolve_player_column(s.world,s.actor,step_binding::kColLogic,0x810,&logic)&&logic==s.logic&&
        step_binding::resolve_player_column(s.world,s.actor,kLock,0xf0,&status)&&status==s.status;
}
inline Loan capture(void* columns,bool active_state,void* ctx=nullptr) {
    Loan s{}; s.active_state=active_state;
    uint64_t q[10]{};
    if (!active()||!identity(s.world,s.actor)||
        !combat_native::copy_checked(step_binding::env().copy,q,reinterpret_cast<uintptr_t>(columns),active_state?0x38:0x50)) return s;
    const uint64_t row=q[active_state?6:9];
    if (row>=combat_native::cols::kMaxRow) return s;
    if (ctx) {
        uint64_t w=0;
        if (!combat_native::copy_checked(step_binding::env().copy,&w,reinterpret_cast<uintptr_t>(ctx),8)||w!=s.world) return s;
    }
    s.input=combat_native::at(q[active_state?2:4],row,0x40);
    s.logic=combat_native::at(q[active_state?3:5],row,0x810);
    s.status=combat_native::at(q[active_state?1:6],row,0xf0);
    s.valid=s.input&&s.logic&&s.status;
    if (!qualify(s)||!held(s.world,s.actor,s.input,s.down)) s.valid=false;
    return s;
}
struct Scope {
    Loan loan{}; Loan* previous=nullptr;
    Scope(void* cols,bool mode,void* ctx=nullptr):loan(capture(cols,mode,ctx)),previous(current) {
        current=loan.valid?&loan:nullptr;
    }
    ~Scope() { current=previous; }
};
inline void active_hook(void* cols,void* dt) {
    const DWORD incoming=GetLastError(); DWORD outgoing=0;
    { Scope scope(cols,true); SetLastError(incoming);
      reinterpret_cast<ActiveFn>(originals[0])(cols,dt); outgoing=GetLastError(); }
    SetLastError(outgoing);
}
inline uint64_t flag_hook(void* input,uint16_t key) {
    const DWORD incoming=GetLastError(); SetLastError(incoming);
    uint64_t result=reinterpret_cast<FlagFn>(originals[1])(input,key);
    const DWORD outgoing=GetLastError(); auto* s=current;
    if (s&&key==action&&reinterpret_cast<uintptr_t>(input)==s->input&&qualify(*s)) {
        uint8_t locked=0;
        if (combat_native::copy_checked(step_binding::env().copy,&locked,s->status,1)) {
            const uint8_t request=static_cast<uint8_t>(s->down!=(locked!=0));
            if (static_cast<uint8_t>(result)!=request) ++transitions;
            result=(result&~uint64_t{0xff})|request;
        }
    }
    SetLastError(outgoing); return result;
}
inline void clear_hook(void* logic,uint64_t mask,uint8_t retain) {
    const DWORD incoming=GetLastError(); auto* s=current;
    if (s&&s->active_state&&mask==kMask&&reinterpret_cast<uintptr_t>(logic)==s->logic&&
        !s->down&&retain&&qualify(*s)) { retain=0; ++release_cleanups; }
    SetLastError(incoming);
    reinterpret_cast<ClearFn>(originals[2])(logic,mask,retain);
    const DWORD outgoing=GetLastError(); SetLastError(outgoing);
}
struct Hook { uint32_t rva; const char* pin; void* detour; void** original; };
inline std::array<Hook,3> hook_specs() {
    return {{
        {0x27e5d50,"488bc44889580848896810488970184889782041564883ec60",reinterpret_cast<void*>(&active_hook),&originals[0]},
        {0x228d190,"4883ec284c8b01498b40088078190075186639501a7306488b4010",reinterpret_cast<void*>(&flag_hook),&originals[1]},
        {0x2295f10,"48895c2408488974241048897c241841564883ec20410fb6f0",reinterpret_cast<void*>(&clear_hook),&originals[2]}
    }};
}
inline std::array<Hook,3> mapping_hook_specs() {
    return {{
        {0x228e680,"488bc44889501053565741544155415641574881ecc0000000",reinterpret_cast<void*>(&producer_hook),&originals[3]},
        {0x297d860,"48895c2410574883ec30488b010fb7da488bf94885c00f8483",reinterpret_cast<void*>(&mapped_hook),&originals[4]},
        {0x297c270,"48895c241048896c2418488974242057415641574883ec304d8bf80f",reinterpret_cast<void*>(&predicate_hook),&originals[5]}
    }};
}
inline bool arm(uint64_t base,bool loaded_contracts_and_module_pin_verified,const std::atomic<bool>& stop) {
    if (!base||!step_binding::env().copy||step_binding::env().base!=base||!loaded_contracts_and_module_pin_verified||enabled.load()||stopped.load()||stop.load()) return false;
    for (auto* original:originals) if (!original) return false;
    action_address=combat_native::at(base,0,0,0x5ceb500);
    action.store(0xffff); stopping=&stop; enabled.store(true);
    if (stop.load()) enabled.store(false); return enabled.load();
}
inline void stop() { stopped.store(true); enabled.store(false); proof_valid.store(false); }
inline void flush(FILE* file) {
    if (!file) return;
    const auto a=transitions.exchange(0),b=release_cleanups.exchange(0);
    if (a||b) std::fprintf(file,"{\"event\":\"held_lock_native\",\"action\":%u,\"transitions\":%llu,\"release_cleanups\":%llu,\"native_target_selection\":true,\"direct_locked_byte_writes\":false}\n",static_cast<unsigned>(action.load()),static_cast<unsigned long long>(a),static_cast<unsigned long long>(b));
    const auto m=mapped_evaluations.exchange(0),o=edge_overrides.exchange(0),u=unsupported_mappings.exchange(0);
    static unsigned reports=0;
    if ((m||o||u)&&reports<1024) {
        ++reports;
        std::fprintf(file,"{\"event\":\"held_lock_mapping\",\"action\":%u,\"evaluations\":%llu,\"edge_modes_to_current\":%llu,\"unsupported\":%llu,\"unsupported_kind_bits\":%u,\"unsupported_mode_bits\":%u,\"current_player_proof\":%s,\"configured_binding_preserved\":true,\"physical_key_override\":false,\"stock_input_result_preserved\":true}\n",static_cast<unsigned>(action.load()),static_cast<unsigned long long>(m),static_cast<unsigned long long>(o),static_cast<unsigned long long>(u),unsupported_kinds.load(),unsupported_modes.load(),proof_valid.load()?"true":"false");
    }
}
}
#endif
