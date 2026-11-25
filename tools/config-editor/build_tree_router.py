"""Build the game's shared connection router for the browser; keep generated code outside Git."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'code/axmol/CastleGuard/Source'
LOCK = threading.Lock()


def runtime_path():
    sources = [SOURCE / name for name in ['TreeConnectionRouter.h', 'TreeConnectionRouter.cpp', 'TreeConnectionRouterWeb.cpp']]
    flags = ['-std=c++17', '-O2', '-sMODULARIZE=1', '-sEXPORT_NAME=createTreeConnectionRouter',
             '-sSINGLE_FILE=1', '-sFILESYSTEM=0', '-sALLOW_MEMORY_GROWTH=1', '-sENVIRONMENT=web,node',
             '-sEXPORTED_FUNCTIONS=["_treeRoute","_malloc","_free"]']
    digest = hashlib.sha256((' '.join(flags)).encode() + b''.join(p.read_bytes() for p in sources)).hexdigest()[:20]
    target = Path(tempfile.gettempdir()) / f'castleguard-tree-router-{digest}.js'
    with LOCK:
        if not target.exists():
            compiler = os.environ.get('CG_EMXX', str(ROOT / 'engine/axmol/tools/external/emsdk/upstream/emscripten/em++'))
            with tempfile.TemporaryDirectory(prefix='cg-tree-router-', dir=target.parent) as directory:
                output = Path(directory) / 'runtime.js'
                subprocess.run([compiler, str(sources[1]), str(sources[2]), *flags, '-o', str(output)],
                               check=True, capture_output=True, text=True, timeout=120)
                os.replace(output, target)
    return target


if __name__ == '__main__':
    print(runtime_path())
