(function (root) {
    "use strict";
    var MAX = "18446744073709551615";
    function u64(v) {
        return typeof v === "string" && /^(0|[1-9][0-9]{0,19})$/.test(v) && (v.length < 20 || v <= MAX);
    }
    function increment(v) {
        if (v === MAX) return null;
        var a = v.split(""), i = a.length - 1;
        while (i >= 0 && a[i] === "9") { a[i] = "0"; i -= 1; }
        if (i < 0) a.unshift("1"); else a[i] = String(Number(a[i]) + 1);
        return a.join("");
    }
    function parse(value) {
        if (typeof value !== "string") throw new Error("Native transport type");
        var result = JSON.parse(value);
        if (!result || typeof result !== "object") throw new Error("Native transport packet");
        return result;
    }
    function start() {
        var ui = root.__cr_loadout_v1, engine = root.engine;
        if (!ui || !engine || typeof engine.call !== "function") return;
        if (root.__cr_loadout_transport_v1) root.__cr_loadout_transport_v1.retire();
        var alive = true, generation = null, serial = "0", model = null;
        var pending = {}, timer = null, modelBusy = false, uncertain = false;
        function call(packet) { return engine.call("__CRLoadoutAbiProbe_01", packet); }
        function bindingsCall(packet, done) {
            if (!alive || !generation) return false;
            try {
                call(packet).then(function (value) {
                    var result;
                    try { result = parse(value); } catch (error) { if (done) done(null); return; }
                    if (result.kind !== "bindings" || result.view_generation !== generation) { if (done) done(null); return; }
                    if (done) done(result);
                }, function () { if (done) done(null); });
            } catch (error) { if (done) done(null); return false; }
            return true;
        }
        function disable() { uncertain = true; ui.setOpeningAllowed(false); ui.publish(null); }
        function matched(packet, kind, entry) {
            return packet.kind === kind && packet.view_generation === generation &&
                packet.client_ticket === entry.client && packet.world_epoch === entry.epoch &&
                u64(packet.native_ticket) && (entry.ticket === "0" || packet.native_ticket === entry.ticket);
        }
        function anyUncertain() {
            var key;
            for (key in pending) if (Object.prototype.hasOwnProperty.call(pending, key) && pending[key].uncertain) return true;
            return false;
        }
        function ack(entry) {
            if (!alive || entry.busy) return;
            entry.busy = true;
            try {
                call("cr.host.ack.v1:" + generation + ":" + entry.client + ":" + entry.ticket + ":" + entry.epoch).then(function (value) {
                    entry.busy = false;
                    if (!alive) return;
                    var packet;
                    try { packet = parse(value); } catch (error) { disable(); return; }
                    if (!matched(packet, "ack", entry) || packet.ack !== true) { disable(); return; }
                    delete pending[entry.client]; uncertain = anyUncertain();
                }, function () { entry.busy = false; disable(); });
            } catch (error) { entry.busy = false; disable(); }
        }
        function pollEntry(entry) {
            if (entry.done) { ack(entry); return; }
            if (!alive || entry.busy) return;
            entry.busy = true;
            try {
                call("cr.host.reply.v1:" + generation + ":" + entry.client + ":" + entry.ticket + ":" + entry.epoch).then(function (value) {
                    entry.busy = false;
                    if (!alive) return;
                    var packet;
                    try { packet = parse(value); } catch (error) { entry.uncertain = true; disable(); return; }
                    if (!matched(packet, "reply", entry) || packet.state === "unknown" || packet.native_ticket === "0") {
                        entry.uncertain = true; disable(); return;
                    }
                    entry.ticket = packet.native_ticket; entry.uncertain = false; uncertain = anyUncertain();
                    if (packet.state === "pending") return;
                    if (packet.state !== "complete" || typeof packet.result !== "number" || packet.result % 1 || packet.result < 0 || packet.result > 8) {
                        entry.uncertain = true; disable(); return;
                    }
                    entry.done = true;
                    try { entry.reply({world_epoch: entry.epoch, result: packet.result}); } catch (error) { disable(); }
                    ack(entry);
                }, function () { entry.busy = false; entry.uncertain = true; disable(); });
            } catch (error) { entry.busy = false; entry.uncertain = true; disable(); }
        }
        function pollModel() {
            if (!alive || !generation || modelBusy) return;
            modelBusy = true;
            try {
                call("cr.host.model.v1:" + generation).then(function (value) {
                    modelBusy = false;
                    if (!alive) return;
                    var packet;
                    try { packet = parse(value); } catch (error) { disable(); return; }
                    if (packet.kind !== "model" || packet.view_generation !== generation || !packet.model || !u64(packet.model.world_epoch)) {
                        disable(); return;
                    }
                    model = packet.model;
                    if (uncertain) { ui.publish(null, false); }
                    else { ui.publish(model, packet.opening_allowed === true); }
                }, function () { modelBusy = false; disable(); });
            } catch (error) { modelBusy = false; disable(); }
        }
        var host = {
            imageProbe: function(channel,status,width,height,uri) {
                if(root.__crmod_diagnostics===false)return;
                if(!alive || !generation) return;
                try {call("cr.host.image.v1:"+generation+":"+channel+":"+status+":"+width+":"+height+":"+uri).then(function(){},function(){});} catch(error) {}
            },
            panel: function (open) { return bindingsCall("cr.host.panel.v1:" + generation + ":" + (open ? "1" : "0")); },
            menu: function(open){return bindingsCall('cr.host.menu.v1:'+generation+':'+(open?'1':'0'));},
            getBindings: function (done) { return bindingsCall("cr.host.bindings.v1:" + generation, done); },
            navigation:function(open,kind){return bindingsCall("cr.host.navigation.v1:"+generation+":"+(open?"1":"0")+":"+(kind||0));},
            navigationProbe:function(kind,id,edge,active,owned){
                if(root.__crmod_diagnostics===false)return;
                if(!alive || !generation)return;
                try{call('cr.host.navprobe.v1:'+generation+':'+kind+':'+id+':'+edge+':'+(active?1:0)+':'+(owned?1:0)).then(function(){},function(){});}catch(error){}
            },
            capture: function(open){return bindingsCall("cr.host.capture.v1:"+generation+":"+(open?"1":"0"));},
            controls: function (open) { return bindingsCall("cr.host.controls.v1:" + generation + ":" + (open ? "1" : "0")); },
            saveBackPause: function(value,done) {return bindingsCall("cr.host.pause.v1:"+generation+":"+value,done);},
            saveLootMultiplier:function(value,done){return bindingsCall("cr.host.loot.v1:"+generation+":"+value,done);},
            saveContactMargin: function(value,done) {return bindingsCall("cr.host.contact.v1:"+generation+":"+value,done);},
            saveBindings: function (value, done) {
                return bindingsCall("cr.host.bind.v1:" + generation + ":" + value.key_primary + ":" + value.key_secondary +
                    ":" + value.pad_primary + ":" + value.pad_secondary + ":" + (value.key_alt_primary||0) + ":" + (value.key_alt_secondary||0), done);
            },
            send: function (request, reply) {
                if (!alive || !generation || uncertain || !model || model.configuration_bound !== true ||
                        model.world_epoch === "0" || typeof reply !== "function") return false;
                var command, client = increment(serial), count = 0, key;
                try { command = JSON.parse(request); } catch (error) { return false; }
                if (!command || command.world_epoch !== model.world_epoch || !client ||
                        (command.action === 2 && (!model.channels[command.channel] || model.channels[command.channel].selection_bound !== true))) return false;
                for (key in pending) if (Object.prototype.hasOwnProperty.call(pending, key)) count += 1;
                if (count >= 32) return false;
                serial = client;
                var entry = {client: client, ticket: "0", epoch: command.world_epoch, reply: reply,
                    busy: false, uncertain: false, done: false};
                pending[client] = entry;
                var envelope = JSON.stringify({view_generation: generation, client_ticket: client, command: command});
                try {
                    call(envelope).then(function (value) {
                        if (!alive) return;
                        var packet;
                        try { packet = parse(value); } catch (error) { entry.uncertain = true; disable(); return; }
                        if (!matched(packet, "admission", entry) || typeof packet.accepted !== "boolean") {
                            entry.uncertain = true; disable(); return;
                        }
                        if (!packet.accepted) {
                            if (packet.native_ticket !== "0" || (packet.result !== 0 && packet.result !== 1)) {
                                entry.uncertain = true; disable(); return;
                            }
                            delete pending[client]; reply({world_epoch: entry.epoch, result: packet.result}); return;
                        }
                        if (packet.native_ticket === "0") { entry.uncertain = true; disable(); return; }
                        entry.ticket = packet.native_ticket; pollEntry(entry);
                    }, function () { entry.uncertain = true; disable(); });
                } catch (error) { entry.uncertain = true; disable(); }
                return true;
            },
            surfaceProbe:function(stage,context,count){
                if(root.__crmod_diagnostics===false)return false;
                if(!alive || !generation || !Number.isInteger(stage) || stage<0 || stage>4 || !Number.isInteger(context) || context<0 || context>2147483647 || !Number.isInteger(count) || count<0 || count>192) return false;
                try{call("cr.host.surface.v1:"+generation+":"+stage+":"+context+":"+count).then(function(){},function(){});return true;}catch(error){return false;}
            },
            onClose: function () {
                ui.setOpeningAllowed(false);
                if (!alive || !generation) return;
                try { call("cr.host.close.v1:" + generation).then(function () {}, function () { disable(); }); }
                catch (error) { disable(); }
            }
        };
        if(root.__crmod_diagnostics===false){delete host.imageProbe;delete host.navigationProbe;delete host.surfaceProbe;}
        function tick() {
            if (!alive || !generation) return;
            var key;
            for (key in pending) if (Object.prototype.hasOwnProperty.call(pending, key)) pollEntry(pending[key]);
            pollModel();
        }
        root.__cr_loadout_transport_v1 = {
            retire: function () {
                alive = false;
                if (timer !== null) root.clearInterval(timer);
                ui.setOpeningAllowed(false); ui.setHost(null); ui.publish(null);
            }
        };
        try {
            call("cr.host.hello.v1").then(function (value) {
                if (!alive) return;
                var packet;
                try { packet = parse(value); } catch (error) { disable(); return; }
                if (packet.kind !== "hello" || packet.protocol !== 1 || !u64(packet.view_generation) || packet.view_generation === "0") {
                    disable(); return;
                }
                generation = packet.view_generation; ui.setHost(host);
                timer = root.setInterval(tick, 33); tick();
            }, function () { disable(); });
        } catch (error) { disable(); }
    }
    root.setTimeout(start, 0);
}(window));
