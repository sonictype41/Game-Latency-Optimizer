import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'app/frontend/main.cpp'
LOG_CPP = ROOT / 'app/core/src/client_log.cpp'
LOG_HPP = ROOT / 'app/core/include/glo/client_log.hpp'


class V0170R7SourceTests(unittest.TestCase):
    def test_handoff_parser_accepts_canonical_and_legacy_forms(self):
        text = UI.read_text(encoding='utf-8')
        self.assertIn('L"glo://connect/?"', text)
        self.assertIn('L"glo://connect?"', text)
        self.assertIn('InvalidSchemeOrAction', text)

    def test_debug_log_is_append_only_and_frontend_aware(self):
        ui = UI.read_text(encoding='utf-8')
        cpp = LOG_CPP.read_text(encoding='utf-8')
        hpp = LOG_HPP.read_text(encoding='utf-8')
        self.assertIn('glo::ClientLog app_log', ui)
        self.assertIn('event=handoff_parse_failed reason=', ui)
        self.assertIn('std::ios::app', cpp)
        self.assertNotIn('std::ios::trunc', cpp)
        self.assertNotIn('opened_once_', cpp + hpp)


if __name__ == '__main__':
    unittest.main()
