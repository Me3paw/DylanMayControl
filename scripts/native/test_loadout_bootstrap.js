setTimeout(async function(){"use strict";
 var passed=[],result;
 function check(value,name){if(!value)throw new Error(name);if(passed.indexOf(name)<0)passed.push(name);}
 try{
  // Pinned cohtml.WindowsDesktop.dll binds no click() on Element/HTMLElement; Chrome does.
  Object.defineProperty(HTMLElement.prototype,'click',{value:undefined,configurable:true,writable:true});
  check(typeof document.createElement('button').click!=='function','Fixture matches Cohtml: buttons have no click()');
  document.body.style.cssText='margin:0;background:#171717;color:#e8e8e8;font:18px Arial;';
 window.ui_menu_navigation_loadout_columns={value:16};window.ui_menu_navigation_controls_columns={value:1};window.ui_menu_navigation_gameplay_columns={value:1};
 var api=window.__cr_loadout_v1,navCells={},current=1,callbacks={},calls=[],browse=0,owner="loadout";
  window.ui_menu_navigation_loadout_id={value:"loadout"};window.ui_loadout_melee_in_loadout_visible={value:true};window.ui_input_active_controller_menu_mouse={value:false};
  for(var i=0;i<3;i++){window['hud_loadout_melee_'+i+'_id']={value:i+1};window['hud_loadout_melee_'+i+'_sort_order']={value:i};window['hud_loadout_melee_'+i+'_cell_size']={value:{x:1,y:1}};window['hud_loadout_melee_'+i+'_locked']={value:false};navCells[i+1]={disabled:false};}
  var exited=0,browserReturns=0;window.ui_menu_navigation_loadout_stack={value:"game"};
  window.stack={back:function(name){check(name==="game","Exit uses native game stack");if(window.ui_loadout_melee_in_browser_is_here && window.ui_loadout_melee_in_browser_is_here.value){browserReturns++;window.ui_loadout_melee_in_browser_is_here.value=false;window.ui_loadout_melee_in_loadout_visible.value=true;owner='loadout';api.setOpeningAllowed(true);}else exited++;},globalState:function(context){return context===owner;}};
  window.navigation={current:function(ctx,layout){check(ctx===owner&&layout===0,'Native navigation owner and layout');return {value:current};},setCurrent:function(ctx,id,layout){current=id;},setCell:function(value){check(value.__Type==='NavigationSetCellArguments'&&value.m_contextId===owner&&value.m_layout===0,'Native cell payload');check(value.m_width>=1&&Number.isInteger(value.m_width)&&value.m_sortOrder>=0,'Native cells have positive integer widths and nonnegative sort order');navCells[value.m_cellId]={disabled:value.m_disabled,width:value.m_width,sort:value.m_sortOrder};},removeCell:function(ctx,id){delete navCells[id];}};
  window.engine={on:function(name,callback){callbacks[name]=callback;},off:function(name,callback){if(callbacks[name]===callback)delete callbacks[name];},trigger:function(name,destination){check(name==='loadout_slot_click'&&destination===1,'Slot opens native Primary page with actual slot ID');window.ui_loadout_melee_in_browser_is_here={value:true};window.ui_loadout_melee_in_loadout_visible.value=false;api.setOpeningAllowed(false);owner='loadout_browser_melee';}};
  var native=document.createElement('section');native.className='melee-loadout';native.style.cssText='pointer-events:none;width:43vh;font:18px Arial;color:#e8e8e8;margin:12vh 0 0 8vh;';
  var heading=document.createElement('h2');heading.textContent='Weapon Forms';native.appendChild(heading);
  var stock=document.createElement('div');stock.className='melee-loadout__container';
  for(i=0;i<3;i++){var button=document.createElement('button');button.className='melee-loadout-cell';button.textContent='Native slot';button.onclick=function(){browse++;};stock.appendChild(button);}native.appendChild(stock);document.body.appendChild(native);
  check(!document.getElementById('cr-loadout-open'),'Popup opener is retired');
  var model={world_epoch:'1',adapter_bound:true,configuration_bound:true,option_count:4,options:[],channels:[]};
  var prefix='coui://base/textures/uiresources/UI/streamed/loadout/melee_slot/';
  var ids=[10,20,30,40],names=['Strike','Glaive','Crush','Drill'],icons=['flurry','slice','crush','drill'];
  for(i=0;i<4;i++)model.options.push({item:{channel:i<2?0:1,id:ids[i]},name:names[i],icon:prefix+'weapon_form_'+icons[i]+'_small.png',enabled:true,validation:{status:1,selected:false}});
  for(var channel=0;channel<2;channel++){var data={ticket:'0',selection_bound:true,active_id:ids[channel*2],active_name:names[channel*2],active_icon:model.options[channel*2].icon,slots:[]};for(var slot=0;slot<3;slot++)data.slots.push({slot:{assigned:true,id:ids[channel*2]},name:names[channel*2],enabled:true,validation:{status:1,selected:slot===0}});model.channels.push(data);}
  var menuOwned=false,nativeMenuOpen=false;var host={menu:function(value){nativeMenuOpen=value;},navigation:function(value){menuOwned=value;},send:function(request,reply){calls.push(JSON.parse(request));reply({world_epoch:model.world_epoch,result:2});return true;},onClose:function(){throw Error('Inline region cannot close the native menu');},controls:function(){},panel:function(){},surfaceProbe:function(stage){surfaces.push(stage);}};var surfaces=[];
  api.publish(model);api.setOpeningAllowed(true);
  check(!document.getElementById('cr-loadout-panel'),'Host absence prevents inline controls');
  var panelUpdates=0,createPanel=window.CRLoadoutPanel.create;
  window.CRLoadoutPanel.create=function(){var created=createPanel.apply(this,arguments),update=created.update;created.update=function(){panelUpdates++;return update.apply(created,arguments);};return created;};
  api.setHost(host);
  var beforePublish=panelUpdates;api.publish(model,true);
  check(panelUpdates===beforePublish+1,'Combined publication updates an existing panel exactly once');
  api.publish(model,false);
  check(!document.getElementById('cr-loadout-panel')&&stock.style.display===''&&!nativeMenuOpen,'Combined publication closes Loadout and releases its menu ownership');
  api.publish(model,true);
  var panel=document.getElementById('cr-loadout-panel');check(panel&&panel.parentNode===native&&panel.getAttribute('role')==='region'&&!panel.hasAttribute('aria-modal'),'Controls extend native Loadout without a modal');
  check(stock.style.display==='none'&&!document.querySelector('.cr-native-browse'),'Useless browser toolbar removed');
  check(document.querySelector('.cr-loadout-settings-toggle').style.display==='none','Loadout has no embedded settings toggle');
  check(getComputedStyle(document.querySelector('.cr-loadout-assignment')).pointerEvents==='auto','Mouse hit testing overrides native ancestor blocker');
  var body=panel;
  var rows=panel.querySelectorAll('.cr-loadout-row');
  check(rows.length===6,'All three slots in each channel are present');
  for(i=0;i<rows.length;i++)check(rows[i].getBoundingClientRect().bottom<=body.getBoundingClientRect().bottom+1,'All slot rows fit within the native body: row='+rows[i].getBoundingClientRect().bottom+', panel='+body.getBoundingClientRect().bottom+', viewport='+innerHeight);
  check(callbacks.OnSelect&&Object.keys(navCells).length>3,'Controller uses native OnSelect and registered cells');
  for(var nc in navCells)if(Number(nc)>=1129467136)check(navCells[nc].width<=16/3&&navCells[nc].width>16/4,'Loadout navigation packs3cells per native row');
  function focus(button){api.publish(model);var found=null;for(var id in navCells){if(!navCells[id].disabled){current=Number(id);api.publish(model);if(button.classList.contains('cr-native-focused')){found=current;break;}}}check(found!==null,'Requested control is reachable by native navigation');current=found;return found;}
  function pulse(edge){model.bindings=Object.assign({},model.bindings||{},{pad_buttons:0,pad_presses:[]});api.publish(model);model.bindings.pad_buttons=edge;model.bindings.pad_presses=[edge];api.publish(model);model.bindings.pad_buttons=0;model.bindings.pad_presses=[];api.publish(model);}
  function select(button){focus(button);pulse(4096);}
  window.stack.globalState=function(){return false;};callbacks.OnSelect();check(browse===0,'Inactive native stack cannot activate stale mod selection');window.stack.globalState=function(context){return context===owner;};
  check(panel.querySelectorAll('.cr-loadout-slot-strip').length===2,'Primary and secondary each have a horizontal three-slot strip');
  if(innerHeight<=800){check(parseFloat(getComputedStyle(panel.querySelector('.cr-loadout-slot')).fontSize)>=11&&parseFloat(getComputedStyle(panel.querySelector('.cr-loadout-name')).fontSize)>=12,'Compact slot typography retains11/12px minimum at720p');}
  check(panel.scrollHeight<=panel.clientHeight+1&&getComputedStyle(panel).overflowY==='hidden','Both weapon rows fit with no mouse scrolling');
  var squares=panel.querySelectorAll('.cr-loadout-assignment');
  for(var sq=0;sq<squares.length;sq++){
    var bounds=squares[sq].getBoundingClientRect(),labelBox=squares[sq].parentNode.querySelector('.cr-loadout-slot').getBoundingClientRect();
    check(bounds.top-labelBox.bottom>=0&&bounds.top-labelBox.bottom<=12,'Slot label stays immediately above square');
    check(bounds.bottom<=panel.getBoundingClientRect().bottom+1,'Every square stays visible inside the fixed panel');
    if(sq%3)check(Math.abs(bounds.top-squares[sq-1].getBoundingClientRect().top)<1,'Three squares align horizontally');
  }
  var square=panel.querySelector('.cr-loadout-assignment').getBoundingClientRect();check(Math.abs(square.width-square.height)<1&&square.width<=96,'Slot has explicit bounded square dimensions');
  var pri3=document.querySelector('[aria-label="Change primary slot 3"]'),sec3=document.querySelector('[aria-label="Change secondary slot 3"]');
  focus(pri3);pulse(2);check(sec3.classList.contains('cr-native-focused'),'Down at primary3 stays in column3 and selects secondary3');
  pulse(2);check(sec3.classList.contains('cr-native-focused'),'Down at secondary3 clamps instead of going to Abilities');
  pulse(1);check(pri3.classList.contains('cr-native-focused'),'Up at secondary3 selects primary3');pulse(1);check(pri3.classList.contains('cr-native-focused'),'Up at primary3 clamps');
  var ability=document.createElement('button');ability.textContent='Native Ability';ability.style.cssText='position:absolute;left:70vh;top:40vh;width:10vh;height:10vh;';ability.setAttribute('data-bind-rmd-nav-cell-ctx','{{ui_menu_navigation_loadout_id.value}}');ability.setAttribute('data-bind-rmd-nav-cell-id','{{test_native_ability_id.value}}');window.test_native_ability_id={value:777};document.body.appendChild(ability);
  pulse(8);check(current===777,'Right from mod boundary hands focus to native Abilities');
  check(!menuOwned,'Native Ability selection releases mod input masking');var nativeCallCount=calls.length;pulse(4096);check(!menuOwned&&calls.length===nativeCallCount&&document.getElementById('cr-loadout-panel'),'A on native Ability does not click a stale mod slot');
  var artifact=document.createElement('button');artifact.textContent='Native Artifact';artifact.style.cssText='position:absolute;left:10vh;top:70vh;width:10vh;height:10vh;';artifact.setAttribute('data-bind-rmd-nav-cell-ctx','{{ui_menu_navigation_loadout_id.value}}');artifact.setAttribute('data-bind-rmd-nav-cell-id','{{test_native_artifact_id.value}}');window.test_native_artifact_id={value:888};document.body.appendChild(artifact);
  current=888;pulse(4096);check(!menuOwned&&calls.length===nativeCallCount,'External native Artifact focus also releases A masking');pulse(1);check(current>=1129467136&&menuOwned,'Up from native Artifact returns to mod slots');
  current=777;pulse(4);check(current>=1129467136&&menuOwned,'Left from native Abilities returns to mod slots');ability.remove();artifact.remove();
  focus(document.querySelector('[aria-label="Change primary slot 1"]'));
  model.bindings=Object.assign({},model.bindings||{},{pad_buttons:0,pad_presses:[]});api.publish(model,true);
  model.bindings.pad_buttons=4096;model.bindings.pad_presses=[4096];api.publish(model,true);
  check(!document.getElementById('cr-loadout-panel')&&nativeMenuOpen&&document.getElementById('cr-weapon-hud').style.display==='none','Combined A publication preserves synchronous picker handoff and final packet visibility');
  model.bindings.pad_buttons=0;model.bindings.pad_presses=[];api.publish(model,false);
  check(!document.getElementById('cr-loadout-panel')&&nativeMenuOpen,'Next browser packet retains native ownership without stale mod controls');
  check(!document.getElementById('cr-loadout-panel'),'Physical A opens the native picker once');check(window.__crmod_diagnostics===false?surfaces.length===0:surfaces.indexOf(3)>=0,'Physical A preserves activation with diagnostic reports controlled by build mode');
  check(!document.getElementById('cr-loadout-panel'),'Native browser owns the page during weapon selection');
  window.hud_loadout_browser_melee_0_id={value:501};window.hud_loadout_browser_melee_0_ability_id={value:20};window.hud_loadout_browser_melee_0_icon={value:model.options[1].icon};
  window.ui_menu_navigation_loadout_browser_melee_id={value:'loadout_browser_melee'};window.hud_loadout_browser_destination_slot={value:1};current=501;window.ui_input_active_controller_menu_mouse.value=true;api.publish(model);callbacks.OnSelect();await new Promise(function(resolve){setTimeout(resolve,0);});window.ui_input_active_controller_menu_mouse.value=false;
  check(calls.length===1&&calls[0].action===0&&calls[0].slot===0&&calls[0].id===20,'Native choice assigns only requested mod slot');
  model.channels[0].slots[0].slot.id=20;model.channels[0].slots[0].name='Glaive';api.publish(model,false);
  check(browserReturns===1&&!document.getElementById('cr-loadout-panel'),'Incoming opening state wins after synchronous native picker return');
  api.publish(model,true);
  check(browserReturns===1&&document.getElementById('cr-loadout-panel'),'Assignment confirmation returns from native picker to Loadout');
  select(document.querySelector('[aria-label="Change primary slot 2"]'));api.publish(model);
  window.ui_loadout_melee_in_browser_is_here.value=false;window.ui_loadout_melee_in_loadout_visible.value=true;owner='loadout';api.publish(model);api.setOpeningAllowed(true);
  callbacks.loadout_browser_slot_click(1,501);await new Promise(function(resolve){setTimeout(resolve,0);});check(calls.length===1,'Cancelled native picker never assigns a slot');
  select(document.querySelector('[aria-label="Change primary slot 3"]'));model.world_epoch='9';api.publish(model);callbacks.loadout_browser_slot_click(1,501);await new Promise(function(resolve){setTimeout(resolve,0);});check(calls.length===1,'Save epoch change invalidates pending native picker');
  window.ui_loadout_melee_in_browser_is_here.value=false;window.ui_loadout_melee_in_loadout_visible.value=true;owner='loadout';api.setOpeningAllowed(true);api.publish(model);
  document.dispatchEvent(new KeyboardEvent('keydown',{keyCode:27,which:27,bubbles:true}));check(exited===1,'Escape exits through native stack rather than a dormant close flag');
  api.setOpeningAllowed(false);check(!document.getElementById('cr-loadout-panel')&&stock.style.display==='','Leaving Loadout restores stock region');
  check(Object.keys(navCells).length===3&&!callbacks.OnSelect,'Only owned native cells and callback are removed');
  var hud=document.getElementById('cr-weapon-hud');check(hud.style.display!=='none','Gameplay HUD remains visible outside equipment');
  check(hud.querySelector('img').getAttribute('src')===window.CRWeaponIcons[model.channels[0].active_icon],'HUD retains embedded stock image');
  var stockIcons=Object.keys(window.CRWeaponIcons);check(stockIcons.length===6,'Six stock icons preserved');
  model.world_epoch='2';api.publish(model);check(hud.querySelectorAll('img')[1].getAttribute('src')===window.CRWeaponIcons[model.channels[1].active_icon],'HUD artwork persists after save change');
  var bindingValue={key_primary:33,key_secondary:34,key_alt_primary:0,key_alt_secondary:0,pad_primary:0,pad_secondary:0,loot_multiplier:3,saved:true},savedBindings=[],savedMargins=[];
  host.getBindings=function(done){done(bindingValue);};
  host.saveBindings=function(value,done){for(var key in value)bindingValue[key]=value[key];savedBindings.push(value);done(Object.assign({accepted:true},bindingValue));};
  host.saveLootMultiplier=function(value,done){bindingValue.loot_multiplier=value;savedMargins.push(value);done(Object.assign({accepted:true},bindingValue));};
  window.ui_stacks_game_states_system_menu_current={value:true};api.publish(model,false);check(hud.style.display==='none','Pause menu hides gameplay weapon HUD');
  check(nativeMenuOpen,'Native system menu suppresses gameplay swap input even without a mod panel');
  window.ui_stacks_game_states_system_menu_current.value=false;
  native.style.display='none';
  var controls=document.createElement('div');controls.className='options-group__options__items options-group__options__items--controls';controls.style.cssText='width:110vh;margin:20vh 0 0 7.4vh;';
  var stockSetting=document.createElement('button');stockSetting.textContent='Native Move Forward setting';controls.appendChild(stockSetting);document.body.appendChild(controls);
  var gameplay=document.createElement('div');gameplay.className='options-group__options__items options-group__options__items--gameplay';gameplay.style.cssText='display:none;width:78vh;margin:20vh 0 0 7.4vh;';var nativeGameplay=document.createElement('button');nativeGameplay.textContent='Native gameplay setting';gameplay.appendChild(nativeGameplay);document.body.appendChild(gameplay);
  window.ui_menu_navigation_controls_id={value:'controls'};window.ui_menu_navigation_gameplay_id={value:'gameplay'};
  window.ui_stacks_menu_options_active={value:true};window.ui_stacks_menu_options_states_controls_current={value:true};window.ui_stacks_menu_options_states_gameplay_current={value:false};owner='controls';api.publish(model);
  panel=document.getElementById('cr-loadout-panel');check(panel.parentNode===controls&&controls.firstChild===panel,'Mod bindings prepend inside native Controls scroll area');
  check(hud.style.display==='none','Options hides weapon HUD');
  check(panel.querySelectorAll('.cr-loadout-binding').length===2&&panel.querySelectorAll('.cr-loadout-binding-device').length===6&&!panel.querySelector('.cr-loadout-bind-clear')&&!panel.querySelector('.cr-loadout-binding-label').offsetWidth,'Two compact device columns replace stacked binding fields');
  check(panel.scrollHeight<=panel.clientHeight+1,'Bindings introduce no nested scrolling');
  var deviceFields=panel.querySelectorAll('.cr-loadout-binding-device');for(i=0;i<6;i+=3){var left=deviceFields[i].getBoundingClientRect(),right=deviceFields[i+2].getBoundingClientRect();check(Math.abs(left.top-right.top)<1&&left.right<=right.left+1,'Keyboard and controller fields share one non-overlapping row');}
  check(stockSetting.parentNode===controls&&!stockSetting.disabled,'Native controls remain unchanged');
  var keyMain=panel.querySelector('[aria-label="Assign primary keyboard / mouse"]'),keyAlt=panel.querySelector('[aria-label="Assign primary keyboard / mouse alternate"]'),keyPad=panel.querySelector('[aria-label="Assign primary controller"]');
  focus(keyMain);pulse(8);check(keyAlt.classList.contains('cr-native-focused'),'Controls Right reaches alternate keyboard field');pulse(8);check(keyPad.classList.contains('cr-native-focused'),'Controls Right reaches controller field');pulse(2);check(panel.querySelector('[aria-label="Assign secondary controller"]').classList.contains('cr-native-focused'),'Controls Down retains controller column');
  stockSetting.setAttribute('data-bind-rmd-nav-cell-ctx','{{ui_menu_navigation_controls_id.value}}');stockSetting.setAttribute('data-bind-rmd-nav-cell-id','({{ui_stacks_menu_options_active.value}} ? 0 : 0)');
  pulse(2);check(current===0&&!menuOwned,'Controls Down hands off to the first native option');pulse(4096);check(current===0&&!menuOwned&&panel.getAttribute('data-cr-capturing')!=='true','A on native setting cannot start a stale mod capture');
  pulse(1);check(current>=1129467136&&menuOwned,'Up from first native option reaches Mod controls');
  var savedEpoch=model.world_epoch;model.world_epoch='0';model.adapter_bound=false;model.configuration_bound=false;api.publish(model);select(keyMain);check(panel.getAttribute('data-cr-capturing')==='true','Options binding remains usable without a live player catalogue');panel.dispatchEvent(new KeyboardEvent('keydown',{keyCode:27,which:27,bubbles:true}));model.world_epoch=savedEpoch;model.adapter_bound=true;model.configuration_bound=true;api.publish(model);

  select(panel.querySelector('[aria-label="Assign primary keyboard / mouse"]'));check(panel.getAttribute('data-cr-capturing')==='true','Native controller navigation starts binding capture');
  callbacks.OnSelect();check(panel.getAttribute('data-cr-capturing')==='true','Native select is ignored while panel itself captures input');
  window.ui_input_active_controller_menu_mouse.value=true;panel.dispatchEvent(new KeyboardEvent('keydown',{keyCode:27,which:27,bubbles:true}));select(panel.querySelector('[aria-label="Assign primary keyboard / mouse"]'));check(panel.getAttribute('data-cr-capturing')==='true','A/Cross confirmation is not blocked by sticky mouse mode');window.ui_input_active_controller_menu_mouse.value=false;
  panel.dispatchEvent(new KeyboardEvent('keydown',{keyCode:36,which:36,bubbles:true}));check(savedBindings.length===1&&savedBindings[0].key_primary===36&&savedBindings[0].key_secondary===34,'Binding transport preserves independent secondary and controller values');
  select(panel.querySelector('[aria-label="Assign primary keyboard / mouse alternate"]'));panel.dispatchEvent(new KeyboardEvent('keydown',{keyCode:35,which:35,bubbles:true}));check(bindingValue.key_alt_primary===35&&bindingValue.key_primary===36,'Independent alternate key is saved without replacing main');
  select(panel.querySelector('[aria-label="Assign primary keyboard / mouse alternate"]'));panel.dispatchEvent(new KeyboardEvent('keydown',{keyCode:46,which:46,bubbles:true}));check(bindingValue.key_alt_primary===0&&bindingValue.key_primary===36,'Delete clears only selected alternate field');
  select(panel.querySelector('[aria-label="Assign primary keyboard / mouse"]'));panel.dispatchEvent(new KeyboardEvent('keydown',{keyCode:27,which:27,bubbles:true}));check(bindingValue.key_primary===36&&panel.getAttribute('data-cr-capturing')==='false','Escape cancels binding without changing it');
  select(panel.querySelector('[aria-label="Assign primary controller"]'));
  model.bindings=Object.assign({pad_buttons:0,pad_presses:[]},bindingValue);api.publish(model);
  model.bindings=Object.assign({},bindingValue,{pad_buttons:8192,pad_presses:[8192]});api.publish(model);
  check(panel.getAttribute('data-cr-capturing')==='false'&&bindingValue.pad_primary===0,'Controller Back cancels capture without assignment');
  select(panel.querySelector('[aria-label="Assign primary controller"]'));model.bindings=Object.assign({},bindingValue,{pad_buttons:0,pad_presses:[]});api.publish(model);
  model.bindings=Object.assign({},bindingValue,{pad_buttons:512,pad_presses:[512]});api.publish(model);check(bindingValue.pad_primary===512,'Controller fresh button capture preserves keyboard fields');
  select(panel.querySelector('[aria-label="Assign primary controller"]'));model.bindings=Object.assign({},bindingValue,{pad_buttons:0,pad_presses:[]});api.publish(model);
  model.bindings=Object.assign({},bindingValue,{pad_buttons:16,pad_presses:[16]});api.publish(model);check(bindingValue.pad_primary===0&&bindingValue.key_primary===36,'Controller Start clears selected controller field');
  delete model.bindings;
  window.ui_stacks_menu_options_states_controls_current.value=false;window.ui_stacks_menu_options_states_gameplay_current.value=true;controls.style.display='none';gameplay.style.display='flex';owner='gameplay';api.publish(model);
  panel=document.getElementById('cr-loadout-panel');check(panel.parentNode===gameplay&&gameplay.firstChild===panel,'Loot multiplier lives under Mod in native Gameplay');
  check(!panel.querySelector('.cr-loadout-binding').offsetWidth&&panel.querySelectorAll('.cr-loadout-margin-choices button').length===3,'Gameplay shows loot control with stomp setting removed');
  select(panel.querySelector('.cr-loot-plus'));check(savedMargins[0]===4&&bindingValue.key_primary===36,'Loot multiplier preserves bindings');
  check(nativeGameplay.parentNode===gameplay&&!nativeGameplay.disabled,'Native gameplay controls preserved');
  window.ui_stacks_menu_options_active.value=false;api.publish(model);check(!document.getElementById('cr-loadout-panel')&&hud.style.display!=='none','Leaving Options removes owned UI and restores gameplay HUD');
  if(window.__cr_test_controls){window.ui_stacks_menu_options_active.value=true;window.ui_stacks_menu_options_states_controls_current.value=true;window.ui_stacks_menu_options_states_gameplay_current.value=false;gameplay.style.display='none';controls.style.display='flex';owner='controls';api.publish(model);}
  else if(window.__cr_test_contact){window.ui_stacks_menu_options_active.value=true;owner='gameplay';api.publish(model);}
  else if(!window.__cr_test_hud){controls.style.display=gameplay.style.display='none';native.style.display='';owner='loadout';api.setOpeningAllowed(true);api.publish(model);}
  // Model APIs exist in the live game, but appended navigation attributes do
  // not reliably attach. Exercise the physical path with those APIs present,
  // without the old fixture's artificial handler initialization.
  api.setOpeningAllowed(false);api.setHost(null);owner='loadout';
  window.ui_stacks_menu_options_active.value=false;controls.style.display=gameplay.style.display='none';native.style.display='';
  window.engine.createJSModel=function(name,value){window[name]=value;};
  window.engine.updateWholeModel=function(){};
  model.bindings=Object.assign({},bindingValue,{pad_buttons:0,pad_presses:[]});
  api.setHost(host);api.setOpeningAllowed(true);api.publish(model);
  panel=document.getElementById('cr-loadout-panel');
  focus(panel.querySelector('[aria-label="Change primary slot 1"]'));api.publish(model);
  pulse(4096);check(!document.getElementById('cr-loadout-panel'),'Physical A opens picker with model APIs present and no attribute initialization');
  window.ui_loadout_melee_in_browser_is_here.value=false;window.ui_loadout_melee_in_loadout_visible.value=true;owner='loadout';
  api.setOpeningAllowed(true);api.publish(model);api.setOpeningAllowed(false);api.setHost(null);
  check(Object.keys(navCells).length===3,'Native cells are restored after physical picker lifecycle');
  await new Promise(function(resolve){setTimeout(resolve,160);});
  owner='controls';controls.style.display='';native.style.display='none';window.ui_stacks_menu_options_active.value=true;
  window.ui_stacks_menu_options_states_controls_current.value=true;window.ui_stacks_menu_options_states_gameplay_current.value=false;
  model.bindings=Object.assign({},bindingValue,{pad_buttons:0,pad_presses:[],key_presses:[],capture_releasing:false});
  api.setHost(host);api.publish(model);panel=document.getElementById('cr-loadout-panel');
  focus(panel.querySelector('[aria-label="Assign primary keyboard / mouse"]'));api.publish(model);
  // Simulate native movement happening before the asynchronous physical queue.
  current=0;model.bindings.pad_buttons=8;model.bindings.pad_presses=[8];api.publish(model);
  check(panel.querySelector('[aria-label="Assign primary keyboard / mouse alternate"]').classList.contains('cr-native-focused'),'A native focus race cannot steal horizontal Mod navigation');
  model.bindings.pad_buttons=0;model.bindings.pad_presses=[];api.publish(model);pulse(8);
  check(panel.querySelector('[aria-label="Assign primary controller"]').classList.contains('cr-native-focused'),'Right reaches controller field with model APIs present');
  pulse(8);check(panel.querySelector('[aria-label="Assign primary controller"]').classList.contains('cr-native-focused'),'Right at Mod field boundary clamps');
  focus(panel.querySelector('[aria-label="Assign primary keyboard / mouse"]'));api.publish(model);
  model.bindings.sample_sequence='400';model.bindings.pad_buttons=8;model.bindings.pad_presses=[8];api.publish(model,false);api.publish(model,false);
  check(panel.querySelector('[aria-label="Assign primary keyboard / mouse alternate"]').classList.contains('cr-native-focused'),'Repeated model delivery moves exactly one field');
  delete model.bindings.sample_sequence;model.bindings.pad_buttons=0;model.bindings.pad_presses=[];api.publish(model);pulse(8);
  focus(panel.querySelector('[aria-label="Assign primary keyboard / mouse"]'));api.publish(model);
  model.bindings.pad_buttons=8;model.bindings.pad_presses=[8,8];api.publish(model);
  check(panel.querySelector('[aria-label="Assign primary controller"]').classList.contains('cr-native-focused'),'Two quick queued taps advance two fields');
  model.bindings.pad_buttons=0;model.bindings.pad_presses=[];api.publish(model);


  pulse(4096);check(panel.getAttribute('data-cr-capturing')==='true','Physical A starts controller capture');
  model.bindings.pad_buttons=0;model.bindings.pad_presses=[];api.publish(model);pulse(8192);
  check(panel.getAttribute('data-cr-capturing')==='false','Physical Back cancels controller capture');
  focus(panel.querySelector('[aria-label="Assign primary keyboard / mouse"]'));api.publish(model);pulse(4096);
  model.bindings=Object.assign({},bindingValue,{pad_buttons:0,pad_presses:[],key_presses:[90]});api.publish(model);
  check(bindingValue.key_primary===90&&panel.getAttribute('data-cr-capturing')==='false','Physical key queue assigns a keyboard field');
  api.setHost(null);check(!navCells[0]||navCells[0].sort===undefined||navCells[0].sort===0,'Native Options row sort remains preserved');
  api.setHost(host);window.ui_stacks_menu_options_states_controls_current.value=false;controls.style.display='none';api.publish(model);
  check(!document.getElementById('cr-loadout-panel')&&nativeMenuOpen,'Other native Options tabs retain menu input ownership without mod fields');
  // Leave the requested surface mounted for the runner's visual QA screenshot.
  window.ui_stacks_menu_options_active.value=false;controls.style.display=gameplay.style.display='none';native.style.display='';owner='loadout';
  api.setHost(host);api.setOpeningAllowed(!window.__cr_test_hud);api.publish(model);
  if(window.__cr_test_controls||window.__cr_test_contact){
    api.setOpeningAllowed(false);native.style.display='none';
    window.ui_stacks_menu_options_active.value=true;window.ui_stacks_menu_options_states_controls_current.value=!!window.__cr_test_controls;window.ui_stacks_menu_options_states_gameplay_current.value=!!window.__cr_test_contact;
    owner=window.__cr_test_controls?'controls':'gameplay';controls.style.display=window.__cr_test_controls?'':'none';gameplay.style.display=window.__cr_test_contact?'':'none';api.publish(model);
  }
  window.scrollTo(0,0);result={ok:true,passed:passed};
 }catch(error){result={ok:false,passed:passed,error:String(error.stack||error)};}
 var out=document.createElement('pre');out.id='cr-test-result';out.textContent=JSON.stringify(result);out.style.display='none';document.body.appendChild(out);
},0);
