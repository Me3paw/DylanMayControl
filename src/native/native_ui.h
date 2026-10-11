// Scalar UI transport. Native script/handler/lifecycle bindings are still required.
#pragma once
#include "diagnostics.h"
#include "loadout_control.h"
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace native_ui {
inline std::string make_owned_bootstrap(std::string_view panel_js, std::string_view css, std::string_view icons_js={}) {
    if (panel_js.empty() || css.empty() || panel_js.find('\0') != std::string_view::npos ||
        css.find('\0') != std::string_view::npos || panel_js.size() + css.size() > 128 * 1024 ||
        icons_js.find('\0') != std::string_view::npos || icons_js.size()>384*1024)
        return {};
    std::string quoted = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < css.size(); ++i) {
        const auto ch = static_cast<unsigned char>(css[i]);
        if (ch == '"' || ch == '\\') { quoted += '\\'; quoted += static_cast<char>(ch); }
        else if (ch < 0x20) {
            quoted += "\\u00"; quoted += hex[ch >> 4]; quoted += hex[ch & 15];
        } else if (i + 2 < css.size() && ch == 0xe2 &&
                   static_cast<unsigned char>(css[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(css[i + 2]) == 0xa8 ||
                    static_cast<unsigned char>(css[i + 2]) == 0xa9)) {
            quoted += static_cast<unsigned char>(css[i + 2]) == 0xa8 ? "\\u2028" : "\\u2029";
            i += 2;
        } else quoted += static_cast<char>(ch);
    }
    quoted += '"';
    std::string script(icons_js);script+='\n';script+=panel_js;
    script += diagnostics::enabled ? "\nwindow.__crmod_diagnostics=true;" : "\nwindow.__crmod_diagnostics=false;";
    script += "\n;(function(root,css){'use strict';";
    script += R"JS(
function mount() {
    if (!root.document || !document.body || !root.CRLoadoutPanel) return;
    if (root.__cr_loadout_v1) { root.__cr_loadout_v1.reset(); return; }
    var panel = null, model = null, allowed = false, nativeHost = null;
    var style = document.createElement('style');
    style.type = 'text/css'; style.textContent = css;
    (document.head || document.body).appendChild(style);
    var escapeHandler=null;
    var anchor=null,stock=null,stockDisplay='',stockHeight='',navigation=null,mode='',menuSent=null;
    var picker=null,pickerListening=false;
    // Preserve Cohtml callback identity on off(); gate native Options actions
    // only while our own field captures input. No game binding is changed.
    var gatedEngine=null,captureReleaseUntil=0;
    function gateNativeInput(){if(root.engine && root.engine!==gatedEngine && typeof root.engine.on==='function' && typeof root.engine.off==='function'){
        gatedEngine=root.engine;
        var originalOn=root.engine.on,originalOff=root.engine.off,inputHandlers=[];
        var menuEvents={OnSelect:1,OnCancel:1,OnBack:1,OnPrev:1,OnNext:1,OnNavigateLeft:1,OnNavigateRight:1};
        function captureEvent(name){
            if(!nativeHost || !panel || (mode!=='bindings'&&mode!=='contact'))return false;
            var region=document.getElementById('cr-loadout-panel');
            if(!region || !document.body.contains(region))return false;
            var context=root[mode==='bindings'?'ui_menu_navigation_controls_id':'ui_menu_navigation_gameplay_id'];
            if(!context||!root.stack||!root.stack.globalState(context.value))return false;
            var capturing=region.getAttribute('data-cr-capturing')==='true';
            if(!capturing)return Date.now()<captureReleaseUntil || !!(model&&model.bindings&&model.bindings.capture_releasing===true);
            if((name==='OnCancel'||name==='OnBack')&&typeof panel.cancelCapture==='function'){panel.cancelCapture();captureReleaseUntil=Date.now()+150;}
            return true;
        }
        root.engine.on=function(name,callback,context){
            if(!menuEvents[name] || typeof callback!=='function')return originalOn.apply(this,arguments);
            var found=null;for(var i=0;i<inputHandlers.length;i++)if(inputHandlers[i].name===name&&inputHandlers[i].callback===callback&&inputHandlers[i].context===context){found=inputHandlers[i];break;}
            if(!found){found={name:name,callback:callback,context:context,wrapped:function(){if(!captureEvent(name))return callback.apply(this,arguments);}};inputHandlers.push(found);}
            return originalOn.call(this,name,found.wrapped,context);
        };
        root.engine.off=function(name,callback,context){
            for(var i=0;i<inputHandlers.length;i++)if(inputHandlers[i].name===name&&inputHandlers[i].callback===callback&&inputHandlers[i].context===context){var found=inputHandlers.splice(i,1)[0];return originalOff.call(this,name,found.wrapped,context);}
            return originalOff.apply(this,arguments);
        };
    }}gateNativeInput();
    function stopPicker(){if(picker && picker.selectListening && root.engine && root.engine.off)root.engine.off('OnSelect',pickerSelect);picker=null;}
    function value(name){var m=root[name];return m && m.value;}
    function browserVisible(){return value('ui_loadout_melee_in_browser_is_here')===true;}
    function weaponArt(uri){return typeof uri==='string'?uri.split('/').pop().replace(/_(small|medium|large)\.png$/,'').toLowerCase():'';}
    function openWeaponPicker(channel,slot){
        if(!model || !nativeHost || model.world_epoch==='0' || channel<0 || channel>1 || slot<0 || slot>2 || !root.engine || typeof root.engine.trigger!=='function')return false;
        var destination=value('hud_loadout_melee_'+(channel===0?0:2)+'_id');
        if(typeof destination!=='number' || destination<0 || Math.floor(destination)!==destination)return false;
        stopPicker();picker={channel:channel,slot:slot,destination:destination,epoch:model.world_epoch,entered:false,chosen:null,accepted:false};
        root.engine.trigger('loadout_slot_click',destination);return true;
    }
    function browserChoice(destination,cell){
        var request=picker;
        if(!request || request.chosen!==null || !browserVisible() || destination!==request.destination || typeof cell!=='number' || cell<0 || Math.floor(cell)!==cell || !model || model.world_epoch!==request.epoch)return;
        var id=null;
        for(var i=0;i<9;i++){
            var prefix='hud_loadout_browser_melee_'+i+'_';if(value(prefix+'id')!==cell)continue;
            if(value(prefix+'unused')===true)return;
            if(value(prefix+'is_remove')===true){id=0;break;}
            var ability=value(prefix+'ability_id'),art=weaponArt(value(prefix+'icon_url')||value(prefix+'icon'));
            for(var j=0;j<model.option_count;j++){
                var option=model.options[j];
                if(option.item.channel===request.channel && option.enabled===true && option.validation.status===1 &&
                    (option.item.id===ability || (art && weaponArt(option.icon)===art))){if(id!==null)return;id=option.item.id;}
            }
            break;
        }
        if(id===null)return;
        request.chosen=id;
        root.setTimeout(function(){
            if(picker!==request || !nativeHost || !model || model.world_epoch!==request.epoch)return;
            var command=JSON.stringify({action:id?0:1,channel:request.channel,slot:request.slot,id:id,world_epoch:request.epoch});
            if(!nativeHost.send(command,function(reply){
                if(picker!==request || !reply || reply.world_epoch!==request.epoch)return;
                if(reply.result===2 || reply.result===4)request.accepted=true;else stopPicker();
            }))stopPicker();
        },0);
    }
    function pickerSelect(){
        if(!picker || !browserVisible() || !root.navigation || !root.stack)return;
        var context=value('ui_menu_navigation_loadout_browser_melee_id');
        if(!context || !root.stack.globalState(context))return;
        var selected=root.navigation.current(context,0);if(selected && typeof selected==='object')selected=selected.value;
        browserChoice(value('hud_loadout_browser_destination_slot'),selected);
    }
    function pickerClick(event){
        if(!picker || !browserVisible())return;
        var node=event.target;while(node && node!==document.body && (!node.classList || !node.classList.contains('melee-loadout-browser__cell')))node=node.parentNode;
        if(!node || node===document.body)return;
        var nodes=document.querySelectorAll('.melee-loadout-browser__cell');
        for(var i=0;i<nodes.length && i<9;i++)if(nodes[i]===node){browserChoice(value('hud_loadout_browser_destination_slot'),value('hud_loadout_browser_melee_'+i+'_id'));break;}
    }
    function refreshPicker(){
        if(!picker)return;
        if(!model || model.world_epoch!==picker.epoch || value('ui_stacks_menu_options_active')===true){stopPicker();return;}
        var visible=browserVisible();if(visible){picker.entered=true;if(!picker.selectListening && root.engine && root.engine.on){root.engine.on('OnSelect',pickerSelect);picker.selectListening=true;}}
        if(picker.chosen!==null && picker.accepted){
            var slot=model.channels[picker.channel].slots[picker.slot].slot;
            if((picker.chosen===0 && !slot.assigned) || (slot.assigned && slot.id===picker.chosen)){
                stopPicker();
                if(visible && root.stack && typeof root.stack.back==='function')root.stack.back(value('ui_menu_navigation_loadout_stack'));
            }
        }else if(picker.entered && !visible && picker.chosen===null)stopPicker();
    }
    var hud = document.createElement('div');
    hud.id = 'cr-weapon-hud'; hud.className = 'cr-weapon-hud';
    hud.style.display = 'none'; hud.setAttribute('aria-live','polite');
    document.body.appendChild(hud);
    var hudSlots=[],nativeImageReports=0;
    function imageSource(uri) { return root.CRWeaponIcons && root.CRWeaponIcons[uri] || uri; }
    for(var h=0;h<2;h+=1) {
        var slot=document.createElement('div'),image=document.createElement('img'),label=document.createElement('span'),state=document.createElement('span');
        slot.className='cr-weapon-slot';image.className='cr-weapon-icon';label.className='cr-weapon-channel';state.className='cr-weapon-state';
        label.textContent=h===0?'Primary':'Secondary';
        slot.appendChild(image);slot.appendChild(label);slot.appendChild(state);hud.appendChild(slot);
        image.style.cssText='display:block;position:relative;width:58px;height:58px;max-width:none;max-height:none;opacity:1;transform:none;margin:13px auto 0;';
        var view={slot:slot,image:image,state:state,uri:null,name:'',queued:false,failed:false,channel:h,wasQueued:false,activeId:null,token:0};
        (function(view) {
            function report(status) {
                if(root.__crmod_diagnostics===false)return;
                if(nativeHost && typeof nativeHost.imageProbe==='function') nativeHost.imageProbe(view.channel,status===1 && imageSource(view.uri)!==view.uri?4:status,
                    Math.max(0,Math.min(8192,Math.round(view.image.offsetWidth||0))),Math.max(0,Math.min(8192,Math.round(view.image.offsetHeight||0))),view.uri||'');
            }
            view.report=report;
            image.onload=function() {
                if(view.image.getAttribute('src')!==imageSource(view.uri)) return;
                view.failed=false;view.image.style.visibility="visible";view.state.textContent=view.queued?"Queued":"";
                report(1);
            };
            image.onerror=function() {view.failed=true;view.image.style.visibility='hidden';view.state.textContent=view.name+(view.queued?' (queued)':'');report(2);};
        })(view);
        hudSlots.push(view);
    }
    refreshOpening();
    function inspectNativeImages() {
        if(root.__crmod_diagnostics===false)return;
        if(!allowed || !nativeHost || typeof nativeHost.imageProbe!=='function' || nativeImageReports>=64) return;
        var images=document.querySelectorAll('img');
        for(var i=0;i<images.length && i<512 && nativeImageReports<64;i+=1) {
            var item=images[i];if(hud.contains(item)) continue;
            if(!item.offsetWidth || !item.offsetHeight) continue;
            var uri=item.getAttribute('src')||'';
            if(uri.indexOf('coui://')!==0 && uri.indexOf('live://')!==0) continue;
            nativeHost.imageProbe(2,3,Math.max(0,Math.min(8192,Math.round(item.offsetWidth||0))),Math.max(0,Math.min(8192,Math.round(item.offsetHeight||0))),uri);
            ++nativeImageReports;
        }
    }
    function retryAfterEquip(view,uri,token,epoch,attempt) {
        root.setTimeout(function() {
            if(!nativeHost || !model || model.world_epoch!==epoch || view.token!==token || view.uri!==uri || view.queued || !view.failed) return;
            view.failed=false;view.image.removeAttribute('src');view.image.setAttribute('src',imageSource(uri));view.report(0);
            if(attempt<2) retryAfterEquip(view,uri,token,epoch,attempt+1);
        },250);
    }
    function refreshHud() {
        var ready = nativeHost && model && model.world_epoch !== '0' &&
            model.adapter_bound === true && model.channels && model.channels.length === 2;
        var optionsOpen=root.ui_stacks_menu_options_active && root.ui_stacks_menu_options_active.value===true;
        var systemOpen=root.ui_stacks_game_states_system_menu_current && root.ui_stacks_game_states_system_menu_current.value===true;
        hud.style.display = ready && !allowed && !optionsOpen && !systemOpen ? '' : 'none';
        if (!ready) return;
        for (var i=0;i<2;i+=1) {
            var channel=model.channels[i],queued=channel.switch_queued===true,view=hudSlots[i];
            var name=(queued?channel.target_name:channel.active_name)||'Unassigned';
            var icon=(queued?channel.target_icon:channel.active_icon)||'';
            view.slot.className='cr-weapon-slot'+(queued?' is-queued':'');
            view.slot.setAttribute('aria-label',(i===0?'Primary: ':'Secondary: ')+name+(queued?' (queued)':''));view.slot.title=name;
            view.image.alt=name;
            var handoff=!queued && (view.wasQueued || view.activeId!==channel.active_id);
            view.wasQueued=queued;view.activeId=channel.active_id;
            view.name=name;view.queued=queued;
            if(view.uri!==icon) {
                ++view.token;view.uri=icon;view.failed=false;
                if(icon) view.image.setAttribute('src',imageSource(icon));else view.image.removeAttribute('src');
                view.report(icon?0:3);
            }
            view.image.style.visibility=icon && !view.failed?'visible':'hidden';
            view.state.textContent=view.failed?name+(queued?' (queued)':''):queued?'Queued':icon?'':name;
            if(handoff && icon) retryAfterEquip(view,icon,view.token,model.world_epoch,1);
        }
    }
    function close() {
        if(escapeHandler){document.removeEventListener('keydown',escapeHandler,true);escapeHandler=null;}
        if(navigation){navigation.close();navigation=null;}
        if(panel){var old=panel;panel=null;old.close();}
        if(stock){stock.style.display=stockDisplay;stock=null;}
        if(anchor) anchor.style.height=stockHeight;
        anchor=null;mode='';
    }
    function findAnchor(){
        if(root.ui_loadout_melee_in_loadout_visible && root.ui_loadout_melee_in_loadout_visible.value!==true) return null;
        var nodes=document.querySelectorAll('.melee-loadout');
        for(var i=0;i<nodes.length;i++) if(nodes[i].offsetWidth && nodes[i].offsetHeight && !nodes[i].classList.contains('hidden')) return nodes[i];
        return null;
    }
    function findOptionsAnchor(){
        if(!root.ui_stacks_menu_options_active || root.ui_stacks_menu_options_active.value!==true) return null;
        var groups=['controls','gameplay'];
        for(var i=0;i<groups.length;i++) {
            var group=groups[i],state=root['ui_stacks_menu_options_states_'+group+'_current'];
            if(!state || state.value!==true) continue;
            var target=document.querySelector('.options-group__options__items--'+group);
            if(target && target.offsetWidth && target.offsetHeight) return {anchor:target,mode:group==='controls'?'bindings':'contact',group:group};
        }
        return null;
    }
    function attach(){
        if(!nativeHost){close();return;}
        var option=findOptionsAnchor(),target=option?option.anchor:allowed?findAnchor():null,nextMode=option?option.mode:'weapons';
        if(anchor && (anchor!==target || mode!==nextMode || !document.body.contains(anchor) || !anchor.offsetWidth || !anchor.offsetHeight)) close();
        if(!target){close();return;}
        if(panel){if(navigation)navigation.input(model && model.bindings);if(panel)panel.update(model);if(navigation)navigation.sync();return;}
        anchor=target;mode=nextMode;stockHeight=anchor.style.height;
        if(mode==='weapons')anchor.style.height='auto';
        stock=mode==='weapons'?anchor.querySelector('.melee-loadout__container'):null;
        if(stock){
            stockDisplay=stock.style.display;
            stock.style.display='none';
        }
        var openingHost=nativeHost;
        var panelHost={nativeNavigation:true,settingsView:mode,openWeaponPicker:mode==='weapons'?openWeaponPicker:null,exit:function(){
            var state=root.ui_menu_navigation_loadout_stack,stackName=state && state.value;
            if(root.stack && typeof root.stack.back==='function' && typeof stackName==='string' && stackName.length<=64) root.stack.back(stackName);
            else if(root.engine && typeof root.engine.trigger==='function') root.engine.trigger('OnCancel');
        },send:function(request,reply){return openingHost.send.call(openingHost,request,reply);},
            onClose:function(){if(openingHost.controls)openingHost.controls(false);if(openingHost.panel)openingHost.panel(false);}};
        ['getBindings','saveBindings','saveContactMargin','saveLootMultiplier','controls','panel','capture'].forEach(function(method){if(typeof openingHost[method]==='function') panelHost[method]=function(){if(method==='capture'&&arguments[0]===false)captureReleaseUntil=Date.now()+150;return openingHost[method].apply(openingHost,arguments);};});
        panel=root.CRLoadoutPanel.create(panelHost,anchor);panel.update(model);
        var region=document.getElementById('cr-loadout-panel'),nativeLabel=anchor.querySelector('.options-keybind__label')||anchor.querySelector('.loadout-heading__content')||anchor;
        var family=root.getComputedStyle(nativeLabel).fontFamily;region.style.fontFamily=family;
        var labels=region.querySelectorAll('button,.cr-loadout-name');for(var f=0;f<labels.length;f++)labels[f].style.fontFamily=family;
        if(option)anchor.insertBefore(document.getElementById('cr-loadout-panel'),anchor.firstChild);
        if(!option)escapeHandler=function(event){
            if(!allowed || !anchor || (event.which||event.keyCode)!==27) return;
            var editing=anchor.querySelector('[data-cr-capturing="true"]'),settings=anchor.querySelector('.cr-loadout-settings');
            if(editing || (settings && settings.style.display!=='none')) return;
            event.preventDefault();event.stopPropagation();panelHost.exit();
        };
        if(escapeHandler)document.addEventListener('keydown',escapeHandler,true);
        if(root.CRLoadoutNativeNavigation){navigation=root.CRLoadoutNativeNavigation.create(option?document.getElementById('cr-loadout-panel'):anchor,root.__crmod_diagnostics===false?null:function(stage,context,count){if(nativeHost && nativeHost.surfaceProbe)nativeHost.surfaceProbe(stage,context,count);},{contextModel:option?'ui_menu_navigation_'+option.group+'_id':null,ownership:function(owned){if(openingHost.navigation)openingHost.navigation(owned,option?option.group==='controls'?1:2:0);},probe:root.__crmod_diagnostics===false?null:function(kind,id,edge,active,owned){if(openingHost.navigationProbe)openingHost.navigationProbe(kind,id,edge,active,owned);}});navigation.sync();}
    }
    function menuOwnership(){
        if(!nativeHost||typeof nativeHost.menu!=='function')return;
        var open=allowed||browserVisible()||value('ui_stacks_menu_options_active')===true||value('ui_stacks_game_states_system_menu_current')===true;
        if(open!==menuSent){menuSent=open;nativeHost.menu(open);}
    }
    function publish(value,openingAllowed){
        model=value;refreshPicker();refreshOpening();
        // Native picker callbacks may change opening during refresh; apply the packet last.
        if(arguments.length>1){
            var next=openingAllowed===true;
            if(next && root.__crmod_diagnostics!==false)root.setTimeout(inspectNativeImages,0);
            if(allowed!==next){allowed=next;refreshOpening();}
        }
    }
    function opening(value){allowed=value===true;if(allowed && root.__crmod_diagnostics!==false)root.setTimeout(inspectNativeImages,0);refreshOpening();}
    function refreshOpening(){refreshHud();attach();menuOwnership();}
    function host(value){gateNativeInput();if(nativeHost&&typeof nativeHost.menu==='function')nativeHost.menu(false);menuSent=null;close();stopPicker();if(pickerListening && root.engine && root.engine.off){root.engine.off('loadout_browser_slot_click',browserChoice);document.removeEventListener('click',pickerClick,true);pickerListening=false;}nativeHost=value && typeof value.send==='function' && typeof value.onClose==='function'?value:null;if(nativeHost && root.engine && root.engine.on){root.engine.on('loadout_browser_slot_click',browserChoice);document.addEventListener('click',pickerClick,true);pickerListening=true;}refreshOpening();}
    root.__cr_loadout_v1 = {
        publish: publish, setHost: host, setOpeningAllowed: opening, close: close,
        reset: function () { opening(false); host(null); publish(null); }
    };
}
// Defer DOM changes until after the borrowed native callback returns.
root.setTimeout(function () {
    if (document.body) mount();
    else document.addEventListener('DOMContentLoaded', mount, false);
}, 0);
)JS";
    script += "})(window,";
    script += quoted;
    script += ");\n";
    if (script.size() > 512 * 1024) return {};
    return script;
}
inline bool parse_command(std::string_view text, loadout_control::MenuEvent& out) {
    out = {};
    if (text.empty() || text.size() > 255) return false;
    std::size_t p = 0;
    const auto white = [&] {
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t' ||
               text[p] == '\r' || text[p] == '\n')) ++p;
    };
    const auto take = [&](char ch) {
        white();
        if (p == text.size() || text[p] != ch) return false;
        ++p;
        return true;
    };
    const auto number = [&](bool quoted, std::uint64_t maximum,
                            std::uint64_t& value) {
        white();
        if (quoted && !take('"')) return false;
        const auto begin = p;
        while (p < text.size() && text[p] >= '0' && text[p] <= '9') ++p;
        if (p == begin || (p - begin > 1 && text[begin] == '0')) return false;
        const auto parsed = std::from_chars(text.data() + begin, text.data() + p, value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + p ||
            value > maximum) return false;
        if (quoted) {
            if (p == text.size() || text[p] != '"') return false;
            ++p;
        }
        return true;
    };
    constexpr std::array<std::string_view, 5> keys{
        "action", "channel", "slot", "id", "world_epoch"};
    constexpr std::array<std::uint64_t, 5> maxima{
        2, 1, loadout_control::kSlotCount-1, std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint64_t>::max()};
    std::array<std::uint64_t, 5> values{};
    unsigned seen = 0;
    if (!take('{')) return false;
    for (unsigned fields = 0; fields < keys.size(); ++fields) {
        if (!take('"')) return false;
        const auto begin = p;
        const auto end = text.find('"', p);
        if (end == std::string_view::npos) return false;
        const auto key = text.substr(begin, end - begin);
        p = end + 1;
        unsigned k = 0;
        while (k < keys.size() && keys[k] != key) ++k;
        if (k == keys.size() || (seen & (1u << k)) || !take(':') ||
            !number(k == 4, maxima[k], values[k])) return false;
        seen |= 1u << k;
        if (fields + 1 < keys.size() && !take(',')) return false;
    }
    if (seen != 31 || !values[4] || !take('}')) return false;
    white();
    if (p != text.size()) return false;
    out = {static_cast<loadout_control::Action>(values[0]),
           static_cast<loadout_control::Channel>(values[1]),
           static_cast<unsigned>(values[2]), static_cast<std::uint32_t>(values[3]),
           values[4]};
    return true;
}

