/* ES5 panel for the native Cohtml view. Host delivery must be bound separately. */
(function (global) {
    "use strict";
    var MAX_U64 = "18446744073709551615";
    function u32(v) { return typeof v === "number" && isFinite(v) && Math.floor(v) === v && v >= 0 && v <= 4294967295; }
    function u64(v) { return typeof v === "string" && /^(0|[1-9][0-9]{0,19})$/.test(v) && (v.length < 20 || v <= MAX_U64); }
    function array(v) { return Object.prototype.toString.call(v) === "[object Array]"; }
    function validation(v) {
        return v && u32(v.status) && v.status <= 4 && typeof v.selected === "boolean";
    }
    function pretty(n) {
        var seg = n.split(/[\/\\]/).pop().replace(/\.[A-Za-z0-9]{1,5}$/, "").replace(/^weapon_form_/, "").replace(/_small$/, "");
        var words = seg.split(/[_\-\s]+/), out = [], i;
        for (i = 0; i < words.length; i += 1) {
            if (words[i]) out.push(words[i].charAt(0).toUpperCase() + words[i].slice(1).toLowerCase());
        }
        seg = out.join(" ");
        return seg.length > 32 ? seg.slice(0, 31) + "\u2026" : seg;
    }
    function label(v) {
        if (!v || typeof v.name !== "string") return "";
        var n = v.name.replace(/^\s+|\s+$/g, "");
        if (n.length > 256) return "";
        return /[\/\\_]/.test(n) ? pretty(n) : n;
    }
    function reason(o) {
        var st = o && o.validation ? o.validation.status : 0;
        return st === 2 ? " (Not selectable yet)" : st === 4 ? " (Out of date)" : st === 3 ? " (Wrong slot type)" : " (Checking)";
    }
    function el(tag, cls, text) {
        var n = document.createElement(tag);
        if (cls) n.className = cls;
        if (typeof text === "string") n.textContent = text;
        return n;
    }
    function text(n, s) { if (n.textContent !== s) n.textContent = s; }
    function contains(p, n) { while (n) { if (n === p) return true; n = n.parentNode; } return false; }
    function valid(m) {
        var c, s, ch, slot, i, o;
        if (!m || !u64(m.world_epoch) || !array(m.channels) || m.channels.length !== 2 ||
                !array(m.options) || !u32(m.option_count) || m.option_count > 64 || m.option_count > m.options.length) return false;
        for (c = 0; c < 2; c += 1) {
            ch = m.channels[c];
            if (!ch || !array(ch.slots) || ch.slots.length !== 3 || !u64(ch.ticket)) return false;
            if (ch.in_flight === true && (!u32(ch.in_flight_id) || ch.ticket === "0")) return false;
            if (ch.pending === true && !u32(ch.pending_id)) return false;
            for (s = 0; s < 3; s += 1) {
                slot = ch.slots[s];
                if (!slot || !slot.slot || typeof slot.slot.assigned !== "boolean" ||
                        !u32(slot.slot.id) || typeof slot.enabled !== "boolean" ||
                        !validation(slot.validation) ||
                        (slot.enabled && (!slot.slot.assigned || slot.validation.status !== 1))) return false;
            }
        }
        for (i = 0; i < m.option_count; i += 1) {
            o = m.options[i];
            if (!o || !o.item || (o.item.channel !== 0 && o.item.channel !== 1) ||
                    !u32(o.item.id) || typeof o.enabled !== "boolean" || !validation(o.validation) ||
                    (o.enabled && o.validation.status !== 1)) return false;
        }
        return true;
    }
    function create(host, parent) {
        host = host || {};
        parent = parent || document.body;
        if (!document.body) throw new Error("Loadout panel requires a ready document");
        if (document.getElementById("cr-loadout-panel")) throw new Error("Loadout panel is already mounted");
        var alive = true, model = null, epoch = null, previousFocus = document.activeElement,compactLayout=null,compactHeight=0;
        var rows = [], status = [], activeNames = [], panel = el("section", "cr-loadout-panel");
        var header = el("div", "cr-loadout-header"), title = el("h2", "cr-loadout-title", "Weapon slots");
        var closeButton = el("button", "cr-loadout-close", "Close"), notice = el("p", "cr-loadout-notice");
        var columns = el("div", "cr-loadout-columns");
        var controlsToggle = el("button", "cr-loadout-settings-toggle", "Controls"), settingsBox = el("section", "cr-loadout-settings");
        var settingsMessage = el("p", "cr-loadout-settings-message"), settingsWaiting = false;
        var controlsOpen = false,settingsView=host.settingsView||"",optionsView=settingsView==="bindings"||settingsView==="contact";
        controlsToggle.type = "button"; controlsToggle.disabled = typeof host.getBindings !== "function";
        settingsBox.style.display = "none";
        settingsBox.appendChild(el("h3", "", "Weapon swap controls"));
        settingsBox.appendChild(el("p", "cr-loadout-help", "Choose keys or buttons unused by the game. Lock-on, attacks and jump use your game bindings."));
        var bindingValues={key_primary:0,key_secondary:0,key_alt_primary:0,key_alt_secondary:0,pad_primary:0,pad_secondary:0}, captureChannel=-1, captureDevice="",capturePadArmed=true,capturePublished=false;
        function bindingSnapshot(){return {key_primary:bindingValues.key_primary,key_secondary:bindingValues.key_secondary,key_alt_primary:bindingValues.key_alt_primary,key_alt_secondary:bindingValues.key_alt_secondary,pad_primary:bindingValues.pad_primary,pad_secondary:bindingValues.pad_secondary};}
        var bindingRows=[],cancelCapture=el("button","cr-loadout-capture-cancel","Cancel");
        cancelCapture.type="button";cancelCapture.style.display="none";
        var keyNames={1:"Mouse Left",2:"Mouse Right",4:"Mouse Middle",5:"Mouse X1",6:"Mouse X2",9:"Tab",13:"Enter",16:"Shift",17:"Ctrl",18:"Alt",32:"Space",33:"Page Up",34:"Page Down",35:"End",36:"Home",37:"Left",38:"Up",39:"Right",40:"Down",45:"Insert",46:"Delete"};
        var padNames={1:"D-pad Up",2:"D-pad Down",4:"D-pad Left",8:"D-pad Right",16:"Start / Options",32:"Back / Share",64:"Left stick",128:"Right stick",256:"LB / L1",512:"RB / R1",4096:"A / Cross",8192:"B / Circle",16384:"X / Square",32768:"Y / Triangle",65536:"LT / L2",131072:"RT / R2"};
        function keyLabel(code) {
            if(keyNames[code]) return keyNames[code];
            if(code>=48 && code<=90) return String.fromCharCode(code);
            if(code>=112 && code<=123) return "F"+(code-111);
            return code ? "Key "+code : "";
        }
        function renderBindings() {
            panel.setAttribute("data-cr-capturing",captureChannel>=0?"true":"false");
            if(capturePublished!==(captureChannel>=0)){capturePublished=captureChannel>=0;if(typeof host.capture==="function")host.capture(capturePublished);}
            for(var i=0;i<bindingRows.length;i+=1) {
                var row=bindingRows[i],suffix=row.channel===0?"primary":"secondary",code=bindingValues[row.device+"_"+suffix];
                var label=row.device!=="pad"?keyLabel(code):padNames[code];
                text(row.assign,captureChannel===row.channel && captureDevice===row.device?"Press a button…":label||"Unassigned");
                row.assign.disabled=settingsWaiting;row.clear.disabled=settingsWaiting || !code;
            }
            cancelCapture.style.display=captureChannel>=0?"":"none";
        }
        function cancelBindingCapture() {captureChannel=-1;captureDevice="";renderBindings();text(settingsMessage,"");}
        function saveControlValues(next) {
            if(settingsWaiting || typeof host.saveBindings!=="function") return;
            var primary=[next.key_primary,next.key_alt_primary],secondary=[next.key_secondary,next.key_alt_secondary],conflict=false;
            for(var a=0;a<2;a++)for(var b=0;b<2;b++)if(primary[a] && primary[a]===secondary[b])conflict=true;
            if(conflict || (next.pad_primary && next.pad_primary===next.pad_secondary)) {
                text(settingsMessage,"That button is assigned to the other weapon. Choose another.");return;
            }
            captureChannel=-1;captureDevice="";settingsWaiting=true;renderBindings();renderMargin();text(settingsMessage,"Saving controls…");
            host.saveBindings(next,function(reply) {
                if(!alive) return;settingsWaiting=false;
                if(!reply || reply.accepted!==true) {renderBindings();renderMargin();text(settingsMessage,"Could not assign. Try again.");return;}
                updateBindings(reply);
            });
        }
        function captureBinding(device,code) {
            if(captureChannel<0 || settingsWaiting || (captureDevice==="pad"?device!=="pad":device!=="key")) return;
            var suffix=captureChannel===0?"primary":"secondary",next=bindingSnapshot();
            next[captureDevice+"_"+suffix]=code;saveControlValues(next);
        }
        var lastInputSequence=null;
        function keyboardInput(presses){
            if(!Array.isArray(presses)||captureChannel<0||captureDevice==='pad')return;
            for(var i=0;i<presses.length&&i<32&&captureChannel>=0;i++){
                var key=presses[i];if(!Number.isInteger(key)||key<1||key>254)continue;
                if(key===27){cancelBindingCapture();return;}
                captureBinding('key',key===46?0:key);
            }
        }
        function bindingRow(channel,title) {
            var row=el("div","cr-loadout-binding");row.appendChild(el("span","",title));
            function deviceField(device,label) {
                var cell=el("div","cr-loadout-binding-device"),assign=el("button","cr-loadout-bind-button","Unassigned"),clear=el("button","cr-loadout-bind-clear","Clear");
                assign.type=clear.type="button";
                assign.setAttribute("aria-label","Assign "+title.toLowerCase()+" "+label.toLowerCase());
                clear.setAttribute("aria-label","Clear "+title.toLowerCase()+" "+label.toLowerCase());
                cell.appendChild(el("span","cr-loadout-binding-label",label));cell.appendChild(assign);if(!optionsView)cell.appendChild(clear);row.appendChild(cell);
                bindingRows.push({channel:channel,device:device,assign:assign,clear:clear});
                assign.onclick=function() {
                    if(settingsWaiting) return;captureChannel=channel;captureDevice=device;capturePadArmed=!optionsView;renderBindings();
                    text(settingsMessage,device!=="pad"?"Press a key or mouse button. Delete clears; Escape cancels.":"Press a controller button. Start clears; Back cancels.");
                };
                clear.onclick=function() {
                    var next=bindingSnapshot();
                    next[device+"_"+(channel===0?"primary":"secondary")]=0;saveControlValues(next);
                };
            }
            deviceField("key","Keyboard / mouse");if(optionsView)deviceField("key_alt","Keyboard / mouse alternate");deviceField("pad","Controller");settingsBox.appendChild(row);
        }
        bindingRow(0,"Primary");bindingRow(1,"Secondary");
        cancelCapture.onclick=cancelBindingCapture;settingsBox.appendChild(cancelCapture);
        var lootMultiplier=3,marginBox=el("section","cr-loadout-contact-margin"),marginChoices=el("div","cr-loadout-margin-choices");
        marginBox.appendChild(el("h3","","Resource loot multiplier"));
        marginBox.appendChild(el("p","cr-loadout-help","Pickup/chest Source, Living Shards and weapon materials. Quest rewards and Talent Points stay normal."));
        var minus=el("button","cr-loot-minus","Decrease"),valueButton=el("button","cr-loot-value","3×"),plus=el("button","cr-loot-plus","Increase");
        [minus,valueButton,plus].forEach(function(button){button.type="button";marginChoices.appendChild(button);});
        minus.setAttribute("aria-label","Decrease resource loot multiplier");plus.setAttribute("aria-label","Increase resource loot multiplier");valueButton.setAttribute("aria-label","Cycle resource loot multiplier");
        function renderMargin(){minus.disabled=settingsWaiting||lootMultiplier<=1;plus.disabled=settingsWaiting||lootMultiplier>=10;valueButton.disabled=settingsWaiting;text(valueButton,lootMultiplier+"×");}
        function saveLoot(value){if(settingsWaiting || typeof host.saveLootMultiplier!=="function")return;settingsWaiting=true;renderMargin();host.saveLootMultiplier(value,function(reply){if(!alive)return;settingsWaiting=false;if(reply&&reply.accepted===true)updateBindings(reply);else{renderMargin();text(settingsMessage,"Could not save multiplier. Try again.");}});}
        minus.onclick=function(){saveLoot(lootMultiplier-1);};plus.onclick=function(){saveLoot(lootMultiplier+1);};valueButton.onclick=function(){saveLoot(lootMultiplier===10?1:lootMultiplier+1);};
        marginBox.appendChild(marginChoices);renderMargin();

        settingsMessage.setAttribute("role","status");settingsBox.appendChild(settingsMessage);
        var settingsTabs=el("div","cr-loadout-settings-tabs"),bindingsPage=el("section","cr-loadout-settings-bindings"),timingPage=el("section","cr-loadout-settings-timing");
        var bindingTab=el("button","cr-loadout-settings-tab","Bindings"),timingTab=el("button","cr-loadout-settings-tab","Loot");
        settingsTabs.setAttribute("role","tablist");settingsTabs.setAttribute("aria-label","Loadout settings");
        bindingTab.type=timingTab.type="button";bindingTab.id="cr-loadout-bindings-tab";timingTab.id="cr-loadout-timing-tab";
        bindingTab.setAttribute("role","tab");timingTab.setAttribute("role","tab");
        bindingsPage.id="cr-loadout-bindings-page";timingPage.id="cr-loadout-timing-page";
        bindingsPage.setAttribute("role","tabpanel");timingPage.setAttribute("role","tabpanel");
        bindingsPage.setAttribute("aria-labelledby",bindingTab.id);timingPage.setAttribute("aria-labelledby",timingTab.id);
        bindingTab.setAttribute("aria-controls",bindingsPage.id);timingTab.setAttribute("aria-controls",timingPage.id);
        while(settingsBox.firstChild) bindingsPage.appendChild(settingsBox.firstChild);
        timingPage.appendChild(marginBox);
        settingsTabs.appendChild(bindingTab);settingsTabs.appendChild(timingTab);
        settingsBox.appendChild(settingsTabs);settingsBox.appendChild(bindingsPage);settingsBox.appendChild(timingPage);settingsBox.appendChild(settingsMessage);
        function settingsTab(timing) {
            cancelBindingCapture();bindingsPage.style.display=timing?"none":"";timingPage.style.display=timing?"":"none";
            bindingTab.setAttribute("aria-selected",timing?"false":"true");timingTab.setAttribute("aria-selected",timing?"true":"false");
            panel.scrollTop=0;
        }
        bindingTab.onclick=function(){settingsTab(false);};timingTab.onclick=function(){settingsTab(true);};settingsTab(false);
        function updateBindings(value) {
            if(!value || settingsWaiting || captureChannel>=0) return;
            var names=["key_primary","key_secondary","pad_primary","pad_secondary"];
            var alt=["key_alt_primary","key_alt_secondary"];for(var ai=0;ai<alt.length;ai++) if(u32(value[alt[ai]]))bindingValues[alt[ai]]=value[alt[ai]];
            for(var i=0;i<names.length;i+=1) if(!u32(value[names[i]])) return;
            for(i=0;i<names.length;i+=1) bindingValues[names[i]]=value[names[i]];
            if(u32(value.loot_multiplier) && value.loot_multiplier>=1 && value.loot_multiplier<=10)lootMultiplier=value.loot_multiplier;
            renderBindings();renderMargin();
            text(settingsMessage,value.save_error===true?"Controls apply now but could not be saved.":value.saved===true?"Controls saved.":"Controls applied. Saving…");
        }
        controlsToggle.onclick=function () {
            cancelBindingCapture();
            controlsOpen=!controlsOpen; columns.style.display=controlsOpen?"none":""; settingsBox.style.display=controlsOpen?"":"none";
            text(controlsToggle,controlsOpen?"Weapons":"Controls");
            if (typeof host.controls==="function") host.controls(controlsOpen);
            if (controlsOpen && typeof host.getBindings==="function") host.getBindings(function (value) {
                if (!alive) return; if (value) updateBindings(value); else text(settingsMessage,"Controls are unavailable. Try reopening this panel.");
            });
        };
        panel.id = "cr-loadout-panel"; panel.tabIndex = -1;
        panel.setAttribute("role", "region"); 
        panel.setAttribute("aria-labelledby", "cr-loadout-heading"); title.id = "cr-loadout-heading";
        notice.setAttribute("role", "status"); notice.setAttribute("aria-live", "polite"); closeButton.type = "button";
        header.appendChild(title); header.appendChild(controlsToggle); header.appendChild(closeButton); panel.appendChild(header);
        panel.appendChild(el("p", "cr-loadout-intro", "Choose a weapon, then Select to assign and equip it. Configure assigns without switching."));
        panel.appendChild(notice); panel.appendChild(columns); panel.appendChild(settingsBox);
        function ready(configuration, channel) {
            return alive && model && model.world_epoch !== "0" && typeof host.send === "function" &&
                (configuration ? (model.configuration_bound === true || model.adapter_bound === true) :
                 (model.adapter_bound === true && (typeof channel !== "number" || model.channels[channel].selection_bound === true)));
        }
        function choices(ch) {
            var out = [], i, o;
            if (!model) return out;
            for (i = 0; i < model.option_count; i += 1) {
                o = model.options[i];
                if (o && o.item && o.item.channel === ch && u32(o.item.id)) out.push(o);
            }
            return out;
        }
        function option(ch, id) {
            var a = choices(ch), i;
            for (i = 0; i < a.length; i += 1) if (a[i].item.id === id) return a[i];
            return null;
        }
        function enabled(o) { return o && o.enabled === true && o.validation && o.validation.status === 1 && label(o) !== ""; }
        function name(ch, id, slot) { return label(slot) || label(option(ch, id)); }
        function matched(slot, r) {
            if (!slot || !slot.slot) return false;
            return r.action === 1 ? slot.slot.assigned !== true : slot.slot.assigned === true && slot.slot.id === r.id;
        }
        function selectConfigured(row) {
            var token = row.currentRequest, s = model && model.channels[row.channel].slots[row.slot];
            if (token && token.selectAfter && token.answered && token.epoch === epoch &&
                    matched(s, token) && s.enabled === true && s.validation.status === 1 && ready(false, row.channel)) {
                token.selectAfter = false;
                row.waiting = null;
                send(row, 2, token.id);
            }
        }
        function send(row, action, id, selectAfter) {
            if (!ready(action !== 2, row.channel) || row.waiting || !u32(id)) return false;
            var e = model.world_epoch, request = {action: action, channel: row.channel, slot: row.slot, id: id, world_epoch: e};
            var token = {action: action, id: id, epoch: e, answered: false, selectAfter: selectAfter === true};
            row.currentRequest = token;
            if (action === 0 || action === 1) row.waiting = token;
            text(notice, "Request sent. Waiting for an update."); render();
            function result(reply) {
                if (!alive || epoch !== e || row.currentRequest !== token || !reply ||
                        reply.world_epoch !== e || !u32(reply.result) || reply.result > 8) return;
                token.answered = true;
                if (token.selectAfter && reply.result !== 2 && reply.result !== 4) token.selectAfter = false;
                if (reply.result === 0 || reply.result === 1) {
                    token.selectAfter = false;
                    if (row.waiting === token) row.waiting = null;
                    text(notice, reply.result === 0 ?
                        "The game is not ready to change this slot. Load into the world, wait a moment, then try again." :
                        "That weapon cannot go in this slot right now. Pick a weapon without \"(Not selectable yet)\" or clear the slot first.");
                }
                render(); // Admission is not selection/effect completion.
                selectConfigured(row);
            }
            try {
                if (host.send(JSON.stringify(request), result) === false && !token.answered) {
                    token.selectAfter = false;
                    if (row.waiting === token) row.waiting = null;
                    text(notice, "The request could not be sent."); render(); return false;
                }
            } catch (error) {
                token.selectAfter = false;
                if (row.waiting === token) row.waiting = null;
                text(notice, "The request could not be sent."); render(); return false;
            }
            return true;
        }
        function addRow(ch, slot, parent) {
            var r = {channel: ch, slot: slot, draft: "", dirty: false, signature: "", waiting: null, currentRequest: null};
            var node = el("div", "cr-loadout-row"), top = el("div", "cr-loadout-row-heading");
            var controls = el("div", "cr-loadout-controls"), side = ch === 0 ? "Primary" : "Secondary";
            r.current = el("span", "cr-loadout-name");r.assign=el("button","cr-loadout-assignment");r.icon=el("img","cr-loadout-assignment-icon");r.icon.alt="";r.assign.type="button";r.assign.setAttribute("aria-label","Change "+side.toLowerCase()+" slot "+(slot+1));r.assign.appendChild(r.icon);r.assign.appendChild(r.current);r.picking=false; r.selected = el("span", "cr-loadout-selected", "Equipped");
            r.picker = el("div", "cr-loadout-picker"); r.choices = []; r.configure = el("button", "", "Assign");
            r.clear = el("button", "", "Clear"); r.select = el("button", "", "Equip"); r.message = el("p", "cr-loadout-row-message");
            r.picker.setAttribute("role", "group"); r.picker.setAttribute("aria-label", side + " slot " + (slot + 1) + " weapon");
            r.configure.type = r.clear.type = r.select.type = "button";
            r.configure.setAttribute("aria-label", "Configure " + side.toLowerCase() + " slot " + (slot + 1));
            r.clear.setAttribute("aria-label", "Clear " + side.toLowerCase() + " slot " + (slot + 1));
            r.select.setAttribute("aria-label", "Select " + side.toLowerCase() + " slot " + (slot + 1));
            top.appendChild(el("span", "cr-loadout-slot", "Slot " + (slot + 1))); top.appendChild(r.selected); top.appendChild(r.assign);
            node.appendChild(top); node.appendChild(r.picker); controls.appendChild(r.configure); controls.appendChild(r.clear); controls.appendChild(r.select);
            node.appendChild(controls); node.appendChild(r.message); parent.appendChild(node);
            r.configure.onclick = function () {
                var id = Number(r.draft);
                if (r.draft !== "" && u32(id) && enabled(option(ch, id))) send(r, 0, id);
            };
            r.clear.onclick = function () { send(r, 1, 0); };
            r.select.onclick = function () {
                var s = model && model.channels[ch].slots[slot];
                var id = Number(r.draft);
                if (r.select.disabled) return;
                if (r.draft !== "" && enabled(option(ch, id)) &&
                        (!s.slot.assigned || s.slot.id !== id)) send(r, 0, id, true);
                else if (s && s.slot.assigned === true && s.enabled === true &&
                        validation(s.validation) && s.validation.status === 1 &&
                        name(ch, s.slot.id, s)) send(r, 2, s.slot.id);
            };
            r.assign.onclick=function(){if(typeof host.openWeaponPicker==="function"){if(!host.openWeaponPicker(ch,slot))text(notice,"Native weapon selection is unavailable. Reopen Loadout.");return;}var open=!r.picking;for(var i=0;i<rows.length;i++)rows[i].picking=false;r.picking=open;render();};
            rows.push(r);
        }
        function channelStatus(ch) {
            if (!model) return "";
            var d = model.channels[ch];
            if (d.in_flight === true) {
                if (!ready(false, ch) || d.terminal !== 1) return "Selection has not been confirmed.";
                return d.pending === true ? "Selecting a weapon. Another change is queued." : "Waiting for selection confirmation.";
            }
            return d.pending === true ? "A weapon change is queued." : "";
        }
        function renderRow(r) {
            var s = model && model.channels[r.channel].slots[r.slot], assigned = s && s.slot.assigned === true;
            var n = assigned ? name(r.channel, s.slot.id, s) : "", list = choices(r.channel), sig = [], i, o, node, draft;
            var canAct = ready(false, r.channel), canConfigure = ready(true);
            if (r.waiting && model && (r.waiting.epoch !== model.world_epoch || matched(s, r.waiting))) {
                r.waiting = null; r.dirty = false;r.picking=false;
            }
            text(r.current, model ? (assigned ? (n || ("Form " + s.slot.id)) : (typeof host.openWeaponPicker==="function"?"Empty":"Unassigned")) : "Weapon information unavailable");
            r.assign.disabled=!canConfigure || !!r.waiting;
            r.picker.style.display=r.picking?"":"none";
            r.configure.style.display=r.picking?"":"none";
            var artwork=assigned && option(r.channel,s.slot.id),uri=artwork && artwork.icon||"";
            r.icon.style.display=uri?"":"none";
            if(uri) {var src=global.CRWeaponIcons && global.CRWeaponIcons[uri]||uri;if(r.icon.getAttribute("src")!==src) r.icon.setAttribute("src",src);}else r.icon.removeAttribute("src");
            r.selected.style.display = typeof host.openWeaponPicker!=="function" && assigned && canConfigure && s.validation && s.validation.selected === true ? "" : "none";
            for (i = 0; i < list.length; i += 1) sig.push([list[i].item.id, label(list[i]), enabled(list[i])]);
            sig = JSON.stringify(sig);
            if (!r.dirty) r.draft = assigned ? String(s.slot.id) : "";
            draft = r.draft;
            if (sig !== r.signature) {
                r.signature = sig; r.choices = [];
                while (r.picker.firstChild) r.picker.removeChild(r.picker.firstChild);
                r.picker.appendChild(el("span", "cr-loadout-pick-hint", list.length ? "Choose a weapon:" : "No weapons found for this slot."));
                for (i = 0; i < list.length; i += 1) {
                    o = list[i]; node = el("button", "cr-loadout-choice", (label(o) || ("Form " + o.item.id)) + (enabled(o) ? "" : reason(o)));
                    node.type = "button"; node.setAttribute("data-id", String(o.item.id));node.setAttribute("data-cr-nav-key","weapon:"+r.channel+":"+r.slot+":"+o.item.id);
                    node.setAttribute("aria-label", "Choose " + (label(o) || ("form " + o.item.id)));
                    node.disabled = !enabled(o);
                    node.onclick = (function (id, button) {
                        return function () { if (!button.disabled) { r.draft = String(id); r.dirty = true; render(); } };
                    }(o.item.id, node));
                    r.picker.appendChild(node); r.choices.push(node);
                }
            }
            if (draft !== "" && !option(r.channel, Number(draft))) { r.draft = ""; r.dirty = false; draft = ""; }
            o = draft === "" ? null : option(r.channel, Number(draft));
            for (i = 0; i < r.choices.length; i += 1) {
                node = r.choices[i];
                node.className = "cr-loadout-choice" + (node.getAttribute("data-id") === draft ? " is-picked" : "");
                node.disabled = !canConfigure || !!r.waiting || !enabled(option(r.channel, Number(node.getAttribute("data-id"))));
            }
            r.picker.disabled = !canConfigure || !!r.waiting || list.length === 0;   // state flag for tests/readers; choice buttons carry the real disabled state
            r.configure.disabled = !canConfigure || !!r.waiting || !enabled(o);
            r.clear.disabled = !canConfigure || !!r.waiting || !assigned;
            // Current selection remains clickable to cancel an unsent other request.
            r.select.disabled = !canAct || !!r.waiting || (draft !== "" ? (!canConfigure || !enabled(o)) :
                (!assigned || s.enabled !== true || !validation(s.validation) || s.validation.status !== 1 || !n));
            text(r.message, r.waiting ? "Waiting for the slot update." : "");
        }
        function focusables() {
            var a = panel.querySelectorAll("button, select"), out = [], i;
            for (i = 0; i < a.length; i += 1) if (!a[i].disabled && (a[i].offsetWidth || a[i].offsetHeight)) out.push(a[i]);
            return out;
        }
        function render() {
            var i;
            for (i = 0; i < rows.length; i += 1) renderRow(rows[i]);
            for (i = 0; i < 2; i += 1) {
                text(status[i], channelStatus(i));
                text(activeNames[i], "Equipped: " + (model && label({name:model.channels[i].active_name}) || "None"));
            }
            if (controlsOpen && model && model.bindings) updateBindings(model.bindings);
            if (!ready(true)) text(notice, "Loadout is unavailable right now.");
            else if (!ready()) text(notice, "Weapon switching is unavailable. You can configure slots.");
            else if (model.catalogue_incomplete === true) text(notice, "Some weapon information is unavailable.");
            else if (notice.textContent === "Loadout is unavailable right now." ||
                    notice.textContent === "Weapon switching is unavailable. You can configure slots." ||
                    notice.textContent === "Some weapon information is unavailable.") text(notice, "");
            if (contains(panel, document.activeElement) && document.activeElement.disabled) controlsToggle.focus();
        }
        function moveFocus(code) {
            var a = focusables(), active = document.activeElement;
            if (!contains(panel, active)) return;
            var from = active.getBoundingClientRect(), x = (from.left + from.right) / 2, y = (from.top + from.bottom) / 2;
            var best = null, score = Infinity, i, box, dx, dy, distance;
            for (i = 0; i < a.length; i += 1) {
                if (a[i] === active) continue;
                box = a[i].getBoundingClientRect(); dx = (box.left + box.right) / 2 - x; dy = (box.top + box.bottom) / 2 - y;
                if ((code === 37 && dx >= -1) || (code === 39 && dx <= 1) || (code === 38 && dy >= -1) || (code === 40 && dy <= 1)) continue;
                distance = code === 37 || code === 39 ? Math.abs(dx) + Math.abs(dy) * 3 : Math.abs(dy) + Math.abs(dx) * 3;
                if (distance < score) { score = distance; best = a[i]; }
            }
            if (best) best.focus();
        }
        function close() {
            if (!alive) return;
            alive = false;
            if(compactLayout)global.removeEventListener("resize",compactLayout,false);
            if (panel.parentNode) panel.parentNode.removeChild(panel);
            if (previousFocus && contains(document.body, previousFocus) && typeof previousFocus.focus === "function") previousFocus.focus();
            if (typeof host.onClose === "function") host.onClose();
        }
        var padBaseline=false, previousPad=0;
        function controllerPress(edge) {
            if (!alive || settingsWaiting) return;
            if(captureChannel>=0) {if(edge & 8192)cancelBindingCapture();else if(edge & 16)captureBinding(captureDevice==="pad"?"pad":"key",0);else if(captureDevice==="pad" && edge && !(edge & (edge-1)))captureBinding("pad",edge);return;}
            var focused=document.activeElement, i, items=focusables();
            if (!contains(panel,focused) || focused===panel) { if (items.length) items[0].focus(); focused=document.activeElement; }
            if (edge & 8192) { if (controlsOpen) controlsToggle.onclick(); else close(); return; }
            if (edge & 768) { if (!controlsToggle.disabled) controlsToggle.onclick(); return; }
            if (focused && focused.tagName==='SELECT' && (edge & 3)) {
                i=focused.selectedIndex+(edge & 1?-1:1); i=Math.max(0,Math.min(focused.options.length-1,i));
                focused.selectedIndex=i; if (focused.onchange) focused.onchange(); return;
            }
            if (edge & 1) moveFocus(38); else if (edge & 2) moveFocus(40);
            else if (edge & 4) moveFocus(37); else if (edge & 8) moveFocus(39);
            if ((edge & 4096) && document.activeElement && document.activeElement.tagName==='BUTTON') document.activeElement.click();
        }
        function controllerInput(value, presses) {
            if (!u32(value)) return;
            if(captureChannel>=0 && captureDevice==="pad" && !capturePadArmed){if(value===0)capturePadArmed=true;previousPad=value;padBaseline=true;return;}
            if(host.nativeNavigation===true && captureChannel<0) {previousPad=value;padBaseline=true;return;}
            if (array(presses)) {
                previousPad=value; padBaseline=true;
                for (var i=0;i<presses.length && i<32 && alive;i+=1) if (u32(presses[i])) controllerPress(presses[i]);
                return;
            }
            if (!padBaseline) { previousPad=value; padBaseline=true; return; }
            var edge=value & ~previousPad; previousPad=value;
            controllerPress(edge);
        }
        panel.onkeydown = function (ev) {
            ev = ev || global.event;
            var code = ev.which || ev.keyCode, a, current, i, target = ev.target || ev.srcElement;
            if(captureChannel>=0) {ev.preventDefault();ev.stopPropagation();if(code===27) cancelBindingCapture();else if(code===46)captureBinding("key",0);else if(!ev.repeat && u32(code) && code>0 && code<=254) captureBinding("key",code);return;}
            if (code === 27) {if(optionsView)return;if(controlsOpen){ev.preventDefault();ev.stopPropagation();controlsToggle.click();}else if(typeof host.exit==="function"){ev.preventDefault();ev.stopPropagation();host.exit();}return;}
            if (code >= 37 && code <= 40 && target.tagName !== "SELECT") { ev.preventDefault(); ev.stopPropagation(); moveFocus(code); }
            else if (ev.repeat && (code === 13 || code === 32)) ev.preventDefault();
        };
        panel.onmousedown=function(ev) {
            if(captureChannel<0 || settingsWaiting) return;
            var node=ev.target;while(node && node!==panel) {if(node.tagName==="BUTTON") return;node=node.parentNode;}
            ev.preventDefault();ev.stopPropagation();var codes=[1,4,2,5,6];
            if(ev.button>=0 && ev.button<codes.length) captureBinding("key",codes[ev.button]);
        };
        panel.oncontextmenu=function(ev) {ev.preventDefault();};
        var c, s, section;
        for (c = 0; c < 2; c += 1) {
            section = el("section", "cr-loadout-channel"); section.appendChild(el("h3", "", c === 0 ? "Primary" : "Secondary"));
            activeNames[c]=el("p", "cr-loadout-equipped", "Equipped: None"); section.appendChild(activeNames[c]);
            status[c] = el("p", "cr-loadout-channel-status"); status[c].setAttribute("aria-live", "polite"); section.appendChild(status[c]);
            var slotParent=section;
            if(typeof host.openWeaponPicker==="function"){slotParent=el("div","cr-loadout-slot-strip");section.appendChild(slotParent);}
            for (s = 0; s < 3; s += 1) addRow(c, s, slotParent);
            columns.appendChild(section);
        }
        panel.appendChild(el("p", "cr-loadout-help", "Choose a slot to assign a weapon. Equip switches to it."));
        closeButton.style.display="none";closeButton.onclick = close; parent.appendChild(panel); render();
        if(settingsView==="weapons") {
            if(typeof host.panel==="function")host.panel(true);
            controlsToggle.style.display="none";
            if(typeof host.openWeaponPicker==="function") {
                panel.className+=" cr-loadout-compact";
                compactLayout=function(){
                    if(!alive || compactHeight===global.innerHeight)return;compactHeight=global.innerHeight;
                    var size=Math.max(58,Math.min(96,Math.round(compactHeight*.085))),gap=Math.max(6,Math.round(compactHeight*.008));
                    var headingHeight=Math.max(18,Math.round(compactHeight*.018)),labelHeight=Math.max(16,Math.round(compactHeight*.016));
                    var titleBand=headingHeight+2*gap,rowHeight=headingHeight+labelHeight+size+3*gap;
                    panel.style.height=(titleBand+2*rowHeight)+"px";panel.style.maxHeight=panel.style.height;panel.style.overflow="hidden";
                    header.style.cssText="position:absolute;left:0;top:0;width:100%;height:"+headingHeight+"px;margin:0;";
                    columns.style.cssText="position:absolute;left:0;top:"+titleBand+"px;width:100%;height:"+(2*rowHeight)+"px;margin:0;display:block;";
                    for(var channel=0;channel<2;channel++){
                        var section=columns.children[channel],strip=section.querySelector('.cr-loadout-slot-strip');
                        section.style.cssText="position:absolute;left:0;top:"+(channel*rowHeight)+"px;width:100%;height:"+rowHeight+"px;margin:0;padding:0;";
                        section.querySelector('h3').style.cssText="position:absolute;left:0;top:0;margin:0;height:"+headingHeight+"px;line-height:"+headingHeight+"px;";
                        strip.style.cssText="position:absolute;left:0;top:"+(headingHeight+gap)+"px;width:100%;height:"+(labelHeight+gap+size)+"px;display:block;margin:0;padding:0;";
                    }
                    for(var ri=0;ri<rows.length;ri++){
                        var row=rows[ri],top=row.assign.parentNode,node=top.parentNode;
                        node.style.cssText="position:absolute;left:"+(row.slot*(size+gap))+"px;top:0;width:"+size+"px;height:"+(labelHeight+gap+size)+"px;margin:0;padding:0;border:0;";
                        top.style.cssText="position:relative;display:block;width:100%;height:100%;margin:0;padding:0;";
                        top.querySelector('.cr-loadout-slot').style.cssText="position:absolute;left:0;top:0;height:"+labelHeight+"px;line-height:"+labelHeight+"px;margin:0;white-space:nowrap;";
                        row.assign.style.position="absolute";row.assign.style.left="0";row.assign.style.top=(labelHeight+gap)+"px";row.assign.style.margin="0";
                        row.assign.style.width=size+"px";row.assign.style.height=size+"px";row.assign.style.minHeight=size+"px";row.assign.style.maxHeight=size+"px";
                        row.icon.style.width=Math.round(size*.5)+"px";row.icon.style.height=Math.round(size*.5)+"px";
                    }
                };
                compactLayout();global.addEventListener("resize",compactLayout,false);
            }
        }
        if(optionsView) {
            panel.className+=" cr-options-panel cr-options-"+settingsView;
            controlsToggle.style.display="none";columns.style.display="none";notice.style.display="none";
            text(title,"Mod");settingsBox.style.display="";settingsTabs.style.display="none";controlsOpen=true;
            settingsTab(settingsView==="contact");
            if(settingsView==="bindings") {
                var labels=el("div","cr-options-binding-headings");
                labels.appendChild(el("span","","Weapon switching"));labels.appendChild(el("span","","Keyboard / mouse — Main / Alternate"));labels.appendChild(el("span","","Controller"));
                bindingsPage.insertBefore(labels,bindingsPage.firstChild);
            }
            if(typeof host.panel==="function")host.panel(true);
            if(typeof host.controls==="function")host.controls(true);
            if(typeof host.getBindings==="function")host.getBindings(function(value){if(alive && value)updateBindings(value);});
        }
        return {
            update: function (m) {
                if (!alive) return false;
                try { if (typeof m === "string") m = JSON.parse(m); } catch (error) { m = null; }
                if (!valid(m)) { model = null; render(); return false; }
                if (epoch !== m.world_epoch) {
                    padBaseline=false;
                    for (var i = 0; i < rows.length; i += 1) {
                        rows[i].draft = ""; rows[i].dirty = false;
                        rows[i].waiting = null; rows[i].currentRequest = null;
                    }
                    epoch = m.world_epoch;
                }
                model = m; render();
                if (model.bindings){var seq=model.bindings.sample_sequence;if(typeof seq!=='string'||seq!==lastInputSequence){lastInputSequence=seq;keyboardInput(model.bindings.key_presses);controllerInput(model.bindings.pad_buttons,model.bindings.pad_presses);}}
                for (var j = 0; j < rows.length; j += 1) selectConfigured(rows[j]);
                return true;
            },
            cancelCapture:cancelBindingCapture,
            close: close
        };
    }
    global.CRLoadoutPanel = {create: create};
}(window));
