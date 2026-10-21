"""Regression checks for Chocolatey (PowerShell) -> MSYS2 NSIS discovery.

The Windows release must not depend on Chocolatey's PATH surviving the shell
boundary. Test the finder, fail-fast behavior, and release workflow handoff.
"""
import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


def load_installer():
    spec = importlib.util.spec_from_file_location(
        "glo_nsis_discovery_under_test", ROOT / "tools" / "build_installer.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class NSISDiscoveryContractTests(unittest.TestCase):
    def test_explicit_path_works_when_nsis_is_absent_from_path(self):
        module = load_installer()
        with tempfile.TemporaryDirectory() as td:
            exe = Path(td) / "makensis.exe"
            exe.write_bytes(b"MZ")
            with mock.patch.object(module.shutil, "which", return_value=None):
                self.assertEqual(exe.resolve(), module.find_makensis(str(exe)))

    def test_environment_path_survives_powershell_to_msys2_handoff(self):
        module = load_installer()
        with tempfile.TemporaryDirectory() as td:
            exe = Path(td) / "Program Files (x86)" / "NSIS" / "makensis.exe"
            exe.parent.mkdir(parents=True)
            exe.write_bytes(b"MZ")
            with mock.patch.dict(os.environ, {"GLO_MAKENSIS": str(exe)}), mock.patch.object(
                module.shutil, "which", return_value=None
            ):
                self.assertEqual(exe.resolve(), module.find_makensis(None))

    def test_chocolatey_default_and_program_files_fallback(self):
        module = load_installer()
        with tempfile.TemporaryDirectory() as td:
            choco = Path(td) / "choco"
            exe = choco / "lib" / "nsis" / "tools" / "makensis.exe"
            exe.parent.mkdir(parents=True)
            exe.write_bytes(b"MZ")
            with mock.patch.dict(os.environ, {
                "GLO_MAKENSIS": "", "ProgramFiles(x86)": "", "ProgramFiles": "",
                "ChocolateyInstall": str(choco)
            }), mock.patch.object(module.shutil, "which", return_value=None):
                self.assertEqual(exe.resolve(), module.find_makensis(None))

    def test_wrong_explicit_path_fails_closed_instead_of_finding_some_other_compiler(self):
        module = load_installer()
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "missing-makensis.exe"
            with mock.patch.object(module.shutil, "which", return_value="/tmp/wrong/makensis"):
                with self.assertRaisesRegex(FileNotFoundError, "explicitly configured"):
                    module.find_makensis(str(p))

    def test_missing_compiler_does_not_delete_previous_installer(self):
        module = load_installer()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td) / "repo"
            (root / "installer" / "resources").mkdir(parents=True)
            for name, val in (("VERSION", "0.0.5"), ("RELEASE", "0.0.5-r2-beta"),
                              ("DISPLAY_VERSION", "0.0.5-r2-beta")):
                (root / name).write_text(val, encoding="utf-8")
            (root / "installer" / "BRANDING.json").write_text(json.dumps({
                "schema": 1, "official_url": "https://gloptimizer.com",
                "github_url": "", "facebook_url": ""
            }), encoding="utf-8")
            (root / "installer" / "resources" / "glo-installer.ico").write_bytes(b"fake")
            out = root / "bin" / "installer"
            out.mkdir(parents=True)
            old = out / "GLO-Setup-old.exe"
            old.write_bytes(b"MZold-valid-setup")
            stage = root / "build" / "installer" / "stage"
            stage.mkdir(parents=True)
            marker = stage / "previous-stage.txt"
            marker.write_text("keep until preflight succeeds", encoding="utf-8")
            old_root = module.ROOT
            module.ROOT = root
            try:
                with mock.patch.dict(os.environ, {"GLO_MAKENSIS": str(root / "absent.exe")}), mock.patch.object(
                    sys, "argv", ["build_installer.py", "--output-dir", str(out)]
                ):
                    with self.assertRaises(FileNotFoundError):
                        module.main()
            finally:
                module.ROOT = old_root
            self.assertTrue(old.exists(), "a failed NSIS lookup must preserve existing installer outputs")
            self.assertTrue(marker.exists(), "do not destroy existing staging on missing NSIS")

    def test_release_workflow_preflights_nsis_and_passes_native_path(self):
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
        self.assertIn("choco install nsis -y --no-progress", workflow)
        self.assertIn("Get-Command makensis.exe", workflow)
        self.assertIn("$LASTEXITCODE -ne 0", workflow)
        self.assertIn('"GLO_MAKENSIS=$exe" >> $env:GITHUB_ENV', workflow)
        self.assertIn('--makensis "$GLO_MAKENSIS"', workflow)
        self.assertLess(workflow.index("Install and verify NSIS"), workflow.index("Build Windows installer"))
        for name in ("windows-app.yml", "release.yml"):
            text = (ROOT / ".github" / "workflows" / name).read_text(encoding="utf-8")
            self.assertIn("tests.test_nsis_discovery_contract", text)


if __name__ == "__main__":
    unittest.main()