struct Receipt { std::uint64_t ticket = 0, view_generation = 0; };
struct ClaimedCommand { Receipt receipt{}; loadout_control::MenuEvent event{}; };
class CommandMailbox {
    enum class State : std::uint8_t { Free, Queued, Running, Complete };
    struct Entry {
        State state = State::Free;
        ClaimedCommand command{};
        loadout_control::Result result = loadout_control::Result::Rejected;
    };
    std::array<Entry, 32> entries_{};
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> active_generation_{0};
    std::uint64_t last_generation_ = 0, next_ticket_ = 0;
    struct Unlock {
        std::atomic_flag& flag;
        ~Unlock() { flag.clear(std::memory_order_release); }
    };
    bool enter() { return !lock_.test_and_set(std::memory_order_acquire); }
    static bool same(Receipt a, Receipt b) {
        return a.ticket && a.ticket == b.ticket &&
               a.view_generation == b.view_generation;
    }
public:
    CommandMailbox() = default;
    CommandMailbox(const CommandMailbox&) = delete;
    CommandMailbox& operator=(const CommandMailbox&) = delete;
    void retire_all() { active_generation_.store(0); }
    // UI lifecycle owner only; never reuse a generation after release.
    bool bind_generation(std::uint64_t generation) {
        if (!generation || !enter()) return false;
        Unlock release{lock_};
        if (active_generation_.load() || generation <= last_generation_) return false;
        entries_ = {};
        last_generation_ = generation;
        active_generation_.store(generation);
        return true;
    }
    void retire(std::uint64_t generation) {
        if (generation) active_generation_.compare_exchange_strong(generation, 0);
        // No waits in release callbacks. Old queued entries cannot be claimed;
        // already Running commands retain native owner/world validation.
    }
    Receipt enqueue(std::uint64_t generation,
                    const loadout_control::MenuEvent& event) {
        if (!generation || !enter()) return {};
        Unlock release{lock_};
        if (active_generation_.load() != generation ||
            next_ticket_ == std::numeric_limits<std::uint64_t>::max()) return {};
        for (auto& entry : entries_) {
            if (entry.state != State::Free) continue;
            entry.command = {{++next_ticket_, generation}, event};
            entry.state = State::Queued;
            return entry.command.receipt;
        }
        return {}; // Full: reject transport; no command was admitted.
    }
    // Sole verified game execution owner. Preserve admission order.
    bool claim(ClaimedCommand& out) {
        out = {};
        if (!enter()) return false;
        Unlock release{lock_};
        const auto generation = active_generation_.load();
        Entry* first = nullptr;
        for (auto& entry : entries_) {
            if (entry.state != State::Queued) continue;
            if (entry.command.receipt.view_generation != generation) {
                entry = {};
                continue;
            }
            if (!first || entry.command.receipt.ticket < first->command.receipt.ticket)
                first = &entry;
        }
        if (!first) return false;
        first->state = State::Running;
        out = first->command;
        return true;
    }
    // If busy, retain scalar arguments and retry publication; NEVER reexecute.
    bool complete(Receipt receipt, loadout_control::Result result) {
        if (!enter()) return false;
        Unlock release{lock_};
        for (auto& entry : entries_) {
            if (entry.state != State::Running || !same(entry.command.receipt, receipt))
                continue;
            if (active_generation_.load() != receipt.view_generation) entry = {};
            else { entry.result = result; entry.state = State::Complete; }
            return true;
        }
        return true; // Already retired/replaced: response is obsolete.
    }
    // Current UI callback only: copy result then free reserved response capacity.
    bool take_reply(Receipt receipt, loadout_control::Result& result) {
        if (!enter()) return false;
        Unlock release{lock_};
        if (active_generation_.load() != receipt.view_generation) return false;
        for (auto& entry : entries_) {
            if (entry.state != State::Complete || !same(entry.command.receipt, receipt))
                continue;
            result = entry.result;
            entry = {};
            return true;
        }
        return false;
    }
};
} // namespace native_ui

