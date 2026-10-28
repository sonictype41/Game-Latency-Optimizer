"""R4 source-level contracts for the desktop shell; no Windows runtime assumed."""
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
HTML=(ROOT/'app/frontend/ui/index.html').read_text(encoding='utf-8')
JS=(ROOT/'app/frontend/ui/app.js').read_text(encoding='utf-8')
CSS=(ROOT/'app/frontend/ui/style.css').read_text(encoding='utf-8')
MAIN=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
CORE=(ROOT/'app/core/src/client_core.cpp').read_text(encoding='utf-8')
WORKER=(ROOT/'app/core/src/network_worker.cpp').read_text(encoding='utf-8')


def test_release_and_no_native_tray_change():
    assert (ROOT/'VERSION').read_text(encoding='utf-8').strip()=='0.1.5'
    assert (ROOT/'RELEASE').read_text(encoding='utf-8').strip()=='0.1.5-beta'
    assert (ROOT/'DISPLAY_VERSION').read_text(encoding='utf-8').strip()=='0.1.5-beta'
    assert 'if(g_ui&&g_ui->web_mode&&g_ui->optimize_memory_on_tray)' in MAIN
    assert 'g_ui->webview.stop();' in MAIN
    assert '0.1.5-beta' in HTML


def test_mode_only_lock_and_topbar_actions():
    assert 'body.connect-locked .top-actions{pointer-events:none' not in CSS
    assert 'body.connect-locked #mode-menu,body.connect-locked #account-menu' not in CSS
    assert 'body.connect-locked #mode-menu{display:none!important}' in CSS
    assert "if(state.logoutPending)return;closeTopMenus();state.page=" in JS
    assert "$('theme-btn').addEventListener('click',()=>{closeTopMenus();state.theme=" in JS
    assert "$('account-btn').addEventListener('click',()=>{if(state.logoutPending||!state.logged)return;" in JS


def test_wallet_moved_and_single_select_prepared():
    assert re.search(r'<div id="service-authenticated" class="hidden">\s*<div id="service-picker">',HTML)
    assert 'id="timebank-btn"' in HTML
    assert 'id="timebank-menu"' in HTML
    assert 'id="timebank-choices" role="radiogroup"' in HTML
    assert 'id="free-bank-choice"' in HTML
    assert HTML.count('role="radio"')==1
    assert 'aria-checked="true"' in HTML
    assert 'id="wallet-progress"' in HTML
    assert 'id="wallet-bar"' in HTML
    assert 'id="earn-time"' in HTML
    assert '.timebank-menu{width:400px;' in CSS
    assert 'display:flex;align-items:center;gap:12px' in CSS
    assert "show('timebank-wrap',state.mode==='services'&&state.logged&&!state.logoutPending)" in JS
    assert "const total=rem+res" in JS
    assert "send('open.time')" in JS
    assert 'state.selectedTimeBank=' in JS
    assert 'state.connection?.active' in JS
    assert 'premium' not in HTML.lower()


def test_pending_and_terminal_connect_reset():
    assert "const label=t(pending?'connect.cardConnecting':'services.connect')" in JS
    assert "else if(!state.waiting&&state.connectStage&&state.connection?.phase==='disconnected')resetConnecting()" in JS
    assert 'state.connectSawWorker' in JS
    assert "if(state.connectStage&&state.connectSource==='services'){const card" not in JS
    assert 'if(state.connection?.active)' in JS


def test_official_diagnostics_redaction_across_worker_ipc():
    header=(ROOT/'app/core/include/glo/client_core.hpp').read_text(encoding='utf-8')
    assert 'bool hide_relay_endpoint_in_log{false};' in header
    assert 'g_ui->options.hide_relay_endpoint_in_log=(source==ConfigSource::OfficialHandoff&&g_ui->official_session)' in MAIN
    assert 'if(g_ui->official_session){' in MAIN
    assert 'event=candidate_probe node=GLO-Relay' in MAIN
    assert 'f.size()==15||f.size()==16||f.size()==18' in WORKER
    assert 'opt.hide_relay_endpoint_in_log=private_log!=0;' in WORKER
    assert 'hex_encode(options.relay_log_name.empty()?' in WORKER
    assert 'options.hide_relay_endpoint_in_log' in CORE
    assert 'log.error("NET002", "could not resolve relay "+relay_log_target' in CORE
    assert 'log.info("VERIFY000", "event=relay_selected "+relay_log_target' in CORE
    assert '"endpoint="+options.relay_host+":"+std::to_string(options.relay_port)' in CORE
    assert 'name.find(\'.\')==std::string::npos' in CORE
    assert 'name.find(\':\')==std::string::npos' in CORE
    assert 'const int resolve_rc = getaddrinfo(options.relay_host.c_str(), port.c_str()' in CORE
