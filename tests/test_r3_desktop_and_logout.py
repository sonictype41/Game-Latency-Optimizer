"""R3 OSS desktop and safe-logout regression contracts."""
from pathlib import Path
import subprocess
import shutil

ROOT=Path(__file__).resolve().parents[1]
MAIN=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
APP=(ROOT/'app/frontend/ui/app.js').read_text(encoding='utf-8')
CSS=(ROOT/'app/frontend/ui/style.css').read_text(encoding='utf-8')
API=(ROOT/'app/frontend/official_api.cpp').read_text(encoding='utf-8')


def test_source_release_and_tray_lifecycle_unchanged():
    assert (ROOT/'RELEASE').read_text(encoding='utf-8').strip()=='0.1.5-beta'
    assert (ROOT/'VERSION').read_text(encoding='utf-8').strip()=='0.1.5'
    assert 'if(g_ui&&g_ui->web_mode&&g_ui->optimize_memory_on_tray)' in MAIN
    assert 'g_ui->webview.stop();' in MAIN
    assert 'optimize_memory_on_tray{true}' in MAIN


def test_native_splash_is_offscreen_animated():
    assert 'void paint_startup_splash(HDC target,RECT rect)' in MAIN
    for anchor in ('CreateCompatibleDC(target)','CreateCompatibleBitmap(target,width,height)',
                   'BitBlt(target,0,0,width,height,dc,0,0,SRCCOPY)',
                   'g.DrawArc(&arc', 'Gdiplus::InterpolationModeHighQualityBicubic',
                   'SetTimer(hwnd,43,33,nullptr)', 'KillTimer(hwnd,43)'):
        assert anchor in MAIN


def test_logout_native_order_and_pending_prepare_guard():
    assert 'void begin_safe_logout(HWND hwnd)' in MAIN
    assert 'void finish_logout_if_safe()' in MAIN
    assert 'cancel_handoff();' in MAIN
    assert 'if(g_ui->logout_pending)' in MAIN
    assert 'late_prepare_discarded' in MAIN
    assert 'if(g_ui->logout_pending)finish_logout_if_safe();' in MAIN
    assert 'g_ui->official_api->logout();' in MAIN
    assert MAIN.index('g_ui->official_api->logout();')>MAIN.index('void finish_logout_if_safe()')
    assert 'reason\\":\\"logout' in API


def test_account_mode_and_input_policy():
    assert "show('account-btn',state.mode==='services'&&state.logged&&!state.logoutPending)" in APP
    assert "document.body.classList.toggle('mode-locked',locked)" in APP
    assert "state.connection?.active" in APP
    assert '.mode-locked .mode-trigger .chevron{display:none!important}' in CSS
    assert 'html,body,body *{-webkit-user-select:none;' in CSS
    assert 'input:not([type=checkbox]):not([type=radio]),textarea' in CSS
    for key in ['copy','cut','paste','select-all']:
        assert f'data-edit="{key}"' in (ROOT/'app/frontend/ui/index.html').read_text(encoding='utf-8')
    assert 'clipboard_write_text(' in MAIN
    assert 'input.paste' in MAIN
    assert 'input.write' in MAIN


def test_auth_unblocks_before_catalog_and_render_costs():
    index=API.index('bool OfficialApi::fetch_details(bool report_error)')
    stop=API.index('void OfficialApi::restore()',index)
    body=API[index:stop]
    assert body.index('send("{\\"type\\":\\"auth') < body.index('L"/app/booster/catalog"')
    assert "lastTranslatedLanguage!==state.language" in APP
    assert 'catalogRendered.catalog===state.catalog' in APP
    assert "if(el.textContent!==value)" in APP
    assert "setTimeout(()=>send('ui.rendered'),0)" in APP


def test_js_syntax():
    node=shutil.which('node')
    if node: subprocess.run([node,'--check',str(ROOT/'app/frontend/ui/app.js')],check=True)
