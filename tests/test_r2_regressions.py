from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def test_native_splash_and_version():
    main=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
    host=(ROOT/'app/frontend/webview_host.cpp').read_text(encoding='utf-8')
    assert 'paint_startup_splash' in main
    assert 'Gdiplus::Bitmap' in main
    assert 'wordmark-light.png' in main
    assert 'ctrl->put_IsVisible(FALSE)' in host
    assert 'ui.rendered' in main
    assert 'void WebviewHost::reveal()' in host
    assert (ROOT/'RELEASE').read_text(encoding='utf-8').strip()=='0.1.5-beta'
    assert (ROOT/'VERSION').read_text(encoding='utf-8').strip()=='0.1.5'

def test_connection_lifecycle_safeguards():
    js=(ROOT/'app/frontend/ui/app.js').read_text(encoding='utf-8')
    main=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
    assert 'connectLocked()' in js
    assert "case 'custom-offer':resetConnecting()" in js
    assert 'connection?.disconnecting' in js
    assert 'state.game||' not in js
    assert "toast(t('connect.timeout'))" not in js
    assert 'active&&g_ui->config?' in main
    assert 'g_ui->last_ui_game)' not in main[main.index('void web_connection_snapshot()'):main.index('void web_connection_snapshot()')+3300]

def test_errors_do_not_expose_session_secrets():
    api=(ROOT/'app/frontend/official_api.cpp').read_text(encoding='utf-8')
    assert 'error_code' in api
    assert 'last_network_error_' in api
    assert 'AUTH_REQUIRED' in api
    assert 'send("{\\"type\\":\\"error' in api

def test_ui_vm():
    node=shutil.which('node')
    if not node:
        import pytest
        pytest.skip('Node.js runtime unavailable')
    subprocess.run([node,str(ROOT/'tests/r2_ui_runtime.cjs')],check=True,cwd=ROOT,timeout=25)
