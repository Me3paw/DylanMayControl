#pragma once
// Current-build Step uses native arbitration, write loans and movement policy.
// No logging, native phase/vector writes or ownership bypass.
#include "combat_v1_runtime.h"
#include "enemy_contact_native.h"
namespace combat_v1_step_native {
using Rows=loadout_actor_resolver::Rows;
using U=std::uintptr_t;
using JumpInput=void(*)(void*,void*,void*,float*);
using Three=void(*)(void*,void*,void*);
using Dodge=void(*)(void*,float*,void*,void*);
using One=void(*)(void*);
using Movement=void(*)(void*,float*);
using Conflict=std::uint8_t(*)(void*,void*,void*,std::uint8_t*);
using Request=void(*)(void*,std::uint64_t,std::uint32_t);
using Motion=void(*)(std::uint32_t,void*,void*,void*,void*,void*,void*,void*);
inline JumpInput input_original=nullptr;
inline Three jump_start_original=nullptr,jump_requests_original=nullptr;
inline Dodge dodge_original=nullptr;
inline One air_limit_original=nullptr;
inline Movement movement_original=nullptr;
inline Conflict conflict_original=nullptr;
inline Request request_original=nullptr;
inline Motion motion_original=nullptr;
inline std::atomic<bool> enabled{false};
inline SRWLOCK mutex=SRWLOCK_INIT;
struct Guard {
 bool held=true;
 Guard(){AcquireSRWLockExclusive(&mutex);}
 ~Guard(){ReleaseSRWLockExclusive(&mutex);}
};
struct Ticket {
 std::uint64_t world=0,actor=0,use=0;
 std::uint32_t dodge_used=0;
 bool down=false,pending=false,pair=false,accepted=false,old_end=false;
 bool air_reset=false,dodge_reset=false,dodge_snapshot=false,jump_ended=false,cut=false,written=false;
};
inline Ticket ticket{};
inline auto& player_logic=combat_v1_runtime::player_logic;
template<class T> inline bool read(U p,T& value){return combat_v1_runtime::read(p,value);}
template<class T> inline bool write(U p,const T& value) {
 auto& e=step_binding::env();
 return p&&e.write&&e.can_write&&e.can_write(reinterpret_cast<void*>(p),sizeof value)&&
  e.write(reinterpret_cast<void*>(p),&value,sizeof value);
}
inline bool running(){return enabled.load(std::memory_order_relaxed)&&combat_v1_runtime::running();}
inline bool same(const Rows& r){return ticket.world==r.location.world&&ticket.actor==r.location.actor;}
inline void identity(const Rows& r){if(!same(r))ticket={r.location.world,r.location.actor};}
inline bool owner(Rows& r){
 if(!running())return false;
 if(combat_v1_runtime::owner(r)&&loadout_actor_resolver::current(r))return true;
 Guard g;if(g.held)ticket={}; // Global player identity unavailable/dead: expire.
 return false;
}
template<std::size_t N> inline bool loan(void* columns,std::uint64_t(&q)[N],Rows& r) {
 return owner(r)&&read(reinterpret_cast<U>(columns),q)&&q[N-1]==r.location.row&&
  q[N-2]==r.location.chunk;
}
inline U col(const std::uint64_t* q,unsigned c,unsigned row,unsigned stride){return combat_native::at(q[c],q[row],stride);}
inline bool resolve(const Rows& r,std::uint32_t hash,unsigned stride,U& address) {
 std::uint64_t p=0;
 if(!step_binding::resolve_player_column(r.location.world,r.location.actor,hash,stride,&p))return false;
 address=static_cast<U>(p);return true;
}
inline bool component(const Rows& r,U actual,std::uint32_t hash,unsigned stride) {
 U expected=0;return actual&&resolve(r,hash,stride,expected)&&actual==expected;
}
inline bool slot(U logic,unsigned index,std::uint8_t& flags,std::uint16_t& result,std::uint8_t& cached) {
 return read(logic+index*0x28+0x1c,flags)&&read(logic+index*0x28+0x1e,result)&&read(logic+index*0x28+0x20,cached);
}
inline void end_jump(U logic) {
 reinterpret_cast<void(*)(void*,std::uint64_t,std::uint8_t)>(combat_v1_runtime::base+0x22d0540)(reinterpret_cast<void*>(logic),0x10,0);
}
inline std::uint64_t active_use(const Rows& r) {
 combat_v1_runtime::Guard guard;
 return combat_v1_runtime::world==r.location.world&&combat_v1_runtime::actor==r.location.actor?
  combat_v1_runtime::active.token.serial:0;
}
inline void input(void* columns,void* abilities,void* world_state,float* dt) {
 const DWORD incoming=GetLastError();Rows r{};std::uint64_t q[9]{};bool fresh=false;
 const bool owned=loan(columns,q,r)&&col(q,6,8,0x810)==r.logic;
 if(owned) {
  player_logic.store(r.logic,std::memory_order_relaxed);
  std::uint8_t phase=255,type=0,index=0,airborne=1;U air=0;
  const U jump=col(q,3,8,0x28);
  if(read(jump+0x14,phase)&&phase==0&&read(jump+0x15,type)&&type==3&&read(jump+0x20,index)&&index==255&&
     resolve(r,step_binding::kColAirborne,0xc,air)&&read(air+8,airborne)&&!airborne) {
   Guard g;if(g.held&&same(r)){ticket.accepted=false;ticket.cut=false;ticket.written=false;}
  }
  bool down=false;
  if(step_binding::env().have_jump_action&&step_binding::jump_down(col(q,1,8,0x40),&down)) {
   Guard g;if(g.held){identity(r);fresh=down&&!ticket.down;ticket.down=down;ticket.pending=false;ticket.pair=false;}
  }
 }
 SetLastError(incoming);input_original(columns,abilities,world_state,dt);const DWORD forwarded=GetLastError();
 if(owned&&fresh&&loadout_actor_resolver::current(r)) {
  std::uint64_t authored=0,enemy=0;std::uint32_t count=0;std::uint8_t control=0;float cooldown=1;
  const U cfg=col(q,0,8,0x80),delay=col(q,5,8,8),njr=col(q,4,8,2);
  const bool ready=read(cfg+8,authored)&&authored&&read(cfg+0x10,count)&&count&&
   read(col(q,2,8,2),control)&&!(control&0x1c)&&read(delay+4,cooldown)&&std::isfinite(cooldown)&&cooldown<=0&&
   // Input precedes native cache reset; current-cycle consume/on_start owns acceptance.
   enemy_contact_native::contact_now(r.location.world,r.location.actor,&enemy)&&
   step_binding::is_enemy(r.location.world,enemy)&&loadout_actor_resolver::current(r);
  if(ready) {
   const std::uint16_t first_jump=0;
   // NJR is our WRITE loan; no resource reset or attack end before acceptance.
   bool armed=false;
   {Guard g;if(g.held&&same(r)&&write(njr,first_jump)){ticket.pending=true;ticket.pair=true;ticket.old_end=false;armed=true;}}
   if(armed)request_original(reinterpret_cast<void*>(r.logic),0x10,0);
  }
 }
 SetLastError(forwarded);
}
__attribute__((noinline)) inline std::uint8_t conflict(void* logic,void* candidate,void* other,std::uint8_t* dependency) {
 const auto caller=reinterpret_cast<U>(__builtin_return_address(0));
 const std::uint8_t original=conflict_original(logic,candidate,other,dependency);const DWORD forwarded=GetLastError();
 Rows r{};std::uint64_t other_mask=0;std::uint8_t result=original;
 if(caller==combat_v1_runtime::base+0x22d201d&&player_logic.load(std::memory_order_relaxed)==reinterpret_cast<U>(logic)&&owner(r)&&r.logic==reinterpret_cast<U>(logic)&&dependency) {
  const auto c=reinterpret_cast<U>(candidate),o=reinterpret_cast<U>(other);
  const bool other_slot=o>=r.logic&&o<r.logic+50*0x28&&(o-r.logic)%0x28==0;
  {
   Guard g;
   if(g.held&&same(r)&&ticket.pair) {
    if(c==r.logic+0xa0&&o==r.logic){result=0;*dependency=0;}
    else if(c==r.logic&&o==r.logic+0xa0){result=1;*dependency=0;}
   }
  }
  if(c==r.logic&&result&&other_slot&&read(o+8,other_mask))combat_v1_runtime::note_competitor(r,other_mask);
 }
 SetLastError(forwarded);return result;
}
inline void jump_start(void* columns,void* status,void* events) {
 const DWORD incoming=GetLastError();Rows r{};std::uint64_t q[7]{};bool accepting=false;
 const bool owned=loan(columns,q,r)&&col(q,3,6,0x810)==r.logic&&component(r,col(q,0,6,0x28),step_binding::kColJump,0x28);
 if(owned) {
  std::uint8_t flags=0,cached=0;std::uint16_t result=0,njr=0;
  if(slot(r.logic,4,flags,result,cached)&&(flags&4)&&cached&&!(result&0x10)&&read(col(q,2,6,2),njr)&&njr==0) {
   Guard g;if(g.held&&same(r)&&ticket.pending){const std::uint8_t zero=0;accepting=write(col(q,0,6,0x28)+0x22,zero);}
  }
 }
 SetLastError(incoming);jump_start_original(columns,status,events);const DWORD forwarded=GetLastError();
 if(owned&&loadout_actor_resolver::current(r)) {
  std::uint8_t flags=0,cached=0,phase=0,type=3,index=255;std::uint16_t result=0;
  const U jump=col(q,0,6,0x28);
  if(slot(r.logic,4,flags,result,cached)&&(flags&4)) {
   const bool success=accepting&&read(jump+0x14,phase)&&(phase==1||phase==2)&&read(jump+0x15,type)&&type==0&&read(jump+0x20,index)&&index==0;
   U dodge_row=0;std::uint32_t dodge_used=0;
   const bool dodge_snapshot=success&&resolve(r,step_binding::kColDodge,0x40,dodge_row)&&read(dodge_row+0x34,dodge_used);
   {Guard g;if(g.held&&same(r)){if(success){ticket.pending=false;ticket.accepted=true;ticket.air_reset=true;ticket.dodge_reset=true;ticket.dodge_used=dodge_used;ticket.dodge_snapshot=dodge_snapshot;ticket.jump_ended=false;ticket.cut=false;ticket.written=false;if(ticket.old_end)ticket.pair=false;}else{const bool down=ticket.down;ticket={r.location.world,r.location.actor};ticket.down=down;}}}
  }
 }
 SetLastError(forwarded);
}
inline void jump_requests(void* columns,void* events,void* status) {
 const DWORD incoming=GetLastError();Rows r{};std::uint64_t q[8]{};
 const bool owned=loan(columns,q,r)&&col(q,4,7,0x810)==r.logic;
 SetLastError(incoming);jump_requests_original(columns,events,status);const DWORD forwarded=GetLastError();
 if(owned&&loadout_actor_resolver::current(r)) {
  std::uint8_t flags=0,cached=0,phase=255;std::uint16_t result=0;
  if(slot(r.logic,4,flags,result,cached)&&cached&&read(col(q,1,7,0x28)+0x14,phase)) {
   Guard g;if(g.held&&same(r)){
    if((result&0x10)&&!(result&8)&&!(flags&4)){ticket.pending=false;ticket.pair=false;}
    if((result&1)&&!(result&8)&&phase==0)ticket.jump_ended=true;
   }
  }
 }
 SetLastError(forwarded);
}
inline void reset_air(const Rows& r,U air) {
 if(!component(r,air,step_binding::kColAirAttacks,0x10))return;
 Guard g;if(g.held&&same(r)&&ticket.air_reset){const std::uint32_t zero=0;if(write(air+8,zero))ticket.air_reset=false;}
}
inline void air_limit(void* columns) {
 const DWORD incoming=GetLastError();Rows r{};std::uint64_t q[4]{};
 if(loan(columns,q,r))reset_air(r,col(q,0,3,0x10));
 SetLastError(incoming);air_limit_original(columns);
}
inline void dodge(void* columns,float* dt,void* events,void* filtering) {
 const DWORD incoming=GetLastError();Rows r{};std::uint64_t q[11]{};
 if(loan(columns,q,r)&&col(q,7,10,0x810)==r.logic&&component(r,col(q,3,10,0x40),step_binding::kColDodge,0x40)) {
  std::uint32_t used=0;std::uint8_t flags=0,cached=0;std::uint16_t result=0;
  const bool state_ok=read(col(q,3,10,0x40)+0x34,used)&&slot(r.logic,1,flags,result,cached)&&!(flags&6);
  Guard g;if(g.held&&same(r)&&ticket.dodge_reset){
   if(!state_ok||!ticket.dodge_snapshot||used!=ticket.dodge_used)ticket.dodge_reset=false;
   else {const std::uint32_t zero=0;if(write(col(q,3,10,0x40)+0x34,zero))ticket.dodge_reset=false;}
  }
 }
 SetLastError(incoming);dodge_original(columns,dt,events,filtering);
}
// Wire PRE unconditionally in lead start hook, independently of was_pending.
inline void attack_start_pre(void* columns) {
 Rows r{};std::uint64_t q[10]{};
 if(loan(columns,q,r)&&col(q,0,9,0x810)==r.logic&&col(q,1,9,0xb0)==r.attack) {
  player_logic.store(r.logic,std::memory_order_relaxed);reset_air(r,col(q,2,9,0x10));
 }
}
// Wire POST after lead updates active token, outside its Guard (avoid recursion).
inline void attack_start_post(const Rows& r,bool accepted) {
 if(!accepted||!running()||!loadout_actor_resolver::current(r))return;
 const auto use=active_use(r);bool stop=false;U jump=0;std::uint8_t phase=255,held=1;std::uint32_t timer=1;
 const bool already_ended=resolve(r,step_binding::kColJump,0x28,jump)&&read(jump+0x14,phase)&&phase==0&&
  read(jump+0x18,timer)&&timer==0&&read(jump+0x21,held)&&held==0;
 {Guard g;if(g.held&&same(r)&&ticket.accepted&&use){ticket.use=use;ticket.cut=true;ticket.jump_ended=already_ended;ticket.written=false;stop=!already_ended;}}
 if(stop)end_jump(r.logic); // Current lead on_start still owns WRITE Logic.
}
// Wire POST lead native melee end, before any later accepted use; no second hook.
inline void attack_end_post(const Rows& r,std::uint16_t result,bool interrupted) {
 if(!(result&1))return;
 Guard g;if(g.held&&same(r)){
  const bool step_pair=ticket.pair;
  ticket.old_end=true;if(ticket.accepted)ticket.pair=false;
  if(ticket.cut){ticket.cut=false;ticket.written=false;}
  if(interrupted&&!step_pair&&!ticket.pending)ticket.accepted=false;
 }
}
__attribute__((noinline)) inline void motion(std::uint32_t type,void* attack,void* targeting,void* transform,void* action,void* output,void* air,void* settings) {
 const auto caller=reinterpret_cast<U>(__builtin_return_address(0));
 motion_original(type,attack,targeting,transform,action,output,air,settings);const DWORD forwarded=GetLastError();
 Rows r{};U state=0;std::uint8_t model=0,active=0;
 if(caller==combat_v1_runtime::base+0x23ad7e0&&owner(r)&&r.attack==reinterpret_cast<U>(attack)&&component(r,reinterpret_cast<U>(settings),0x71f971c3,2)&&
    resolve(r,0xe6b9cb0b,0x40,state)&&read(state+0x34,model)&&model==5&&read(r.attack+0xa0,active)&&(active&1)) {
  const auto use=active_use(r);Guard g;
  if(g.held&&same(r)&&ticket.cut&&ticket.jump_ended&&ticket.use==use){const std::uint16_t value=5;if(write(reinterpret_cast<U>(settings),value))ticket.written=true;}
 }
 SetLastError(forwarded);
}
inline void movement(void* columns,float* dt) {
 const DWORD incoming=GetLastError();Rows r{};std::uint64_t q[20]{};std::uint16_t settings=0;std::uint8_t model=0;bool consuming=false;
 if(loan(columns,q,r)&&component(r,col(q,7,19,0x40),0xe6b9cb0b,0x40)&&read(col(q,7,19,0x40)+0x34,model)&&model==5&&read(col(q,14,19,2),settings)&&settings==5) {
  const auto use=active_use(r);Guard g;if(g.held&&same(r))consuming=ticket.cut&&ticket.written&&ticket.jump_ended&&ticket.use==use;
 }
 SetLastError(incoming);movement_original(columns,dt);const DWORD forwarded=GetLastError();
 if(consuming&&loadout_actor_resolver::current(r)){Guard g;if(g.held&&same(r)){ticket.accepted=false;ticket.cut=false;ticket.written=false;}}
 SetLastError(forwarded);
}
inline constexpr native_pins::Pin pins[]={
 {"v1_step_input",0x243b190,14,"4c89442418488954241053564154415541574881ec90000000c5fc1011c5fc104920c5fb104140c5fb11442470488b4c2470498bd9c4e37d19d001c4e1f97ec0"},
 {"v1_step_conflict",0x22d1c10,14,"405357415641574883ec484d8bf966c74424300810498bf84c8d542430488bda4c8bf132c06666660f1f840000000000410a02488d4c243249ffc24c3bd175f0"},
 {"v1_step_jump_start",0x243c240,14,"488bc44c89401848895010535556574155415641574881ec90000000c5fc1011c5fb104130c5fc11542440c5fb1140a84c8b70a8488be9488b7c24404d8beec4"},
 {"v1_step_jump_requests",0x243be20,15,"4c8bdc49895310564881ecd0000000c5fc105120c5fc100949895b18488bd949896bf049897be8498bf84d8963e04d8d4308c4e37d19d001c4c3f916c101c4e3"},
 {"v1_step_dodge",0x26986e0,14,"488bc44c8948204c89401853565741544155415641574881ecd0000000c5fc1009c5fc105120c5fb104150c5f82970b8c5f82978a84c8be1c5fc114c2450c5fb"},
 {"v1_step_air_limit",0x23abb40,14,"4883ec28c5fc1009c4e37d19c801c4e3f916c0014869c8b0000000488bd048c1e204c4e3f916c801c5fc110c2448031424807c01700075048b02eb038b420485"},
 {"v1_step_motion",0x23acb30,15,"48895c240848896c24104889742418574883ec404c8b8c2488000000bdffff00004c8b542478488bda8bf1c5f057c96641c7010f010fb78280000000410fb678"},
 {"v1_step_movement",0x236db30,20,"488bc45641564881ece80100004c8b8998000000488b71384d8bc14c8b714849c1e0064903f04d03f0807e34050f852d0400004889580848896810488978184c"},
 {"v1_step_request",0x22d0880,15,"48895c2410488974241848897c242041564883ec30418bd8488bf24c8bf1c5f857c04885d27807c4e1fa2ac2eb18488bc6488bce48d1e983e001480bc8c4e1fa"},
 {"v1_step_request_end",0x22d0540,15,"48895c2408488974241048897c241841564883ec20410fb6f0488bda4c8bf1c5f857c04885d27807c4e1fa2ac2eb18488bcb48d1e9488bc383e001480bc8c4e1"},
};
inline bool prepare(std::uintptr_t base,const std::atomic<bool>& stopped) {
 for(const auto& pin:pins)if(!native_pins::verify(combat_native::binding().copy,base,pin))return false;
 const auto* input_pin=native_pins::find("input_flag_a");
 if(!input_pin||!native_pins::verify(combat_native::binding().copy,base,*input_pin))return false;
 std::uint16_t action=UINT16_MAX;
 if(!read(base+0x5ce6598,action)||action==UINT16_MAX||!enemy_contact_native::arm(base,stopped))return false;
 request_original=reinterpret_cast<Request>(base+0x22d0880);
 auto& e=step_binding::env();e.write=&gameplay_runtime::write;e.can_write=&gameplay_runtime::writable;
 e.pins_ok=true;e.have_jump_action=true;e.jump_action=action;
 return true;
}
} // namespace combat_v1_step_native
