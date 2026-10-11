/* Synchronous DOM checks, run by test_loadout_dom.py in an isolated Chrome. */
(function () {
    "use strict";
    var passed = [], sent = [], callbacks = [], panel, closed = 0;
    function check(value, name) { if (!value) throw new Error(name); passed.push(name); }
    function copy(value) { return JSON.parse(JSON.stringify(value)); }
    function model(epoch) {
        var m = {world_epoch: epoch || "18446744073709551615", adapter_bound: true, configuration_bound: true,
            option_count: 4, options: [], channels: []}, c, s;
        for (c = 0; c < 2; c += 1) {
            var ch = {ticket: "0", in_flight: false, pending: false, selection_bound: true, terminal: 0, slots: []};
            for (s = 0; s < 3; s += 1) ch.slots.push({slot: {assigned: true, id: c ? 8 : 4294967295},
                name: c ? "Secondary weapon" : "Primary weapon", enabled: true,
                validation: {status: 1, selected: s === 0}});
            m.channels.push(ch);
        }
        m.options = [
            {item: {channel: 0, id: 4294967295}, name: "Primary weapon", enabled: true, validation: {status: 1, selected: true}},
            {item: {channel: 0, id: 7}, name: "Locked weapon", enabled: false, validation: {status: 2, selected: false}},
            {item: {channel: 0, id: 9}, name: "Another primary weapon", enabled: true, validation: {status: 1, selected: false}},
            {item: {channel: 1, id: 8}, name: "Secondary weapon", enabled: true, validation: {status: 1, selected: true}}
        ];
        return m;
    }
    function rows() { return document.querySelectorAll(".cr-loadout-row"); }
    function button(row, text) {
        var buttons = row.querySelectorAll("button"), i;
        for (i = 0; i < buttons.length; i += 1) if (buttons[i].textContent === text) return buttons[i];
        throw new Error("Missing " + text);
    }
    function actionsDisabled() {
        var r = rows(), i, j, a;
        for (i = 0; i < r.length; i += 1) {
            a = r[i].querySelectorAll("button, select");
            for (j = 0; j < a.length; j += 1) if (!a[j].disabled) return false;
        }
        return true;
    }
    function key(target, code, shift) {
        var event = document.createEvent("Event"); event.initEvent("keydown", true, true);
        event.which = code; event.keyCode = code; event.shiftKey = !!shift;
        target.dispatchEvent(event);
    }
    var result;
    try {
        var previous = document.getElementById("previous-focus"); previous.focus();
        panel = window.CRLoadoutPanel.create({send: function (request, done) {
            sent.push(JSON.parse(request)); callbacks.push(done); return true;
        }, onClose: function () { closed += 1; }});
        check(rows().length === 6, "six independent slot rows");
        check(actionsDisabled(), "unbound actions disabled");
        var m = model(); check(panel.update(m), "max u64 string accepted");
        check(document.querySelector(".cr-loadout-notice").textContent === "", "availability notice clears when valid data arrives");
        var partial = copy(m); partial.catalogue_incomplete = true; panel.update(partial);
        check(document.querySelector(".cr-loadout-notice").textContent.indexOf("Some weapon") === 0, "incomplete catalogue notice displayed");
        panel.update(m);
        check(document.querySelector(".cr-loadout-notice").textContent === "", "complete catalogue clears incomplete notice");
        var r = rows()[0], select = button(r, "Equip"), configure = button(r, "Assign");
        var picker = r.querySelector(".cr-loadout-picker");
        check(picker.querySelector('button[data-id="7"]').disabled, "locked option disabled");
        select.click(); check(sent[0].id === 4294967295 && sent[0].world_epoch === m.world_epoch,
            "max u32 ID and exact u64 epoch delivered");
        var bad = copy(m); bad.world_epoch = 42;
        check(!panel.update(bad) && actionsDisabled(), "numeric u64 epoch rejected");
        bad = copy(m); bad.channels[0].ticket = 42;
        check(!panel.update(bad), "numeric u64 ticket rejected");
        bad = copy(m); bad.world_epoch = "18446744073709551616";
        check(!panel.update(bad), "overflow u64 rejected");
        bad = copy(m); bad.channels[0].slots[0].validation.status = 2;
        check(!panel.update(bad) && actionsDisabled(), "unavailable enabled slot rejected");
        check(panel.update(m), "valid model restores actions");
        picker.querySelector('button[data-id="9"]').click();
        configure.click(); var rejected = callbacks[callbacks.length - 1];
        check(select.disabled && configure.disabled, "configure waits for authoritative model");
        check(!panel.update(null), "missing model disables actions");
        rejected({world_epoch: m.world_epoch, result: 1}); panel.update(m);
        check(!select.disabled && !configure.disabled, "same-epoch rejection survives missing model");
        configure.click(); var authoritative = copy(m);
        authoritative.channels[0].slots[0].slot.id = 9;
        authoritative.channels[0].slots[0].name = "Another primary weapon";
        authoritative.channels[0].slots[0].validation.selected = false;
        check(panel.update(authoritative) && !select.disabled &&
            r.querySelector(".cr-loadout-name").textContent === "Another primary weapon",
            "authoritative configure update clears wait and displays native name");
        panel.update(m);
        select.click(); var superseded = callbacks[callbacks.length - 1]; select.click();
        var notice = document.querySelector(".cr-loadout-notice").textContent;
        superseded({world_epoch: m.world_epoch, result: 1});
        check(document.querySelector(".cr-loadout-notice").textContent === notice,
            "superseded same-epoch callback ignored");
        configure.click(); var old = callbacks[callbacks.length - 1];
        var next = model("22"); panel.update(next);
        picker.querySelector('button[data-id="9"]').click(); configure.click();
        old({world_epoch: m.world_epoch, result: 1});
        check(select.disabled, "stale-world result cannot clear current wait");
        var current = callbacks[callbacks.length - 1];
        current({world_epoch: "22", result: 1});
        check(!select.disabled, "current rejection clears wait");
        next.channels[0].pending = true; next.channels[0].pending_id = 7; panel.update(next);
        check(!select.disabled, "current selection can cancel unsent other request");
        picker.querySelector('button[data-id="4294967295"]').click();
        var before = sent.length; select.click();
        check(sent.length === before + 1 && sent[sent.length - 1].action === 2,
            "selected slot issues selection request");
        var empty = model("23");
        empty.channels[0].slots[0] = {slot: {assigned: false, id: 0}, enabled: false,
            validation: {status: 0, selected: false}};
        panel.update(empty);
        check(select.disabled, "unassigned slot needs an unlocked weapon choice");
        picker.querySelector('button[data-id="9"]').click();
        before = sent.length; select.click();
        var assignment = callbacks[callbacks.length - 1];
        check(sent.length === before + 1 && sent[before].action === 0 && sent[before].id === 9,
            "Select assigns the chosen weapon in an empty slot");
        select.click(); check(sent.length === before + 1, "pending assignment ignores repeated Select");
        assignment({world_epoch: "23", result: 2});
        check(sent.length === before + 1, "configure reply waits for matching slot model");
        var configured = copy(empty);
        configured.channels[0].slots[0] = {slot: {assigned: true, id: 9}, name: "Another primary weapon",
            enabled: true, validation: {status: 1, selected: false}};
        panel.update(configured); panel.update(configured);
        check(sent.length === before + 2 && sent[before + 1].action === 2 && sent[before + 1].id === 9,
            "matching native assignment triggers exactly one selection");
        picker.querySelector('button[data-id="4294967295"]').click();
        before = sent.length; select.click();
        assignment = callbacks[callbacks.length - 1];
        configured.channels[0].slots[0].slot.id = 4294967295;
        panel.update(configured);
        check(sent.length === before + 1, "matching model alone cannot trigger selection before configure reply");
        assignment({world_epoch: "23", result: 2});
        check(sent.length === before + 2 && sent[before + 1].action === 2,
            "configure reply completes selection when model arrived first");
        panel.update(empty); picker.querySelector('button[data-id="9"]').click();
        before = sent.length; select.click(); assignment = callbacks[callbacks.length - 1];
        assignment({world_epoch: "23", result: 1}); panel.update(configured);
        check(sent.length === before + 1, "rejected assignment never selects later");
        panel.update(empty); picker.querySelector('button[data-id="9"]').click();
        before = sent.length; select.click(); assignment = callbacks[callbacks.length - 1];
        panel.update(model("24")); assignment({world_epoch: "23", result: 2});
        check(sent.length === before + 1, "world change cancels pending assign-and-select");
        var focus=document.querySelector('.cr-loadout-assignment');focus.focus();
        key(focus,9,false);
        check(document.getElementById('cr-loadout-panel').getAttribute('role')==='region' && !document.getElementById('cr-loadout-panel').hasAttribute('aria-modal'),'Inline region has no modal focus ownership');
        key(document.activeElement, 27, false); panel.close();
        check(closed === 1 && document.activeElement === previous, "close is idempotent and restores focus");
        callbacks[callbacks.length - 1]({world_epoch: "22", result: 1});
        check(!document.getElementById("cr-loadout-panel"), "late result does not remount closed panel");
        var bindings={key_primary:33,key_secondary:34,pad_primary:0,pad_secondary:0,revision:"1",saved:true,pad_buttons:0,contact_margin_mm:35,back_pause_ms:50};
        var appliedBindings=null, controlsState=false, settingsClosed=0;
        panel=window.CRLoadoutPanel.create({send:function () { return false; },
            controls:function (open) { controlsState=open; }, getBindings:function (done) { done(copy(bindings)); },
            saveBindings:function (value,done) { appliedBindings=value;var margin=bindings.contact_margin_mm,pause=bindings.back_pause_ms; bindings=value;bindings.contact_margin_mm=margin;bindings.back_pause_ms=pause; bindings.revision="2"; bindings.saved=false; bindings.pad_buttons=0; bindings.accepted=true; done(copy(bindings)); },
            saveContactMargin:function(value,done) {bindings.contact_margin_mm=value;bindings.accepted=true;bindings.saved=false;done(copy(bindings));},
            saveBackPause:function(value,done) {bindings.back_pause_ms=value;bindings.accepted=true;bindings.saved=false;done(copy(bindings));},
            onClose:function () { settingsClosed+=1; }});
        var settingsModel=model("25"); settingsModel.bindings=copy(bindings);
        settingsModel.channels[0].active_name="Strike"; settingsModel.channels[1].active_name="Crush";
        panel.update(settingsModel);
        check(document.querySelector(".cr-loadout-equipped").textContent === "Equipped: Strike", "actual equipped weapon shown independently of slot assignment");
        document.querySelector(".cr-loadout-settings-toggle").click();
        check(controlsState && document.querySelector(".cr-loadout-settings").style.display!=="none", "controls page activates native editing ownership");
        var primary=document.querySelector('[aria-label="Assign primary keyboard / mouse"]'),secondary=document.querySelector('[aria-label="Assign secondary keyboard / mouse"]');
        check(!document.querySelector('.cr-loadout-settings select') && primary.textContent==='Page Up','existing bindings shown without selection lists');
        primary.click();key(primary,65,false);
        check(appliedBindings.key_primary===65 && appliedBindings.key_secondary===34,'press-to-bind captures keyboard and preserves other action');
        bindings.saved=true;settingsModel.bindings=copy(bindings);panel.update(settingsModel);
        check(document.querySelector('.cr-loadout-settings-message').textContent==='Controls saved.','saved acknowledgement comes from native model');
        secondary.click();var oldBinding=appliedBindings;
        key(secondary,65,false);
        check(appliedBindings===oldBinding && document.querySelector('.cr-loadout-settings-message').textContent.indexOf('other weapon')>=0,'duplicate binding rejected while capture remains active');
        key(secondary,27,false);
        check(document.getElementById('cr-loadout-panel') && secondary.textContent==='Page Down','Escape cancels capture without closing panel');
        document.querySelector('[aria-label="Assign primary controller"]').click();settingsModel.bindings.pad_presses=[4];settingsModel.bindings.pad_buttons=0;panel.update(settingsModel);
        check(appliedBindings.pad_primary===4 && appliedBindings.key_primary===65,'controller capture accepts fresh button and retains keyboard binding');
        bindings.saved=true;settingsModel.bindings=copy(bindings);settingsModel.bindings.pad_presses=[];panel.update(settingsModel);
        document.querySelector('[aria-label="Clear primary keyboard / mouse"]').click();
        check(appliedBindings.key_primary===0 && appliedBindings.pad_primary===4,'Keyboard Clear preserves controller assignment');
        bindings.saved=true;settingsModel.bindings=copy(bindings);settingsModel.bindings.pad_presses=[];panel.update(settingsModel);
        check(document.getElementById('cr-loadout-timing-page').style.display==='none','Timing controls are a separate hidden sub-tab');
        document.getElementById('cr-loadout-timing-tab').click();
        check(document.getElementById('cr-loadout-bindings-page').style.display==='none' && document.getElementById('cr-loadout-timing-page').style.display!=='none','Entity jump tab shows margin without binding rows');
        var marginChoices=document.querySelectorAll('[aria-label="Entity jump contact margin"] button');
        check(marginChoices.length===3 && marginChoices[0].textContent==='2 cm' && marginChoices[1].textContent==='3.5 cm' && marginChoices[2].textContent==='5 cm','Exactly three contact margin options');
        check(marginChoices[1].getAttribute('aria-pressed')==='true','Default contact margin is3.5cm');
        marginChoices[2].click();check(bindings.contact_margin_mm===50 && bindings.key_secondary===34,'Margin update preserves bindings');
        check(marginChoices[2].getAttribute('aria-pressed')==='true','Authoritative contact margin acknowledgement selected');
        check(!document.querySelector('[aria-label="Backward movement pause"]'),'Removed Back pause has no active UI controls');
        document.getElementById('cr-loadout-bindings-tab').click();
        check(primary.textContent==='Unassigned','cleared binding displays Unassigned');
        document.querySelector('[aria-label="Assign primary controller"]').click();key(primary,66,false);
        check(appliedBindings.key_primary===0 && appliedBindings.pad_primary===4,'Controller capture ignores keyboard input');
        key(primary,27,false);
        document.querySelector('[aria-label="Clear primary controller"]').click();
        check(appliedBindings.key_primary===0 && appliedBindings.pad_primary===0,'Controller Clear preserves keyboard assignment');
        check(bindings.contact_margin_mm===50 && bindings.back_pause_ms===50,'Binding changes preserve gameplay tuning');
        bindings.saved=true;settingsModel.bindings=copy(bindings);panel.update(settingsModel);
        delete settingsModel.bindings.pad_presses;
        settingsModel.bindings.pad_buttons=0; panel.update(settingsModel); settingsModel.bindings.pad_buttons=8192; panel.update(settingsModel);
        check(!controlsState && document.getElementById("cr-loadout-panel"), "controller Back returns from controls to weapons");
        settingsModel.bindings.pad_buttons=0; panel.update(settingsModel); settingsModel.bindings.pad_buttons=8192; panel.update(settingsModel);
        check(settingsClosed===1 && !document.getElementById("cr-loadout-panel"), "controller Back closes weapons panel");
        // Leave one bound panel visible for the screenshot after interaction checks.
        panel = window.CRLoadoutPanel.create({send: function () { return false; },
            getBindings:function (done) { done(copy(bindings)); }, controls:function () {}, saveBindings:function () { return false; },saveContactMargin:function(){},saveBackPause:function(){} });
        var visual=copy(settingsModel);
        visual.option_count=6;
        visual.options=[
            {item:{channel:0,id:698021297},name:"Strike",enabled:true,validation:{status:1,selected:true}},
            {item:{channel:0,id:3413371114},name:"Glaive",enabled:false,validation:{status:2,selected:false}},
            {item:{channel:0,id:1737076817},name:"Cleave",enabled:true,validation:{status:1,selected:false}},
            {item:{channel:1,id:444122270},name:"Crush",enabled:true,validation:{status:1,selected:true}},
            {item:{channel:1,id:2860349769},name:"Drill",enabled:true,validation:{status:1,selected:false}},
            {item:{channel:1,id:1282433425},name:"Extend",enabled:true,validation:{status:1,selected:false}}];
        for (var vc=0;vc<2;vc+=1) for (var vs=0;vs<3;vs+=1) {
            var assignedOption=visual.options[vc*3+(vs===0?0:vs===1?2:0)];
            visual.channels[vc].slots[vs]={slot:{assigned:vs<2,id:assignedOption.item.id},name:assignedOption.name,
                enabled:vs<2,validation:{status:vs<2?1:0,selected:vs===0}};
        }
        panel.update(visual);
        if (window.__cr_test_controls) {document.querySelector(".cr-loadout-settings-toggle").click();document.getElementById("cr-loadout-timing-tab").click();}
        result = {ok: true, passed: passed};
    } catch (error) { result = {ok: false, passed: passed, error: String(error.stack || error)}; }
    var out = document.createElement("pre"); out.id = "cr-test-result";
    out.textContent = JSON.stringify(result); document.body.appendChild(out);
}());
