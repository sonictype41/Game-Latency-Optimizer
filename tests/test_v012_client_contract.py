"""OSS v0.1.3-beta: UI state and native lifecycle regression checks."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def read(name): return (ROOT/name).read_text(encoding='utf-8')

def test_versions_and_release_changelog():
    assert read('VERSION').strip()=='0.1.5'
    assert read('RELEASE').strip()=='0.1.5-beta'
    assert '## [0.1.3-beta]' in read('CHANGELOG.md')

def test_native_minimize_separate_from_tray():
    native=read('app/frontend/main.cpp')
    assert 'case WM_GETMINMAXINFO:' in native
    assert 'optimize_on_minimize{false}' in native
    assert 'optimize_memory_on_tray{true}' in native
    assert 'SIZE_MINIMIZED){if(g_ui&&g_ui->optimize_memory_on_tray&&g_ui->optimize_on_minimize)' in native
    assert 'case WM_CLOSE:minimize_to_tray(hwnd)' in native
    assert 'settings.optimizeOnMinimize' in native
    assert 'optimize_on_minimize' in native

def test_new_settings_and_hover_and_dashboard():
    html=read('app/frontend/ui/index.html')
    css=read('app/frontend/ui/style.css')
    js=read('app/frontend/ui/app.js')
    assert 'id="optimize-on-minimize"' in html
    assert 'id="minimize-opt-row"' in html
    assert "show('minimize-opt-row',state.optimizeMemory)" in js
    assert ".game-card:hover .game-connect" in css
    assert 'id="service-disconnect"' in html
    assert 'id="service-stat-ping"' in html
    assert '#service-authenticated.session-live > .wallet{display:none}' not in css
    assert 'id="timebank-wrap"' in read('app/frontend/ui/index.html')
    assert 'classList.toggle(\'with-quality\',quality)' in js
    assert '.settings-title,.settings-sections{width:min(100%,790px);margin-left:auto;margin-right:auto}' in css

def test_auth_loading_preserves_tokens_on_network_failures():
    js=read('app/frontend/ui/app.js')
    api=read('app/frontend/official_api.cpp')
    assert "authStatus:'checking'" in js
    assert "show('startup-loading',boot)" in js
    assert 'state.loginPending' in js
    assert 'case \'connect-progress\'' in js
    assert 'if(reached&&(code==401||code==403))' in api
    assert 'offline' in api
    assert 'auth.retry' in read('app/frontend/main.cpp')

def test_network_core_protocol_untouched():
    # Core should not import WebView2 or UI render dependencies.
    core=read('app/core/src/client_core.cpp')
    assert 'WebView2' not in core


def test_minimum_window_size_std_min_operands_match_int_type():
    # RECT members are LONG in the Windows SDK, but minw/minh are int.
    # std::min requires two operands of the same type under MinGW GCC 16.
    native=read('app/frontend/main.cpp')
    assert 'std::min(minw,static_cast<int>(monitor.rcWork.right-monitor.rcWork.left))' in native
    assert 'std::min(minh,static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top))' in native
    assert 'std::min(minw,monitor.rcWork.right-monitor.rcWork.left)' not in native
    assert 'std::min(minh,monitor.rcWork.bottom-monitor.rcWork.top)' not in native
