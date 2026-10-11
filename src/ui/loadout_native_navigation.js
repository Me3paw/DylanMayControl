/* Native navigation API and model names from the pinned game's UI bundle. */
(function(root){"use strict";
    var BASE=1129467136,lastDelivery=null;
    function scalar(name){var model=root[name];if(typeof model==="number" || typeof model==="string")return model;return model && (typeof model.value==="number" || typeof model.value==="string")?model.value:null;}
    function currentValue(context){var v=root.navigation.current(context,0);return v && typeof v==="object"?v.value:v;}
    function valid(value){return typeof value==="number" && isFinite(value) && Math.floor(value)===value && value>=0 && value<=2147483647;}
    function create(region,report,options){
        options=options||{};var contextModel=options.contextModel||"ui_menu_navigation_loadout_id";
        var listening=false,lastMissing=-1,context=null,cells={},keys={},next=0,closed=false,signature="",stock=[],previous=null;
        var lastOwned=null,rawEnabled=false,baseline=false,previousPad=0,released=false,ownershipSent=null,lastSampled=null,lastProbe='';
        // Physical mod input is sampled before the plugin filters native XInput.
        // Native cells still own focus outside this region; OnSelect also serves keyboard input.
        function capturing(){return region.getAttribute('data-cr-capturing')==='true'||!!region.querySelector('[data-cr-capturing="true"]');}
        function available(){return root.navigation && typeof root.navigation.setCell==="function" && typeof root.navigation.removeCell==="function" && typeof root.navigation.current==="function" && typeof root.navigation.setCurrent==="function" && root.engine && typeof root.engine.on==="function" && typeof root.engine.off==="function";}
        function registered(id,sort,disabled,width,height){
            root.navigation.setCell({__Type:"NavigationSetCellArguments",m_contextId:context,m_cellId:id,m_sortOrder:sort,m_width:width||1,m_height:height||1,m_disabled:disabled,m_layout:0});
        }
        function clear(){
            if(context!==null && available()){
                for(var id in cells) if(Object.prototype.hasOwnProperty.call(cells,id)) root.navigation.removeCell(context,Number(id));
                for(var i=0;i<stock.length;i++) registered(stock[i].id,stock[i].sort,stock[i].disabled,stock[i].width,stock[i].height);
                if(root.stack && typeof root.stack.globalState==="function" && root.stack.globalState(context) && cells[currentValue(context)] && previous!==null) root.navigation.setCurrent(context,previous,0);
            }
            cells={};stock=[];context=null;signature="";
        }
        function sync(){
            if(closed) return false;
            if(!available()){if(report && lastMissing!==0){lastMissing=0;report(0,0,0);}return false;}
            if(!listening){root.engine.on("OnSelect",select);listening=true;}
            var id=scalar(contextModel);if(!(typeof id==="string" && id.length>0 && id.length<=64) && !valid(id)){if(report && lastMissing!==1){lastMissing=1;report(1,0,typeof root[contextModel]==="undefined"?0:typeof root[contextModel]==="object"?1:typeof root[contextModel]==="number"?2:3);}return false;}
            if(context!==id){clear();context=id;previous=currentValue(context);
                for(var i=0;!options.contextModel && i<3;i++){
                    var prefix="hud_loadout_melee_"+i+"_",cell=scalar(prefix+"id"),order=scalar(prefix+"sort_order"),size=root[prefix+"cell_size"],locked=root[prefix+"locked"];
                    if(valid(cell) && valid(order) && size && size.value && typeof size.value.x==="number" && typeof size.value.y==="number") stock.push({id:cell,sort:order,width:size.value.x,height:size.value.y,disabled:!!(locked && locked.value)});
                }
            }
            var buttons=region.querySelectorAll("button"),active={},parts=[];
            for(var j=0;j<buttons.length;j++){
                var button=buttons[j];if(!button.offsetWidth || !button.offsetHeight) continue;
                var key=button.getAttribute("data-cr-nav-key")||button.getAttribute("aria-label")||button.textContent;
                if(!Object.prototype.hasOwnProperty.call(keys,key)){if(next>=192) continue;keys[key]=BASE+next++;}
                var cellID=keys[key];active[cellID]=button;parts.push(cellID+":"+j+":"+(button.disabled?1:0));
            }
            var current=currentValue(context),sig=parts.join("|");
            if(sig!==signature){
                for(var old in cells) if(Object.prototype.hasOwnProperty.call(cells,old) && !active[old]) root.navigation.removeCell(context,Number(old));
                for(var s=0;s<stock.length;s++) registered(stock[s].id,stock[s].sort,true,stock[s].width,stock[s].height);
                var columns=scalar(contextModel.replace(/_id$/,"_columns")),width=Math.max(1,Math.floor((typeof columns==="number" && columns>0 && columns<=64?columns:options.contextModel?1:16)/3));
                var sort=1000;for(var cell in active) if(Object.prototype.hasOwnProperty.call(active,cell)) registered(Number(cell),sort++,active[cell].disabled,width,1);
                if(!active[current] && (cells[current] || stock.some(function(value){return value.id===current;}) || (options.contextModel && signature==="" && root.stack && root.stack.globalState(context)))){
                    for(var first in active) if(Object.prototype.hasOwnProperty.call(active,first) && !active[first].disabled){root.navigation.setCurrent(context,Number(first),0);break;}
                }
                cells=active;signature=sig;if(report) report(2,typeof context==="number"?context:0,parts.length);
            }
            current=currentValue(context);
            if(cells[current] && !cells[current].disabled){lastOwned=Number(current);released=false;}
            else {lastOwned=null;released=true;}
            var ownsInput=root.stack && typeof root.stack.globalState==="function" && root.stack.globalState(context);
            if(options.contextModel&&ownsInput&&cells[current])root.focusedOptionName=''; // Prevent stale native rows from acting on a mod field.
            for(var cid in cells) if(Object.prototype.hasOwnProperty.call(cells,cid)){
                var node=cells[cid],focused=ownsInput && Number(cid)===current;
                node.classList.toggle("cr-native-focused",focused);
                if(focused && !(root.ui_input_active_controller_menu_mouse && root.ui_input_active_controller_menu_mouse.value===true) && document.activeElement!==node){node.focus();if(node.scrollIntoView) node.scrollIntoView({block:"nearest"});}
            }
            ownership(!!cells[current] && !cells[current].disabled && ownsInput);
            return true;
        }
        function select(){
            if(rawEnabled && (previousPad&4096))return;
            if(closed || context===null || !available()) return;
            var active=scalar(contextModel),current=currentValue(context),button=cells[current];

            var capture=region.getAttribute("data-cr-capturing")==="true" || region.querySelector("[data-cr-capturing=\"true\"]");if(capture) return;
            if(!root.stack || typeof root.stack.globalState!=="function" || !root.stack.globalState(context)) return;
            if(active!==context || !button || button.disabled || !button.offsetWidth || !button.offsetHeight || !document.body.contains(region)) return;
            activate(button);if(report) report(3,typeof context==="number"?context:0,Object.keys(cells).length);sync();
        }
        // The game's Cohtml binds no HTMLElement.click(); run the button's own handler.
        function activate(button){if(typeof button.onclick==="function")button.onclick.call(button);else if(typeof button.click==="function")button.click();}
        function ownership(value){if(ownershipSent!==value){ownershipSent=value;if(typeof options.ownership==="function")options.ownership(value);}}
        function probe(edge){
            if(typeof options.probe!=='function' || context===null)return;
            var id=currentValue(context),active=!!(root.stack && root.stack.globalState(context)),owned=!!cells[id] && !cells[id].disabled && active;
            id=valid(id)?id:2147483647;var signature=id+':'+active+':'+owned;
            if(edge || signature!==lastProbe){lastProbe=signature;options.probe(contextModel==='ui_menu_navigation_controls_id'?1:contextModel==='ui_menu_navigation_gameplay_id'?2:0,id,edge||0,active,owned);}
        }
        function focus(id){if(!cells[id] || cells[id].disabled)return;lastOwned=Number(id);released=false;root.navigation.setCurrent(context,lastOwned,0);cells[id].focus();sync();}
        function nativeID(node){
            var expression=node.getAttribute('data-bind-rmd-nav-cell-id')||'',match=expression.match(/\{\{([a-zA-Z0-9_]+)\.value\}\}/),id=match?scalar(match[1]):null;
            if(!valid(id)){var fixed=expression.match(/\?\s*(\d+)\s*:\s*(\d+)/);if(fixed && fixed[1]===fixed[2])id=Number(fixed[1]);}
            return valid(id)?id:null;
        }
        function nativeCells(){
            var nodes=document.querySelectorAll('[data-bind-rmd-nav-cell-id]'),result=[];
            for(var i=0;i<nodes.length;i++){
                var node=nodes[i],ctx=node.getAttribute('data-bind-rmd-nav-cell-ctx')||'';
                var style=root.getComputedStyle(node),disabled=node.getAttribute('data-bind-rmd-nav-cell-disabled')||'',match=disabled.match(/^\{\{([a-zA-Z0-9_]+)\.value\}\}$/),locked=match && root[match[1]] && root[match[1]].value===true;
                if(!region.contains(node) && node.offsetWidth && node.offsetHeight && style.visibility!=='hidden' && !node.disabled && !locked && !node.classList.contains('loadout-button--unfocused') && !node.classList.contains('charms-loadout-cell--inert') && ctx.indexOf(contextModel)>=0 && nativeID(node)!==null)result.push(node);
            }
            return result;
        }
        function nativeEntry(edge,from){
            var code=edge&1?38:edge&4?37:0;
            if(!code || (options.contextModel && code!==38))return;
            var nodes=nativeCells(),origin=null;
            for(var i=0;i<nodes.length;i++)if(nativeID(nodes[i])===from){origin=nodes[i];break;}
            if(!origin)return;
            var box=origin.getBoundingClientRect(),x=(box.left+box.right)/2,y=(box.top+box.bottom)/2;
            // Native Options owns its ordinary rows; enter Mod only above the first one.
            if(options.contextModel)for(i=0;i<nodes.length;i++){var other=nodes[i].getBoundingClientRect();if((other.top+other.bottom)/2<y-1)return;}
            var best=null,score=Infinity;
            for(var id in cells){var button=cells[id];if(button.disabled)continue;var target=button.getBoundingClientRect(),dx=(target.left+target.right)/2-x,dy=(target.top+target.bottom)/2-y;
                if((code===37 && dx>=-1)||(code===38 && dy>=-1))continue;
                var distance=code===37?Math.abs(dx)+Math.abs(dy)*8:Math.abs(dy)+Math.abs(dx)*8;
                if(distance<score){score=distance;best=Number(id);}
            }
            if(best!==null)focus(best);
        }
        function nativeExit(button,code){
            var origin=button.getBoundingClientRect(),nodes=nativeCells(),best=null,score=Infinity;
            for(var i=0;i<nodes.length;i++){
                var node=nodes[i],id=nativeID(node);
                var box=node.getBoundingClientRect(),dx=(box.left+box.right-origin.left-origin.right)/2,dy=(box.top+box.bottom-origin.top-origin.bottom)/2;
                if((code===39&&dx<=1)||(code===40&&dy<=1))continue;
                var distance=code===39?Math.abs(dx)+Math.abs(dy)*3:Math.abs(dy)+Math.abs(dx)*3;
                if(distance<score){score=distance;best=id;}
            }
            if(best!==null){released=true;lastOwned=null;ownership(false);root.navigation.setCurrent(context,best,0);}
        }
        function press(edge,from){
            if(closed||context===null||!root.stack||!root.stack.globalState(context))return;
            if(capturing())return;
            if(root.ui_options_keybinds_rebinding_awaiting_input && root.ui_options_keybinds_rebinding_awaiting_input.value===true)return;
            var id=currentValue(context),button=cells[id];
            // The native menu may have moved before the asynchronous model RPC.
            // Use the last sampled physical focus for this edge when it belonged to Mod.
            if(cells[from] && edge&0x100f){id=from;button=cells[id];focus(id);}
            if(!button){nativeEntry(edge,from);return;}
            var code=edge&1?38:edge&2?40:edge&4?37:edge&8?39:0;
            if(code){
                var from=button.getBoundingClientRect(),best=null,score=Infinity;
                for(var candidate in cells){var target=cells[candidate];if(target.disabled||target===button)continue;var box=target.getBoundingClientRect(),dx=(box.left+box.right-from.left-from.right)/2,dy=(box.top+box.bottom-from.top-from.bottom)/2;
                    if((code===37&&dx>=-1)||(code===39&&dx<=1)||(code===38&&dy>=-1)||(code===40&&dy<=1))continue;
                    var distance=(code===37||code===39)?Math.abs(dx)+Math.abs(dy)*8:Math.abs(dy)+Math.abs(dx)*8;
                    if(distance<score){score=distance;best=Number(candidate);}
                }
                if(best!==null)focus(best);else if((!options.contextModel&&code===39)||(options.contextModel&&code===40))nativeExit(button,code);else focus(id);
            }
            if(edge&4096){focus(lastOwned===null?id:lastOwned);button=cells[lastOwned];if(button&&!button.disabled){activate(button);if(report)report(3,typeof context==='number'?context:0,Object.keys(cells).length);sync();}}
        }
        function input(bindings){
            if(!bindings || typeof bindings.pad_buttons!=='number')return;
            if(!sync())return; // Native abilities/artifacts can change focus without a mod arrow.
            if(typeof bindings.sample_sequence==='string'){if(lastDelivery===bindings.sample_sequence){probe(0);return;}lastDelivery=bindings.sample_sequence;}
            rawEnabled=true;var value=bindings.pad_buttons,edges=bindings.pad_presses;
            if(bindings.capture_releasing===true){previousPad=value;baseline=true;lastSampled=currentValue(context);probe(0);return;}
            if(Array.isArray(edges)){previousPad=value;baseline=true;for(var i=0;i<edges.length&&i<32&&!closed&&context!==null;i++){press(edges[i],lastSampled===null?currentValue(context):lastSampled);if(!closed&&context!==null)lastSampled=currentValue(context);}}
            else if(!baseline){previousPad=value;baseline=true;}
            else{var edge=value&~previousPad;previousPad=value;press(edge,lastSampled===null?currentValue(context):lastSampled);}
            if(closed || context===null)return; // A can destroy this adapter while opening the native picker.
            lastSampled=currentValue(context);
            if(typeof options.probe==='function')probe(Array.isArray(edges)?edges.reduce(function(all,value){return all|value;},0):value);
        }
        return {sync:sync,input:input,close:function(){if(closed)return;closed=true;ownership(false);if(listening && root.engine && root.engine.off)root.engine.off("OnSelect",select);clear();}};
    }
    root.CRLoadoutNativeNavigation={create:create};
}(window));
