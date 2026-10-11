#pragma once
#include "native_ui_echo.h"
#include "loadout_player_catalogue.h"
#ifdef _WIN32
namespace native_ui_transport {
inline std::string bindings_json(std::uint64_t generation,bool accepted,bool consume_presses=false);
struct Entry {
    bool used=false, completed=false;
    std::uint64_t client=0, epoch=0;
    native_ui::Receipt receipt{};
    loadout_control::MenuEvent event{};
    loadout_control::Result result=loadout_control::Result::Unbound;
};
inline std::array<Entry,32> entries{};
inline std::uint64_t current_generation=0,last_client=0;
inline std::atomic<bool> enabled{false};
inline std::atomic<bool> opening_verified{false},opening_allowed{false},close_requested{false};
inline std::atomic<std::uint64_t> admissions{0},completions{0},failures{0};
inline std::atomic<bool> hello_seen{false},model_seen{false};
struct ImageProbe { uint32_t ms=0,channel=0,status=0,width=0,height=0;char uri[256]{}; };
inline SRWLOCK image_probe_lock=SRWLOCK_INIT;
inline std::array<ImageProbe,64> image_probes{};
inline unsigned image_probe_count=0;
inline std::atomic<unsigned> image_probe_total{0};
struct SurfaceProbe {uint32_t ms=0,stage=0,context=0,cells=0;};
inline std::array<SurfaceProbe,32> surface_probes{};inline unsigned surface_count=0,surface_total=0;
struct NavigationProbe {uint32_t ms=0,kind=0,selected=0,edge=0;bool active=false,owned=false;};
inline std::array<NavigationProbe,32> navigation_probes{};inline unsigned navigation_count=0,navigation_total=0;
using MenuPredicate=std::uint8_t (*)(void*);
inline MenuPredicate original_menu_predicate=nullptr;
inline void observe_current_menu(std::uint64_t stacks) {
    const DWORD error=GetLastError();
    opening_verified.store(true);
    opening_allowed.store(stacks && original_menu_predicate && original_menu_predicate(reinterpret_cast<void*>(stacks))!=0);
    SetLastError(error);
}
inline std::uint8_t menu_predicate(void* stacks) {
    const auto result=original_menu_predicate(stacks);
    const DWORD forwarded=GetLastError();
    // Observe the same native predicate the Player equipment-catalogue body uses.
    if (enabled.load() && loadout_player_catalogue::current_loan) {
        opening_verified.store(true);
        opening_allowed.store(result!=0);
    }
    SetLastError(forwarded);
    return result;
}
inline bool live(std::uint64_t generation) {
    return enabled.load() && native_ui_echo::live(generation);
}
inline std::string decimal(std::uint64_t n) {
    return std::to_string(static_cast<unsigned long long>(n));
}
inline std::string quoted(std::string_view text) {
    std::string out="\"";
    constexpr char hex[]="0123456789abcdef";
    for (unsigned char ch:text) {
        if (ch=='"' || ch=='\\') { out+='\\'; out+=static_cast<char>(ch); }
        else if (ch<0x20) { out+="\\u00"; out+=hex[ch>>4]; out+=hex[ch&15]; }
        else out+=static_cast<char>(ch);
    }
    out+='"'; return out;
}
inline const char* boolean(bool value) { return value ? "true" : "false"; }
inline std::string u64(std::uint64_t n) { return quoted(decimal(n)); }
inline bool read_decimal(std::string_view text,std::size_t& p,std::uint64_t& out) {
    out=0; const auto begin=p;
    while (p<text.size() && text[p]>='0' && text[p]<='9') {
        const unsigned digit=static_cast<unsigned>(text[p]-'0');
        if (out>(UINT64_MAX-digit)/10) return false;
        out=out*10+digit; ++p;
    }
    return p>begin && p-begin<=20 && (p-begin==1 || text[begin]!='0');
}
inline bool literal(std::string_view text,std::size_t& p,std::string_view wanted) {
    if (text.substr(p,wanted.size())!=wanted) return false;
    p+=wanted.size(); return true;
}
inline bool envelope(std::string_view text,std::uint64_t& generation,
                     std::uint64_t& client,loadout_control::MenuEvent& command) {
    std::size_t p=0;
    if (!literal(text,p,"{\"view_generation\":\"") || !read_decimal(text,p,generation) ||
        !literal(text,p,"\",\"client_ticket\":\"") || !read_decimal(text,p,client) ||
        !literal(text,p,"\",\"command\":") || !generation || !client ||
        text.size()<=p || text.back()!='}') return false;
    return native_ui::parse_command(text.substr(p,text.size()-p-1),command);
}
template<std::size_t N> inline bool rpc(std::string_view text,std::string_view prefix,
                std::uint64_t (&values)[N],unsigned count) {
    if (count>N) return false;
    std::size_t p=0;
    if (!literal(text,p,prefix)) return false;
    for (unsigned i=0;i<count;++i) {
        if (!read_decimal(text,p,values[i])) return false;
        if (i+1<count && !literal(text,p,":")) return false;
    }
    return p==text.size();
}
inline const char* name(const loadout_player_catalogue::Catalogue& catalogue,unsigned channel,std::uint32_t id) {
    if (!id || channel>=2) return "";
    for (std::size_t i=0;i<catalogue.count;++i) {
        const auto& item=catalogue.items[i];
        if (item.id==id && (item.channels&(1u<<channel))) return item.name;
    }
    return "";
}
inline const char* icon(const loadout_player_catalogue::Catalogue& catalogue,unsigned channel,std::uint32_t id) {
    for(std::size_t i=0;i<catalogue.count;++i) {
        const auto& item=catalogue.items[i];
        if(item.id==id && channel<2 && (item.channels&(1u<<channel))) return item.icon;
    }
    return "";
}
inline std::string validation(loadout_control::Validation value) {
    return "{\"status\":"+decimal(static_cast<unsigned>(value.status))+",\"selected\":"+boolean(value.selected)+"}";
}
inline std::string model_json(std::uint64_t generation) {
    loadout_player_catalogue::Published source{};
    const bool available=loadout_player_catalogue::copy_published(generation,source);
    const auto& model=source.menu;
    std::string out="{\"kind\":\"model\",\"view_generation\":"+u64(generation)+
        ",\"opening_allowed\":"+boolean(available && opening_verified.load() && opening_allowed.load())+
        ",\"model\":{\"world_epoch\":"+u64(model.world_epoch)+
        ",\"adapter_bound\":"+boolean(available && model.adapter_bound)+
        ",\"configuration_bound\":"+boolean(available && model.configuration_bound)+
        ",\"catalogue_incomplete\":"+boolean(model.catalogue_incomplete)+",\"channels\":[";
    for (unsigned c=0;c<2;++c) {
        if (c) out+=',';
        const auto& channel=model.channels[c];
        const auto target=channel.pending ? channel.pending_id : channel.in_flight ? channel.in_flight_id : source.catalogue.loadout[c];
        out+="{\"selection_bound\":"+std::string(boolean(available && channel.selection_bound))+",\"pending\":"+boolean(channel.pending)+",\"in_flight\":"+boolean(channel.in_flight)+
            ",\"active_id\":"+decimal(source.catalogue.loadout[c])+",\"active_name\":"+quoted(name(source.catalogue,c,source.catalogue.loadout[c]))+
            ",\"target_id\":"+decimal(target)+",\"target_name\":"+quoted(name(source.catalogue,c,target))+
            ",\"active_icon\":"+quoted(icon(source.catalogue,c,source.catalogue.loadout[c]))+
            ",\"target_icon\":"+quoted(icon(source.catalogue,c,target))+
            ",\"switch_queued\":"+boolean(channel.pending || channel.in_flight)+
            ",\"pending_id\":"+decimal(channel.pending_id)+",\"in_flight_id\":"+decimal(channel.in_flight_id)+
            ",\"ticket\":"+u64(channel.ticket)+",\"terminal\":"+decimal(static_cast<unsigned>(channel.terminal))+",\"slots\":[";
        for (unsigned s=0;s<loadout_control::kSlotCount;++s) {
            if (s) out+=',';
            const auto& slot=channel.slots[s];
            out+="{\"slot\":{\"assigned\":"+std::string(boolean(slot.slot.assigned))+",\"id\":"+decimal(slot.slot.id)+
                "},\"validation\":"+validation(slot.validation)+",\"enabled\":"+boolean(available && slot.enabled)+
                ",\"name\":"+quoted(name(source.catalogue,c,slot.slot.id))+"}";
        }
        out+="]}";
    }
    out+="],\"option_count\":"+decimal(model.option_count)+",\"options\":[";
    for (std::size_t i=0;i<model.option_count;++i) {
        if (i) out+=',';
        const auto& option=model.options[i];
        const unsigned channel=static_cast<unsigned>(option.item.channel);
        out+="{\"item\":{\"channel\":"+decimal(channel)+",\"id\":"+decimal(option.item.id)+
            "},\"validation\":"+validation(option.validation)+",\"enabled\":"+boolean(available && option.enabled)+
            ",\"name\":"+quoted(name(source.catalogue,channel,option.item.id))+",\"icon\":"+quoted(icon(source.catalogue,channel,option.item.id))+"}";
    }
    out+="],\"bindings\":"+bindings_json(generation,true,true)+"}}"; return out;
}
inline Entry* find(std::uint64_t client) {
    for (auto& entry:entries) if (entry.used && entry.client==client) return &entry;
    return nullptr;
}
inline bool same(const loadout_control::MenuEvent& a,const loadout_control::MenuEvent& b) {
    return a.action==b.action && a.channel==b.channel && a.slot==b.slot && a.id==b.id && a.world_epoch==b.world_epoch;
}
inline std::string admission(std::uint64_t generation,std::uint64_t client,std::uint64_t epoch,
                             std::uint64_t ticket,bool accepted,loadout_control::Result result) {
    return "{\"kind\":\"admission\",\"view_generation\":"+u64(generation)+",\"client_ticket\":"+u64(client)+
        ",\"native_ticket\":"+u64(ticket)+",\"world_epoch\":"+u64(epoch)+",\"accepted\":"+boolean(accepted)+
        ",\"result\":"+decimal(static_cast<unsigned>(result))+"}";
}
inline std::string submit(std::string_view text,std::uint64_t generation) {
    std::uint64_t supplied_generation=0,client=0;
    loadout_control::MenuEvent command{};
    if (!envelope(text,supplied_generation,client,command) || supplied_generation!=generation)
        return "{\"kind\":\"error\",\"reason\":\"invalid_envelope\"}";
    if (auto* existing=find(client)) {
        if (!same(existing->event,command)) return "{\"kind\":\"error\",\"reason\":\"correlation_mismatch\"}";
        return admission(generation,client,command.world_epoch,existing->receipt.ticket,true,loadout_control::Result::Waiting);
    }
    if (client<=last_client) return admission(generation,client,command.world_epoch,0,false,loadout_control::Result::Rejected);
    loadout_player_catalogue::Published source{};
    if (!loadout_player_catalogue::copy_published(generation,source) || !source.menu.configuration_bound ||
        source.menu.world_epoch!=command.world_epoch ||
        (command.action==loadout_control::Action::SelectSlot &&
         !source.menu.channels[static_cast<unsigned>(command.channel)].selection_bound))
        return admission(generation,client,command.world_epoch,0,false,loadout_control::Result::Unbound);
    Entry* available=nullptr;
    for (auto& entry:entries) if (!entry.used) { available=&entry; break; }
    if (!available) return admission(generation,client,command.world_epoch,0,false,loadout_control::Result::Rejected);
    const auto receipt=native_ui::sdk::mailbox.enqueue(generation,command);
    if (!receipt.ticket) return admission(generation,client,command.world_epoch,0,false,loadout_control::Result::Rejected);
    *available={}; available->used=true; available->client=client; available->epoch=command.world_epoch;
    available->receipt=receipt; available->event=command; last_client=client; CRMOD_DIAGNOSTIC(++admissions);
    return admission(generation,client,command.world_epoch,receipt.ticket,true,loadout_control::Result::Waiting);
}
inline std::string reply(const std::uint64_t (&v)[4],std::uint64_t generation) {
    Entry* entry=find(v[1]);
    const bool matches=v[0]==generation && entry && entry->epoch==v[3] && (!v[2] || entry->receipt.ticket==v[2]);
    const auto ticket=matches ? entry->receipt.ticket : 0;
    if (matches && !entry->completed && native_ui::sdk::mailbox.take_reply(entry->receipt,entry->result)) {
        entry->completed=true; CRMOD_DIAGNOSTIC(++completions);
    }
    std::string out="{\"kind\":\"reply\",\"view_generation\":"+u64(generation)+",\"client_ticket\":"+u64(v[1])+
        ",\"native_ticket\":"+u64(ticket)+",\"world_epoch\":"+u64(v[3])+",\"state\":"+
        quoted(!matches ? "unknown" : entry->completed ? "complete" : "pending");
    if (matches && entry->completed) out+=",\"result\":"+decimal(static_cast<unsigned>(entry->result));
    out+='}'; return out;
}
inline std::string acknowledge(const std::uint64_t (&v)[4],std::uint64_t generation) {
    Entry* entry=find(v[1]);
    bool accepted=v[0]==generation && v[1] && v[2] && v[3];
    if (entry) {
        accepted=accepted && entry->completed && entry->epoch==v[3] && entry->receipt.ticket==v[2];
        if (accepted) *entry={};
    }
    return "{\"kind\":\"ack\",\"view_generation\":"+u64(generation)+",\"client_ticket\":"+u64(v[1])+
        ",\"native_ticket\":"+u64(v[2])+",\"world_epoch\":"+u64(v[3])+",\"ack\":"+boolean(accepted)+"}";
}
inline std::string bindings_json(std::uint64_t generation,bool accepted=true,bool consume_presses) {
    static std::atomic<std::uint64_t> sample_sequence{0};
    const auto sample=consume_presses?sample_sequence.fetch_add(1)+1:sample_sequence.load();
    const auto s=mod_bindings::snapshot();
    const auto presses=consume_presses ? mod_bindings::take_pad_presses() : mod_bindings::PadPresses{};
    const auto keys=consume_presses ? mod_bindings::take_key_presses() : mod_bindings::PadPresses{};
    std::string key_events="[";for(unsigned i=0;i<keys.count;++i){if(i)key_events+=",";key_events+=decimal(keys.values[i]);}key_events+="]";
    std::string events="[";
    for (unsigned i=0;i<presses.count;++i) { if(i) events+=','; events+=decimal(presses.values[i]); } events+=']';
    return "{\"kind\":\"bindings\",\"view_generation\":"+u64(generation)+",\"accepted\":"+boolean(accepted)+
        ",\"key_primary\":"+decimal(s.key[0])+",\"key_secondary\":"+decimal(s.key[1])+
        ",\"key_alt_primary\":"+decimal(s.key_alt[0])+",\"key_alt_secondary\":"+decimal(s.key_alt[1])+
        ",\"pad_primary\":"+decimal(s.pad[0])+",\"pad_secondary\":"+decimal(s.pad[1])+
        ",\"loot_multiplier\":"+decimal(s.loot_multiplier)+
        ",\"back_pause_ms\":"+decimal(s.back_pause_ms)+",\"contact_margin_mm\":"+decimal(s.contact_margin_mm)+",\"revision\":"+u64(s.revision)+",\"saved\":"+boolean(mod_bindings::saved_revision.load()==s.revision)+
        ",\"save_error\":"+boolean(mod_bindings::save_error.load())+",\"pad_buttons\":"+decimal(mod_bindings::last_pad.load())+
        ",\"sample_sequence\":"+u64(sample)+",\"pad_presses\":"+events+",\"key_presses\":"+key_events+",\"capture_releasing\":"+boolean(mod_bindings::capture_releasing.load())+",\"pad_dropped\":"+decimal(mod_bindings::pad_dropped.load())+"}";
}
inline void emit(void* context,std::uint64_t generation,const std::string& json) {
    if (!live(generation) || json.size()>65535) { CRMOD_DIAGNOSTIC(++failures); return; }
    const auto base=native_ui::sdk::guard.cohtml_base;
    auto* binder=reinterpret_cast<native_ui_echo::Begin>(base+0x5f15c8)(context);
    if (!binder || !live(generation) || !native_ui_echo::binder_shape(binder)) { CRMOD_DIAGNOSTIC(++failures); return; }
    reinterpret_cast<native_ui_echo::Write>(base+0x5ebf3c)(binder,json.c_str());
    if (!live(generation) || reinterpret_cast<native_ui_echo::Error>(base+0x30669c)(context)) { CRMOD_DIAGNOSTIC(++failures); return; }
    reinterpret_cast<native_ui_echo::End>(base+0x5f1770)(context);
}
inline void invoke(native_ui_invoke_probe::Handler* handler,void* context) {
    const DWORD saved=GetLastError();
    struct Restore { DWORD value; ~Restore() { SetLastError(value); } } restore{saved};
    const auto generation=handler->generation;
    if (!live(generation)) return;
    if (current_generation!=generation) {
        entries={}; current_generation=generation; last_client=0;
        opening_verified.store(false); opening_allowed.store(false); close_requested.store(false);
        mod_bindings::editing.store(false);
        mod_bindings::set_panel_open(false);
        CRMOD_DIAGNOSTIC(native_ui_echo::stage.store(0));
    }
    char owned[513]{}; std::size_t size=0;
    if (!native_ui_echo::read_one(context,generation,owned,size)) { CRMOD_DIAGNOSTIC(++failures); return; }
    const std::string_view input(owned,size);
    if (diagnostics::enabled && (input==native_ui_echo::request || input==native_ui_echo::confirmation)) {
        native_ui_echo::invoke_owned(handler,context,owned,size); return;
    }
    std::string output;
    std::uint64_t values[4]{};
    std::uint64_t binding_values[7]{};
    std::uint64_t navigation_values[6]{};
    if (input=="cr.host.hello.v1") {
        CRMOD_DIAGNOSTIC(hello_seen.store(true));
        output="{\"kind\":\"hello\",\"protocol\":1,\"view_generation\":"+u64(generation)+"}";
    } else if (rpc(input,"cr.host.model.v1:",values,1) && values[0]==generation) {
        CRMOD_DIAGNOSTIC(model_seen.store(true)); output=model_json(generation);
    }
    else if(diagnostics::enabled && rpc(input,"cr.host.surface.v1:",values,4) && values[0]==generation && values[1]<=4 && values[2]<=INT32_MAX && values[3]<=192) {
        if(surface_total<256 && TryAcquireSRWLockExclusive(&image_probe_lock)) {
            if(surface_count<surface_probes.size()){surface_probes[surface_count++]={static_cast<uint32_t>(GetTickCount64()),static_cast<uint32_t>(values[1]),static_cast<uint32_t>(values[2]),static_cast<uint32_t>(values[3])};++surface_total;}
            ReleaseSRWLockExclusive(&image_probe_lock);
        }
        output="{\"kind\":\"surface_probe\",\"accepted\":true}";
    }
    else if(diagnostics::enabled && input.substr(0,17)=="cr.host.image.v1:") {
        std::size_t pos=17;std::uint64_t fields[5]{};bool valid=true;
        for(unsigned i=0;i<5 && valid;++i) valid=read_decimal(input,pos,fields[i]) && literal(input,pos,":");
        const auto uri=input.substr(pos);
        valid=valid && fields[0]==generation && fields[1]<4 && fields[2]<6 && fields[3]<=8192 && fields[4]<=8192 && uri.size()<256 &&
            (uri.empty() || uri.substr(0,7)=="coui://" || uri.substr(0,7)=="live://");
        if(valid && image_probe_total.load()<512 && TryAcquireSRWLockExclusive(&image_probe_lock)) {
            if(image_probe_count<image_probes.size()) {
                auto& p=image_probes[image_probe_count++];p={};p.ms=static_cast<uint32_t>(GetTickCount64());p.channel=static_cast<uint32_t>(fields[1]);
                p.status=static_cast<uint32_t>(fields[2]);p.width=static_cast<uint32_t>(fields[3]);p.height=static_cast<uint32_t>(fields[4]);
                std::memcpy(p.uri,uri.data(),uri.size());++image_probe_total;
            }
            ReleaseSRWLockExclusive(&image_probe_lock);
        }
        output="{\"kind\":\"image_probe\",\"accepted\":"+std::string(boolean(valid))+"}";
    }
    else if (rpc(input,"cr.host.bindings.v1:",values,1) && values[0]==generation) {
        mod_bindings::editing.store(true); output=bindings_json(generation);
    }
    else if ((rpc(input,"cr.host.bind.v1:",binding_values,7) || rpc(input,"cr.host.bind.v1:",binding_values,5)) && binding_values[0]==generation) {
        bool accepted=false;
        if (binding_values[1]<=254 && binding_values[2]<=254 && binding_values[3]<=0x3ffff && binding_values[4]<=0x3ffff && binding_values[5]<=254 && binding_values[6]<=254) {
            mod_bindings::Settings next=mod_bindings::snapshot();
            next.key={static_cast<unsigned>(binding_values[1]),static_cast<unsigned>(binding_values[2])};
            next.pad={static_cast<unsigned>(binding_values[3]),static_cast<unsigned>(binding_values[4])};
            next.key_alt={static_cast<unsigned>(binding_values[5]),static_cast<unsigned>(binding_values[6])};
            accepted=mod_bindings::apply(next);
        }
        output=bindings_json(generation,accepted);
    }
    else if(rpc(input,"cr.host.loot.v1:",values,2) && values[0]==generation && values[1]>=1 && values[1]<=10) {
        auto next=mod_bindings::snapshot();next.loot_multiplier=static_cast<unsigned>(values[1]);output=bindings_json(generation,mod_bindings::apply(next));
    }
    else if(rpc(input,"cr.host.contact.v1:",values,2) && values[0]==generation) {
        bool accepted=false;
        if(values[1]==20 || values[1]==35 || values[1]==50) {
            auto next=mod_bindings::snapshot();next.contact_margin_mm=static_cast<unsigned>(values[1]);accepted=mod_bindings::apply(next);
        }
        output=bindings_json(generation,accepted);
    }
    else if(rpc(input,"cr.host.pause.v1:",values,2) && values[0]==generation) {
        bool accepted=false;
        if(values[1]==10 || values[1]==20 || values[1]==30 || values[1]==40 || values[1]==50 || values[1]==500) {
            auto next=mod_bindings::snapshot();next.back_pause_ms=static_cast<unsigned>(values[1]);accepted=mod_bindings::apply(next);
        }
        output=bindings_json(generation,accepted);
    }
    else if ((rpc(input,"cr.host.navigation.v1:",values,3) || rpc(input,"cr.host.navigation.v1:",values,2)) && values[0]==generation && values[1]<=1 && values[2]<=2) {mod_bindings::menu_kind.store(static_cast<unsigned>(values[2]));mod_bindings::menu_owned.store(values[1]!=0 && mod_bindings::panel_open.load());output=bindings_json(generation);}
    else if(diagnostics::enabled && rpc(input,"cr.host.navprobe.v1:",navigation_values,6) && navigation_values[0]==generation && navigation_values[1]<=2 && navigation_values[2]<=INT32_MAX && navigation_values[3]<=0x3ffff && navigation_values[4]<=1 && navigation_values[5]<=1){
        if(navigation_total<256 && TryAcquireSRWLockExclusive(&image_probe_lock)){
            if(navigation_count<navigation_probes.size()){
                navigation_probes[navigation_count++]={static_cast<uint32_t>(GetTickCount64()),static_cast<uint32_t>(navigation_values[1]),static_cast<uint32_t>(navigation_values[2]),static_cast<uint32_t>(navigation_values[3]),navigation_values[4]!=0,navigation_values[5]!=0};++navigation_total;
            }
            ReleaseSRWLockExclusive(&image_probe_lock);
        }
        output="{\"kind\":\"navigation_probe\",\"accepted\":true}";
    }
    else if (rpc(input,"cr.host.capture.v1:",values,2) && values[0]==generation && values[1]<=1) {
        mod_bindings::set_capture(values[1]!=0); output=bindings_json(generation);
    }
    else if (rpc(input,"cr.host.controls.v1:",values,2) && values[0]==generation && values[1]<=1) {
        mod_bindings::editing.store(values[1]!=0); output=bindings_json(generation);
    }
    else if (rpc(input,"cr.host.panel.v1:",values,2) && values[0]==generation && values[1]<=1) {
        mod_bindings::set_panel_open(values[1]!=0); output=bindings_json(generation);
    }
    else if(rpc(input,"cr.host.menu.v1:",values,2) && values[0]==generation && values[1]<=1){mod_bindings::menu_active.store(values[1]!=0);output=bindings_json(generation);}
    else if (rpc(input,"cr.host.reply.v1:",values,4) && values[0]==generation && values[1] && values[3]) output=reply(values,generation);
    else if (rpc(input,"cr.host.ack.v1:",values,4)) output=acknowledge(values,generation);
    else if (rpc(input,"cr.host.close.v1:",values,1) && values[0]==generation) {
        mod_bindings::editing.store(false);
        mod_bindings::set_panel_open(false);
        close_requested.store(true); opening_allowed.store(false);
        output="{\"kind\":\"close\",\"view_generation\":"+u64(generation)+",\"requested\":true}";
    } else if (!input.empty() && input.front()=='{') output=submit(input,generation);
    else output="{\"kind\":\"error\",\"reason\":\"invalid_request\"}";
    emit(context,generation,output);
}
inline bool prepare() {
    if (enabled.load() || native_ui_invoke_probe::attempted.load() ||
        !native_ui_echo::enabled.load() || !native_ui_invoke_probe::recording.load()) return false;
    native_ui_invoke_probe::after_capture=&invoke;
    enabled.store(true); return true;
}
inline void set_native_opening(bool allowed,bool contract_verified) {
    opening_verified.store(contract_verified); opening_allowed.store(allowed && contract_verified);
}
inline void stop() { enabled.store(false); opening_allowed.store(false); mod_bindings::editing.store(false); mod_bindings::menu_active.store(false); mod_bindings::set_panel_open(false); }
inline void flush(FILE* log) {
    if constexpr(!diagnostics::enabled)return;
    if (!log) return;
    if(TryAcquireSRWLockExclusive(&image_probe_lock)) {
        for(unsigned i=0;i<image_probe_count;++i) {
            const auto& p=image_probes[i];const auto uri=quoted(p.uri);
            std::fprintf(log,"{\"event\":\"weapon_image_probe\",\"ms\":%u,\"channel\":%u,\"origin\":\"%s\",\"status\":%u,\"embedded_icon_loaded\":%s,\"width\":%u,\"height\":%u,\"uri\":%s}\n",p.ms,p.channel,p.channel==3?"artwork_cache":p.channel==2?"native_dom":"custom_hud",p.status,p.status==4 && p.channel<2?"true":"false",p.width,p.height,uri.c_str());
        }
        for(unsigned i=0;i<surface_count;++i){const auto& p=surface_probes[i];std::fprintf(log,"{\"event\":\"native_loadout_surface\",\"ms\":%u,\"stage\":%u,\"context\":%u,\"cells\":%u}\n",p.ms,p.stage,p.context,p.cells);}
        for(unsigned i=0;i<navigation_count;++i){const auto& p=navigation_probes[i];std::fprintf(log,"{\"event\":\"controller_menu_focus\",\"ms\":%u,\"context\":\"%s\",\"selected_cell\":%u,\"pressed\":%u,\"context_active\":%s,\"mod_cell_owned\":%s}\n",p.ms,p.kind==1?"controls":p.kind==2?"gameplay":"loadout",p.selected,p.edge,boolean(p.active),boolean(p.owned));}
        navigation_count=0;
        surface_count=0;
        image_probe_count=0;ReleaseSRWLockExclusive(&image_probe_lock);
    }
    static std::uint64_t previous_a=UINT64_MAX,previous_c=UINT64_MAX,previous_f=UINT64_MAX;
    static bool previous_hello=false,previous_model=false;
    const auto a=admissions.load(),c=completions.load(),f=failures.load();
    const bool hello=hello_seen.load(),model=model_seen.load();
    if (a==previous_a && c==previous_c && f==previous_f && hello==previous_hello && model==previous_model) return;
    previous_a=a; previous_c=c; previous_f=f;
    previous_hello=hello; previous_model=model;
    std::fprintf(log,"{\"event\":\"native_ui_transport\",\"admissions\":%llu,\"completions\":%llu,\"failures\":%llu,\"hello_seen\":%s,\"model_seen\":%s,\"opening_bound\":%s,\"close_requested\":%s}\n",
        static_cast<unsigned long long>(a),static_cast<unsigned long long>(c),static_cast<unsigned long long>(f),
        boolean(hello),boolean(model),
        boolean(opening_verified.load()),boolean(close_requested.load()));
}
}
#endif
