import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_notes_helper():
    path = ROOT / "tools" / "extract_release_notes.py"
    spec = importlib.util.spec_from_file_location("glo_release_notes", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class ReleaseWorkflowContractTests(unittest.TestCase):
    def test_current_release_has_extractable_changelog_notes(self):
        helper = load_notes_helper()
        release = (ROOT / "RELEASE").read_text(encoding="utf-8").strip()
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        notes = helper.extract_entry(changelog, f"v{release}")
        self.assertIn("### Added", notes)
        self.assertIn("### Fixed", notes)
        self.assertNotIn(f"## [{release}]", notes)

    def test_release_workflow_auto_tags_main_and_publishes(self):
        workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        for required in (
            'branches:',
            '- main',
            'workflow_dispatch:',
            'permissions:',
            'contents: write',
            'git ls-remote --exit-code --tags origin',
            'gh release view "$TAG"',
            'gh api --method DELETE',
            'Deleting orphan tag',
            '--target "$GITHUB_SHA"',
            'python ./tools/fetch_wintun.py',
            'python ./tools/build_installer.py',
            'bash ./tools/build_relay.sh all',
            'tools/extract_release_notes.py',
            'SHA256SUMS.txt',
            'gh release create',
            'Existing prerelease $TAG will be rebuilt and its assets replaced',
            'Moving mutable prerelease tag $TAG',
            'gh release edit "$TAG"',
            'gh release upload "$TAG" dist/* --clobber',
            '-F force=true',
            '--notes-file RELEASE_NOTES.md',
            '--latest',
            'gh release view "$TAG"',
            'gh api --method DELETE',
            'Deleting orphan tag',
        ):
            self.assertIn(required, workflow)
        self.assertNotIn('tags:\n      - "v*"', workflow)
        self.assertNotIn("Publish prerelease", workflow)
        self.assertNotIn("Publish stable release", workflow)
        self.assertNotIn("PRERELEASE", workflow)
        self.assertIn('Stable GitHub Release $TAG already exists; stable releases are immutable.', workflow)
        self.assertNotIn('GitHub Release $TAG already exists; no new release is needed.', workflow)


    def test_public_build_and_release_files_have_no_machine_specific_paths(self):
        paths = [
            ROOT / "tools/build_app.sh",
            ROOT / "tools/build_relay.sh",
            ROOT / "tools/build_installer.py",
            ROOT / ".github/workflows/windows-app.yml",
            ROOT / ".github/workflows/linux-relay.yml",
            ROOT / ".github/workflows/release.yml",
        ]
        text = "\n".join(p.read_text(encoding="utf-8") for p in paths)
        for forbidden in (
            "Admin_Sonic", "C:/Users/", "C:\\Users\\", "/c/Users/",
            "D:/a/", "/home/runner/", "DESKTOP-", "src/OSS",
        ):
            self.assertNotIn(forbidden, text)

    def test_release_workflow_does_not_publish_service_or_private_assets(self):
        workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        for forbidden in ('src/' + 'Production', 'safe/', 'prod.zip', '_prod.zip'):
            self.assertNotIn(forbidden, workflow)


if __name__ == "__main__":
    unittest.main()
