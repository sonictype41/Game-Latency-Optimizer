import ast
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class CiPortabilityTests(unittest.TestCase):
    def test_linux_relay_workflow_does_not_require_executable_bit(self):
        workflow = (ROOT / ".github/workflows/linux-relay.yml").read_text(encoding="utf-8")
        self.assertIn("run: bash ./tools/build_relay.sh all", workflow)

    def test_python_source_reads_are_explicit_utf8(self):
        offenders = []
        for base in (ROOT / "tests", ROOT / "tools"):
            for path in base.rglob("*.py"):
                if path == Path(__file__).resolve():
                    continue
                tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
                for node in ast.walk(tree):
                    if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Attribute):
                        continue
                    if node.func.attr != "read_text":
                        continue
                    has_encoding = any(kw.arg == "encoding" for kw in node.keywords)
                    if not has_encoding:
                        offenders.append(f"{path.relative_to(ROOT)}:{node.lineno}")
        self.assertEqual([], offenders, "read_text() must specify encoding='utf-8': " + ", ".join(offenders))

if __name__ == "__main__":
    unittest.main()
