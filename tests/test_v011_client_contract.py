"""Regression contracts for v0.1.1 UI and WebView2 lifecycle."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]

class V011ClientContract(unittest.TestCase):
    def test_release(self):
        self.assertEqual((ROOT/'VERSION').read_text(encoding='utf-8').strip(),'0.1.2')
        self.assertEqual((ROOT/'RELEASE').read_text(encoding='utf-8').strip(),'0.1.2-beta')
    def test_custom_mode_no_account_and_svg(self):
        html=(ROOT/'app/frontend/ui/index.html').read_text(encoding='utf-8')
        js=(ROOT/'app/frontend/ui/app.js').read_text(encoding='utf-8')
        self.assertIn("show('account-btn',state.mode==='services')",js)
        self.assertNotIn('Independent',html)
        self.assertNotIn('CONNECTION SETUP',html)
        self.assertIn('i-github',html)
        self.assertIn('id="settings-back"',html)
        self.assertIn('data-tip="settings.memory.help"',html)
    def test_native_graph_and_prefs(self):
        cpp=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        self.assertIn('sample_quality_history(snap)',cpp)
        self.assertIn('quality_history.size()>151',cpp)
        self.assertIn('now-g_ui->quality_history.front().tick>300000',cpp)
        self.assertIn('settings.optimizeMemory',cpp)
        self.assertIn('settings.showGraph',cpp)
        self.assertIn('optimize_memory_on_tray{true}',cpp)
        self.assertIn('show_network_quality_graph{true}',cpp)
        self.assertIn('WM_GLO_WEB_TRAY',cpp)
        self.assertIn('g_ui->webview.stop()',cpp)
    def test_async_callbacks_weak(self):
        src=(ROOT/'app/frontend/webview_host.cpp').read_text(encoding='utf-8')
        self.assertIn('std::weak_ptr<Internal> owner',src)
        self.assertIn('auto shared=owner.lock()',src)
        self.assertIn('internal_.reset()',src)
    def test_graph_only_live_relay(self):
        js=(ROOT/'app/frontend/ui/app.js').read_text(encoding='utf-8')
        self.assertIn("c.phase==='gameplay'&&c.route==='RELAY'",js)
        self.assertIn('Number.isFinite(c.ping_ms)',js)
        self.assertIn('Number.isFinite(c.loss_pct)',js)
        self.assertIn("state.showGraph&&state.page==='services'",js)
        self.assertIn('if(!isRelay(c)){resetGraph();}',js)
    def test_i18n(self):
        js=(ROOT/'app/frontend/ui/app.js').read_text(encoding='utf-8')
        for key in ('settings.memory','settings.graph','services.title','connect.warning','modal.time.title','stats.loss'):
            self.assertIn("'"+key+"'",js)
        self.assertIn('document.documentElement.lang=state.language',js)
if __name__=='__main__':unittest.main()
