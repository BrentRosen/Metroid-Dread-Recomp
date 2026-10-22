"""Turn the installed (verified) Dread v2.1.0 build sources into the patch set setup applies to fresh codegen output.
Usage: make_patches.py PRISTINE_BUILD_DIR INSTALLED_BUILD_DIR OUT.json FILE...

Format 2 carries no generated code: per file, the SHA-256 of the pristine codegen output (deterministic for the
verified 2.1.0 ExeFS) and of the result, and hunks [first_line, end_line, [our lines]] that replace pristine lines
first_line..end_line-1 (0-based, applied bottom-up). Only the lines we wrote or changed are stored, no context."""
import sys, json, difflib, hashlib
from pathlib import Path


def sha(text):
    return hashlib.sha256(text.encode('latin-1')).hexdigest()


def apply(text, hunks):
    lines = text.split('\n')
    for i1, i2, new in sorted(hunks, key=lambda h: h[0], reverse=True):
        lines[i1:i2] = new
    return '\n'.join(lines)


def main():
    pri, ins, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    files = {}
    for name in sys.argv[4:]:
        a = (pri / name).read_bytes().decode('latin-1').replace('\r\n', '\n')
        b = (ins / name).read_bytes().decode('latin-1').replace('\r\n', '\n')
        al, bl = a.split('\n'), b.split('\n')
        hunks = [[i1, i2, bl[j1:j2]] for tag, i1, i2, j1, j2 in
                 difflib.SequenceMatcher(None, al, bl, autojunk=False).get_opcodes() if tag != 'equal']
        assert apply(a, hunks) == b, name
        files[name] = {'pristine': sha(a), 'result': sha(b), 'hunks': hunks}
    out.write_text(json.dumps({'format': 2, 'files': files}, indent=1))
    print(out, {k: len(v['hunks']) for k, v in files.items()})


if __name__ == '__main__':
    main()
