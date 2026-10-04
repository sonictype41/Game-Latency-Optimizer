from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class R9SourceTests(unittest.TestCase):
 def test_quality_wire_and_relay_aggregate(self):
  h=(ROOT/'protocol/include/glo/protocol.hpp').read_text(encoding='utf-8');c=(ROOT/'app/core/src/client_core.cpp').read_text(encoding='utf-8');e=(ROOT/'relay/dataplane/events.go').read_text(encoding='utf-8');m=(ROOT/'relay/dataplane/main.go').read_text(encoding='utf-8')
  self.assertIn('kQualityReportVersion = 1',h);self.assertIn('encode_quality_report(report)',c);self.assertIn('s2c_session_loss',c);self.assertIn('RelayLatencyMs',e);self.assertIn('usage.quality',m)
 def test_official_provenance(self):
  s=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8');self.assertIn('ConfigSource::OfficialHandoff',s);self.assertIn('relay_name',s);self.assertNotIn('Official Service',s);self.assertIn('trusted?ConfigSource::OfficialHandoff:ConfigSource::ThirdPartyHandoff',s)
 def test_installer_auto_upgrade(self):
  s=(ROOT/'installer/nsis/GLO.nsi').read_text(encoding='utf-8-sig');self.assertIn('$(ProgressStopping)',s);self.assertIn('$(ProgressUninstalling)',s);self.assertIn('taskkill.exe',s);self.assertIn('PurgeExistingInstallationFiles',s)
if __name__=='__main__': unittest.main()
