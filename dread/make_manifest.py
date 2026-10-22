"""RomFS manifest for setup validation: one line per file, 'relative/path<TAB>size<TAB>sha1' (sorted). No content."""
import sys, os, hashlib
root = sys.argv[1]; rows = []
for dp, dn, fn in os.walk(root):
    for n in fn:
        p = os.path.join(dp, n); h = hashlib.sha1()
        with open(p, 'rb') as f:
            for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
        rows.append('%s\t%d\t%s' % (os.path.relpath(p, root).replace(os.sep, '/'), os.path.getsize(p), h.hexdigest()))
rows.sort()
open(sys.argv[2], 'w', newline='\n').write('# Metroid Dread v2.1.0 RomFS (010093801237C000): path, size, sha1\n' + '\n'.join(rows) + '\n')
print(len(rows), 'files')
