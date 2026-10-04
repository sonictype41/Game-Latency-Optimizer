import importlib.util
import os
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/build_app.sh'
HELPER = ROOT / 'tools/build_cache_fingerprint.py'


def load_helper():
    spec = importlib.util.spec_from_file_location('glo_build_cache_fingerprint', HELPER)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class BuildCacheContractTests(unittest.TestCase):
    def test_mutable_cmake_tree_is_content_fingerprinted(self):
        text = SCRIPT.read_text(encoding='utf-8')
        self.assertIn('BUILD_CACHE_SCHEMA="glo-app-cmake-v2"', text)
        self.assertIn('SOURCE_FINGERPRINT=', text)
        self.assertIn('.glo-source-fingerprint', text)
        self.assertIn('CMAKE_CACHE_STATE="INVALIDATED"', text)
        self.assertIn('build inputs changed', text)
        self.assertIn('legacy/incomplete tree has no source fingerprint', text)
        self.assertIn('LEGACY_ROOT_KEY=', text)
        self.assertIn('legacy mutable tree purged; immutable caches preserved', text)
        self.assertIn('rm -rf "$BUILD"', text)
        self.assertIn('rm -rf "$LEGACY_BUILD"', text)
        self.assertIn('Commit the fingerprint only after a successful configure/build', text)

    def test_immutable_and_compiler_caches_survive_mutable_invalidation(self):
        text = SCRIPT.read_text(encoding='utf-8')
        # libsodium remains independently keyed by immutable source checksum/toolchain.
        self.assertIn('SODIUM_KEY="$(hash_text "$EXPECTED|$TOOLCHAIN_ID|$CONFIG_ARGS")"', text)
        self.assertNotIn('SOURCE_FINGERPRINT', text[text.index('SODIUM_KEY='):text.index('SODIUM_HIT=0')])
        # ccache remains outside the mutable CMake build tree, so unchanged translation
        # units can be reused even after the CMake/Ninja tree is invalidated.
        self.assertIn('"$CACHE_ROOT/ccache"', text)
        self.assertIn('export CCACHE_DIR=', text)
        self.assertNotIn('rm -rf "$CACHE_ROOT/ccache"', text)

    def test_fingerprint_uses_contents_not_mtime_and_ignores_unrelated_tests(self):
        helper = load_helper()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for rel, data in {
                'CMakeLists.txt': 'project(test)\n',
                'VERSION': '0.17.0\n',
                'RELEASE': '0.17.1\n',
                'cmake/ClientPolicy.cmake': 'set(X 1)\n',
                'app/frontend/main.cpp': 'int main(){return 0;}\n',
                'protocol/protocol.cpp': 'int protocol(){return 1;}\n',
                'secure_transport/src/secure_transport.cpp': 'int secure(){return 2;}\n',
                'third_party/wintun/INFO.json': '{}\n',
                'tools/build_app.sh': '#!/bin/sh\n',
                'tools/build_cache_fingerprint.py': '# helper\n',
                'tools/stamp_build_info.py': '# stamp\n',
            }.items():
                path = root / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(data, encoding='utf-8')
            first = helper.compute_fingerprint(root)
            main = root / 'app/frontend/main.cpp'
            now = time.time() + 100
            os.utime(main, (now, now))
            self.assertEqual(first, helper.compute_fingerprint(root), 'mtime-only changes must not invalidate')

            unrelated = root / 'tests/diagnostic.txt'
            unrelated.parent.mkdir(parents=True, exist_ok=True)
            unrelated.write_text('not a build input\n', encoding='utf-8')
            self.assertEqual(first, helper.compute_fingerprint(root), 'unrelated tests must not invalidate app build tree')

            wintun_info = root / 'third_party/wintun/INFO.json'
            wintun_info.write_text('{\"sha256\":\"changed\"}\n', encoding='utf-8')
            self.assertNotEqual(first, helper.compute_fingerprint(root), 'pinned Wintun metadata changes must invalidate')
            wintun_info.write_text('{}\n', encoding='utf-8')
            self.assertEqual(first, helper.compute_fingerprint(root))

            main.write_text('int main(){return 7;}\n', encoding='utf-8')
            self.assertNotEqual(first, helper.compute_fingerprint(root), 'source byte changes must invalidate')

    def test_real_source_fingerprint_is_stable_sha256(self):
        helper = load_helper()
        value = helper.compute_fingerprint(ROOT)
        self.assertRegex(value, r'^[0-9a-f]{64}$')
        self.assertEqual(value, helper.compute_fingerprint(ROOT))


if __name__ == '__main__':
    unittest.main(verbosity=2)
