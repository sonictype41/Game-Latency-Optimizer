"""r5 source contracts: no implicit API polling and no change to dataplane/tray."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
MAIN=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf8')
API=(ROOT/'app/frontend/official_api.cpp').read_text(encoding='utf8')
HDR=(ROOT/'app/frontend/official_api.hpp').read_text(encoding='utf8')
JS=(ROOT/'app/frontend/ui/app.js').read_text(encoding='utf8')
HTML=(ROOT/'app/frontend/ui/index.html').read_text(encoding='utf8')
CSS=(ROOT/'app/frontend/ui/style.css').read_text(encoding='utf8')

def test_no_wallet_timer_catalog_dependency():
    assert 'last_web_second%30' not in MAIN
    assert 'else g_ui->official_api->sync_cached()' in MAIN
    assert '*action=="wallet.refresh"' in MAIN
    assert 'void OfficialApi::refresh_wallet()' in API
    assert 'if(wallet_requested_.exchange(true))return;' in API
    assert 'now-wallet_updated_<std::chrono::seconds(15)' in API
    assert 'now-wallet_failed_<std::chrono::seconds(5)' in API
    wallet=API.split('void OfficialApi::refresh_wallet(){',1)[1].split('// Map only known',1)[0]
    assert 'L"/app/me"' in wallet
    assert 'L"/app/booster/catalog"' not in wallet
    assert 'fetch_details()' not in wallet
    assert 'invalidate_wallet_cache' in MAIN

def test_auth_transient_state_and_no_credential_clear():
    fetch=API.split('bool OfficialApi::fetch_details(bool report_error)',1)[1].split('void OfficialApi::restore()',1)[0]
    assert 'if(refresh_.empty()){' in fetch
    assert 'if(report_error&&!identity_verified_)' in fetch
    assert 'wallet-status' in fetch
    assert 'attempt<2' in API
    assert 'void OfficialApi::sync_cached()' in API
    assert 'state.authStatus=\'offline\'' in JS
    assert 'if(!state.logoutPending&&!state.logged)' in JS
    assert 'if(!access_.empty())request(L"POST",L"/app/auth/logout"' in API

def test_button_only_and_placeholder_when_no_real_samples():
    for game in ('roblox','valorant','pubg'):
        assert f'data-game="{game}" tabindex="0" role="button"' not in HTML
    assert ".game-connect').addEventListener('click'" in JS
    assert "card.addEventListener('click'" not in JS
    assert 'id="quality-placeholder"' in HTML
    assert 'id="quality-placeholder-text"' in HTML
    assert 'show(\'quality-placeholder\',present&&!chartHasSamples)' in JS
    assert "c.phase==='gameplay'?'chart.collecting':'chart.waiting'" in JS
    assert 'style="width:' not in HTML.split('id="quality-placeholder"',1)[1].split('id="quality-chart-card"',1)[0]
    assert 'role="status"' in HTML

def test_graph_setting_removed_but_history_retained():
    assert 'id="show-graph"' not in HTML
    assert 'settings.showGraph' not in MAIN
    assert 'show_network_quality_graph' not in MAIN
    assert 'showGraph' not in JS
    assert 'settings.graph' not in JS
    assert 'sample_quality_history(snap)' in MAIN
    assert 'graph_history_json()' in MAIN
    assert 'if(!valid){g_ui->quality_history.clear()' in MAIN

def test_versions_and_oss_invariants():
    assert (ROOT/'VERSION').read_text(encoding='utf-8').strip()=='0.1.5'
    assert (ROOT/'RELEASE').read_text(encoding='utf-8').strip()=='0.1.5-beta'
    assert (ROOT/'DISPLAY_VERSION').read_text(encoding='utf-8').strip()=='0.1.5-beta'
    assert 'if(g_ui&&g_ui->web_mode&&g_ui->optimize_memory_on_tray)' in MAIN
    assert 'g_ui->webview.stop();' in MAIN
    assert 'art_route_id' not in (ROOT/'app/core/src/client_core.cpp').read_text(encoding='utf-8')
