import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class BrandPolicyTests(unittest.TestCase):
    def test_policy_and_readme_boundary(self):
        policy=(ROOT/'BRAND_POLICY.md').read_text(encoding='utf-8')
        readme=(ROOT/'README.md').read_text(encoding='utf-8')
        self.assertIn('The MIT license',policy)
        self.assertIn('must not',policy)
        self.assertIn('BRAND_POLICY.md',readme)
        self.assertNotIn('# GLO v',readme)
        self.assertIn('CHANGELOG.md',readme)
        self.assertIn('https://github.com/sonictype41/Game-Latency-Optimizer',policy)
    def test_docs_brand_assets_are_target_sized(self):
        for name,limit in [('glo-wordmark-640.png',128*1024),('glo-mark-192.png',64*1024)]:
            p=ROOT/'docs/assets'/name
            self.assertTrue(p.is_file())
            self.assertLess(p.stat().st_size,limit)
if __name__=='__main__':unittest.main()
