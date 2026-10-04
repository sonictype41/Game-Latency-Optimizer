import hashlib, json, struct, unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class WindowsResourceContractTests(unittest.TestCase):
    def setUp(self):
        self.info = json.loads((ROOT / 'third_party/wintun/INFO.json').read_text(encoding='utf-8'))
        self.dll = ROOT / 'third_party/wintun' / self.info['file']['path']

    def require_dll(self):
        if not self.dll.is_file():
            self.skipTest('Pinned Wintun runtime is fetched by the Windows build/CI.')

    def test_wintun_metadata_contract(self):
        self.assertEqual('0.14.1', self.info['version'])
        self.assertEqual('https://www.wintun.net/builds/wintun-0.14.1.zip', self.info['upstream']['url'])
        self.assertEqual('07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51', self.info['upstream']['archive_sha256'])
        self.assertEqual('e5da8447dc2c320edc0fc52fa01885c103de8c118481f683643cacc3220dafce', self.info['file']['sha256'])
        self.assertEqual(427552, int(self.info['file']['size']))

    def test_wintun_metadata_and_hash(self):
        self.require_dll()
        b = self.dll.read_bytes()
        self.assertEqual(427552, len(b))
        self.assertEqual(self.info['file']['sha256'], hashlib.sha256(b).hexdigest())

    def test_wintun_is_amd64_signed_pe(self):
        self.require_dll()
        b = self.dll.read_bytes()
        pe = struct.unpack_from('<I', b, 0x3c)[0]
        self.assertEqual(b'PE\0\0', b[pe:pe+4])
        self.assertEqual(0x8664, struct.unpack_from('<H', b, pe+4)[0])
        opt = pe + 24
        self.assertEqual(0x20b, struct.unpack_from('<H', b, opt)[0])
        dd = opt + 112
        cert, size = struct.unpack_from('<II', b, dd+32)
        self.assertGreater(cert, 0)
        self.assertGreater(size, 0)

    def test_resource_tools_license_and_icon_exist(self):
        self.assertTrue((ROOT / 'tools/fetch_wintun.py').is_file())
        self.assertTrue((ROOT / 'tools/verify_windows_resources.py').is_file())
        self.assertTrue((ROOT / 'third_party/wintun/LICENSE.txt').is_file())
        installer_icon = ROOT / 'installer/resources/glo-installer.ico'
        self.assertTrue(installer_icon.is_file())
        icon = installer_icon.read_bytes()
        self.assertGreater(len(icon), 1024)
        self.assertLess(len(icon), 128*1024)
        reserved, kind, count = struct.unpack_from('<HHH', icon, 0)
        self.assertEqual((0, 1), (reserved, kind))
        self.assertGreaterEqual(count, 4)


if __name__ == '__main__':
    unittest.main()
