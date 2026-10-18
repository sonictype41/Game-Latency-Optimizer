import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class R1VerificationContract(unittest.TestCase):
    def test_global_default_is_three_seconds(self):
        opts = (ROOT / 'app/core/include/glo/client_core.hpp').read_text(encoding='utf-8')
        self.assertIn('force_handover_grace_ms{3000}', opts)
        self.assertNotIn('force_handover_grace_ms{1800}', opts)

    def test_timeout_is_not_reset_for_each_new_flow(self):
        src = (ROOT / 'app/core/src/client_core.cpp').read_text(encoding='utf-8')
        self.assertIn('if(!relay_locked && !verify_deadline)', src)
        self.assertIn('verify_deadline=*verify_started+std::chrono::milliseconds(options.force_handover_grace_ms)', src)
        self.assertIn('if(reverse_already_seen) lock_relay', src)

    def test_reverse_injection_not_gated_on_worker_state(self):
        src = (ROOT / 'app/core/src/client_core.cpp').read_text(encoding='utf-8')
        self.assertIn('if (injection_ok && collecting)', src)
        self.assertNotIn('tunnel.inject_reply(d.flow_id, d.payload, &injected) &&', src)
        self.assertIn('VERIFY006', src)

    def test_wire_version_stays_compatible(self):
        self.assertEqual((ROOT/'VERSION').read_text(encoding='utf-8').strip(), '0.0.6')
        self.assertEqual((ROOT/'RELEASE').read_text(encoding='utf-8').strip(), '0.0.6-beta')

if __name__ == '__main__':
    unittest.main()
