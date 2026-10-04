import importlib.util,os
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


class WindowsInstallerContractTests(unittest.TestCase):
    def setUp(self):
        self.nsi = (ROOT / 'installer/nsis/GLO.nsi').read_text(encoding='utf-8-sig')

    def test_per_user_non_admin_install_contract(self):
        self.assertIn('RequestExecutionLevel user', self.nsi)
        self.assertIn('InstallDir "$LOCALAPPDATA\\GLO"', self.nsi)
        self.assertIn('SetShellVarContext current', self.nsi)
        self.assertNotIn('HKLM', self.nsi)
        self.assertIn('WriteRegStr HKCU', self.nsi)
        self.assertNotIn('MUI_PAGE_COMPONENTS', self.nsi)
        self.assertIn('Page custom OptionsPageCreate OptionsPageLeave', self.nsi)
        self.assertIn('SectionIn RO', self.nsi)

    def test_branded_bilingual_installer_contract(self):
        self.assertIn('Caption "GLO Client Installer"', self.nsi)
        self.assertIn('BrandingText "GLO | Open-source game routing"', self.nsi)
        self.assertIn('Page custom WelcomePageCreate', self.nsi)
        self.assertIn('Page custom FinishPageCreate FinishPageLeave', self.nsi)
        self.assertNotIn('MUI_PAGE_WELCOME', self.nsi)
        self.assertNotIn('MUI_PAGE_FINISH', self.nsi)
        self.assertIn('MUI_LANGUAGE "English"', self.nsi)
        self.assertIn('MUI_LANGUAGE "Vietnamese"', self.nsi)
        self.assertIn('MUI_LANGDLL_DISPLAY', self.nsi)
        self.assertIn('MUI_LANGDLL_ALWAYSSHOW', self.nsi)
        self.assertIn('Choose setup language / Chọn ngôn ngữ cài đặt', self.nsi)
        self.assertIn('Install the official GLO Client for Windows', self.nsi)
        self.assertIn('Cài đặt GLO Client chính thức cho Windows', self.nsi)
        self.assertIn('Open-source game routing. Free official service.', self.nsi)
        self.assertIn('Định tuyến game mã nguồn mở. Mạng chính thức miễn phí.', self.nsi)
        self.assertIn('Official Website', self.nsi)
        self.assertIn('GitHub', self.nsi)
        self.assertIn('Facebook', self.nsi)
        self.assertIn('DISPLAY_VERSION', self.nsi)
        self.assertIn('MUI_ICON "${INSTALLER_ICON}"', self.nsi)
        self.assertIn('MUI_UNICON "${INSTALLER_ICON}"', self.nsi)

    def test_links_are_configured_without_inventing_unpublished_social_urls(self):
        branding = json.loads((ROOT / 'installer/BRANDING.json').read_text(encoding='utf-8'))
        self.assertEqual(1, branding['schema'])
        self.assertEqual('https://gloptimizer.com', branding['official_url'])
        self.assertTrue(branding['github_url'].startswith('https://github.com/'))
        self.assertEqual('', branding['facebook_url'])
        self.assertIn('HAS_GITHUB', self.nsi)
        self.assertIn('HAS_FACEBOOK', self.nsi)
        self.assertIn('LangString LinksLabel', self.nsi)

    def test_display_and_release_versions_are_separate(self):
        version=(ROOT/'VERSION').read_text(encoding='utf-8').strip()
        release=(ROOT/'RELEASE').read_text(encoding='utf-8').strip()
        display=(ROOT/'DISPLAY_VERSION').read_text(encoding='utf-8').strip()
        self.assertRegex(version, r'^[0-9]+\.[0-9]+\.[0-9]+$')
        self.assertRegex(release, rf'^{re.escape(version)}(?:-(?:r[1-9][0-9]*|alpha(?:\.[1-9][0-9]*)?|beta(?:\.[1-9][0-9]*)?))?$')
        self.assertTrue(display==release or re.fullmatch(rf'{re.escape(version)}-[0-9]{{8}}',display))
        self.assertIn('DisplayVersion" "${DISPLAY_VERSION}"', self.nsi)
        ui=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')
        self.assertIn('kDisplayVersion[] = L"v" GLO_WIDEN(GLO_RELEASE)', ui)


    def test_nsis_static_symbol_contract(self):
        lang_defs=set(re.findall(r'^LangString\s+([A-Za-z0-9_]+)\s+', self.nsi, re.M))
        refs={name for name in re.findall(r'\$\(([A-Za-z0-9_]+)\)', self.nsi)}
        self.assertEqual(set(), refs-lang_defs)
        functions=re.findall(r'^Function\s+([^\s]+)', self.nsi, re.M)
        self.assertEqual(len(functions), len(set(functions)), 'duplicate NSIS function name')
        self.assertEqual(len(functions), len(re.findall(r'^FunctionEnd$', self.nsi, re.M)))
        self.assertEqual(self.nsi.count('${If}'), self.nsi.count('${EndIf}'))
        for line in self.nsi.splitlines():
            if not line.startswith('Page custom '):
                continue
            parts=line.split()
            create=parts[2]
            leave=parts[3] if len(parts)>3 else ''
            self.assertIn(create, functions)
            if leave:
                self.assertIn(leave, functions)
        raw=(ROOT/'installer/nsis/GLO.nsi').read_bytes()
        self.assertTrue(raw.startswith(b'\xef\xbb\xbf'), 'Unicode NSIS source must keep UTF-8 BOM')

    def test_uninstaller_and_shortcuts(self):
        self.assertIn('WriteUninstaller "$INSTDIR\\uninstall0000.exe"', self.nsi)
        self.assertIn('GLOGenericClientV0170', self.nsi)
        self.assertIn('${WM_CLOSE}', self.nsi)
        self.assertIn('taskkill.exe', self.nsi.lower())
        self.assertIn('CreateDirectory "$SMPROGRAMS\\GLO"', self.nsi)
        self.assertIn('CreateDesktopShortcut', self.nsi)

    def test_existing_install_is_uninstalled_before_new_files_are_copied(self):
        self.assertIn('Var ExistingInstallDir', self.nsi)
        self.assertIn('ReadRegStr $ExistingInstallDir HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GLO" "InstallLocation"', self.nsi)
        self.assertIn('Function RemoveExistingInstallation', self.nsi)
        self.assertIn("ExecWait '\"$ExistingUninstaller\" /S' $0", self.nsi)
        self.assertIn('existing_retry:', self.nsi);self.assertIn('Call PurgeExistingInstallationFiles', self.nsi);self.assertIn('IntCmp $3 20', self.nsi)
        section=self.nsi[self.nsi.index('Section "GLO Client" SEC_MAIN'):]
        self.assertLess(section.index('Call RemoveExistingInstallation'), section.index('File /oname=GLO.exe'))
        self.assertIn('writes a fresh protocol handler only after the old installation is gone', self.nsi)

    def test_installer_uses_staging_not_raw_third_party(self):
        self.assertIn('${STAGE_DIR}\\wintun.dll', self.nsi)
        self.assertIn('${STAGE_DIR}\\WINTUN_INFO.json', self.nsi)
        self.assertNotIn('third_party\\wintun', self.nsi)

    def test_uninstall_preserves_user_settings_and_logs(self):
        self.assertIn('settings.json and logs are user data and intentionally survive uninstall', self.nsi)
        self.assertNotIn('RMDir /r "$INSTDIR"', self.nsi)

    def test_msys_makensis_and_cached_build_contract(self):
        builder = (ROOT / 'tools/build_installer.py').read_text(encoding='utf-8')
        app_builder = (ROOT / 'tools/build_app.sh').read_text(encoding='utf-8')
        self.assertNotIn('cygpath', builder)
        self.assertIn('write_nsis_driver', builder)
        self.assertIn('GLO.generated.nsi', builder)
        self.assertIn('All filesystem references consumed by makensis are relative', builder)
        self.assertIn('local_script = installer_build_root / "GLO.nsi"', builder)
        self.assertIn('local_icon = installer_build_root / "glo-installer.ico"', builder)
        self.assertNotIn('f"/DSTAGE_DIR=', builder)
        self.assertNotIn('f"/DOUT_FILE=', builder)
        self.assertNotIn('f"/DRELEASE_VERSION=', builder)
        self.assertNotIn('f"/DPROTOCOL_VERSION=', builder)
        self.assertIn('shutil.rmtree(installer_build_root)', builder)
        self.assertIn('GLO-Setup-*.exe', builder)
        self.assertIn('GLO_BUILD_CACHE_DIR', app_builder)
        self.assertIn('SODIUM_KEY=', app_builder)
        self.assertIn('CMAKE_HIT=', app_builder)
        self.assertNotIn('ccache-msys-launcher.sh', app_builder)
        self.assertIn('MINGW_PREFIX', app_builder)
        self.assertIn('MINGW_PACKAGE_PREFIX', app_builder)
        self.assertIn('/bin/ccache.exe', app_builder)
        self.assertIn('native MinGW ccache compile probe failed', app_builder)
        self.assertIn('pacman -S $PACKAGE_HINT', app_builder)
        self.assertIn('ccache compiler [compiler options]', app_builder)
        self.assertNotIn('cygpath -u "$compiler"', app_builder)
        self.assertIn('-DCMAKE_C_COMPILER_LAUNCHER=', app_builder)
        self.assertIn('-DCMAKE_CXX_COMPILER_LAUNCHER=', app_builder)
        self.assertNotIn('rm -rf "$BUILD" "$OUT_ROOT/app"', app_builder)

    def test_builder_invokes_makensis_with_driver_only(self):
        spec = importlib.util.spec_from_file_location('glo_build_installer', ROOT / 'tools/build_installer.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix='glo installer $ ') as td:
            root = Path(td) / 'OSS'
            (root / 'installer/nsis').mkdir(parents=True)
            (root / 'installer/resources').mkdir(parents=True)
            (root / 'tools').mkdir()
            (root / 'VERSION').write_text('9.8.7\n', encoding='utf-8')
            (root / 'RELEASE').write_text('9.8.7-r3\n', encoding='utf-8')
            (root / 'DISPLAY_VERSION').write_text('9.8.7-12312026\n', encoding='utf-8')
            (root / 'installer/BRANDING.json').write_text(json.dumps({
                'schema': 1,
                'official_url': 'https://gloptimizer.com',
                'github_url': '',
                'facebook_url': '',
            }), encoding='utf-8')
            (root / 'installer/resources/glo-installer.ico').write_bytes(b'ico-test')
            (root / 'installer/nsis/GLO.nsi').write_text('; test main script\n', encoding='utf-8')
            fake_makensis = Path(td) / 'makensis.exe'
            fake_makensis.write_bytes(b'MZ')
            build_dir = Path(td) / 'bin/app'
            build_dir.mkdir(parents=True)
            out = Path(td) / 'bin/installer'
            out.mkdir(parents=True)
            unrelated = out / 'keep-me.txt'
            unrelated.write_text('keep', encoding='utf-8')
            stale = out / 'GLO-Setup-old.exe'
            stale.write_bytes(b'MZold')
            calls = []

            def fake_run(argv, check=True, **kwargs):
                calls.append([str(x) for x in argv])
                if len(calls) == 2:
                    self.assertEqual(2, len(argv), 'makensis must receive only executable + generated .nsi driver')
                    self.assertFalse(any(str(x).startswith('/D') for x in argv[1:]))
                    self.assertEqual('GLO.generated.nsi', str(argv[1]))
                    self.assertIn('cwd', kwargs)
                    driver = Path(kwargs['cwd']) / str(argv[1])
                    text = driver.read_text(encoding='utf-8-sig')
                    for name in (
                        'STAGE_DIR', 'OUT_FILE', 'RELEASE_VERSION', 'PROTOCOL_VERSION',
                        'DISPLAY_VERSION', 'OFFICIAL_URL', 'GITHUB_URL', 'FACEBOOK_URL',
                        'HAS_GITHUB', 'HAS_FACEBOOK', 'INSTALLER_ICON'
                    ):
                        self.assertIsNotNone(re.search(rf'^!define {name} ".*"$', text, re.M), text)
                    self.assertIn('!define RELEASE_VERSION "9.8.7-r3"', text)
                    self.assertIn('!define DISPLAY_VERSION "9.8.7-12312026"', text)
                    self.assertIn('!define OFFICIAL_URL "https://gloptimizer.com"', text)
                    self.assertIn('!define HAS_GITHUB "0"', text)
                    self.assertIn('!define HAS_FACEBOOK "0"', text)
                    self.assertIn('!define STAGE_DIR "stage"', text)
                    self.assertIn('!define OUT_FILE "output/GLO-Setup-9.8.7-r3-x64.exe"', text)
                    self.assertIn('!define INSTALLER_ICON "glo-installer.ico"', text)
                    self.assertIn('!include "GLO.nsi"', text)
                    installer = Path(kwargs['cwd']) / 'output' / 'GLO-Setup-9.8.7-r3-x64.exe'
                    installer.parent.mkdir(parents=True, exist_ok=True)
                    installer.write_bytes(b'MZ' + b'0' * 128)
                return mock.Mock(returncode=0)

            old_root = module.ROOT
            module.ROOT = root
            argv = ['build_installer.py', '--build-dir', str(build_dir), '--output-dir', str(out), '--makensis', str(fake_makensis)]
            try:
                with mock.patch.object(module.subprocess, 'run', side_effect=fake_run), mock.patch.object(sys, 'argv', argv), mock.patch.dict(os.environ, {'GLO_BUILD_CACHE_DIR': str(Path(td) / 'cache')}):
                    module.main()
            finally:
                module.ROOT = old_root
            self.assertEqual(2, len(calls))
            self.assertEqual(str(fake_makensis.resolve()), calls[1][0])
            self.assertTrue((out / 'GLO-Setup-9.8.7-r3-x64.exe').is_file())
            self.assertTrue((out / 'GLO-Setup-9.8.7-r3-x64.exe.sha256').is_file())
            self.assertFalse(stale.exists())
            self.assertEqual('keep', unrelated.read_text(encoding='utf-8'))

    def test_branding_url_validation(self):
        spec = importlib.util.spec_from_file_location('glo_build_installer_branding', ROOT / 'tools/build_installer.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / 'installer').mkdir()
            old_root = module.ROOT
            module.ROOT = root
            try:
                def write(data):
                    (root / 'installer/BRANDING.json').write_text(json.dumps(data), encoding='utf-8')
                write({'schema': 1, 'official_url': 'https://example.test', 'github_url': 'https://example.test/repo', 'facebook_url': ''})
                with self.assertRaises(ValueError):
                    module.read_branding()
                write({'schema': 1, 'official_url': 'https://example.test', 'github_url': 'https://github.com/', 'facebook_url': ''})
                with self.assertRaises(ValueError):
                    module.read_branding()
                write({'schema': 1, 'official_url': 'https://example.test', 'github_url': '', 'facebook_url': 'https://facebook.com/'})
                with self.assertRaises(ValueError):
                    module.read_branding()
                write({'schema': 1, 'official_url': 'http://example.test', 'github_url': '', 'facebook_url': ''})
                with self.assertRaises(ValueError):
                    module.read_branding()
                write({'schema': 1, 'official_url': 'https://example.test', 'github_url': 'https://github.com/example/glo', 'facebook_url': 'https://facebook.com/example'})
                result = module.read_branding()
                self.assertEqual('https://github.com/example/glo', result['github_url'])
                self.assertEqual('https://facebook.com/example', result['facebook_url'])
            finally:
                module.ROOT = old_root

    def test_nsis_define_quoting(self):
        spec = importlib.util.spec_from_file_location('glo_build_installer_quote', ROOT / 'tools/build_installer.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        quoted = module.nsis_quote('C:\\Users\\A $ B\\"quoted"')
        self.assertEqual('"C:\\Users\\A $$ B\\$\\"quoted$\\""', quoted)
        with self.assertRaises(ValueError):
            module.nsis_quote('bad\nvalue')

    def test_build_scripts_are_offline(self):
        text = '\n'.join((ROOT / p).read_text(encoding='utf-8') for p in ['tools/build_installer.py', 'tools/package_windows.py'])
        for marker in ('requests.get', 'urllib.request', 'Invoke-WebRequest', 'curl ', 'wget '):
            self.assertNotIn(marker, text)


if __name__ == '__main__':
    unittest.main()
