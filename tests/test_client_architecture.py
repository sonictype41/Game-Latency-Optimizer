from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class V0122ClientArchitecture(unittest.TestCase):
    def test_ui_manifest_is_non_elevated(self):
        manifest=(ROOT/'app/core/glo.manifest').read_text(encoding='utf-8')
        self.assertIn('level="asInvoker"',manifest); self.assertNotIn('requireAdministrator',manifest)

    def test_generic_ui_has_uri_and_self_host_without_account_code(self):
        ui=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        self.assertIn('parse_handoff_uri',ui); self.assertIn('Kết nối đến máy chủ bên thứ ba được self-host',ui); self.assertIn('Import JSON file',ui)
        for token in ('Sign in with Google','access_token','GLO Network'):
            self.assertNotIn(token,ui)
        self.assertFalse((ROOT/'app/frontend/Auth').exists())

    def test_on_demand_same_binary_worker(self):
        src=(ROOT/'app/core/src/network_worker.cpp').read_text(encoding='utf-8'); main=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        self.assertIn('lpVerb=L"runas"',src); self.assertIn('--network-worker',src)
        self.assertIn('run_network_worker_if_requested()',main); self.assertIn('PIPE_REJECT_REMOTE_CLIENTS',src)

    def test_worker_receives_data_only_session_config(self):
        src=(ROOT/'app/core/src/network_worker.cpp').read_text(encoding='utf-8')
        self.assertIn('opt.session_grant=std::move(grant)',src)
        self.assertIn('opt.relay_public_key',src); self.assertIn('opt.game_exe_path.clear()',src)
        self.assertNotIn('BINDING',src); self.assertNotIn('SESSION_GRANT',src)

    def test_config_contract_forbids_filesystem_and_auth_fields(self):
        src=(ROOT/'app/core/src/session_config.cpp').read_text(encoding='utf-8')
        self.assertIn('allowed={"schema","relay","relay_name","relay_public_key","game","grant","timeout_message","profile_id","profile_revision","gameplay_ipv4","port_min","port_max"}',src)
        self.assertIn('Unsupported config field',src)

    def test_profile_router_has_no_wfp_gate(self):
        core=(ROOT/'app/core/src/client_core.cpp').read_text(encoding='utf-8')
        self.assertIn('set_profile_routes',core)
        self.assertNotIn('make_endpoint_gate',core)
        self.assertFalse((ROOT/'app/core/src/wfp_endpoint_gate.cpp').exists())


    def test_session_clock_comes_from_secure_transport(self):
        core=(ROOT/'app/core/src/client_core.cpp').read_text(encoding='utf-8'); hdr=(ROOT/'secure_transport/include/glo/secure_transport.hpp').read_text(encoding='utf-8')
        self.assertIn('remaining_seconds()',hdr); self.assertIn('transport.remaining_seconds()',core)
        self.assertNotIn('session_deadline',core)

    def test_review_first_ui_does_not_auto_connect(self):
        ui=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        paste=ui[ui.index('void paste_config()'):ui.index('void import_config()')]
        imp=ui[ui.index('void import_config()'):ui.index('void clear_config_after_use()')]
        self.assertNotIn('connect_loaded()',paste)
        self.assertNotIn('connect_loaded()',imp)
        self.assertIn('CONFIG READY',ui)
        self.assertIn('const bool ready=',ui)
        self.assertNotIn('Session left:  --',ui)

    def test_one_time_grant_is_cleared_after_success(self):
        ui=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        self.assertIn('s.session_id!=0',ui)
        self.assertIn('options.session_grant.clear()',ui)
        self.assertIn('config->grant.clear()',ui)
        self.assertIn('clear_config_after_use()',ui)

    def test_connected_ui_hides_config_actions_and_uses_dynamic_game(self):
        ui=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        self.assertIn('paste_rect=RECT{}',ui);self.assertIn('import_rect=RECT{}',ui)
        self.assertIn('RELAY ENDPOINT',ui);self.assertIn('selected_game',ui)
        self.assertNotIn('Game đang chạy',ui);self.assertNotIn('Game chưa chạy',ui)

    def test_restored_shell_keeps_theme_and_settings(self):
        ui=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        for token in ('ThemeMode', 'paint_settings', 'draw_metric', 'draw_theme_icon', 'Detailed debug logging'):
            self.assertIn(token,ui)

if __name__=='__main__': unittest.main()
