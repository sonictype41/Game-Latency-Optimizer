import re, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; CORE=ROOT/'app/core'
class WindowsClientContractTests(unittest.TestCase):
    def test_client_error_code_references_exist_in_enum(self):
        header=(CORE/'include/glo/client_core.hpp').read_text(encoding='utf-8'); m=re.search(r'enum class ClientErrorCode\s*\{(?P<body>.*?)\};',header,re.S); self.assertIsNotNone(m)
        declared={x.strip().split('=')[0].strip() for x in m.group('body').split(',') if x.strip()}; refs=set()
        for base in (CORE,ROOT/'app/frontend'):
            for p in base.rglob('*'):
                if p.suffix in {'.hpp','.cpp'}: refs.update(re.findall(r'ClientErrorCode::([A-Za-z_][A-Za-z0-9_]*)',p.read_text(encoding='utf-8', errors='ignore')))
        self.assertEqual([],sorted(refs-declared))
    def test_wintun_endpoint_api_uses_preflight_endpoint(self):
        h=(CORE/'include/glo/wintun_tunnel.hpp').read_text(encoding='utf-8'); self.assertIn('PreflightEndpoint',h)
    def test_ttl_is_not_config_or_provider_data(self):
        h=(CORE/'include/glo/client_core.hpp').read_text(encoding='utf-8'); core=(CORE/'src/client_core.cpp').read_text(encoding='utf-8'); cfg=(CORE/'src/session_config.cpp').read_text(encoding='utf-8')
        self.assertIn('session_remaining_seconds',h); self.assertIn('transport.remaining_seconds()',core); self.assertNotIn('session_ttl',cfg)
        self.assertNotIn('SessionKeyProvider',h); self.assertNotIn('acquire_session',core)
    def test_win32_layout_uses_int_coordinates_without_long_template_mix(self):
        ui=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        self.assertIn('const int card_bottom=',ui)
        self.assertIn('int action_top=card_bottom+14',ui)
        self.assertNotIn('std::min(h-84,card.bottom+24)',ui)

    def test_debug_logging_exposes_open_log_file_link_only_when_enabled(self):
        ui=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        self.assertIn('debug_log_link_rect',ui)
        self.assertIn('Open log file',ui)
        self.assertIn('Mở tệp nhật ký',ui)
        self.assertIn('if(g_ui->debug_logging){g_ui->debug_log_link_rect=',ui)
        self.assertIn('open_debug_log_file(hwnd)',ui)
        self.assertIn('glo::debug_log_path()',ui)
        self.assertIn('LocalAppData',ui)
    def test_ui_has_no_private_or_account_material(self):
        ui=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        for x in ('issuer_seed','issuer_private','access_token','api_origin','web_origin','Google'):
            self.assertNotIn(x,ui)
if __name__=='__main__': unittest.main()
