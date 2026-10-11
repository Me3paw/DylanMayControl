#pragma once
#include "native_ui.h"
#include "loadout_catalogue_join.h"
#include "loadout_form_requirements.h"
#include "loadout_lookup_probe.h"
#include "loadout_key_requests.h"
#ifdef _WIN32
namespace loadout_player_catalogue {
using DispatcherA=void (*)(void*,void*,void*);
using DispatcherB=void (*)(void*,std::uint64_t,void*);
using Body=void (*)(void*,void*,void*,void*,void*,void*);
inline void* original_a=nullptr;
inline void* original_b=nullptr;
inline void* original_body=nullptr;
inline std::uintptr_t exe_base=0;
inline const std::atomic<bool>* shared_stopping=nullptr;
inline std::atomic<bool> enabled{false};
inline std::atomic<std::uint64_t> captured{0},invalid{0},dropped{0};
// Copied full handle for passive filtering; never a mutation authority.
inline std::atomic<std::uint64_t> observed_player_actor{0};
struct Loan { std::uintptr_t world=0; mutable bool saw_body=false; };
inline thread_local const Loan* current_loan=nullptr;
struct Item {
    std::uint32_t id=0;
    std::uint16_t raw_flags=0;
    std::uint8_t category=UINT8_MAX,channels=0;
    bool known=false,acquired=false,selected=false;
    char name[256]{},icon[256]{};
};
struct Catalogue {
    std::uint64_t world=0,actor=0,graph_key=0,cfg_key=0,definitions_key=0;
    // Identity from the real Player body loan, never dereferenced after it.
    std::uint64_t ui_stacks_key=0;
    std::uint32_t revision=0;
    std::array<std::uint32_t,4> loadout{};
    std::array<Item,64> items{};
    std::size_t count=0;
    bool valid=false,incomplete=false;
};
struct Published {
    Catalogue catalogue{};
    loadout_control::MenuModel menu{};
    std::uint64_t view_generation=0;
};
using AdapterFactory=loadout_control::Adapter (*)(Catalogue&,std::uint64_t);
inline AdapterFactory adapter_factory=nullptr;
inline SRWLOCK state_lock=SRWLOCK_INIT;
inline loadout_control::Control control;
inline bool slots_restored=false;
inline Published published{};
inline std::uint64_t catalogue_token=0;
inline native_ui::Receipt pending_receipt{};
inline loadout_control::Result pending_result=loadout_control::Result::Unbound;
inline bool active() { return enabled.load() && shared_stopping && !shared_stopping->load(); }
inline bool node_pin_matches() {
    unsigned char bytes[25]{};
    return exe_base && loadout_attack_probe::copy(exe_base+0x1dda220,bytes,sizeof(bytes)) &&
        std::memcmp(bytes,loadout_lookup_probe::node_pin,sizeof(bytes))==0;
}
inline bool copy_name(std::uintptr_t definition,char (&out)[256]) {
    std::memset(out,0,sizeof(out));
    std::uintptr_t field=0,address=0;
    std::uint32_t header[2]{};
    if (!loadout_attack_probe::add(definition,0xf0,field) ||
        !loadout_attack_probe::copy(field,header,sizeof(header)) || !header[1] || header[1]>255 ||
        !loadout_attack_probe::add(field,8,address)) return false;
    const std::uint64_t capacity_byte=header[0]>>24;
    const std::uint64_t inline_capacity=capacity_byte<0x80 ? capacity_byte*8 : capacity_byte*0x100-0x7c00;
    const std::uint64_t allocation_bytes=static_cast<std::uint64_t>(header[0]&0xffffff)*8;
    if (inline_capacity<allocation_bytes) {
        std::uint64_t pointer=0;
        if (header[1]>allocation_bytes || !loadout_attack_probe::copy(address,&pointer,sizeof(pointer)) ||
            !pointer || pointer>(std::numeric_limits<std::uintptr_t>::max)()) return false;
        address=static_cast<std::uintptr_t>(pointer);
    } else if (header[1]>inline_capacity || header[1]>0x48) return false;
    if (!loadout_attack_probe::copy(address,out,header[1])) { std::memset(out,0,sizeof(out)); return false; }
    for (unsigned i=0;i<header[1];++i) if (static_cast<unsigned char>(out[i])<0x20) {
        std::memset(out,0,sizeof(out)); return false;
    }
    wchar_t check[256]{};
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,out,static_cast<int>(header[1]),check,256)) {
        std::memset(out,0,sizeof(out)); return false;
    }
    return true;
}
// Human-readable form label. The definition string at +0xf0 is the form's icon path
// (coui://.../weapon_form_cleave_spin_small.png); no separate display-name field is evidenced, so the label is derived from the file name:
// last path segment, extension, "weapon_form_" prefix and "_small" suffix removed, words title-cased ("Cleave Spin"). Falls back to "Form <id>".
inline void short_name(const char* in,std::uint32_t id,char (&out)[256]) {
    char word[128]{}; std::size_t n=0;
    const char* segment=in ? in : "";
    for (const char* p=segment;*p;++p) if (*p=='/' || *p=='\\') segment=p+1;
    for (const char* p=segment;*p && n<sizeof(word)-1;++p) word[n++]=*p;
    word[n]=0;
    for (std::size_t i=n;i>0 && n-i<6;--i) if (word[i-1]=='.') { n=i-1; word[n]=0; break; }
    const auto starts=[&](const char* prefix) { std::size_t k=0; while (prefix[k] && k<n && word[k]==prefix[k]) ++k; return !prefix[k]; };
    std::size_t begin=starts("weapon_form_") ? 12 : 0;
    if (n>=begin+6 && std::memcmp(word+n-6,"_small",6)==0) { n-=6; word[n]=0; }
    std::memset(out,0,sizeof(out)); std::size_t o=0; bool boundary=true;
    for (std::size_t i=begin;i<n && o<32;++i) {
        char c=word[i];
        if (c=='_' || c=='-' || c==' ') { if (o && out[o-1]!=' ') out[o++]=' '; boundary=true; continue; }
        if (boundary && c>='a' && c<='z') c=static_cast<char>(c-'a'+'A');
        else if (!boundary && c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
        out[o++]=c; boundary=false;
    }
    while (o && out[o-1]==' ') out[--o]=0;
    if (!o) std::snprintf(out,sizeof(out),"Form %u",id);
}
inline bool capture(std::uintptr_t world,std::uintptr_t columns,std::uintptr_t graph,Catalogue& out) {
    out={}; std::uint64_t q[5]{};
    if (!world || !graph || !columns || !node_pin_matches() ||
        !loadout_attack_probe::copy(columns,q,sizeof(q)) || q[4]>UINT32_MAX) return false;
    std::uintptr_t loadout=0,cfg=0,mud=0,address=0;
    if (!loadout_attack_probe::row_address(q[0],q[4],0x10,loadout) ||
        !loadout_attack_probe::row_address(q[1],q[4],0xa0,cfg) ||
        !loadout_attack_probe::row_address(q[2],q[4],8,mud) ||
        !loadout_attack_probe::row_address(q[3],q[4],8,address) ||
        !loadout_attack_probe::add(address,0x10,address)) return false;
    Catalogue result{}; result.world=world; result.graph_key=graph; result.cfg_key=cfg;
    if (!loadout_attack_probe::copy(address,&result.actor,sizeof(result.actor)) ||
        !loadout_attack_probe::copy(loadout,result.loadout.data(),sizeof(result.loadout))) return false;
    loadout_catalogue_join::Facts joined{};
    if (!loadout_catalogue_join::inspect(graph,mud,joined,&loadout_attack_probe::copy)) return false;
    result.revision=joined.revision;
    const auto tree=static_cast<std::uintptr_t>(joined.tree_key_address);
    std::uint64_t definitions=0,nodes=0,head=0,first=0;
    std::uint32_t definition_count=0,node_count=0;
    if (!loadout_attack_probe::add(cfg,8,address) || !loadout_attack_probe::copy(address,&definitions,sizeof(definitions)) ||
        !loadout_attack_probe::add(cfg,0x10,address) || !loadout_attack_probe::copy(address,&definition_count,sizeof(definition_count)) ||
        definition_count>2048 || (definition_count && !loadout_lookup_probe::readable_span(
            static_cast<std::uintptr_t>(definitions),static_cast<std::uint64_t>(definition_count)*0x140)) ||
        !loadout_attack_probe::add(tree,8,address) || !loadout_attack_probe::copy(address,&nodes,sizeof(nodes)) ||
        !loadout_attack_probe::add(tree,0x10,address) || !loadout_attack_probe::copy(address,&node_count,sizeof(node_count)) ||
        node_count>2048) return false;
    result.definitions_key=definitions;
    if (node_count && (!loadout_lookup_probe::readable_span(static_cast<std::uintptr_t>(nodes),
            static_cast<std::uint64_t>(node_count)*0x268) ||
        !loadout_attack_probe::add(tree,0x18,address) || !loadout_attack_probe::copy(address,&head,sizeof(head)) ||
        !loadout_lookup_probe::readable_span(static_cast<std::uintptr_t>(head),0x28) ||
        !loadout_attack_probe::copy(static_cast<std::uintptr_t>(head),&first,sizeof(first)) ||
        !loadout_lookup_probe::readable_span(static_cast<std::uintptr_t>(first),0x28))) return false;
    const auto get_node=reinterpret_cast<loadout_lookup_probe::Getter>(exe_base+0x1dda220);
    const auto limit=definition_count<64 ? definition_count : 64u;
    result.incomplete=definition_count>limit;
    for (unsigned i=0;i<limit;++i) {
        std::uintptr_t definition=0; std::uint32_t id=0;
        if (!loadout_attack_probe::row_address(definitions,i,0x140,definition) ||
            !loadout_attack_probe::copy(definition,&id,sizeof(id))) return false;
        if (!id) continue;
        Item item{}; item.id=id;
        loadout_form_requirements::Summary requirements{};
        if (!loadout_form_requirements::classify(definition,id,requirements,&loadout_attack_probe::copy)) {
            result.incomplete=true; continue;
        }
        item.category=requirements.native_category; item.channels=requirements.channels;
        char raw_name[256]{};
        if (!copy_name(definition,raw_name)) raw_name[0]=0;
        if(std::strncmp(raw_name,"coui://",7)==0) std::memcpy(item.icon,raw_name,sizeof item.icon);
        short_name(raw_name,id,item.name);
        // Names confirmed by the human's captured native weapon selections;
        // the icon filename can name an attack variant instead of its weapon.
        if (id==698021297u) std::snprintf(item.name,sizeof(item.name),"Strike");
        else if (id==1737076817u) std::snprintf(item.name,sizeof(item.name),"Cleave");
        else if (id==3413371114u) std::snprintf(item.name,sizeof(item.name),"Glaive");
        if (node_count) {
            loadout_lookup_probe::Lookup found{};
            if (get_node(&found,reinterpret_cast<void*>(tree),id)!=&found || found.found>1) return false;
            if (found.found) {
                loadout_current_node::Facts node{};
                if (!loadout_current_node::inspect(graph,tree,found.pointer,id,node,&loadout_attack_probe::copy)) return false;
                item.known=true; item.raw_flags=node.raw_flags;
                // Native group-check failure sets0x200 and demotes to stage4.
                // Mixed/transitional base states cannot grant configuration.
                item.acquired=!(node.raw_flags&0x200) &&
                    ((node.raw_low_six_bits==0x20 && node.native_case2_selected) ||
                     (node.raw_low_six_bits==0x08 && node.native_case3_selectable));
                item.selected=node.native_case2_selected && result.loadout[item.category]==id;
            }
        }
        for (std::size_t j=0;j<result.count;++j) if (result.items[j].id==id) return false;
        result.items[result.count++]=item;
    }
    loadout_catalogue_join::Facts final_join{};
    if (!loadout_catalogue_join::inspect(graph,mud,final_join,&loadout_attack_probe::copy) ||
        final_join.revision!=joined.revision || final_join.tree_key_address!=joined.tree_key_address) return false;
    result.valid=true; out=result; return true;
}
inline loadout_control::Validation validate(void* context,loadout_control::Channel channel,std::uint32_t id) {
    const auto& source=*static_cast<const Catalogue*>(context);
    if (!source.valid || !id || static_cast<unsigned>(channel)>=2) return {};
    for (std::size_t i=0;i<source.count;++i) {
        const auto& item=source.items[i]; if (item.id!=id) continue;
        if (!(item.channels&(1u<<static_cast<unsigned>(channel)))) return {loadout_control::Status::WrongChannel,false};
        if (!item.known) return {loadout_control::Status::Unknown,false};
        return {item.acquired ? loadout_control::Status::Available : loadout_control::Status::Unavailable,item.selected};
    }
    return {source.incomplete ? loadout_control::Status::Unknown : loadout_control::Status::Stale,false};
}
inline bool same_source(const Catalogue& a,const Catalogue& b) {
    return a.valid && b.valid && a.world==b.world && a.actor==b.actor && a.graph_key==b.graph_key &&
        a.cfg_key==b.cfg_key && a.definitions_key==b.definitions_key &&
        a.ui_stacks_key==b.ui_stacks_key && a.revision==b.revision;
}
inline void configure_current(Catalogue& source) {
    if (!active() || !source.valid) return;
    if (!TryAcquireSRWLockExclusive(&state_lock)) { CRMOD_DIAGNOSTIC(++dropped); return; }
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&state_lock); } } unlock;
    if (!active()) return;
    if(!slots_restored){if(!control.restore_assignments(mod_bindings::snapshot().loadout))return;slots_restored=true;}
    if (!same_source(source,published.catalogue)) { if (catalogue_token==UINT64_MAX) return; ++catalogue_token; }
    loadout_control::Adapter adapter{};
    adapter.catalogue_epoch=catalogue_token; adapter.owner_scope_current=true;
    adapter.context=&source; adapter.validate=&validate;
    if (adapter_factory) adapter=adapter_factory(source,catalogue_token);
    auto& mailbox=native_ui::sdk::mailbox;
    if (pending_receipt.ticket) {
        if (!mailbox.complete(pending_receipt,pending_result)) return;
        pending_receipt={};
    }
    for (unsigned i=0;i<4 && active();++i) {
        native_ui::ClaimedCommand command{};
        if (!mailbox.claim(command)) break;
        const auto result=control.on_menu_event(adapter,command.event);
        if (!mailbox.complete(command.receipt,result)) {
            pending_receipt=command.receipt; pending_result=result; break;
        }
    }
    loadout_key_requests::consume(control,adapter,
        {source.world,source.actor,source.graph_key,catalogue_token,native_ui::sdk::generation.load()});
    (void)mod_bindings::set_loadout(control.assignments()); // Worker persists IDs; no file I/O in the native callback.
    std::array<loadout_control::CatalogueItem,64> options{};
    std::size_t count=0; bool incomplete=source.incomplete;
    for (std::size_t i=0;i<source.count;++i) {
        const auto& item=source.items[i];
        for (unsigned channel=0;channel<2;++channel) {
            if (!(item.channels&(1u<<channel))) continue;
            if (count==options.size()) { incomplete=true; continue; }
            options[count++]={static_cast<loadout_control::Channel>(channel),item.id};
        }
    }
    published.catalogue=source; published.menu=control.menu(adapter,options.data(),count);
    published.menu.catalogue_incomplete|=incomplete;
    published.view_generation=native_ui::sdk::generation.load();
}
inline bool copy_published(std::uint64_t generation,Published& out) {
    out={}; if (!active() || !generation || !TryAcquireSRWLockShared(&state_lock)) return false;
    const bool accepted=active() && published.view_generation==generation && published.catalogue.valid;
    if (accepted) out=published;
    ReleaseSRWLockShared(&state_lock); return accepted;
}
inline void invalidate_published() {
    observed_player_actor.store(0);
    if (!active() || !TryAcquireSRWLockExclusive(&state_lock)) return;
    published.catalogue.valid=false;
    published.menu.configuration_bound=false;
    published.menu.adapter_bound=false;
    for (auto& channel:published.menu.channels)
        for (auto& slot:channel.slots) { slot.enabled=false; slot.validation={}; }
    for (auto& option:published.menu.options) { option.enabled=false; option.validation={}; }
    ReleaseSRWLockExclusive(&state_lock);
}
inline void body(void* columns,void* state,void* stacks,void* facts,void* graph,void* lists) {
    const DWORD incoming=GetLastError(); SetLastError(incoming);
    reinterpret_cast<Body>(original_body)(columns,state,stacks,facts,graph,lists);
    const DWORD forwarded=GetLastError();
    if (active()) {
        const auto* loan=current_loan;
        if (loan) loan->saw_body=true;
        Catalogue source{};
        if (loan && capture(loan->world,reinterpret_cast<std::uintptr_t>(columns),
                reinterpret_cast<std::uintptr_t>(graph),source)) {
            source.ui_stacks_key=reinterpret_cast<std::uintptr_t>(stacks);
            if (!source.ui_stacks_key) {
                CRMOD_DIAGNOSTIC(++invalid); invalidate_published();
                SetLastError(forwarded); return;
            }
            observed_player_actor.store(source.actor);
            CRMOD_DIAGNOSTIC(++captured); configure_current(source);
        } else { CRMOD_DIAGNOSTIC(++invalid); invalidate_published(); }
    }
    SetLastError(forwarded);
}
struct RestoreLoan { const Loan* previous; ~RestoreLoan() { current_loan=previous; } };
inline void dispatcher_a(void* first,void* middle,void* system) {
    const DWORD incoming=GetLastError(); DWORD forwarded=0;
    { Loan loan{}; loadout_attack_probe::copy(reinterpret_cast<std::uintptr_t>(first),&loan.world,sizeof(loan.world));
      RestoreLoan restore{current_loan}; current_loan=&loan; SetLastError(incoming);
      reinterpret_cast<DispatcherA>(original_a)(first,middle,system); forwarded=GetLastError();
      if (active() && !loan.saw_body) invalidate_published(); }
    SetLastError(forwarded);
}
inline void dispatcher_b(void* first,std::uint64_t selector,void* system) {
    const DWORD incoming=GetLastError(); DWORD forwarded=0;
    { Loan loan{}; loadout_attack_probe::copy(reinterpret_cast<std::uintptr_t>(first),&loan.world,sizeof(loan.world));
      RestoreLoan restore{current_loan}; current_loan=&loan; SetLastError(incoming);
      reinterpret_cast<DispatcherB>(original_b)(first,selector,system); forwarded=GetLastError();
      if (active() && !loan.saw_body) invalidate_published(); }
    SetLastError(forwarded);
}
inline bool arm(std::uintptr_t base,bool all_three_loaded_contracts_verified,bool exact_executable_verified,
                bool module_and_trampolines_pinned,const std::atomic<bool>& stopping) {
    if (!base || !all_three_loaded_contracts_verified || !exact_executable_verified ||
        !module_and_trampolines_pinned || !original_a || !original_b || !original_body ||
        enabled.load() || stopping.load()) return false;
    exe_base=base; if (!node_pin_matches()) { exe_base=0; return false; }
    shared_stopping=&stopping; enabled.store(true);
    if (stopping.load()) enabled.store(false);
    return enabled.load();
}
inline void stop() { enabled.store(false); observed_player_actor.store(0); }
} // namespace loadout_player_catalogue
#endif
