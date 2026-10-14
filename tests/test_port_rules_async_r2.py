from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class R2Contracts(unittest.TestCase):
 def test_ipc_port_rules_and_wintun(self):
  client=(ROOT/'app/core/src/network_worker.cpp').read_text(encoding='utf-8')
  tunnel=(ROOT/'app/core/src/wintun_tunnel.cpp').read_text(encoding='utf-8')
  self.assertIn('parse_udp_port_rules(expr,opt.port_rules)',client)
  self.assertIn('udp_port_rules_text(options.port_rules)',client)
  self.assertIn('udp_port_allowed(*game_port_rules.load',tunnel)
 def test_nonblocking_handoff(self):
  ui=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
  self.assertIn('WM_GLO_HANDOFF_DONE',ui)
  self.assertIn('void cancel_handoff()',ui)
  self.assertIn('task->cancelled.load()',ui)
  proc=ui[ui.index('LRESULT CALLBACK wnd_proc('):]
  self.assertNotIn('http_post_json(',proc)
  self.assertNotIn('measure_relay_candidates(',proc)
  self.assertIn('InAppModal::HandoffBusy',proc)
  self.assertIn('run_handoff_task(task,uri,hwnd)',ui)
  self.assertIn('std::thread([task,uri,hwnd=',ui)
if __name__=='__main__':unittest.main()
