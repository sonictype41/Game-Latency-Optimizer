"""v0.1.3-r1-beta: hidden TFT support and Custom/Official state isolation."""
from pathlib import Path
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_hidden_tft_native_contract():
    profiles = (ROOT / 'app/core/include/glo/game_profile.hpp').read_text(encoding='utf-8')
    worker = (ROOT / 'app/core/src/network_worker.cpp').read_text(encoding='utf-8')
    detector = (ROOT / 'app/core/src/game_detector.cpp').read_text(encoding='utf-8')
    native = (ROOT / 'app/frontend/main.cpp').read_text(encoding='utf-8')
    html = (ROOT / 'app/frontend/ui/index.html').read_text(encoding='utf-8')
    assert 'TFT = 5' in profiles
    assert '{GameId::TFT, "tft", "Teamfight Tactics", true}' in profiles
    assert 'game!=static_cast<int>(GameId::TFT)' in worker
    assert 'TFTClient-Win64-Shipping.exe' in detector
    assert 'data-game="tft"' not in html
    assert '(*game=="roblox"||*game=="valorant"||*game=="pubg")' in native  # Official closed
    assert 'Unsupported game profile' not in native  # Native now delegates to parser


def test_web_handoff_failures_report_to_frontend():
    native = (ROOT / 'app/frontend/main.cpp').read_text(encoding='utf-8')
    assert 'event=session_config_rejected' in native
    assert 'json_escape(error)' in native
    assert 'Incomplete handoff response.' in native
    assert '"+std::string(g_ui->official_session?"services":"custom")+' in native
    assert 'g_ui->handoff_task||g_ui->official_preparing' in native


def test_web_lifecycle_regression_with_node():
    if not shutil.which('node'):
        import pytest
        pytest.skip('Node.js unavailable')
    src = (ROOT / 'app/frontend/ui/app.js').read_text(encoding='utf-8')
    helpers = src[src.index('function resetConnecting()'):src.index('function saveView()')]
    recv = src[src.index('function receive(data)'):src.index("bridge?.addEventListener('message'")]
    script = r'''"use strict";
const assert=require('node:assert/strict');
const state={connectStage:'',connectSource:'',connectSawWorker:false,connectAttempt:0,
 mode:'services',page:'services',game:'roblox',region:'SG',waiting:false,
 connection:null,loginPending:false,authStatus:'authenticated',history:[],logged:true};
function render(){} function toast(){} function modal(){} function resetGraph(){}
function isRelay(){return false;} function t(x){return x;}
function $(id){return {value:'vi'};}
''' + helpers + recv + r'''
// First reproduce the exact Custom TEST config rejection -> Services regression.
receive({type:'connect-progress',stage:'verifying',source:'custom'});
assert.equal(state.connectSource,'connect');
assert.equal(state.connectStage,'verifying');
receive({type:'connection',connection:{active:false,phase:'disconnected',game:'roblox'},waiting:false});
receive({type:'error',message:'Unsupported game profile'});
assert.equal(state.connectStage,'');
assert.equal(state.connectSource,'');
assert.equal(state.waiting,false);
// Explicitly verify Official and Custom modes stay distinct.
beginConnecting('preparing','services');
const id=state.connectAttempt;
receive({type:'connect-progress',stage:'verifying',source:'services'});
assert.equal(state.connectAttempt,id); // same attempt, old timeout token remains valid
assert.equal(state.connectSource,'services');
receive({type:'connection',connection:{active:true,phase:'connecting'},waiting:false});
assert.equal(state.connectSawWorker,true);
receive({type:'connection',connection:{active:false,phase:'disconnected'},waiting:false});
assert.equal(state.connectStage,'');
assert.equal(state.waiting,false);
// Another attempt must not inherit old worker activity.
beginConnecting('preparing','services');
assert.equal(state.connectSawWorker,false);
receive({type:'need-time',message:'Not enough balance'});
assert.equal(state.connectStage,'');
assert.equal(state.waiting,false);
// Successful connection terminates its pending state.
beginConnecting('verifying','connect');
receive({type:'connection',connection:{active:true,phase:'waiting'},waiting:false});
assert.equal(state.connectStage,'');
console.log('web lifecycle scenarios: PASS');
'''
    result = subprocess.run(['node', '-e', script], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
