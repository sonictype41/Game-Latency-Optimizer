import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEXT_EXTS = {'.md', '.txt', '.py', '.sh', '.cpp', '.hpp', '.h', '.c', '.cc', '.go', '.json', '.yml', '.yaml', '.nsi', '.cmake', ''}
SKIP_DIRS = {'vendor', 'third_party', '.git', 'bin', 'build', '__pycache__'}

# Keep these split so this regression test does not contain the forbidden
# literals it is designed to detect in published project-owned source/docs.
FORBIDDEN = [
    'src/' + 'Production',
    'Production/' + 'Backend',
    'Frontend/' + 'Panel',
    'Node' + 'Agent',
    'Turn' + 'stile',
    'X' + 'DP',
    'glo-update' + '-',
    'sonictype31' + '.workers.dev',
    '/opt/' + 'glo',
    '/etc/' + 'systemd',
    '/usr/local/' + 'libexec',
    '/run/' + 'glo',
    'auth-' + 'config.json',
    'gateway' + '.json',
    'deploy-' + 'manifest',
    'private ' + 'analytics',
    'node ' + 'orchestration',
    'operator ' + 'Panel',
]

SECRET_SUFFIXES = {'.key', '.pem', '.p12', '.pfx', '.secret'}
SECRET_NAMES = {'.env', '.env.local', '.env.production'}
SECRET_PATTERNS = [
    re.compile(r'BEGIN (?:RSA |EC |OPENSSH |PRIVATE )?PRIVATE KEY', re.I),
    re.compile(r'client[_-]?secret\s*[:=]', re.I),
    re.compile(r'cloudflare.{0,30}(?:token|secret)\s*[:=]', re.I),
    re.compile(r'api[_-]?key\s*[:=]\s*[A-Za-z0-9_-]{16,}', re.I),
]


def project_files():
    for path in ROOT.rglob('*'):
        if not path.is_file():
            continue
        if any(part in SKIP_DIRS for part in path.relative_to(ROOT).parts):
            continue
        yield path


class PublicBoundaryTests(unittest.TestCase):
    def test_no_service_private_tree_or_infrastructure_names(self):
        violations = []
        for path in project_files():
            if path == Path(__file__):
                continue
            if path.suffix.lower() not in TEXT_EXTS:
                continue
            text = path.read_text(encoding='utf-8', errors='ignore')
            for token in FORBIDDEN:
                if token.lower() in text.lower():
                    violations.append(f'{path.relative_to(ROOT)}: {token}')
        self.assertEqual([], violations)

    def test_no_secret_files_or_private_key_material(self):
        bad_files = []
        bad_text = []
        for path in project_files():
            rel = path.relative_to(ROOT)
            if path.suffix.lower() in SECRET_SUFFIXES or path.name.lower() in SECRET_NAMES:
                bad_files.append(str(rel))
                continue
            if path.suffix.lower() not in TEXT_EXTS:
                continue
            text = path.read_text(encoding='utf-8', errors='ignore')
            if any(p.search(text) for p in SECRET_PATTERNS):
                bad_text.append(str(rel))
        self.assertEqual([], bad_files)
        self.assertEqual([], bad_text)


if __name__ == '__main__':
    unittest.main()
