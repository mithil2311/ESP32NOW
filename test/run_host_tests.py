#!/usr/bin/env python3
"""Test the actual firmware contact/LED logic without uploading to a board."""
from pathlib import Path
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="espnow-tests-") as directory:
    executable = Path(directory) / "range_link_tests"
    subprocess.run([
        "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
        "-I", str(project / "include"),
        str(project / "test/host/test_range_link.cpp"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
