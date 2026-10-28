'use strict';
// Headless UI state regression: this is not a WebView2 renderer or native Windows build.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const sent=[],els=new Map(),events={};
function fake(){const classes=new Set(),handlers={};return {
 __handlers:handlers,style:{},dataset:{},textContent:'',value:'',src:'',disabled:false,isConnected:true,checked:false,readOnly:false,
 classList:{contains:k=>classes.has(k),toggle(k,v){v??=!classes.has(k);v?classes.add(k):classes.delete(k)},add:k=>classes.add(k),remove:k=>classes.delete(k)},
 setAttribute(k,v){this[k]=String(v)},getAttribute(k){return this[k]},addEventListener(k,fn){handlers[k]=fn},click(){handlers.click?.({target:this,preventDefault(){},stopPropagation(){}})},
 querySelector(){return fake()},getBoundingClientRect(){return {width:600,height:180,left:0,top:0,right:600,bottom:180}},getContext(){return null},appendChild(n){n.parentNode=this},append(){},replaceChildren(){},closest(){return null},focus(){},dispatchEvent(){}
};}
const buttons=new Map();const cards=['valorant','roblox','pubg'].map(game=>{let card=fake();card.dataset.game=game;let button=fake();buttons.set(game,button);card.querySelector=()=>button;return card;});
const bank=fake();bank.dataset.bank='free';const bankChoice=fake();bankChoice.closest=()=>bank;
const document={body:fake(),documentElement:{lang:'vi'},visibilityState:'visible',getElementById(id){if(id==='free-bank-choice')return bankChoice;if(!els.has(id))els.set(id,fake());return els.get(id)},querySelectorAll(sel){return sel==='.game-card'?cards:sel==='#timebank-choices [role=radio]'?[bankChoice]:[]},querySelector(){return null},createElement:()=>fake(),addEventListener(k,fn){events[k]=fn}};
const window={chrome:{webview:{postMessage(m){sent.push(m)},addEventListener(){}}},addEventListener(){},devicePixelRatio:1};
const ctx={document,window,innerWidth:1080,innerHeight:800,Event:function(){},setTimeout(fn){fn();return 1},clearTimeout(){},getComputedStyle(){return {getPropertyValue(){return '#fff'}}},console};vm.createContext(ctx);vm.runInContext(fs.readFileSync(path.resolve(__dirname,'../app/frontend/ui/app.js'),'utf8'),ctx);
const receive=window.gloReceive, $=document.getElementById.bind(document);
receive({type:'init',mode:'services',language:'vi',game:'roblox',theme:'dark'});
receive({type:'auth',logged:false});
assert($('timebank-wrap').classList.contains('hidden'),'no bank for logged-out');
receive({type:'auth',logged:true,email:'user@example.com'});
receive({type:'wallet',wallet:{remaining_seconds:540,reserved_seconds:60,max_balance_seconds:10800}});
assert(!$('timebank-wrap').classList.contains('hidden'),'bank shown in authenticated lobby');
assert.equal($('wallet-value').textContent,'(10 / 180 phút)');
assert.equal($('wallet-progress')['aria-valuenow'],'6');
$('timebank-btn').click();assert(!$('timebank-menu').classList.contains('hidden'),'menu opens');
assert(sent.some(v=>v.action==='wallet.refresh'),'time bank sends on-demand refresh');
assert($('wallet-sync-status').classList.contains('hidden'),'fresh cached wallet has no loading flash');
receive({type:'wallet-status',status:'loading'});
assert.equal($('wallet-sync-status').textContent,'Đang cập nhật...','loading text on real request');
receive({type:'wallet-status',status:'error'});
assert.equal($('wallet-sync-status').textContent,'Không thể cập nhật, đang hiển thị số dư trước đó.');
assert.equal($('wallet-value').textContent,'(10 / 180 phút)','last valid wallet survives refresh failure');
receive({type:'wallet-status',status:'cached'});
assert($('wallet-sync-status').classList.contains('hidden'),'cached status clears loading');
assert.equal($('timebank-btn')['aria-expanded'],'true');
bankChoice.click();assert.equal(bankChoice['aria-checked'],'true','free bank checked');
$('earn-time').click();assert(sent.some(v=>v.action==='open.time'),'earn action preserved');
assert($('timebank-menu').classList.contains('hidden'),'earn closes menu');
// Only the specific button starts a connection, never the parent card.
assert(!cards[1].__handlers.click,'Roblox artwork/card has no click listener');
assert(!cards[1].__handlers.keydown,'Roblox card has no Enter handler');
const nBefore=sent.filter(v=>v.action==='services.connect').length;
buttons.get('roblox').click();
assert.equal(sent.filter(v=>v.action==='services.connect').length,nBefore+1,'Connect button sends once');
receive({type:'connection',connection:{active:false,phase:'disconnected',source:'official'},waiting:false});
// Waiting sessions show a static placeholder instead of fake samples.
receive({type:'connection',connection:{active:true,phase:'waiting',source:'official',game:'roblox'},waiting:false,history:[]});
assert(!$('quality-placeholder').classList.contains('hidden'),'waiting placeholder shown');
assert($('quality-chart-card').classList.contains('hidden'),'no synthetic chart');
receive({type:'connection',connection:{active:true,phase:'gameplay',route:'DIRECT',source:'official',game:'roblox'},waiting:false,history:[]});
assert(!$('quality-placeholder').classList.contains('hidden'),'no RELAY samples so still placeholder');
receive({type:'connection',connection:{active:false,phase:'disconnected',source:'official'},waiting:false,history:[]});
assert($('quality-placeholder').classList.contains('hidden'),'placeholder cleared on disconnect');
// Real Connecting through services; abandon before the worker was ever observed.
receive({type:'connection',connection:{active:false,phase:'disconnected',source:'official'},waiting:false});
vm.runInContext("connectGame('roblox')",ctx);
assert.equal(buttons.get('roblox').textContent,'Đang kết nối...');
receive({type:'connection',connection:{active:false,phase:'disconnected',source:'official'},waiting:false});
assert.equal(buttons.get('roblox').textContent,'Kết nối','terminal idle clears connecting label');
assert(!$('mode-btn').disabled,'mode unlocked after authoritative idle');
vm.runInContext("connectGame('roblox')",ctx);
receive({type:'connection',connection:{active:true,phase:'connecting',source:'official',game:'roblox'},waiting:false});
assert($('mode-btn').disabled,'mode locked while connecting');
receive({type:'connection',connection:{active:true,phase:'waiting',source:'official',game:'roblox'},waiting:false});
assert.equal(buttons.get('roblox').textContent,'Kết nối','active dashboard no stale card label');
assert(!$('timebank-wrap').classList.contains('hidden'),'bank visible during active services session');
$('account-btn').click();assert(!$('account-menu').classList.contains('hidden'),'account clickable while connected');
$('timebank-btn').click();assert($('account-menu').classList.contains('hidden'),'time bank closes account');
assert(!$('timebank-menu').classList.contains('hidden'),'time bank clickable while connected');
$('settings-btn').click();assert(document.body.classList.contains('settings-layout'),'settings clickable while connected');
assert($('timebank-menu').classList.contains('hidden'),'settings closes menu');
$('settings-back').click();$('theme-btn').click();assert.equal($('wordmark').src,'assets/wordmark-dark.png','theme clickable connected');
receive({type:'connection',connection:{active:false,phase:'disconnected',source:'official'},waiting:false});
assert(!$('mode-btn').disabled,'mode unlocked on disconnect');
vm.runInContext("setMode('connect')",ctx);
assert($('timebank-wrap').classList.contains('hidden'),'bank hidden for Custom Connect');
console.log('r5 UI runtime PASS: bank visibility/progress, menu controls, live topbar, terminal connect reset, active disconnect, Custom Connect');
