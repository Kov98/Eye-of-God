import re
p = r'C:\Users\pedri\Downloads\Eye-of-God-main\.oracle\laudo.pdf'
data = open(p, 'rb').read()

streams = []
pos = 0
while True:
    i = data.find(b'stream\n', pos)
    if i < 0: break
    j = data.find(b'endstream', i)
    if j < 0: break
    declared = re.search(rb'/Length\s+(\d+)', data[max(0, i-200):i])
    body = data[i+7:j]
    streams.append((int(declared.group(1)) if declared else -1, body))
    pos = j + 9

print('streams:', len(streams))
bad_len = sum(1 for d, b in streams if d != len(b.rstrip(b'\n')) and d != len(b))
for idx, (d, b) in enumerate(streams):
    if d != len(b) and d != len(b.rstrip(b'\n')):
        print('  /Length divergente no stream', idx, 'declarado', d, 'real', len(b))

def unescape(s):
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == 0x5c and i+1 < len(s):
            n = s[i+1]
            if n in (0x6e,): out.append(10); i += 2
            elif n in (0x72,): out.append(13); i += 2
            elif n in (0x74,): out.append(9); i += 2
            elif n in (0x28,0x29,0x5c): out.append(n); i += 2
            elif 0x30 <= n <= 0x37:
                oct_digits = s[i+1:i+4]
                out.append(int(oct_digits, 8) & 0xFF); i += 1 + len(oct_digits)
            else: out.append(n); i += 2
        else:
            out.append(c); i += 1
    return bytes(out)

total_text = []
for idx, (d, b) in enumerate(streams):
    for m in re.finditer(rb'\((.*?)(?<!\\)\)\s*Tj', b, re.S):
        total_text.append(unescape(m.group(1)))

text = b'\n'.join(total_text).decode('cp1252', errors='replace')
print('--- texto extraido (%d linhas) ---' % len(total_text))
print(text[:1800])
