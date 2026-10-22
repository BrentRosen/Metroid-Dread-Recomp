"""Turn the installed (verified) Dread v2.1.0 build sources into anchored patches against fresh codegen output.
Usage: make_patches.py PRISTINE_BUILD_DIR INSTALLED_BUILD_DIR OUT.json FILE...
Each patch is [old, new] where old occurs exactly once in the pristine file (context grows until unique)."""
import sys, json, difflib
from pathlib import Path
pri, ins, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
res = {}
for name in sys.argv[4:]:
    a = (pri / name).read_bytes().decode('latin-1').replace('\r\n', '\n')
    b = (ins / name).read_bytes().decode('latin-1').replace('\r\n', '\n')
    al, bl = a.split('\n'), b.split('\n')
    ops = [o for o in difflib.SequenceMatcher(None, al, bl, autojunk=False).get_opcodes() if o[0] != 'equal']
    pairs = []
    for tag, i1, i2, j1, j2 in ops:
        ctx = 1
        while True:
            s, e = max(0, i1 - ctx), min(len(al), i2 + ctx)
            old = '\n'.join(al[s:e])
            if a.count(old) == 1: break
            ctx += 1
        new = '\n'.join(al[s:i1] + bl[j1:j2] + al[i2:e])
        pairs.append([old, new])
    t = a
    for old, new in pairs:
        assert t.count(old) == 1, (name, old[:80]); t = t.replace(old, new)
    assert t == b, name
    res[name] = pairs
out.write_text(json.dumps(res, indent=1)); print(out, {k: len(v) for k, v in res.items()})
