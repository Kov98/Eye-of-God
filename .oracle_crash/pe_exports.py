import struct, sys, bisect

def exports(path):
    data = open(path, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    assert data[pe:pe+4] == b'PE\0\0'
    nsec = struct.unpack_from('<H', data, pe+6)[0]
    optsz = struct.unpack_from('<H', data, pe+20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', data, opt)[0]
    ddoff = opt + (112 if magic == 0x20b else 96)
    edir_rva, edir_sz = struct.unpack_from('<II', data, ddoff)
    secoff = opt + optsz
    secs = []
    for i in range(nsec):
        s = secoff + 40*i
        name = data[s:s+8].rstrip(b'\0').decode()
        vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', data, s+8)
        secs.append((vaddr, vsize, raddr, rsize, name))
    def rva2off(rva):
        for vaddr, vsize, raddr, rsize, name in secs:
            if vaddr <= rva < vaddr + max(vsize, rsize):
                return raddr + (rva - vaddr)
        raise ValueError('rva 0x%x fora das secoes' % rva)
    o = rva2off(edir_rva)
    nnames = struct.unpack_from('<I', data, o+24)[0]
    names_rva = struct.unpack_from('<I', data, o+32)[0]
    ords_rva  = struct.unpack_from('<I', data, o+36)[0]
    no = rva2off(names_rva)
    oo = rva2off(ords_rva)
    out = []
    for i in range(nnames):
        nr = struct.unpack_from('<I', data, no+4*i)[0]
        ordi = struct.unpack_from('<H', data, oo+2*i)[0]
        fn = rva2off(nr)
        end = data.index(b'\0', fn)
        name = data[fn:end].decode('utf-8', 'replace')
        frva = struct.unpack_from('<I', data, rva2off(rva2off and edir_rva) + 28 + 4*ordi)[0]
        out.append((frva, name))
    out.sort()
    return out

targets = {
    r'C:\msys64\ucrt64\bin\Qt6Widgets.dll': [0x5d5ac, 0x5d5c1, 0x174b32],
    r'C:\msys64\ucrt64\bin\Qt6Core.dll':    [0x419bfb, 0xee76c, 0xf426a],
}
for dll, offs in targets.items():
    ex = exports(dll)
    rvas = [e[0] for e in ex]
    print('==', dll.split(chr(92))[-1], len(ex), 'exports ==')
    for t in offs:
        i = bisect.bisect_right(rvas, t) - 1
        if i < 0:
            print('  0x%06x -> nada' % t)
        else:
            rva, name = ex[i]
            print('  0x%06x -> %s (0x%06x, +0x%x)' % (t, name, rva, t - rva))
