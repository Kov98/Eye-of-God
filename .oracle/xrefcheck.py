import re, sys
p = r'C:\Users\pedri\Downloads\Eye-of-God-main\.oracle\laudo.pdf'
data = open(p, 'rb').read()
print('bytes:', len(data))
i = data.rfind(b'startxref')
print('startxref at:', i)
m = re.match(rb'startxref\s+(\d+)\s+%%EOF', data[i:])
assert m, 'startxref/%%EOF malformado'
xref_at = int(m.group(1))
print('xref offset declarado:', xref_at)
seg = data[xref_at:xref_at+40]
print('bytes no offset:', seg[:32])
assert seg.startswith(b'xref'), 'xref nao esta no offset declarado'
# parse entries
lines = data[xref_at:].split(b'\n')
count = int(lines[1].split()[1])
print('objetos declarados:', count)
bad = 0
for n in range(1, count):
    off = int(lines[2+n].split()[0])
    expect = rb'%d 0 obj' % n
    actual = data[off:off+len(expect)]
    if actual != expect:
        print('  OFFSET ERRADO obj', n, 'esperava', expect, 'achou', actual[:20])
        bad += 1
print('offsets errados:', bad)
print('PASS' if bad == 0 else 'FAIL')