#ifdef _WIN32
#include <windows.h>
#include <cstring>
#include <string>
namespace native_ui { namespace sdk {
struct Guard {
    std::uintptr_t exe_base = 0, cohtml_base = 0;
    bool executable_and_dll_hashes_verified = false;
    bool lifecycle_signatures_verified = false;
};
struct HookSpec { std::uintptr_t target; void* detour; void** original; };
using Callback = void (*)(void*);
using Destroy = std::uintptr_t (*)(void*, std::uint32_t);
using BeforeScript = const char* (*)(void*, std::uint64_t);
inline BeforeScript before_script = nullptr;
inline void* original_dom = nullptr;
inline void* original_release = nullptr;
inline void* original_destroy = nullptr;
inline Guard guard{};
inline const std::atomic<bool>* shared_stopping = nullptr;
inline std::string owned_bootstrap;
inline CommandMailbox mailbox;
inline std::atomic<bool> prepared{false}, enabled{false}, stopped{false};
inline std::atomic<unsigned> callbacks{0};
inline std::atomic<DWORD> ui_thread{0};
inline std::atomic<std::uintptr_t> listener_key{0}, view_key{0};
inline std::atomic<std::uint64_t> generation{0}, next_generation{0}, issued_generation{0};
inline std::atomic<bool> dom_seen{false};
struct CallbackScope {
    CallbackScope() { CRMOD_DIAGNOSTIC(++callbacks); }
    ~CallbackScope() { CRMOD_DIAGNOSTIC(--callbacks); }
};
struct Borrowed { void* view = nullptr; std::uintptr_t listener = 0; };
inline bool lifecycle_active() {
    return prepared.load() && !stopped.load() &&
           shared_stopping && !shared_stopping->load();
}
inline bool active() { return lifecycle_active() && enabled.load(); }
inline bool game_listener(void* listener) {
    if (!prepared.load() || !listener) return false;
    std::uintptr_t table = 0;
    std::memcpy(&table, listener, sizeof(table));
    return table == guard.exe_base + 0x5110c98;
}
inline bool view_shape(void* view) {
    if (!view) return false;
    const auto* table = *reinterpret_cast<const std::uintptr_t* const*>(view);
    return reinterpret_cast<std::uintptr_t>(table) == guard.cohtml_base + 0x6046f8 &&
           table[0x1e0 / 8] == guard.cohtml_base + 0x55b654 &&
           table[0x1f0 / 8] == guard.cohtml_base + 0x0b7750;
}
inline Borrowed borrow_listener(void* listener) {
    if (!lifecycle_active() || !game_listener(listener)) return {};
    const auto* bytes = static_cast<const unsigned char*>(listener);
    std::uint32_t id = UINT32_MAX;
    void* owner = nullptr;
    void* view = nullptr;
    std::memcpy(&id, bytes + 0x08, sizeof(id));
    std::memcpy(&owner, bytes + 0x10, sizeof(owner));
    std::memcpy(&view, bytes + 0x28, sizeof(view));
    if (id == UINT32_MAX || !owner || !view_shape(view)) return {};
    return {view, reinterpret_cast<std::uintptr_t>(listener)};
}
inline bool current_ui_thread() {
    const DWORD current = GetCurrentThreadId();
    DWORD expected = 0;
    ui_thread.compare_exchange_strong(expected, current);
    return ui_thread.load() == current;
}
inline std::uint64_t associate(const Borrowed& borrowed) {
    if (!borrowed.view || !current_ui_thread() || !lifecycle_active()) return 0;
    const auto live_listener = listener_key.load();
    if (live_listener) {
        return live_listener == borrowed.listener &&
               view_key.load() == reinterpret_cast<std::uintptr_t>(borrowed.view)
               ? generation.load() : 0;
    }
    const auto previous = next_generation.load();
    if (previous == UINT64_MAX) return 0;
    const auto next = previous + 1;
    next_generation.store(next); // Sole verified UI callback thread.
    if (!mailbox.bind_generation(next)) return 0;
    dom_seen.store(false);
    issued_generation.store(0);
    view_key.store(reinterpret_cast<std::uintptr_t>(borrowed.view));
    generation.store(next);
    listener_key.store(borrowed.listener);
    if (!lifecycle_active()) { mailbox.retire_all(); return 0; }
    return next;
}
inline void retire_listener(void* listener) {
    if (!game_listener(listener)) return;
    const auto key = reinterpret_cast<std::uintptr_t>(listener);
    if (listener_key.load() != key) return;
    // Unexpected-thread retirement disables admission without racing identity updates.
    if (ui_thread.load() != GetCurrentThreadId()) {
        stopped.store(true); enabled.store(false); mailbox.retire_all(); return;
    }
    std::uintptr_t expected = key;
    if (!listener_key.compare_exchange_strong(expected, 0)) return;
    const auto retired = generation.exchange(0);
    mailbox.retire(retired);
    view_key.store(0);
    dom_seen.store(false);
    issued_generation.store(0);
}
inline void issue_bootstrap(void* current_view, std::uint64_t expected_generation) {
    if (!active() || !current_view || !expected_generation ||
        !current_ui_thread() || !dom_seen.load() ||
        generation.load() != expected_generation ||
        view_key.load() != reinterpret_cast<std::uintptr_t>(current_view) ||
        !view_shape(current_view)) return;
    using Ready = bool (*)(void*);
    using Execute = void (*)(void*, const char*);
    const auto ready = reinterpret_cast<Ready>(guard.cohtml_base + 0x55b654);
    if (!ready(current_view)) return;
    std::uint64_t empty = 0;
    if (!issued_generation.compare_exchange_strong(empty, expected_generation)) return;
    if (!active() || generation.load() != expected_generation) return;
    const char* source = owned_bootstrap.c_str();
    if (before_script) {
        const char* alternate = before_script(current_view, expected_generation);
        if (alternate) source = alternate;
    }
    if (!active() || generation.load() != expected_generation ||
        view_key.load() != reinterpret_cast<std::uintptr_t>(current_view)) return;
    const auto execute = reinterpret_cast<Execute>(guard.cohtml_base + 0x0b7750);
    execute(current_view, source);
    // FINAL View operation; execution can reenter. Issued is not rendering success.
}
inline void dom_built(void* listener) {
    const DWORD incoming = GetLastError();
    CallbackScope scope;
    const auto borrowed = borrow_listener(listener);
    const auto current_generation = associate(borrowed);
    if (current_generation) dom_seen.store(true);
    SetLastError(incoming);
    reinterpret_cast<Callback>(original_dom)(listener);
    const DWORD forwarded = GetLastError();
    // Root must verify imported empty OnDOMBuilt IAT destination cohtml+09cac0.
    if (current_generation) issue_bootstrap(borrowed.view, current_generation);
    SetLastError(forwarded);
}
inline void bindings_released(void* listener) {
    const DWORD incoming = GetLastError();
    CallbackScope scope;
    retire_listener(listener);
    SetLastError(incoming);
    reinterpret_cast<Callback>(original_release)(listener);
}
inline std::uintptr_t listener_destroyed(void* listener, std::uint32_t flags) {
    const DWORD incoming = GetLastError();
    CallbackScope scope;
    retire_listener(listener);
    SetLastError(incoming);
    return reinterpret_cast<Destroy>(original_destroy)(listener, flags);
}
inline void on_current_view(void* view) {
    const DWORD incoming = GetLastError();
    CallbackScope scope;
    if (active() && view_shape(view) && current_ui_thread())
        issue_bootstrap(view, generation.load());
    SetLastError(incoming);
}
inline std::array<HookSpec, 3> hook_specs(std::uintptr_t exe_base) {
    return {{{exe_base + 0x364479b, reinterpret_cast<void*>(&dom_built), &original_dom},
             {exe_base + 0x36447a7, reinterpret_cast<void*>(&bindings_released), &original_release},
             {exe_base + 0x3443fb0, reinterpret_cast<void*>(&listener_destroyed), &original_destroy}}};
}
inline bool prepare(const Guard& verified, std::string bootstrap,
                    const std::atomic<bool>& stopping) {
    if (prepared.load() || stopped.load() || stopping.load() ||
        !verified.exe_base || !verified.cohtml_base ||
        !verified.executable_and_dll_hashes_verified ||
        !verified.lifecycle_signatures_verified || bootstrap.empty() ||
        bootstrap.size() > 512 * 1024 || bootstrap.find('\0') != std::string::npos)
        return false;
    guard = verified;
    owned_bootstrap = std::move(bootstrap);
    shared_stopping = &stopping;
    prepared.store(true);
    return true;
}
inline bool arm(bool installed_and_pinned = false) {
    if (!installed_and_pinned || !prepared.load() || stopped.load() ||
        shared_stopping->load() || !original_dom || !original_release ||
        !original_destroy) return false;
    enabled.store(true);
    if (stopped.load() || shared_stopping->load()) enabled.store(false);
    return enabled.load();
}
inline void stop() {
    stopped.store(true);
    enabled.store(false);
    mailbox.retire_all();
    // Never wait whole callbacks: SDK execution may reenter stop on this thread.
    // State, script, module and trampolines remain pinned through process exit.
}
inline void flush(FILE* log) {
    if constexpr(!diagnostics::enabled)return;
    if (!log) return;
    // Diagnostic scalars only: these are not a coherent game ownership snapshot.
    static std::uint64_t previous_generation = UINT64_MAX, previous_issued = UINT64_MAX;
    static int previous_active = -1;
    const auto gen = generation.load(), issued = issued_generation.load();
    const int is_active = active() ? 1 : 0;
    if (gen == previous_generation && issued == previous_issued && is_active == previous_active) return;
    previous_generation = gen; previous_issued = issued; previous_active = is_active;
    std::fprintf(log, "{\"event\":\"native_ui_lifecycle\",\"prepared\":%s,\"active\":%s,"
        "\"generation\":%llu,\"issued_generation\":%llu,\"dom_seen\":%s,\"ui_thread\":%lu,"
        "\"rendering_verified\":false,\"gameplay_bound\":false}\n", prepared.load() ? "true" : "false",
        is_active ? "true" : "false", gen, issued, dom_seen.load() ? "true" : "false", ui_thread.load());
    std::fflush(log);
}
}} // namespace native_ui::sdk
#endif
