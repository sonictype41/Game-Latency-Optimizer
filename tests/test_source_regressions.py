from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SourceRegressionTests(unittest.TestCase):
    def test_redeem_error_message_uses_lpcwstr(self):
        ui = (ROOT / 'app/frontend/main.cpp').read_text(encoding='utf-8')
        self.assertIn('fail(err.empty()?L"Provider could not redeem a session."', ui)
        self.assertIn('MessageBoxW(hwnd,error.c_str(),L"GLO - Handoff"', ui)
        self.assertIn('finish_handoff_task(HWND hwnd)', ui)
        self.assertNotIn('L"Could not redeem a session from the measured relays.\\n")+err,L"GLO - Handoff"', ui)

    def test_measurements_remain_service_selected_contract(self):
        ui = (ROOT / 'app/frontend/main.cpp').read_text(encoding='utf-8')
        self.assertIn('\\"measurements\\"', ui)
        self.assertIn('measure_relay_candidates', ui)
        self.assertNotIn('rank_relay_candidates', ui)


if __name__ == '__main__':
    unittest.main()
