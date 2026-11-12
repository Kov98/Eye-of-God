import re
p = r'C:\Users\pedri\Downloads\Eye-of-God-main\.oracle\laudo.pdf'
data = open(p, 'rb').read()

def literal_ends(s, start):
    """Given s[start] == '(' find the index just past the matching ')'."""
    depth = 0
    i = start
    while i < len(s):
        c = s[i]
        if c == 0x5c:      # backslash: skip next byte
            i += 2; continue
        if c == 0x28: depth += 1
        elif c == 0x29:
            depth -= 1
            if depth == 0: return i + 1
        i += 1
    return -1

def unescape(b):
    out = []
    i = 0
    while i < len(b):
        c = b[i]
        if c == 0x5c and i + 1 < len(b):
            n = b[i+1]
            if n == 0x6e: out.append(10); i += 2
            elif n == 0x72: out.append(13); i += 2
            elif n == 0x74: out.append(9); i += 2
            elif n in (0x28, 0x29, 0x5c): out.append(n); i += 2
            elif 0x30 <= n <= 0x37:
                j = i + 1
                oct_digits = b''
                while j < len(b) and len(oct_digits) < 3 and 0x30 <= b[j] <= 0x37:
                    oct_digits += bytes([b[j]]); j += 1
                out.append(int(oct_digits, 8) & 0xFF); i = j
            else: out.append(n); i += 2
        else:
            out.append(c); i += 1
    return bytes(out)

streams = []
pos = 0
while True:
    i = data.find(b'stream\n', pos)
    if i < 0: break
    j = data.find(b'endstream', i)
    if j < 0: break
    streams.append(data[i+7:j])
    pos = j + 9

lines = []
for s in streams:
    i = 0
    while i < len(s):
        if s[i] == 0x28:                     # '('
            end = literal_ends(s, i)
            if end < 0: break
            raw = s[i+1:end-1]
            # only take it if 'Tj' follows the closing paren
            rest = s[end:end+8].strip()
            if rest.startswith(b'Tj'):
                lines.append(unescape(raw).decode('cp1252', errors='replace'))
            i = end
        else:
            i += 1

text = '\n'.join(lines)
print('linhas de texto:', len(lines))
print('--- primeiras 40 ---')
print('\n'.join(lines[:40]))
print('--- procurando vazamento de operador ---')
leaks = [l for l in lines if 'BT ' in l or 'Td (' in l or 'Tf ' in l]
print('linhas contaminadas com operador PDF:', len(leaks))
for l in leaks[:5]: print('   ', l[:120])
