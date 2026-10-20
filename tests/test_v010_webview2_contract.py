from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]

class Webview2Contract(unittest.TestCase):
    def test_native_secrets_stay_outside_javascript(self):
        src=(ROOT/'app/frontend/ui/app.js').read_text(encoding='utf-8')
        self.assertIn("send('auth.redeem'",src)
        self.assertIn("send('services.connect'",src)
        for secret in ('refresh_token','access_token','localStorage','sessionStorage'):
            self.assertNotIn(secret,src)
        self.assertIn('CryptProtectData',(ROOT/'app/frontend/official_api.cpp').read_text(encoding='utf-8'))
    def test_local_locked_down_webview(self):
        src=(ROOT/'app/frontend/webview_host.cpp').read_text(encoding='utf-8')
        for statement in ('SetVirtualHostNameToFolderMapping','glo.local','AreDevToolsEnabled(FALSE)','AreHostObjectsAllowed(FALSE)','get_Source','put_Cancel(TRUE)'):
            self.assertIn(statement,src)
        html=(ROOT/'app/frontend/ui/index.html').read_text(encoding='utf-8')
        self.assertIn("connect-src 'none'",html)
        self.assertIn('Nhận thêm thời gian',html)
        self.assertIn('valorant-presskit.jpg',html)
    def test_native_routes_preserved(self):
        cpp=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        for symbol in ('WM_GLO_INCOMING_URI','run_handoff_task','measure_relay_candidates','connect_loaded','glo://open','custom.confirm','auth.redeem','services.connect'):
            self.assertIn(symbol,cpp)
        self.assertTrue((ROOT/'app/core/src/wintun_tunnel.cpp').exists())
    def test_mingw_compile_regressions_for_native_booster(self):
        # Prevent the GCC 16 collision with std::quoted and the Win32 type/
        # COM header errors that previously broke the first v0.1.0-beta build.
        api=(ROOT/'app/frontend/official_api.cpp').read_text(encoding='utf-8')
        cpp=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        cmake=(ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('std::string json_quote(',api)
        self.assertNotIn('quoted(',api)
        self.assertEqual(api.count('json_quote('),8)
        self.assertIn('open_external_link(g_ui->window,widen(*url).c_str()',cpp)
        self.assertIn('#include <objbase.h>',cpp)
        self.assertIn('CoInitializeEx(',cpp)
        self.assertIn('CoUninitialize()',cpp)
        self.assertIn('shell32 ole32 comdlg32',cmake)

    def test_windows_staging_contains_webview2(self):
        cmake=(ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
        installer=(ROOT/'installer/nsis/GLO.nsi').read_text(encoding='utf-8-sig')
        pack=(ROOT/'tools/package_windows.py').read_text(encoding='utf-8')
        self.assertIn('app/frontend/webview_host.cpp',cmake)
        self.assertIn('WebView2Loader.dll',cmake)
        self.assertIn('WebView2Loader.dll',installer)
        self.assertIn('STAGE_DIR}\\ui',installer)
        self.assertIn('output_dir / "ui"',pack)
