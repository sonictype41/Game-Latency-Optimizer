import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class R3FlowLifecycleContract(unittest.TestCase):
    def test_versions_and_protocol_remain_compatible(self):
        self.assertEqual((ROOT/'VERSION').read_text(encoding="utf-8").strip(), '0.1.3')
        self.assertEqual((ROOT/'RELEASE').read_text(encoding="utf-8").strip(), '0.1.3-beta')
        self.assertEqual((ROOT/'DISPLAY_VERSION').read_text(encoding="utf-8").strip(), '0.1.3-beta')

    def test_local_idle_reclamation_is_two_way_and_consistent(self):
        src = (ROOT/'app/core/src/wintun_tunnel.cpp').read_text(encoding="utf-8")
        header = (ROOT/'app/core/include/glo/flow_lifecycle.hpp').read_text(encoding="utf-8")
        self.assertIn("kIdleMs = 60'000", header)
        self.assertIn("kSweepMs = 5'000", header)
        self.assertIn('sweep_idle_flows(steady_now_ms())', src)
        self.assertIn('it->second.last_activity_ms = steady_now_ms()', src)  # reverse
        self.assertGreaterEqual(src.count('steady_now_ms()};'), 2)           # C2S, new + reused
        self.assertIn('removed = evict_idle_local_flows(', src)
        self.assertIn('notified.erase(it->first)', header)
        self.assertIn('it = flows.erase(it)', header)
        self.assertIn('if (event_fn)', src)
        self.assertIn('event_fn(ev);  // Always outside flows_mu.', src)
        self.assertIn('if (reset_sequence) next_flow = 1;', src)

    def test_temporary_flow_rejection_retains_route_without_ignoring_transport_failures(self):
        src = (ROOT/'app/core/src/client_core.cpp').read_text(encoding="utf-8")
        self.assertIn('if (flow_local) continue;', src)
        self.assertIn('classify_relay_reject(reason, ev.relay_flow_id)', src)
        self.assertIn('RouteEventType::RelayUnavailable || ev.type==RouteEventType::TunnelStopped', src)
        self.assertIn('event=fail_open reason=relay_admission reject=', src)
        self.assertNotIn('RouteEventType::TunnelStopped || ev.type==RouteEventType::RelayRejected', src)
        self.assertIn('"heartbeat_timeout"', src)
        self.assertIn('"wintun_stopped"', src)

if __name__ == '__main__':
    unittest.main()
