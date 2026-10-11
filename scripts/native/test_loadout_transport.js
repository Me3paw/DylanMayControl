"use strict";
const assert=require("assert"),fs=require("fs"),vm=require("vm");
const pending=[],published=[],opening=[];let tick,host,cleared=0;
const root={__crmod_diagnostics:false,
    __cr_loadout_v1:{publish:function(){published.push(JSON.parse(JSON.stringify(Array.from(arguments))));},setOpeningAllowed:v=>opening.push(v),setHost:v=>{host=v;}},
    engine:{call:(method,packet)=>{assert.strictEqual(method,"__CRLoadoutAbiProbe_01");return {then:(ok,fail)=>pending.push({packet,ok,fail})};}},
    setTimeout:f=>f(),setInterval:(f,delay)=>{assert.strictEqual(delay,33);tick=f;return 1;},clearInterval:id=>{assert.strictEqual(id,1);cleared++;}};
vm.runInNewContext(fs.readFileSync(require.resolve("../../src/ui/loadout_transport.js"),"utf8"),{window:root});
assert.strictEqual(pending[0].packet,"cr.host.hello.v1");
pending.shift().ok(JSON.stringify({kind:"hello",protocol:1,view_generation:"77"}));
assert(host && !host.imageProbe && !host.navigationProbe && !host.surfaceProbe);
assert.strictEqual(pending.length,1);tick();assert.strictEqual(pending.length,1); // One outstanding model request.
const model={world_epoch:"1",bindings:{sample_sequence:"5",pad_presses:[8]}};
function reply(allowed,generation="77") {
    assert.strictEqual(pending[0].packet,"cr.host.model.v1:77");
    pending.shift().ok(JSON.stringify({kind:"model",view_generation:generation,opening_allowed:allowed,model}));
}
reply(true);assert.strictEqual(published.length,1);assert.deepStrictEqual(published[0],[model,true]);assert.deepStrictEqual(opening,[]);
tick();reply(false);assert.deepStrictEqual(published[1],[model,false]);assert.deepStrictEqual(opening,[]);
tick();reply(true,"78");assert.deepStrictEqual(published[2],[null]);assert.deepStrictEqual(opening,[false]);
tick();reply(true);assert.deepStrictEqual(published[3],[null,false]); // Uncertain transport stays closed.
tick();const delayed=pending.shift();root.__cr_loadout_transport_v1.retire();
const retiredCount=published.length;delayed.ok(JSON.stringify({kind:"model",view_generation:"77",opening_allowed:true,model}));
assert.strictEqual(published.length,retiredCount);assert.strictEqual(cleared,1);assert.strictEqual(host,null);
console.log("PASS: model publication carries opening state once; busy/stale/uncertain/retired replies and physical sample payload remain guarded");
