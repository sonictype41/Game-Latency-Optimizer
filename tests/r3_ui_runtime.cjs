'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const path=require('node:path');
const sent=[],els=new Map(),events={};
function fake(){const cs=new Set(),listeners={};return {__listeners:listeners,style:{},dataset:{},textContent:'',src:'',value:'',checked:false,disabled:false,readOnly:false,isConnected:true,selectionStart:0,selectionEnd:0,parentNode:null,
 classList:{contains:c=>cs.has(c),toggle(c,v){if(v===undefined)v=!cs.has(c);v?cs.add(c):cs.delete(c)},add:c=>cs.add(c),remove:c=>cs.delete(c)},
 setAttribute(k,v){this[k]=v},getAttribute(k){return this[k]},addEventListener(k,fn){listeners[k]=fn},focus(){},click(){listeners.click?.({target:this,preventDefault(){}})},
 appendChild(c){c.parentNode=this;return c},append(){},replaceChildren(){},querySelector(){return fake()},closest(){return null},getBoundingClientRect(){return {width:700,height:150,left:0,right:700,top:0,bottom:150}},getContext(){return null},
 dispatchEvent(){},setSelectionRange(start,end){this.selectionStart=start;this.selectionEnd=end},select(){this.selectionStart=0;this.selectionEnd=this.value.length},
 setRangeText(text,start,end){this.value=this.value.slice(0,start)+text+this.value.slice(end)}
};}
const cards=['valorant','roblox','pubg'].map(game=>{const x=fake();x.dataset.game=game;return x});
const document={body:fake(),documentElement:{lang:'vi'},visibilityState:'visible',getElementById(id){if(!els.has(id))els.set(id,fake());return els.get(id)},
 querySelectorAll(s){return s==='.game-card'?cards:[]},querySelector(s){return s.includes('game-card')?cards.find(c=>s.includes(c.dataset.game))||null:null},createElement(){return fake()},addEventListener(k,fn){events[k]=fn}};
const window={chrome:{webview:{postMessage(x){sent.push(x)},addEventListener(){}}},devicePixelRatio:1,addEventListener(){}};
const ctx={window,document,innerWidth:1080,innerHeight:800,Event:function(){},setTimeout(fn){fn();return 1},clearTimeout(){},getComputedStyle(){return {getPropertyValue(){return '#fff'}}},console};vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.resolve(__dirname,'../app/frontend/ui/app.js'),'utf8'),ctx);
const receive=window.gloReceive,$=document.getElementById.bind(document);
receive({type:'init',mode:'services',game:'roblox',theme:'dark',language:'vi'});
receive({type:'auth',logged:false});
assert($('account-btn').classList.contains('hidden'),'account hidden on sign in');
receive({type:'auth',logged:true,email:'test@example.com'});
assert(!$('account-btn').classList.contains('hidden'),'account shown when logged in');
receive({type:'connection',connection:{active:true,phase:'waiting',game:'roblox',source:'official',region:'SG'},waiting:false});
assert($('mode-btn').disabled);assert(document.body.classList.contains('mode-locked'));
assert(!$('account-btn').classList.contains('hidden'),'account remains available for logout');
vm.runInContext("state.logoutPending=true;render()",ctx);
assert(!$('logout-loading').classList.contains('hidden'));
receive({type:'auth',logged:true,email:'late@example.com'});
assert(!$('logout-loading').classList.contains('hidden'),'late login cannot interrupt logout');
receive({type:'connection',connection:{active:false,phase:'disconnected'},waiting:false});
receive({type:'auth',logged:false,reason:'logout'});
assert($('logout-loading').classList.contains('hidden'));
assert($('account-btn').classList.contains('hidden'));
assert($('signin').classList.contains('hidden')===false);
receive({type:'auth',logged:true,email:'test@example.com'});
receive({type:'connection',connection:{active:true,phase:'waiting',game:'tft',source:'custom'},waiting:false});
receive({type:'auth',logged:false});
assert($('signin').classList.contains('hidden'),'active session must not overlap sign-in UI');
// Native clipboard bridge actions from the input-only right click menu.
const input=$('login-code');input.value='ABCDEFGHIJ';input.selectionStart=0;input.selectionEnd=5;
input.closest=()=>input;
events.contextmenu({target:input,clientX:20,clientY:25,preventDefault(){}});
assert(!$('input-context-menu').classList.contains('hidden'),'input context menu opens');
const cut=fake();cut.dataset.edit='cut';cut.closest=()=>cut;
$('input-context-menu').__listeners.click({target:cut});
assert(sent.some(m=>m.action==='input.write'&&m.text==='ABCDE'));
receive({type:'clipboard-written',ok:true});
assert.equal(input.value,'FGHIJ','Cut only after native clipboard succeeds');
input.selectionStart=0;input.selectionEnd=0;
events.contextmenu({target:input,clientX:30,clientY:25,preventDefault(){}});
const paste=fake();paste.dataset.edit='paste';paste.closest=()=>paste;
$('input-context-menu').__listeners.click({target:paste});
assert(sent.some(m=>m.action==='input.paste'));
receive({type:'clipboard-text',text:'Z'});
assert.equal(input.value,'ZFGHIJ');
const out=fake();let blocked=false;
events.keydown({target:out,ctrlKey:true,metaKey:false,key:'a',preventDefault(){blocked=true}});
assert(blocked,'Ctrl+A outside an editable field is blocked');
console.log('r3 UI runtime PASS: account, mode lock, safe logout, stale auth, native clipboard cut/paste, nonselectable page');
