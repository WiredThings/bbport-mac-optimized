#!/usr/bin/env python3
"""Reconfigure the bundled source from a relocated prototype directory."""
from pathlib import Path
import json,subprocess
p=Path(__file__).resolve().parent
args=json.loads((p/'evidence/configure.json').read_text())
old=next(x.split('=',1)[1] for x in args if x.startswith('-DBB_ARM64_PROTOTYPE_DIR='))
subprocess.run([x.replace(old,str(p)) for x in args],check=True,cwd=p)
