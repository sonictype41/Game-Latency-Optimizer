"""Regression checks for reliable MSYS2 NSIS installer discovery.

The Windows release must not depend on the Chocolatey V2 feed or on ambiguous
PATH differences. Test explicit native-path handoff and failure behavior.
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
        # Use MSYS2, not Chocolatey's V2 API which can return a transient 504.
        self.assertIn("mingw-w64-x86_64-nsis", workflow)
        self.assertNotIn("choco install", workflow)
        self.assertIn("command -v makensis.exe", workflow)
        self.assertIn('MSYS_NO_PATHCONV=1 "$exe" /VERSION', workflow)
        self.assertIn('native_exe="$(cygpath -w "$exe")"', workflow)
        self.assertIn('echo "GLO_MAKENSIS=$native_exe" >> "$GITHUB_ENV"', workflow)
        self.assertIn('--makensis "$GLO_MAKENSIS"', workflow)
        self.assertLess(workflow.index("Verify NSIS compiler"), workflow.index("Build Windows installer"))
        for name in ("windows-app.yml", "release.yml"):
            text = (ROOT / ".github" / "workflows" / name).read_text(encoding="utf-8")
            self.assertIn("tests.test_nsis_discovery_contract", text)

    def test_msys2_nsis_is_in_release_windows_job_package_list(self):
        try:
            import yaml
        except ImportError:
            self.skipTest("PyYAML unavailable")
        workflow = yaml.safe_load((ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8"))
        windows = workflow["jobs"]["windows"]
        steps = windows["steps"]
        setup = next(step for step in steps if step.get("uses", "").startswith("msys2/setup-msys2@"))
        self.assertIn("mingw-w64-x86_64-nsis", setup["with"]["install"].split())
        self.assertIn("mingw-w64-x86_64-gcc", setup["with"]["install"].split())
        verify_pos = next(i for i, step in enumerate(steps) if step.get("name", "").startswith("Verify NSIS compiler"))
        builder_pos = next(i for i, step in enumerate(steps) if step.get("name", "") == "Build Windows installer")
        self.assertLess(verify_pos, builder_pos)
        preflight = steps[verify_pos]
        self.assertNotIn("shell", preflight)  # inherits the MSYS2 shell
        self.assertIn('exit 1', preflight["run"])


if __name__ == "__main__":
    unittest.main()
