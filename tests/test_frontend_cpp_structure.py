"""Source-level preflight for the Win32 frontend.

This catches accidentally commented-out closing braces even on hosts where the
Windows SDK/MinGW toolchain is unavailable. It does not replace a Windows build.
"""

from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
FRONTEND = ROOT / "app/frontend/main.cpp"


def structural_brace_errors(source: str) -> list[str]:
    """Find unmatched C++ braces while ignoring literals and comments."""
    stack: list[tuple[int, int]] = []
    errors: list[str] = []
    i = 0
    line = 1
    n = len(source)
    while i < n:
        ch = source[i]
        nxt = source[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            eol = source.find("\n", i + 2)
            if eol == -1:
                break
            i = eol
            continue
        if ch == "/" and nxt == "*":
            end = source.find("*/", i + 2)
            if end == -1:
                errors.append(f"Unterminated block comment at line {line}")
                break
            line += source.count("\n", i, end + 2)
            i = end + 2
            continue
        # C++ raw string literals: R"delimiter(contents)delimiter".
        if ch == "R" and nxt == '"':
            opening = source.find("(", i + 2)
            if opening != -1 and opening - i <= 18:
                delimiter = source[i + 2:opening]
                if not any(c in ' \\\t\r\n()' for c in delimiter):
                    end = source.find(")" + delimiter + '"', opening + 1)
                    if end == -1:
                        errors.append(f"Unterminated raw string at line {line}")
                        break
                    after = end + len(delimiter) + 2
                    line += source.count("\n", i, after)
                    i = after
                    continue
        if ch in ('"', "'"):
            opening_line = line
            quote = ch
            i += 1
            while i < n:
                if source[i] == "\\":
                    if i + 1 < n and source[i + 1] == "\n":
                        line += 1
                    i += 2
                    continue
                if source[i] == quote:
                    i += 1
                    break
                if source[i] == "\n":
                    line += 1
                i += 1
            else:
                errors.append(f"Unterminated quoted literal at line {opening_line}")
            continue
        if ch == "{":
            stack.append((line, i))
        elif ch == "}":
            if stack:
                stack.pop()
            else:
                errors.append(f"Unexpected closing brace at line {line}")
        elif ch == "\n":
            line += 1
        i += 1
    errors.extend(f"Unclosed opening brace at line {where}" for where, _ in stack)
    return errors


class FrontendCppStructure(unittest.TestCase):
    def test_main_cpp_braces_balance(self):
        source = FRONTEND.read_text(encoding="utf-8")
        self.assertEqual([], structural_brace_errors(source))

    def test_rejects_a_brace_accidentally_inside_line_comment(self):
        source = FRONTEND.read_text(encoding="utf-8")
        good = "// Worker thread: never dereference UI state while probing.\n    }"
        self.assertIn(good, source)
        broken = source.replace(good, "// Worker thread: never dereference UI state while probing.}", 1)
        self.assertTrue(structural_brace_errors(broken))

    def test_relay_probe_function_compiles_with_native_cpp(self):
        # Compile the *actual extracted* C++ function, not a hand-written model.
        # This catches the missing closing brace that previously broke WinMain.
        compiler = shutil.which("g++") or shutil.which("clang++")
        if not compiler:
            self.skipTest("No native C++ compiler available")
        source = FRONTEND.read_text(encoding="utf-8")
        start = source.index("std::vector<RelayCandidate> measure_relay_candidates(")
        end = source.index("bool url_decode_component(", start)
        unit = (
            "#include <vector>\n#include <optional>\n#include <future>\n"
            "#include <string>\n#include <algorithm>\n#include <cstddef>\n"
            "struct RelayCandidate { std::string id; double rtt_ms = 1e30; };\n"
            "std::optional<double> probe_relay_candidate(const RelayCandidate&);\n"
            + source[start:end]
            + "\nint main() { return 0; }\n"
        )
        result = subprocess.run(
            [compiler, "-std=c++20", "-fsyntax-only", "-x", "c++", "-"],
            input=unit, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, check=False, timeout=30,
        )
        self.assertEqual(0, result.returncode, result.stderr)

    def test_winmain_is_outside_anonymous_namespace(self):
        source = FRONTEND.read_text(encoding="utf-8")
        self.assertRegex(source, r"(?m)^}\s*\n\s*int WINAPI WinMain\(")
        self.assertEqual(1, len(re.findall(r"(?m)^int WINAPI WinMain\(", source)))


if __name__ == "__main__":
    unittest.main()
