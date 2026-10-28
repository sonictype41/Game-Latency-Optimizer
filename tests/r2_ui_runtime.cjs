'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const path=require('node:path');
const sent=[];const elements=new Map();
const fake=()=>{
 const names=new Set();const listeners={};
 const item={style:{},dataset:{},textContent:'',src:'',value:'',checked:false,disabled:false,parentNode:null,
   classList:{add(x){names.add(x)},remove(x){names.delete(x)},contains(x){return names.has(x)},toggle(x,v){if(v===undefined)v=!names.has(x);v?names.add(x):names.delete(x)}},
   setAttribute(k,v){this[k]=v},getAttribute(k){return this[k]},
   addEventListener(k,fn){listeners[k]=fn},dispatch(k){listeners[k]?.({key:'Enter',preventDefault(){}})},
   appendChild(x){x.parentNode=this;return x},append(){},replaceChildren(){},querySelector(){return fake()},getContext(){return null},getBoundingClientRect(){return {width:700,height:150,left:0,right:700,top:0,bottom:150}},
 };return item;
};
for (const name of ['services','connect','settings','mode-menu','account-menu'])elements.set(name,fake());
const cards=['valorant','roblox','pubg','tft'].map(game=>{const el=fake();el.dataset.game=game;return el});
const document={body:fake(),documentElement:{lang:'vi'},visibilityState:'visible',
  getElementById(id){if(!elements.has(id))elements.set(id,fake());return elements.get(id)},
  querySelectorAll(sel){if(sel==='.game-card')return cards;return []},
  querySelector(sel){if(sel.includes('game-card'))return cards.find(c=>sel.includes(c.dataset.game))||null;return null},
  createElement(){return fake()},addEventListener(){}};
const window={chrome:{webview:{postMessage(m){sent.push(m)},addEventListener(){}}},devicePixelRatio:1,addEventListener(){}};
const ctx={window,document,innerWidth:1080,requestAnimationFrame:fn=>fn(),setTimeout(fn){fn();return 1},clearTimeout(){},getComputedStyle(){return {getPropertyValue(){return '#fff'}}},console};vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.resolve(__dirname,'../app/frontend/ui/app.js'),'utf8'),ctx);
const receive=window.gloReceive;
receive({type:'init',mode:'services',game:'valorant',theme:'dark',language:'vi'});
assert(sent.some(x=>x.action==='ui.ready'));assert(sent.some(x=>x.action==='ui.rendered'));
receive({type:'auth',logged:true,email:'test@example.com'});
receive({type:'connection',connection:{active:false,phase:'disconnected',game:'',region:'SG'},waiting:false});
assert.equal(elements.get('active-game-image').src,'assets/custom-game-placeholder.png');
receive({type:'connection',connection:{active:true,phase:'waiting',game:'roblox',source:'official',region:'SG'},waiting:false});
assert.equal(elements.get('active-game-name').textContent,'Roblox');
assert.equal(elements.get('active-game-image').src,'assets/roblox-presskit.jpg');
receive({type:'connection',connection:{active:false,phase:'disconnected',game:'',region:'SG'},waiting:false});
assert.equal(elements.get('active-game-image').src,'assets/custom-game-placeholder.png');
receive({type:'connection',connection:{active:true,phase:'waiting',game:'tft',source:'custom',region:'SG'},waiting:false});
assert.equal(elements.get('active-game-name').textContent,'Teamfight Tactics');
assert.equal(elements.get('active-game-image').src,'assets/tft-presskit.jpg');
receive({type:'connection',connection:{active:false,phase:'disconnected',game:'',region:'SG'},waiting:false});
vm.runInContext("connectGame('roblox')",ctx);
assert.equal(elements.get('mode-btn').disabled,true);
assert(sent.some(x=>x.action==='services.connect'&&x.game==='roblox'));
receive({type:'error',message:'Backend refused prepare'});
assert.equal(elements.get('mode-btn').disabled,false);
vm.runInContext("setMode('connect')",ctx);
document.getElementById('custom-uri').value='glo://connect/?v=1&issuer=fake&token=fake';
elements.get('inspect-uri').dispatch('click');
assert.equal(elements.get('mode-btn').disabled,true);
assert.equal(elements.get('connect-progress').classList.contains('hidden'),false);
receive({type:'custom-offer',game:'tft',source:'test-provider'});
assert.equal(elements.get('mode-btn').disabled,false);
vm.runInContext("setMode('services')",ctx);
vm.runInContext("state.page='settings';render()",ctx);
assert.equal(document.body.classList.contains('settings-layout'),true);
console.log('r2 UI runtime cases PASS: first frame, game identity, TFT, stale disconnect, lock, custom progress, settings');
