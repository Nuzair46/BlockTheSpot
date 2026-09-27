#!/usr/bin/env python3
"""Build and run portable tests with a C++20 compiler (Linux/macOS)."""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
out = root / 'out' / 'tools'
out.mkdir(parents=True, exist_ok=True)
compiler = os.environ.get('CXX', 'g++')
flags = ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-g']
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
for source, name in [('tests/core.cpp', 'core-tests'), ('tests/mods.cpp', 'mod-tests'), ('tools/patch-tool.cpp', 'patch-tool')]:
    subprocess.run([compiler, *flags, str(root/source), '-o', str(out/name)], check=True)
subprocess.run([str(out/'core-tests')], check=True)
subprocess.run([str(out/'mod-tests')], check=True)
subprocess.run([str(out/'patch-tool'), 'inspect', str(root/'config.ini')], check=True)
