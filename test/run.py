#!/usr/bin/env python3
"""Build protocol-independent rendering and calibration tests as C++17."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xteink-rendering-') as directory:
    for name in ('test', 'calibration'):
        executable = Path(directory) / name
        subprocess.run([
            os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
            '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-I' + str(ROOT / 'include'), str(ROOT / 'test' / (name + '.cpp')),
            '-o', str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
