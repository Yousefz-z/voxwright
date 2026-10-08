#!/usr/bin/env python3
"""Repository hygiene check run by CI and before every commit.

Fails when any tracked or new (non-ignored) text file contains:
  * the names of tools or vendors that must not appear in this repository,
  * attribution trailers in files,
  * em or en dash characters.

Patterns are stored reversed so this file does not match itself.
Usage: python3 tools/hygiene/check_hygiene.py [--commit-msg FILE]
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# Each entry: (reversed phrase, case sensitive, whole word, description).
# Reversed text keeps the literal words out of this file.
_RULES = [
    ("edualc", False, True, "assistant name"),
    ("ciporhtna", False, True, "vendor name"),
    ("domeciov", False, False, "reference product name"),
    ("IA", True, True, "abbreviation for machine intelligence"),
    ("MLL", True, True, "abbreviation for language models"),
    ("tpgtahc", False, False, "assistant name"),
    ("tolipoc", False, True, "assistant name"),
    (":yb-derohtua-oc", False, False, "attribution trailer"),
    ("htiw detareneg", False, False, "attribution phrase"),
    ("yb detareneg", False, False, "attribution phrase"),
    ("detareneg-otua", False, False, "attribution phrase"),
    ("edoc detareneg", False, False, "attribution phrase"),
]
_DASHES = {chr(0x2014): "em dash", chr(0x2013): "en dash"}
_BINARY_SUFFIXES = {".png", ".ico", ".icns", ".wav", ".onnx", ".ttf", ".otf", ".bin", ".gz", ".zip"}


def _compile_rules() -> list[tuple[re.Pattern[str], str]]:
    compiled = []
    for reversed_phrase, case_sensitive, whole_word, description in _RULES:
        pattern = re.escape(reversed_phrase[::-1])
        if whole_word:
            pattern = rf"\b{pattern}\b"
        flags = 0 if case_sensitive else re.IGNORECASE
        compiled.append((re.compile(pattern, flags), description))
    return compiled


def _repository_files(root: Path) -> list[Path]:
    output = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=root,
        check=True,
        capture_output=True,
    ).stdout
    return [root / name for name in output.decode("utf-8").split("\0") if name]


def _scan_text(text: str, label: str, rules) -> list[str]:
    problems = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        for pattern, description in rules:
            if pattern.search(line):
                problems.append(f"{label}:{line_number}: forbidden {description}")
        for char, description in _DASHES.items():
            if char in line:
                problems.append(f"{label}:{line_number}: forbidden {description} character")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--commit-msg", type=Path, help="also check a commit message file")
    args = parser.parse_args()

    root = Path(
        subprocess.run(["git", "rev-parse", "--show-toplevel"], check=True, capture_output=True, text=True)
        .stdout.strip()
    )
    rules = _compile_rules()
    problems: list[str] = []
    for path in _repository_files(root):
        if path.suffix.lower() in _BINARY_SUFFIXES or not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            problems.append(f"{path.relative_to(root)}: not valid UTF-8 text")
            continue
        problems.extend(_scan_text(text, str(path.relative_to(root)), rules))
    if args.commit_msg:
        problems.extend(_scan_text(args.commit_msg.read_text(encoding="utf-8"), "commit message", rules))

    for problem in problems:
        print(problem)
    if problems:
        print(f"\n{len(problems)} hygiene problem(s) found.")
        return 1
    print("Hygiene check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
