#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
pdfcheck.py -- validador de conformidade PDF independente.

Parser de PDF escrito a mao. Nenhuma biblioteca de PDF. Nenhuma dependencia
externa. Somente stdlib do CPython 3.13 (re, zlib, sys, os, json).

Uso:
    python pdfcheck.py <arquivo.pdf> [--json] [--runs]

Saida:
    [PASS] Cxx titulo            checagem que passou
    [FAIL] Cxx titulo            checagem que reprovou (detalhe na mesma linha/bloco)
    [SKIP] Cxx titulo            impossivel verificar (com o motivo declarado)
    [INFO] ...                   dado medido, sem juizo
    texto extraido dos content streams impresso por inteiro

Exit code:
    0 = nenhuma checagem FAIL (arquivo estruturalmente conforme o que foi checado)
    1 = pelo menos um FAIL, ou arquivo ilegivel
"""

import sys
import os
import re
import zlib
import json

# ---------------------------------------------------------------------------
# constantes de sintaxe (ISO 32000-1, secao 7.2)
# ---------------------------------------------------------------------------
WS = b"\x00\x09\x0a\x0c\x0d\x20"
DELIM = b"()<>[]{}/%"
XREF_RE = re.compile(rb"^(\d{10}) (\d{5}) ([nf])(\r\n| \r| \n)$")
INDIRECT_RE = re.compile(rb"(?<![0-9])(\d{1,10})\s+(\d{1,5})\s+obj\b")

# larguras AFM da Helvetica (unidades/1000). Fonte de referencia para estimar
# o avanco horizontal quando o /Widths da fonte nao esta disponivel.
_HELV_RAW = (
    "278 278 355 556 556 889 667 222 333 333 389 584 278 333 278 278 "
    "556 556 556 556 556 556 556 556 556 556 278 278 584 584 584 556 "
    "1015 667 667 722 722 667 611 778 722 278 500 667 556 833 722 778 "
    "667 778 722 667 611 722 667 944 667 667 611 278 278 278 469 556 "
    "222 556 556 500 556 556 278 556 556 222 222 500 222 833 556 556 "
    "556 556 333 500 278 556 500 722 500 500 500 334 260 334 584"
)
HELV_W = {32 + i: int(v) for i, v in enumerate(_HELV_RAW.split())}
DEFAULT_W = 500


class PDFError(Exception):
    pass


class Unsupported(Exception):
    pass


# ---------------------------------------------------------------------------
# modelo de objetos PDF
# ---------------------------------------------------------------------------
class PDFOp(str):
    __slots__ = ()


class PDFName(str):
    __slots__ = ()


class PDFRef(object):
    __slots__ = ("num", "gen")

    def __init__(self, num, gen):
        self.num = int(num)
        self.gen = int(gen)

    def __eq__(self, o):
        return isinstance(o, PDFRef) and o.num == self.num and o.gen == self.gen

    def __ne__(self, o):
        return not self.__eq__(o)

    def __hash__(self):
        return hash((self.num, self.gen))

    def __repr__(self):
        return "<%d %d R>" % (self.num, self.gen)


class PDFString(bytes):
    __slots__ = ()


class PDFDict(dict):
    __slots__ = ()


class PDFStream(object):
    __slots__ = ("dict", "raw", "declared_raw", "declared", "data_start",
                 "actual_len", "endstream_at", "eol", "len_ok", "len_cands",
                 "finalized", "objnum")

    def __init__(self, d, data_start, eol):
        self.dict = d
        self.data_start = data_start
        self.eol = eol
        self.declared_raw = d.get("Length")
        self.declared = None
        self.raw = None
        self.actual_len = None
        self.endstream_at = None
        self.len_ok = None
        self.len_cands = ()
        self.finalized = False
        self.objnum = None

    def __repr__(self):
        return "<stream len=%r dict_keys=%r>" % (self.declared, sorted(self.dict.keys()))


def is_stream(o):
    return isinstance(o, PDFStream)


# ---------------------------------------------------------------------------
# lexer / parser de objetos
# ---------------------------------------------------------------------------
def _is_num_token(t):
    if not t:
        return False
    if t[0] not in b"+-.0123456789":
        return False
    try:
        float(t)
        return True
    except ValueError:
        return False


class Lexer(object):
    __slots__ = ("buf", "pos", "n")

    def __init__(self, buf, pos=0):
        self.buf = buf
        self.pos = pos
        self.n = len(buf)

    def skip_ws(self):
        buf = self.buf
        n = self.n
        p = self.pos
        while p < n:
            c = buf[p]
            if c in WS:
                p += 1
            elif c == 0x25:  # '%' comment
                while p < n and buf[p] not in (0x0a, 0x0d):
                    p += 1
            else:
                break
        self.pos = p

    def eof(self):
        return self.pos >= self.n

    def read_token(self):
        """Proximo token cru (bytes). b'' no fim do stream."""
        self.skip_ws()
        buf = self.buf
        n = self.n
        p = self.pos
        if p >= n:
            return b""
        c = buf[p]
        if c in DELIM:
            if buf[p:p + 2] in (b"<<", b">>"):
                self.pos = p + 2
                return buf[p:p + 2]
            self.pos = p + 1
            return buf[p:p + 1]
        start = p
        while p < n and buf[p] not in WS and buf[p] not in DELIM:
            p += 1
        self.pos = p
        return buf[start:p]

    def parse(self, allow_ref=True):
        self.skip_ws()
        buf = self.buf
        n = self.n
        p = self.pos
        if p >= n:
            raise PDFError("fim de dados inesperado ao ler objeto")
        c = buf[p]
        if c == 0x2f:
            return self._parse_name()
        if c == 0x28:
            return self._parse_lit_string()
        if c == 0x3c:
            if buf[p:p + 2] == b"<<":
                return self._parse_dict()
            return self._parse_hex_string()
        if c == 0x5b:
            return self._parse_array()
        if c in b"]>)":
            raise PDFError("token inesperado %r" % bytes([c]))
        tok = self.read_token()
        if not tok:
            raise PDFError("token vazio")
        if tok == b"true":
            return True
        if tok == b"false":
            return False
        if tok == b"null":
            return None
        if _is_num_token(tok):
            is_int = b"." not in tok
            if allow_ref and is_int:
                save = self.pos
                try:
                    t2 = self.read_token()
                    if t2 and _is_num_token(t2) and b"." not in t2:
                        save2 = self.pos
                        t3 = self.read_token()
                        if t3 == b"R":
                            return PDFRef(int(tok), int(t2))
                        self.pos = save2
                    self.pos = save
                except Exception:
                    self.pos = save
            try:
                return int(tok) if is_int else float(tok)
            except ValueError:
                raise PDFError("numero invalido %r" % tok)
        return PDFOp(tok.decode("latin-1"))

    def _parse_name(self):
        buf = self.buf
        n = self.n
        p = self.pos + 1
        out = bytearray()
        while p < n and buf[p] not in WS and buf[p] not in DELIM:
            c = buf[p]
            if c == 0x23 and p + 2 < n:  # '#'
                h = buf[p + 1:p + 3]
                try:
                    out.append(int(h, 16))
                    p += 3
                    continue
                except ValueError:
                    pass
            out.append(c)
            p += 1
        self.pos = p
        return PDFName(out.decode("latin-1"))

    def _parse_lit_string(self):
        buf = self.buf
        n = self.n
        p = self.pos + 1
        depth = 1
        out = bytearray()
        while p < n:
            c = buf[p]
            if c == 0x5c:  # backslash
                p += 1
                if p >= n:
                    break
                e = buf[p]
                if e == 0x6e:
                    out.append(0x0a)
                    p += 1
                elif e == 0x72:
                    out.append(0x0d)
                    p += 1
                elif e == 0x74:
                    out.append(0x09)
                    p += 1
                elif e == 0x62:
                    out.append(0x08)
                    p += 1
                elif e == 0x66:
                    out.append(0x0c)
                    p += 1
                elif e in (0x28, 0x29, 0x5c):
                    out.append(e)
                    p += 1
                elif e in (0x0a, 0x0d):
                    p += 1
                    if e == 0x0d and p < n and buf[p] == 0x0a:
                        p += 1
                elif 0x30 <= e <= 0x37:
                    v = 0
                    k = 0
                    while k < 3 and p < n and 0x30 <= buf[p] <= 0x37:
                        v = v * 8 + (buf[p] - 0x30)
                        p += 1
                        k += 1
                    out.append(v & 0xFF)
                else:
                    out.append(e)
                    p += 1
            elif c == 0x28:
                depth += 1
                out.append(c)
                p += 1
            elif c == 0x29:
                depth -= 1
                p += 1
                if depth == 0:
                    break
                out.append(c)
            else:
                out.append(c)
                p += 1
        self.pos = p
        return PDFString(bytes(out))

    def _parse_hex_string(self):
        buf = self.buf
        n = self.n
        p = self.pos + 1
        digits = bytearray()
        while p < n and buf[p] != 0x3e:
            c = buf[p]
            if c in WS:
                p += 1
                continue
            digits.append(c)
            p += 1
        self.pos = p + 1
        if len(digits) % 2:
            digits.append(0x30)
        try:
            return PDFString(bytes.fromhex(digits.decode("latin-1")))
        except ValueError:
            raise PDFError("string hexadecimal invalida")

    def _parse_array(self):
        self.pos += 1
        arr = []
        while True:
            self.skip_ws()
            if self.pos >= self.n:
                raise PDFError("array sem ']'")
            if self.buf[self.pos] == 0x5d:
                self.pos += 1
                return arr
            arr.append(self.parse())

    def _parse_dict(self):
        self.pos += 2
        d = PDFDict()
        while True:
            self.skip_ws()
            if self.pos >= self.n:
                raise PDFError("dicionario sem '>>'")
            if self.buf[self.pos:self.pos + 2] == b">>":
                self.pos += 2
                return d
            k = self.parse()
            if not isinstance(k, PDFName):
                raise PDFError("chave de dicionario nao e /Name: %r" % (k,))
            v = self.parse()
            d[k] = v


def read_stream_at(buf, kw_pos, d):
    """kw_pos aponta para o 's' de 'stream'."""
    n = len(buf)
    p = kw_pos + 6
    eol = b""
    if buf[p:p + 2] == b"\r\n":
        eol = b"\r\n"
        p += 2
    elif buf[p:p + 1] == b"\n":
        eol = b"\n"
        p += 1
    elif buf[p:p + 1] == b"\r":
        eol = b"\r"
        p += 1
    st = PDFStream(d, p, eol)
    return st


def parse_indirect(buf, offset):
    """Le 'N G obj ... endobj' em offset. Retorna (num, gen, obj)."""
    lx = Lexer(buf, offset)
    num = lx.parse(allow_ref=False)
    gen = lx.parse(allow_ref=False)
    if not isinstance(num, int) or not isinstance(gen, int):
        raise PDFError("em %d: esperava 'N G obj', achei %r %r" % (offset, num, gen))
    kw = lx.read_token()
    if kw != b"obj":
        raise PDFError("em %d: esperava 'obj', achei %r" % (offset, kw))
    obj = lx.parse()
    lx.skip_ws()
    if buf[lx.pos:lx.pos + 6] == b"stream":
        if not isinstance(obj, PDFDict):
            raise PDFError("objeto %d: 'stream' sem dicionario" % num)
        return num, gen, read_stream_at(buf, lx.pos, obj)
    return num, gen, obj


def scan_object_headers(doc):
    """Cabecalhos 'N G obj' que realmente parseiam como objeto indireto completo.

    Descarta ocorrencias que caiam dentro dos dados de um stream (um laudo pode
    citar o texto '12 0 obj' dentro de uma string; isso nao e um objeto), e
    exige que o objeto termine em 'endobj' para nao confundir texto com objeto.
    """
    buf = doc.buf
    regions = []
    for st in doc.streams.values():
        if st.data_start is not None and st.raw is not None:
            regions.append((st.data_start, st.data_start + len(st.raw) + 2))
    regions.sort()
    found = {}
    for m in INDIRECT_RE.finditer(buf):
        off = m.start()
        hit = False
        for a, b in regions:
            if a <= off < b:
                hit = True
                break
        if hit:
            continue
        num = int(m.group(1))
        try:
            n2, g2, obj = parse_indirect(buf, off)
        except Exception:
            continue
        if n2 != num:
            continue
        e = buf.find(b"endobj", m.end())
        if e < 0:
            continue
        found.setdefault(num, []).append(off)
    return found


def walk_refs(v, out, depth=0):
    """Coleta todos os PDFRef alcancaveis em v."""
    if depth > 64:
        return
    if isinstance(v, PDFRef):
        out.add(v.num)
    elif isinstance(v, PDFDict):
        for k, x in v.items():
            walk_refs(x, out, depth + 1)
    elif isinstance(v, dict):
        for x in v.values():
            walk_refs(x, out, depth + 1)
    elif isinstance(v, list):
        for x in v:
            walk_refs(x, out, depth + 1)
    elif isinstance(v, PDFStream):
        walk_refs(v.dict, out, depth + 1)


# ---------------------------------------------------------------------------
# filtros de stream
# ---------------------------------------------------------------------------
def apply_predictor(data, parms):
    if not isinstance(parms, dict):
        return data
    pred = parms.get("Predictor", 1)
    if not isinstance(pred, int) or pred <= 1:
        return data
    colors = parms.get("Colors", 1)
    bpc = parms.get("BitsPerComponent", 8)
    columns = parms.get("Columns", 1)
    if not all(isinstance(x, int) and x > 0 for x in (colors, bpc, columns)):
        raise Unsupported("/DecodeParms com Colors/BitsPerComponent/Columns invalidos")
    bpp = max(1, (colors * bpc + 7) // 8)
    rowlen = (columns * colors * bpc + 7) // 8
    if pred == 2:
        out = bytearray(data)
        for r in range(0, len(out), rowlen):
            for i in range(bpp, min(rowlen, len(out) - r)):
                out[r + i] = (out[r + i] + out[r + i - bpp]) & 0xFF
        return bytes(out)
    out = bytearray()
    prev = bytearray(rowlen)
    pos = 0
    n = len(data)
    while pos < n:
        ft = data[pos]
        pos += 1
        row = bytearray(data[pos:pos + rowlen])
        pos += rowlen
        if len(row) < rowlen:
            break
        if ft == 1:
            for i in range(bpp, rowlen):
                row[i] = (row[i] + row[i - bpp]) & 0xFF
        elif ft == 2:
            for i in range(rowlen):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(rowlen):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(rowlen):
                a = row[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                if pa <= pb and pa <= pc:
                    pr = a
                elif pb <= pc:
                    pr = b
                else:
                    pr = c
                row[i] = (row[i] + pr) & 0xFF
        elif ft != 0:
            raise Unsupported("predictor PNG tipo %d desconhecido" % ft)
        out += row
        prev = row
    return bytes(out)


def flate_decode(data):
    try:
        return zlib.decompress(data)
    except zlib.error:
        pass
    errs = []
    for wbits, label in ((-15, "raw-deflate"), (47, "auto")):
        try:
            d = zlib.decompressobj(wbits)
            out = d.decompress(data)
            out += d.flush()
            return out
        except zlib.error as e:
            errs.append("%s: %s" % (label, e))
    try:
        d = zlib.decompressobj()
        out = d.decompress(data)
        out += d.flush()
        return out
    except zlib.error as e:
        errs.append("truncado: %s" % e)
    raise PDFError("FlateDecode falhou: " + " | ".join(errs))


def ascii_hex_decode(data):
    out = bytearray()
    hexd = bytearray()
    for b in data:
        if b == 0x3e:
            break
        if b in WS:
            continue
        hexd.append(b)
    if len(hexd) % 2:
        hexd.append(0x30)
    try:
        return bytes.fromhex(hexd.decode("latin-1"))
    except ValueError:
        raise PDFError("ASCIIHexDecode: digito hex invalido")


def ascii85_decode(data):
    out = bytearray()
    group = []
    data = data.strip()
    if data.startswith(b"<~"):
        data = data[2:]
    for b in data:
        if b in WS:
            continue
        if b == 0x7e:
            break
        if b == 0x7a and not group:
            out += b"\x00\x00\x00\x00"
            continue
        if not (0x21 <= b <= 0x75):
            raise PDFError("ASCII85Decode: byte invalido 0x%02x" % b)
        group.append(b - 33)
        if len(group) == 5:
            v = 0
            for g in group:
                v = v * 85 + g
            out += v.to_bytes(4, "big")
            group = []
    if group:
        k = len(group)
        group += [84] * (5 - k)
        v = 0
        for g in group:
            v = v * 85 + g
        out += v.to_bytes(4, "big")[:k - 1]
    return bytes(out)


def runlength_decode(data):
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        L = data[i]
        i += 1
        if L == 128:
            break
        if L < 128:
            out += data[i:i + L + 1]
            i += L + 1
        else:
            if i >= n:
                break
            out += bytes([data[i]]) * (257 - L)
            i += 1
    return bytes(out)


def lzw_decode(data, early=1):
    out = bytearray()
    table = [bytes([i]) for i in range(256)] + [b"", b""]
    bits = 9
    buf = 0
    nbits = 0
    prev = None
    for byte in data:
        buf = (buf << 8) | byte
        nbits += 8
        while nbits >= bits:
            nbits -= bits
            code = (buf >> nbits) & ((1 << bits) - 1)
            if code == 256:
                table = [bytes([i]) for i in range(256)] + [b"", b""]
                bits = 9
                prev = None
                continue
            if code == 257:
                return bytes(out)
            if prev is None:
                entry = table[code]
            elif code < len(table):
                entry = table[code]
                table.append(prev + entry[:1])
            else:
                entry = prev + prev[:1]
                table.append(entry)
            out += entry
            prev = entry
            n = len(table) + early
            if n >= 4096:
                bits = 12
            elif n >= 2048:
                bits = 12
            elif n >= 1024:
                bits = 11
            elif n >= 512:
                bits = 10
    return bytes(out)


FILTERS = {
    "FlateDecode": flate_decode,
    "Fl": flate_decode,
    "ASCIIHexDecode": ascii_hex_decode,
    "AHx": ascii_hex_decode,
    "ASCII85Decode": ascii85_decode,
    "A85": ascii85_decode,
    "RunLengthDecode": runlength_decode,
    "RL": runlength_decode,
}


# ---------------------------------------------------------------------------
# documento
# ---------------------------------------------------------------------------
class PDFDoc(object):
    def __init__(self, data, path="<memoria>"):
        self.path = path
        self.buf = data
        self.xref = {}
        self.cache = {}
        self.trailer = PDFDict()
        self.trailers = []
        self.xref_kind = None
        self.xref_sections = []
        self.startxref = None
        self.startxref_pos = None
        self.objstm_cache = {}
        self._resolving = set()
        self.streams = {}
        self.xref_error = None
        self.hybrid = False
        self._fonts = {}

    # -- xref ------------------------------------------------------------
    def find_startxref(self):
        """Localiza o ultimo 'startxref' e le o offset declarado."""
        if self.startxref is not None:
            return self.startxref
        buf = self.buf
        i = buf.rfind(b"startxref")
        if i < 0:
            raise PDFError("startxref nao encontrado no arquivo")
        self.startxref_pos = i
        lx = Lexer(buf, i + 9)
        off = lx.parse(allow_ref=False)
        if not isinstance(off, int) or isinstance(off, bool):
            raise PDFError("startxref nao seguido de inteiro: %r" % (off,))
        self.startxref = off
        return off

    def load_xref(self):
        buf = self.buf
        cur = self.find_startxref()
        seen = set()
        first = True
        while cur is not None:
            if cur in seen:
                raise PDFError("cadeia /Prev circular no offset %d" % cur)
            seen.add(cur)
            if cur < 0 or cur >= len(buf):
                raise PDFError("offset de xref %d fora do arquivo (tamanho %d)" % (cur, len(buf)))
            if buf[cur:cur + 4] == b"xref":
                kind = "tabela"
            else:
                kind = "stream"
            if first:
                self.xref_kind = kind
                first = False
            try:
                if kind == "tabela":
                    ent, tr = self._parse_xref_table(cur)
                else:
                    ent, tr = self._parse_xref_stream(cur)
                # arquivo hibrido (PDF 1.5): o trailer classico traz /XRefStm e a
                # secao classica sozinha fica incompleta. Pela ISO 32000-1 7.5.8.4
                # o stream e lido primeiro e a tabela classica tem precedencia.
                xrefstm = tr.get("XRefStm")
                if kind == "tabela" and isinstance(xrefstm, int) and not isinstance(xrefstm, bool):
                    ent2, _tr2 = self._parse_xref_stream(xrefstm)
                    for num, e2 in ent2.items():
                        if num not in ent:
                            ent[num] = e2
                    self.hybrid = True
            except (PDFError, Unsupported) as e:
                self.xref_error = str(e)
                raise
            self.trailers.append(tr)
            for num, e in ent.items():
                if num not in self.xref:
                    self.xref[num] = e
            for k, v in tr.items():
                if k not in self.trailer:
                    self.trailer[k] = v
            prev = tr.get("Prev")
            cur = prev if isinstance(prev, int) else None

    def _parse_xref_table(self, off):
        buf = self.buf
        lx = Lexer(buf, off)
        kw = lx.read_token()
        if kw != b"xref":
            raise PDFError("em %d: esperava 'xref', achei %r" % (off, kw))
        entries = {}
        sections = []
        while True:
            lx.skip_ws()
            if lx.pos >= len(buf):
                raise PDFError("xref truncado: fim de arquivo antes de 'trailer'")
            if buf[lx.pos:lx.pos + 7] == b"trailer":
                lx.pos += 7
                break
            try:
                start = lx.parse(allow_ref=False)
                count = lx.parse(allow_ref=False)
            except PDFError as e:
                raise PDFError("xref truncado em %d: %s" % (lx.pos, e))
            if not isinstance(start, int) or not isinstance(count, int) or start < 0 or count < 0:
                raise PDFError("cabecalho de secao xref invalido: %r %r" % (start, count))
            lx.skip_ws()
            base = lx.pos
            need = count * 20
            if base + need > len(buf):
                raise PDFError(
                    "xref truncado: secao '%d %d' exige %d bytes, restam %d"
                    % (start, count, need, len(buf) - base))
            bad = []
            for k in range(count):
                raw = buf[base + k * 20: base + k * 20 + 20]
                onum = start + k
                m = XREF_RE.match(raw)
                if not m:
                    if len(bad) < 4:
                        bad.append((onum, raw))
                    continue
                typ = m.group(3).decode()
                e = {"offset": int(m.group(1)), "gen": int(m.group(2))}
                if typ == "n":
                    e["type"] = "n"
                else:
                    e["type"] = "f"
                entries[onum] = e
            sec = {"start": start, "count": count, "bad": bad}
            sections.append(sec)
            self.xref_sections = sections
            lx.pos = base + need
        trailer = lx.parse()
        if not isinstance(trailer, PDFDict):
            raise PDFError("trailer nao e dicionario")
        self.xref_sections = sections
        return entries, trailer

    def _parse_xref_stream(self, off):
        num, gen, obj = parse_indirect(self.buf, off)
        if not isinstance(obj, PDFStream):
            raise PDFError("em %d: objeto em startxref nao e stream" % off)
        d = obj.dict
        if d.get("Type") != "XRef":
            raise PDFError("objeto %d: stream de xref sem /Type /XRef" % num)
        self.finalize(obj, num)
        data = self.decode(obj)
        W = d.get("W")
        if not (isinstance(W, list) and len(W) == 3 and all(isinstance(x, int) for x in W)):
            raise PDFError("xref stream %d: /W invalido %r" % (num, W))
        size = d.get("Size")
        index = d.get("Index")
        if not isinstance(index, list) or len(index) < 2:
            if not isinstance(size, int):
                raise PDFError("xref stream %d: sem /Size nem /Index" % num)
            index = [0, size]
        w1, w2, w3 = W
        rowlen = w1 + w2 + w3
        if rowlen == 0:
            raise PDFError("xref stream %d: /W zera o tamanho da linha" % num)
        entries = {}
        pos = 0
        for i in range(0, len(index) - 1, 2):
            start = index[i]
            count = index[i + 1]
            if not isinstance(start, int) or not isinstance(count, int):
                raise PDFError("xref stream %d: /Index invalido" % num)
            for k in range(count):
                if pos + rowlen > len(data):
                    raise PDFError("dados do xref stream %d truncados" % num)
                f1 = int.from_bytes(data[pos:pos + w1], "big") if w1 else 1
                f2 = int.from_bytes(data[pos + w1:pos + w1 + w2], "big") if w2 else 0
                f3 = int.from_bytes(data[pos + w1 + w2:pos + rowlen], "big") if w3 else 0
                pos += rowlen
                onum = start + k
                if f1 == 0:
                    entries[onum] = {"type": "f", "offset": f2, "gen": f3}
                elif f1 == 1:
                    entries[onum] = {"type": "n", "offset": f2, "gen": f3}
                elif f1 == 2:
                    entries[onum] = {"type": "c", "objstm": f2, "idx": f3}
        return entries, d

    # -- streams ---------------------------------------------------------
    def finalize(self, st, objnum=None):
        if st.finalized:
            return st
        st.finalized = True
        st.objnum = objnum
        if isinstance(st.declared_raw, PDFRef):
            try:
                st.declared = self.resolve_int(st.declared_raw)
            except PDFError:
                st.declared = None
        elif isinstance(st.declared_raw, int) and not isinstance(st.declared_raw, bool):
            st.declared = st.declared_raw
        buf = self.buf
        n = len(buf)
        p = st.data_start
        e = buf.find(b"endstream", p)
        if e < 0:
            raise PDFError("stream sem 'endstream' (objeto %r)" % (objnum,))
        st.endstream_at = e
        cands = [e - p]
        if e - 1 >= p and buf[e - 1:e] in (b"\n", b"\r"):
            cands.append(e - 1 - p)
        if e - 2 >= p and buf[e - 2:e] == b"\r\n":
            cands.append(e - 2 - p)
        st.len_cands = tuple(sorted(set(cands)))
        if st.declared is None:
            st.len_ok = None
            use = e - p
            if buf[e - 2:e] == b"\r\n":
                use = e - 2 - p
            elif buf[e - 1:e] in (b"\n", b"\r"):
                use = e - 1 - p
            st.raw = buf[p:p + use]
            st.actual_len = use
        else:
            st.len_ok = st.declared in st.len_cands
            take = st.declared if 0 <= st.declared <= n - p else (e - p)
            st.raw = buf[p:p + take]
            st.actual_len = len(st.raw)
        return st

    def decode(self, st):
        if not st.finalized:
            self.finalize(st, st.objnum)
        data = st.raw or b""
        d = st.dict
        filt = d.get("Filter")
        parms = d.get("DecodeParms")
        if parms is None:
            parms = d.get("DP")
        if isinstance(filt, PDFRef):
            filt = self.resolve(filt)
        if isinstance(parms, PDFRef):
            parms = self.resolve(parms)
        if isinstance(filt, str):
            filt = [filt]
        if isinstance(filt, list):
            if isinstance(parms, dict):
                parms = [parms]
            if not isinstance(parms, list):
                parms = [None] * len(filt)
            for i, f in enumerate(filt):
                if isinstance(f, PDFRef):
                    f = self.resolve(f)
                name = str(f)
                pr = parms[i] if i < len(parms) else None
                if isinstance(pr, PDFRef):
                    pr = self.resolve(pr)
                fn = FILTERS.get(name)
                if fn is None:
                    raise Unsupported("/Filter /%s nao suportado por este validador" % name)
                data = fn(data)
                if name in ("FlateDecode", "Fl"):
                    data = apply_predictor(data, pr)
                elif name == "LZWDecode":
                    early = 1
                    if isinstance(pr, dict) and isinstance(pr.get("EarlyChange"), int):
                        early = pr["EarlyChange"]
                    data = lzw_decode(data, early)
                    data = apply_predictor(data, pr)
        elif filt is not None:
            raise PDFError("/Filter de tipo inesperado: %r" % (filt,))
        return data

    # -- resolucao -------------------------------------------------------
    def resolve(self, v, depth=0):
        while isinstance(v, PDFRef):
            if depth > 64:
                raise PDFError("cadeia de referencias longa demais")
            v = self.get_object(v.num)
            depth += 1
        return v

    def resolve_int(self, v):
        v = self.resolve(v)
        if isinstance(v, bool) or not isinstance(v, int):
            raise PDFError("esperava inteiro, achei %r" % (v,))
        return v

    def get_object(self, num):
        if num in self.cache:
            return self.cache[num]
        if num in self._resolving:
            raise PDFError("referencia circular no objeto %d" % num)
        e = self.xref.get(num)
        if e is None:
            raise PDFError("objeto %d nao existe (numero ausente na tabela xref)" % num)
        if e["type"] == "f":
            raise PDFError("objeto %d esta marcado como livre na xref" % num)
        self._resolving.add(num)
        try:
            if e["type"] == "n":
                off = e["offset"]
                if off < 0 or off >= len(self.buf):
                    raise PDFError("objeto %d: offset %d fora do arquivo" % (num, off))
                n2, g2, obj = parse_indirect(self.buf, off)
                if n2 != num:
                    raise PDFError(
                        "xref aponta objeto %d para o offset %d, mas o objeto escrito ali e %d %d obj"
                        % (num, off, n2, g2))
                if isinstance(obj, PDFStream):
                    self.finalize(obj, num)
                    self.streams[num] = obj
                self.cache[num] = obj
                return obj
            if e["type"] == "c":
                obj = self._from_objstm(e["objstm"], e["idx"], num)
                self.cache[num] = obj
                return obj
            raise PDFError("entrada de xref de tipo desconhecido %r" % (e["type"],))
        finally:
            self._resolving.discard(num)

    # -- metricas de fonte ----------------------------------------------
    def font_info(self, fdict):
        """FontInfo do dicionario de fonte (larguras, espaco de codigo, /ToUnicode)."""
        if not isinstance(fdict, dict):
            return None
        key = id(fdict)
        if key in self._fonts:
            return self._fonts[key]
        fi = FontInfo()
        st = fdict.get("Subtype")
        fi.subtype = str(st) if st is not None else None
        bf = fdict.get("BaseFont")
        fi.basefont = str(bf) if bf is not None else None
        if fi.subtype == "Type0":
            fi.code_bytes = 2      # Identity-H / Identity-V, o caso usado por subset
        tu = fdict.get("ToUnicode")
        if tu is not None:
            try:
                tobj = self.resolve(tu) if isinstance(tu, PDFRef) else tu
                if isinstance(tobj, PDFStream):
                    fi.tounicode = parse_tounicode_cmap(self.decode(tobj))
            except (PDFError, Unsupported):
                fi.tounicode = None
        if fi.subtype == "Type0":
            fi.widths, fi.dw = self._type0_widths(fdict)
            fi.width_source = "w-array" if fi.widths else "dw"
        else:
            w = fdict.get("Widths")
            first = fdict.get("FirstChar")
            try:
                w = self.resolve(w) if isinstance(w, PDFRef) else w
            except PDFError:
                w = None
            if isinstance(w, list) and isinstance(first, int):
                table = {}
                for i, v in enumerate(w):
                    try:
                        v = self.resolve(v) if isinstance(v, PDFRef) else v
                    except PDFError:
                        v = None
                    if isinstance(v, (int, float)) and not isinstance(v, bool):
                        table[first + i] = float(v)
                if table:
                    fi.widths = table
                    fi.width_source = "widths"
            fd = fdict.get("FontDescriptor")
            try:
                fd = self.resolve(fd) if isinstance(fd, PDFRef) else fd
            except PDFError:
                fd = None
            if isinstance(fd, dict):
                mw = fd.get("MissingWidth")
                if isinstance(mw, (int, float)) and not isinstance(mw, bool):
                    fi.dw = float(mw)
            if fi.widths is None:
                # sem /Widths: so a tabela AFM da Helvetica e confiavel
                fi.width_source = "afm-helvetica"
        self._fonts[key] = fi
        return fi

    def _type0_widths(self, fdict):
        """(/W table, /DW) do descendente de uma fonte Type0."""
        df = fdict.get("DescendantFonts")
        try:
            df = self.resolve(df) if isinstance(df, PDFRef) else df
        except PDFError:
            df = None
        if not isinstance(df, list) or not df:
            return None, 1000.0
        cid = df[0]
        try:
            cid = self.resolve(cid) if isinstance(cid, PDFRef) else cid
        except PDFError:
            cid = None
        if not isinstance(cid, dict):
            return None, 1000.0
        dw = cid.get("DW", 1000)
        if not isinstance(dw, (int, float)) or isinstance(dw, bool):
            dw = 1000.0
        W = cid.get("W")
        try:
            W = self.resolve(W) if isinstance(W, PDFRef) else W
        except PDFError:
            W = None
        table = {}
        if isinstance(W, list):
            i = 0
            while i < len(W):
                c = W[i]
                try:
                    c = self.resolve(c) if isinstance(c, PDFRef) else c
                except PDFError:
                    c = None
                if not isinstance(c, int) or isinstance(c, bool):
                    i += 1
                    continue
                if i + 1 < len(W) and isinstance(W[i + 1], list):
                    for k, v in enumerate(W[i + 1]):
                        try:
                            v = self.resolve(v) if isinstance(v, PDFRef) else v
                        except PDFError:
                            v = None
                        if isinstance(v, (int, float)) and not isinstance(v, bool):
                            table[c + k] = float(v)
                    i += 2
                elif i + 2 < len(W):
                    c2 = W[i + 1]
                    w = W[i + 2]
                    try:
                        c2 = self.resolve(c2) if isinstance(c2, PDFRef) else c2
                        w = self.resolve(w) if isinstance(w, PDFRef) else w
                    except PDFError:
                        i += 3
                        continue
                    if (isinstance(c2, int) and not isinstance(c2, bool)
                            and isinstance(w, (int, float)) and not isinstance(w, bool)
                            and 0 <= c2 - c <= 65535):
                        for cc in range(c, c2 + 1):
                            table[cc] = float(w)
                    i += 3
                else:
                    break
        return (table or None), float(dw)

    def _from_objstm(self, stmnum, idx, want):
        key = (stmnum, want)
        if key in self.objstm_cache:
            return self.objstm_cache[key]
        st = self.get_object(stmnum)
        if not isinstance(st, PDFStream):
            raise PDFError("objeto %d deveria ser object stream (%d pedido)" % (stmnum, want))
        d = st.dict
        if d.get("Type") != "ObjStm":
            raise PDFError("objeto %d nao tem /Type /ObjStm" % stmnum)
        n = d.get("N")
        first = d.get("First")
        if not isinstance(n, int) or not isinstance(first, int):
            raise PDFError("object stream %d sem /N ou /First" % stmnum)
        data = self.decode(st)
        lx = Lexer(data, 0)
        pairs = []
        for _i in range(n):
            onum = lx.parse(allow_ref=False)
            ooff = lx.parse(allow_ref=False)
            if not isinstance(onum, int) or not isinstance(ooff, int):
                raise PDFError("object stream %d: cabecalho invalido" % stmnum)
            pairs.append((onum, ooff))
        for onum, ooff in pairs:
            if onum == want:
                lx2 = Lexer(data, first + ooff)
                obj = lx2.parse()
                self.objstm_cache[key] = obj
                return obj
        raise PDFError("objeto %d nao esta no object stream %d" % (want, stmnum))


# ---------------------------------------------------------------------------
# metricas de fonte (simples e CID) e /ToUnicode
# ---------------------------------------------------------------------------
class FontInfo(object):
    __slots__ = ("subtype", "basefont", "code_bytes", "tounicode",
                 "widths", "dw", "width_source")

    def __init__(self):
        self.subtype = None
        self.basefont = None
        self.code_bytes = 1
        self.tounicode = None
        self.widths = None
        self.dw = None
        self.width_source = "none"


def decode_utf16be(bs):
    try:
        return bytes(bs).decode("utf-16-be", "replace")
    except Exception:
        return ""


def parse_tounicode_cmap(data):
    """dict codigo(int) -> texto, a partir de um CMap /ToUnicode (bfchar/bfrange)."""
    m = {}
    for block in re.finditer(rb"beginbfchar(.*?)endbfchar", data, re.S):
        for mt in re.finditer(rb"<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]*)>", block.group(1)):
            try:
                code = int(mt.group(1), 16)
                dst = bytes.fromhex(mt.group(2).decode("latin-1"))
            except ValueError:
                continue
            m[code] = decode_utf16be(dst)
    for block in re.finditer(rb"beginbfrange(.*?)endbfrange", data, re.S):
        body = block.group(1)
        for mt in re.finditer(
                rb"<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>\s*(?:<([0-9A-Fa-f]*)>|\[(.*?)\])",
                body, re.S):
            try:
                lo = int(mt.group(1), 16)
                hi = int(mt.group(2), 16)
            except ValueError:
                continue
            if hi < lo or (hi - lo) > 65535:
                continue
            if mt.group(3) is not None:
                dsthex = mt.group(3).decode("latin-1")
                nb = max(1, len(dsthex) // 2)
                try:
                    base = int(dsthex, 16) if dsthex else 0
                except ValueError:
                    continue
                for i, code in enumerate(range(lo, hi + 1)):
                    try:
                        m[code] = decode_utf16be((base + i).to_bytes(nb, "big"))
                    except OverflowError:
                        break
            else:
                items = re.findall(rb"<([0-9A-Fa-f]*)>", mt.group(4) or b"")
                for i, code in enumerate(range(lo, hi + 1)):
                    if i >= len(items):
                        break
                    try:
                        m[code] = decode_utf16be(bytes.fromhex(items[i].decode("latin-1")))
                    except ValueError:
                        continue
    return m


# ---------------------------------------------------------------------------
# tokenizer de content stream
# ---------------------------------------------------------------------------
def tokenize_content(data):
    lx = Lexer(data, 0)
    out = []
    while True:
        lx.skip_ws()
        if lx.pos >= lx.n:
            break
        c = data[lx.pos]
        if data[lx.pos:lx.pos + 2] == b"<<":
            out.append(("obj", lx.parse()))
            continue
        if c in b"([/<":
            try:
                out.append(("obj", lx.parse()))
            except PDFError:
                lx.pos += 1
            continue
        start = lx.pos
        tok = lx.read_token()
        if not tok:
            lx.pos = start + 1
            out.append(("stray", data[start:start + 1]))
            continue
        if _is_num_token(tok):
            out.append(("obj", float(tok) if b"." in tok else int(tok)))
        elif tok in (b"true", b"false", b"null"):
            out.append(("obj", lx.parse()))
        elif tok in (b">", b"]", b")", b">>"):
            out.append(("stray", tok))
        else:
            out.append(("op", tok.decode("latin-1")))
    return out


ARITY = {
    "b": 4, "B": 0, "b*": 4, "B*": 0, "BDC": 2, "BMC": 1, "BT": 0, "BX": 0,
    "c": 6, "cm": 6, "CS": 1, "cs": 1, "d": 2, "d0": 2, "d1": 6, "Do": 1,
    "DP": 2, "EMC": 0, "ET": 0, "EX": 0, "f": 0, "F": 0, "f*": 0, "G": 1,
    "g": 1, "gs": 1, "h": 0, "i": 1, "j": 1, "J": 1, "K": 4, "k": 4, "l": 2,
    "m": 2, "M": 1, "MP": 1, "n": 0, "q": 0, "Q": 0, "re": 4, "RG": 3,
    "rg": 3, "ri": 1, "s": 0, "S": 0, "SC": -1, "sc": -1, "SCN": -1,
    "scn": -1, "sh": 1, "T*": 0, "Tc": 1, "Td": 2, "TD": 2, "Tf": 2, "Tj": 1,
    "TJ": 1, "TL": 1, "Tm": 6, "Tr": 1, "Ts": 1, "Tw": 1, "Tz": 1, "v": 4,
    "w": 1, "W": 0, "W*": 0, "y": 4, "'": 1, '"': 3, "BX": 0,
}


# ---------------------------------------------------------------------------
# matrizes
# ---------------------------------------------------------------------------
IDENT = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


def mat_mul(m, n):
    a1, b1, c1, d1, e1, f1 = m
    a2, b2, c2, d2, e2, f2 = n
    return (a1 * a2 + b1 * c2, a1 * b2 + b1 * d2,
            c1 * a2 + d1 * c2, c1 * b2 + d1 * d2,
            e1 * a2 + f1 * c2 + e2, e1 * b2 + f1 * d2 + f2)


def mat_apply(m, x, y):
    a, b, c, d, e, f = m
    return (a * x + c * y + e, b * x + d * y + f)


# ---------------------------------------------------------------------------
# interpretador de content stream com extracao de texto
# ---------------------------------------------------------------------------
class TextRun(object):
    __slots__ = ("text", "x0", "y0", "x1", "y1", "size", "font", "page",
                 "unmapped", "approx", "raw", "codes1")

    def __init__(self, text, x0, y0, x1, y1, size, font, page,
                 unmapped=0, approx=0, raw=b"", codes1=True):
        self.text = text
        self.x0 = x0
        self.y0 = y0
        self.x1 = x1
        self.y1 = y1
        self.size = size
        self.font = font
        self.page = page
        self.unmapped = unmapped
        self.approx = approx
        self.raw = raw
        self.codes1 = codes1

    def __repr__(self):
        return "<run %r at (%.1f,%.1f)>" % (self.text, self.x0, self.y0)


class ContentInterp(object):
    def __init__(self, doc, page_index):
        self.doc = doc
        self.page = page_index
        self.runs = []
        self.errors = []
        self.op_counts = {}
        self.fonts_used = set()
        self.unknown_ops = {}
        self.text_ops = 0
        self.form_depth = 0
        self.codes_total = 0
        self.codes_mapped = 0
        self.codes_unmapped = 0
        self.approx_runs = 0

    # -- utilidades de string -------------------------------------------
    @staticmethod
    def string_to_text(b):
        if isinstance(b, PDFString):
            raw = bytes(b)
        elif isinstance(b, bytes):
            raw = b
        else:
            return ""
        if raw[:2] == b"\xfe\xff":
            try:
                return raw[2:].decode("utf-16-be", "replace")
            except Exception:
                return ""
        if raw[:2] == b"\xff\xfe":
            try:
                return raw[2:].decode("utf-16-le", "replace")
            except Exception:
                return ""
        return raw.decode("latin-1")

    @staticmethod
    def code_width(fi, code):
        """Largura do glifo em unidades/1000, ou None se nao houver dado."""
        if fi is not None:
            if fi.widths is not None:
                v = fi.widths.get(code)
                if v is not None:
                    return v
            if fi.dw is not None:
                return fi.dw
            if fi.width_source == "afm-helvetica" and fi.code_bytes == 1:
                return float(HELV_W.get(code, DEFAULT_W))
            if fi.subtype == "Type0":
                return 1000.0
        if fi is None:
            return float(HELV_W.get(code, DEFAULT_W))
        return None

    # -- execucao --------------------------------------------------------
    def run(self, data, resources, page_index=None):
        doc = self.doc
        terms = tokenize_content(data)
        gs = {
            "ctm": IDENT, "font": None, "fontname": None, "size": 0.0,
            "tc": 0.0, "tw": 0.0, "tz": 100.0, "tl": 0.0, "ts": 0.0,
        }
        gstack = []
        tm = IDENT
        tlm = IDENT
        operands = []
        path_pts = 0
        bt_depth = 0
        q_depth = 0
        max_q = 0
        i = 0
        n = len(terms)
        while i < n:
            kind, val = terms[i]
            i += 1
            if kind != "op":
                operands.append(val)
                continue
            op = val
            self.op_counts[op] = self.op_counts.get(op, 0) + 1
            if op == "BI":
                j = i
                while j < n and not (terms[j][0] == "op" and terms[j][1] == "EI"):
                    j += 1
                i = min(j + 1, n)
                operands = []
                continue
            if op == "q":
                gstack.append(dict(gs))
                q_depth += 1
                max_q = max(max_q, q_depth)
                operands = []
                continue
            if op == "Q":
                if not gstack:
                    self.errors.append("Q sem q correspondente (pagina %d)" % (self.page + 1))
                else:
                    gs = gstack.pop()
                    q_depth -= 1
                operands = []
                continue
            if op == "BT":
                bt_depth += 1
                tm = IDENT
                tlm = IDENT
                operands = []
                continue
            if op == "ET":
                bt_depth -= 1
                if bt_depth < 0:
                    self.errors.append("ET sem BT correspondente (pagina %d)" % (self.page + 1))
                    bt_depth = 0
                operands = []
                continue
            if op == "cm":
                if len(operands) >= 6:
                    m = tuple(float(x) for x in operands[-6:])
                    gs["ctm"] = mat_mul(m, gs["ctm"])
                operands = []
                continue
            if op == "Tf":
                if len(operands) >= 2:
                    gs["fontname"] = str(operands[-2])
                    gs["size"] = float(operands[-1])
                    self.fonts_used.add(gs["fontname"])
                    gs["font"] = self._lookup_font(resources, gs["fontname"])
                    gs["fontinfo"] = self.doc.font_info(gs["font"])
                operands = []
                continue
            if op == "TL":
                if operands:
                    gs["tl"] = float(operands[-1])
                operands = []
                continue
            if op == "Tc":
                if operands:
                    gs["tc"] = float(operands[-1])
                operands = []
                continue
            if op == "Tw":
                if operands:
                    gs["tw"] = float(operands[-1])
                operands = []
                continue
            if op == "Tz":
                if operands:
                    gs["tz"] = float(operands[-1])
                operands = []
                continue
            if op == "Ts":
                if operands:
                    gs["ts"] = float(operands[-1])
                operands = []
                continue
            if op == "Tm":
                if len(operands) >= 6:
                    tm = tuple(float(x) for x in operands[-6:])
                    tlm = tm
                operands = []
                continue
            if op in ("Td", "TD"):
                if len(operands) >= 2:
                    tx = float(operands[-2])
                    ty = float(operands[-1])
                    if op == "TD":
                        gs["tl"] = -ty
                    tlm = mat_mul((1.0, 0.0, 0.0, 1.0, tx, ty), tlm)
                    tm = tlm
                operands = []
                continue
            if op == "T*":
                tlm = mat_mul((1.0, 0.0, 0.0, 1.0, 0.0, -gs["tl"]), tlm)
                tm = tlm
                operands = []
                continue
            if op == "Tj":
                if operands:
                    tm = self._show(operands[-1], gs, tm)
                    self.text_ops += 1
                operands = []
                continue
            if op == "TJ":
                if operands and isinstance(operands[-1], list):
                    arr = operands[-1]
                    for el in arr:
                        if isinstance(el, (int, float)) and not isinstance(el, bool):
                            tx = -float(el) / 1000.0 * gs["size"] * (gs["tz"] / 100.0)
                            tm = mat_mul((1.0, 0.0, 0.0, 1.0, tx, 0.0), tm)
                        else:
                            tm = self._show(el, gs, tm)
                    self.text_ops += 1
                operands = []
                continue
            if op == "'":
                tlm = mat_mul((1.0, 0.0, 0.0, 1.0, 0.0, -gs["tl"]), tlm)
                tm = tlm
                if operands:
                    tm = self._show(operands[-1], gs, tm)
                    self.text_ops += 1
                operands = []
                continue
            if op == '"':
                if len(operands) >= 3:
                    gs["tw"] = float(operands[-3])
                    gs["tc"] = float(operands[-2])
                    tlm = mat_mul((1.0, 0.0, 0.0, 1.0, 0.0, -gs["tl"]), tlm)
                    tm = tlm
                    tm = self._show(operands[-1], gs, tm)
                    self.text_ops += 1
                operands = []
                continue
            if op == "Do":
                if operands:
                    self._do_xobject(resources, str(operands[-1]), gs)
                operands = []
                continue
            if op in ("m", "l"):
                path_pts += 1
            exp = ARITY.get(op)
            if exp is None:
                self.unknown_ops[op] = self.unknown_ops.get(op, 0) + 1
                if op in ("BT", "ET", "Tj", "TJ", "Tf", "Td", "Tm"):
                    self.errors.append("operador suspeito em content stream: %r" % op)
            elif exp >= 0 and len(operands) < exp:
                self.errors.append(
                    "operador '%s' na pagina %d com %d operandos, exige %d"
                    % (op, self.page + 1, len(operands), exp))
            operands = []
        if bt_depth != 0:
            self.errors.append("BT/ET desbalanceado na pagina %d (saldo %d)" % (self.page + 1, bt_depth))
        if gstack:
            self.errors.append("q/Q desbalanceado na pagina %d (saldo %d)" % (self.page + 1, len(gstack)))

    def _lookup_font(self, resources, name):
        if not isinstance(resources, dict):
            return None
        fonts = resources.get("Font")
        if isinstance(fonts, PDFRef):
            try:
                fonts = self.doc.resolve(fonts)
            except PDFError:
                return None
        if not isinstance(fonts, dict):
            return None
        f = fonts.get(name)
        if isinstance(f, PDFRef):
            try:
                return self.doc.resolve(f)
            except PDFError:
                return None
        return f

    def _do_xobject(self, resources, name, gs):
        if self.form_depth > 6:
            return
        if not isinstance(resources, dict):
            return
        xo = resources.get("XObject")
        if isinstance(xo, PDFRef):
            try:
                xo = self.doc.resolve(xo)
            except PDFError:
                return
        if not isinstance(xo, dict):
            return
        ref = xo.get(name)
        try:
            obj = self.doc.resolve(ref) if isinstance(ref, PDFRef) else ref
        except PDFError:
            return
        if not isinstance(obj, PDFStream):
            return
        if obj.dict.get("Subtype") != "Form":
            return
        try:
            data = self.doc.decode(obj)
        except (PDFError, Unsupported):
            return
        res = obj.dict.get("Resources")
        try:
            res = self.doc.resolve(res) if isinstance(res, PDFRef) else res
        except PDFError:
            res = None
        if not isinstance(res, dict):
            res = resources
        self.form_depth += 1
        try:
            self.run(data, res)
        finally:
            self.form_depth -= 1

    def _show(self, s, gs, tm):
        if not isinstance(s, (PDFString, bytes)):
            return tm
        raw = bytes(s)
        fi = gs.get("fontinfo")
        size = gs["size"]
        if fi is not None and fi.code_bytes == 2:
            codes = [(raw[i] << 8) | raw[i + 1] for i in range(0, len(raw) - 1, 2)]
        else:
            codes = list(raw)
        parts = []
        adv = 0.0
        unmapped = 0
        approx = 0
        for code in codes:
            ch = None
            if fi is not None and fi.tounicode:
                ch = fi.tounicode.get(code)
            if ch is None and (fi is None or fi.code_bytes == 1):
                ch = chr(code)
            if ch is None:
                unmapped += 1
                ch = "\ufffd"
            parts.append(ch)
            w = self.code_width(fi, code)
            if w is None:
                approx += 1
                w = 500.0
            adv += w / 1000.0 * size + gs["tc"]
            if code == 32:
                adv += gs["tw"]
        self.codes_total += len(codes)
        self.codes_unmapped += unmapped
        self.codes_mapped += len(codes) - unmapped
        if approx:
            self.approx_runs += 1
        text = "".join(parts)
        adv *= gs["tz"] / 100.0
        start_ts = (0.0, gs["ts"])
        end_ts = (adv, gs["ts"])
        p0 = mat_apply(gs["ctm"], *mat_apply(tm, *start_ts))
        p1 = mat_apply(gs["ctm"], *mat_apply(tm, *end_ts))
        # altura aproximada da caixa do texto (ascent 0.75, descent -0.25 do corpo)
        asc = size * 0.75
        desc = size * 0.25
        ytop_a = mat_apply(gs["ctm"], *mat_apply(tm, 0.0, gs["ts"] + asc))[1]
        ybot_a = mat_apply(gs["ctm"], *mat_apply(tm, 0.0, gs["ts"] - desc))[1]
        y0 = min(ybot_a, ytop_a)
        y1 = max(ybot_a, ytop_a)
        x0 = min(p0[0], p1[0])
        x1 = max(p0[0], p1[0])
        self.runs.append(TextRun(text, x0, y0, x1, y1, size, gs["fontname"],
                                 self.page, unmapped, approx, raw,
                                 not (fi is not None and fi.code_bytes == 2)))
        return mat_mul((1.0, 0.0, 0.0, 1.0, adv, 0.0), tm)


# ---------------------------------------------------------------------------
# relatorio
# ---------------------------------------------------------------------------
class Report(object):
    def __init__(self):
        self.rows = []
        self.notes = []

    def add(self, cid, status, title, detail=None):
        self.rows.append({"id": cid, "status": status, "title": title, "detail": detail or []})

    def ok(self, cid, title, detail=None):
        self.add(cid, "PASS", title, detail)

    def fail(self, cid, title, detail=None):
        self.add(cid, "FAIL", title, detail)

    def skip(self, cid, title, detail=None):
        self.add(cid, "SKIP", title, detail)

    def note(self, text):
        self.notes.append(text)

    def counts(self):
        c = {"PASS": 0, "FAIL": 0, "SKIP": 0}
        for r in self.rows:
            c[r["status"]] = c.get(r["status"], 0) + 1
        return c

    def print_report(self, out):
        for r in self.rows:
            out.write("[%s] %s %s\n" % (r["status"], r["id"], r["title"]))
            for d in r["detail"]:
                out.write("        %s\n" % d)


# ---------------------------------------------------------------------------
# validacao
# ---------------------------------------------------------------------------
def validate(data, path, rep, want_json=False):
    buf = data
    n = len(buf)
    result = {"path": path, "bytes": n, "pages": [], "checks": [], "text": []}

    rep.note("arquivo: %s (%d bytes)" % (path, n))

    # C01 arquivo nao vazio
    if n == 0:
        rep.fail("C01", "arquivo legivel e nao vazio", ["tamanho 0"])
        return result
    rep.ok("C01", "arquivo legivel e nao vazio", ["%d bytes" % n])

    # C02 cabecalho
    head = buf[:1024]
    m = re.search(rb"%PDF-(\d)\.(\d)", head)
    if not m:
        rep.fail("C02", "cabecalho %PDF-1.x nos primeiros 1024 bytes",
                 ["primeiros bytes: %r" % buf[:16]])
    else:
        ver = "%s.%s" % (m.group(1).decode(), m.group(2).decode())
        first = buf[:8]
        okpos = buf.startswith(b"%PDF-")
        d = ["versao declarada: %s" % ver]
        if not okpos:
            d.append("cabecalho nao esta no byte 0; esta em %d (bytes 0..%d sao lixo)"
                     % (m.start(), m.start() - 1))
        if ver not in ("1.0", "1.1", "1.2", "1.3", "1.4", "1.5", "1.6", "1.7", "2.0"):
            d.append("versao incomum: %s" % ver)
        if okpos:
            rep.ok("C02", "cabecalho %PDF-1.x nos primeiros 1024 bytes", d)
        else:
            rep.fail("C02", "cabecalho %PDF-1.x nos primeiros 1024 bytes", d)

    # C03 %%EOF
    tail = buf[-1024:] if n > 1024 else buf
    eof_pos = tail.rfind(b"%%EOF")
    if eof_pos < 0:
        rep.fail("C03", "marcador %%EOF no fim do arquivo",
                 ["%%EOF ausente nos ultimos %d bytes" % len(tail)])
    else:
        abs_pos = (n - len(tail)) + eof_pos
        after = buf[abs_pos + 5:]
        trailing = after.strip()
        cnt = buf.count(b"%%EOF")
        d = ["marcador no byte {} de {}".format(abs_pos, n),
             "ocorrencias do marcador no arquivo: {}".format(cnt)]
        if trailing:
            d.append("{} bytes uteis depois do marcador ({!r})".format(len(trailing), trailing[:40]))
            rep.fail("C03", "marcador %%EOF no fim do arquivo", d)
        else:
            d.append("bytes depois do marcador: apenas espaco em branco ({})".format(len(after)))
            rep.ok("C03", "marcador %%EOF no fim do arquivo", d)

    # C04 startxref
    doc = PDFDoc(buf, path)
    sx_ok = False
    try:
        off = doc.find_startxref()
    except PDFError as e:
        off = None
        rep.fail("C04", "startxref presente e dentro do arquivo", [str(e)])
    if off is not None:
        d = ["startxref no byte %d aponta para o byte %d" % (doc.startxref_pos, off)]
        if off < 0 or off >= n:
            d.append("offset fora do arquivo (0..%d)" % (n - 1))
            rep.fail("C04", "startxref presente e dentro do arquivo", d)
        else:
            d.append("conteudo no destino: %r" % buf[off:off + 24])
            if not (buf[off:off + 4] == b"xref" or INDIRECT_RE.match(buf, off)):
                d.append("destino nao e 'xref' nem 'N G obj'")
                rep.fail("C04", "startxref presente e dentro do arquivo", d)
            else:
                sx_ok = True
                rep.ok("C04", "startxref presente e dentro do arquivo", d)

    # C05 carregar xref
    xref_loaded = False
    if sx_ok:
        try:
            doc.load_xref()
            xref_loaded = True
            ninuse = sum(1 for e in doc.xref.values() if e["type"] == "n")
            ncomp = sum(1 for e in doc.xref.values() if e["type"] == "c")
            nfree = sum(1 for e in doc.xref.values() if e["type"] == "f")
            d05 = ["tipo: %s" % doc.xref_kind,
                   "entradas: %d em uso, %d comprimidas (object stream), %d livres"
                   % (ninuse, ncomp, nfree)]
            if len(doc.trailers) > 1:
                d05.append("atualizacoes incrementais: %d secoes de xref na cadeia /Prev"
                           % len(doc.trailers))
            if doc.hybrid:
                d05.append("arquivo hibrido: /XRefStm lido alem da tabela classica")
            rep.ok("C05", "secao cross-reference (tabela ou stream) parseada", d05)
        except (PDFError, Unsupported) as e:
            rep.fail("C05", "secao cross-reference (tabela ou stream) parseada",
                     ["tipo detectado: %s" % doc.xref_kind, str(e)])
    else:
        rep.skip("C05", "secao cross-reference parseada", ["startxref invalido (C04)"])

    # C29 documento aberto (sem /Encrypt)
    if xref_loaded:
        enc = doc.trailer.get("Encrypt")
        if enc is not None:
            rep.fail("C29", "documento nao criptografado (/Encrypt ausente)",
                     ["/Encrypt = %r" % (enc,),
                      "com criptografia os streams e as strings nao podem ser lidos sem a "
                      "senha: nada do conteudo e verificavel"])
        else:
            rep.ok("C29", "documento nao criptografado (/Encrypt ausente)", [])
    else:
        rep.skip("C29", "documento nao criptografado", ["xref nao carregada"])

    # C06 formato das entradas de 20 bytes
    if doc.xref_kind == "tabela" and doc.xref_sections:
        bad_total = 0
        d = []
        for s in doc.xref_sections:
            d.append("secao %d %d: %d entradas, %d fora do formato de 20 bytes"
                     % (s["start"], s["count"], s["count"], len(s["bad"])))
            bad_total += len(s["bad"])
            for onum, raw in s["bad"]:
                d.append("objeto %d: entrada crua %r (len=%d)" % (onum, raw, len(raw)))
        if bad_total:
            rep.fail("C06", "entradas de xref com 20 bytes exatos (10+1+5+1+1+2)", d)
        else:
            rep.ok("C06", "entradas de xref com 20 bytes exatos (10+1+5+1+1+2)", d[:1])
    elif xref_loaded:
        rep.skip("C06", "entradas de xref com 20 bytes exatos",
                 ["xref em stream (%s): o formato de 20 bytes nao se aplica" % doc.xref_kind])
    else:
        rep.skip("C06", "entradas de xref com 20 bytes exatos",
                 ["nenhuma seção de xref em tabela foi lida"])

    # C07..C08 offsets e objetos
    if xref_loaded:
        inuse = sorted(k for k, e in doc.xref.items() if e["type"] == "n")
        mismatches = []
        parse_errs = []
        deltas = []
        for num in inuse:
            off = doc.xref[num]["offset"]
            if off < 0 or off >= n:
                mismatches.append("objeto %d: offset %d fora do arquivo" % (num, off))
                continue
            mm = INDIRECT_RE.match(buf, off)
            if not mm:
                mismatches.append("objeto %d: no offset %d nao ha 'N G obj' (bytes: %r)"
                                  % (num, off, buf[off:off + 20]))
                continue
            if int(mm.group(1)) != num:
                mismatches.append("objeto %d: offset %d aponta para o objeto %s"
                                  % (num, off, mm.group(1).decode()))
                deltas.append(int(mm.group(1)) - num)
        if mismatches:
            if deltas and len(set(deltas)) == 1 and deltas[0] != 0:
                mismatches.append(
                    "padrao: as %d discordancias sao o mesmo deslocamento de %+d objeto(s); "
                    "a tabela foi escrita como se a primeira entrada fosse o objeto %d"
                    % (len(deltas), deltas[0],
                       (min(doc.xref_sections[0]["start"] for doc in [doc])
                        if doc.xref_sections else 0)))
            rep.fail("C07", "cada offset da xref abre no objeto declarado", mismatches[:20])
        else:
            rep.ok("C07", "cada offset da xref abre no objeto declarado",
                   ["%d objetos em uso conferidos" % len(inuse)])

        for num in inuse:
            try:
                doc.get_object(num)
            except (PDFError, Unsupported) as e:
                parse_errs.append(str(e))
        if parse_errs:
            rep.fail("C08", "todo objeto em uso parseia ate 'endobj'", parse_errs[:20])
        else:
            rep.ok("C08", "todo objeto em uso parseia ate 'endobj'",
                   ["%d objetos parseados" % len(inuse)])
    else:
        rep.skip("C07", "cada offset da xref abre no objeto declarado", ["xref nao carregada"])
        rep.skip("C08", "todo objeto em uso parseia ate 'endobj'", ["xref nao carregada"])

    # C09 /Size
    if xref_loaded:
        size = doc.trailer.get("Size")
        d = []
        maxnum = max(doc.xref.keys()) if doc.xref else 0
        nentries = sum(s["count"] for s in doc.xref_sections) if doc.xref_kind == "tabela" else len(doc.xref)
        problems = []
        if not isinstance(size, int) or isinstance(size, bool):
            problems.append("/Size ausente ou nao inteiro: %r" % (size,))
        else:
            d.append("/Size = %d" % size)
            d.append("maior numero de objeto na xref = %d" % maxnum)
            d.append("entradas na tabela = %d" % nentries)
            if size < maxnum + 1:
                problems.append("/Size (%d) menor que maior objeto+1 (%d)" % (size, maxnum + 1))
            has_prev = len(doc.trailers) > 1
            if not has_prev and size != nentries:
                problems.append("/Size (%d) diferente do numero de entradas da tabela (%d)"
                                % (size, nentries))
        if problems:
            rep.fail("C09", "trailer /Size coerente com a tabela xref", d + problems)
        else:
            rep.ok("C09", "trailer /Size coerente com a tabela xref", d)
    else:
        rep.skip("C09", "trailer /Size coerente com a tabela xref", ["xref nao carregada"])

    # C10 /Root
    root = None
    if xref_loaded:
        rv = doc.trailer.get("Root")
        if rv is None:
            rep.fail("C10", "trailer /Root presente e resolvivel",
                     ["/Root ausente do trailer; chaves: %s" % sorted(doc.trailer.keys())])
        elif not isinstance(rv, PDFRef):
            rep.fail("C10", "trailer /Root presente e resolvivel",
                     ["/Root nao e referencia indireta: %r" % (rv,)])
        else:
            try:
                root = doc.resolve(rv)
                rep.ok("C10", "trailer /Root presente e resolvivel",
                       ["/Root = %r -> %s" % (rv, type(root).__name__)])
            except (PDFError, Unsupported) as e:
                rep.fail("C10", "trailer /Root presente e resolvivel",
                         ["/Root = %r nao resolve: %s" % (rv, e)])
    else:
        rep.skip("C10", "trailer /Root presente e resolvivel", ["xref nao carregada"])

    # C11 catalog
    if isinstance(root, dict):
        t = root.get("Type")
        d = ["/Type = %r" % (t,)]
        if t == "Catalog":
            if "Pages" in root:
                rep.ok("C11", "objeto /Root e /Type /Catalog com /Pages", d)
            else:
                rep.fail("C11", "objeto /Root e /Type /Catalog com /Pages", d + ["/Pages ausente"])
        else:
            rep.fail("C11", "objeto /Root e /Type /Catalog com /Pages", d)
    elif xref_loaded:
        rep.skip("C11", "objeto /Root e /Type /Catalog", ["/Root nao resolveu (C10)"])

    # C12 referencias pendentes
    if xref_loaded and root is not None:
        refs = set()
        for num in sorted(doc.cache):
            walk_refs(doc.cache[num], refs)
        walk_refs(doc.trailer, refs)
        dangling = []
        for r in sorted(refs):
            try:
                doc.get_object(r)
            except (PDFError, Unsupported) as e:
                dangling.append(str(e))
        if dangling:
            rep.fail("C12", "toda referencia indireta resolve para um objeto existente",
                     dangling[:20])
        else:
            rep.ok("C12", "toda referencia indireta resolve para um objeto existente",
                   ["%d numeros de objeto referenciados" % len(refs)])
    else:
        rep.skip("C12", "toda referencia indireta resolve", ["xref/catalog indisponivel"])

    # C13 objetos fantasma (objeto completo escrito no arquivo e fora da xref)
    hdrs = {}
    if xref_loaded:
        hdrs = scan_object_headers(doc)
        found = set(hdrs.keys())
        known = set(doc.xref.keys())
        shadow = sorted(x for x in found if x not in known)
        free_but_written = sorted(x for x in found
                                  if x in known and doc.xref[x]["type"] == "f")
        d = ["objetos completos no arquivo: %d; numeros na xref: %d" % (len(found), len(known))]
        bad = []
        if shadow:
            bad.append("objetos escritos no arquivo mas ausentes da xref: %s" % shadow[:20])
        if free_but_written:
            bad.append("objetos escritos no arquivo mas marcados livres na xref: %s"
                       % free_but_written[:20])
        if bad:
            rep.fail("C13", "nenhum objeto fantasma fora da tabela xref", d + bad)
        else:
            rep.ok("C13", "nenhum objeto fantasma fora da tabela xref", d)
    else:
        rep.skip("C13", "nenhum objeto fantasma fora da tabela xref", ["xref nao carregada"])

    # C28 mesmo numero de objeto escrito duas vezes
    if hdrs:
        dups = []
        for num in sorted(hdrs):
            offs = hdrs[num]
            if len(offs) > 1:
                dups.append("objeto %d escrito %d vezes, nos offsets %s"
                            % (num, len(offs), offs[:6]))
        if dups:
            rep.fail("C28", "cada numero de objeto aparece uma unica vez no arquivo", dups[:12])
        else:
            rep.ok("C28", "cada numero de objeto aparece uma unica vez no arquivo",
                   ["%d numeros distintos" % len(hdrs)])
    else:
        rep.skip("C28", "cada numero de objeto aparece uma unica vez no arquivo",
                 ["nenhum cabecalho de objeto verificado"])

    # -- arvore de paginas ------------------------------------------------
    pages = []
    page_parents = {}
    count_problems = []
    tree_problems = []
    if isinstance(root, dict):
        try:
            pv = root.get("Pages")
            pages_root = doc.resolve(pv) if isinstance(pv, PDFRef) else pv
            if not isinstance(pages_root, dict):
                tree_problems.append("/Pages nao resolve para dicionario: %r" % (pages_root,))
            else:
                stack = [(pages_root, pv if isinstance(pv, PDFRef) else None, None)]
                seen = set()
                while stack:
                    node, noderef, parent_ref = stack.pop()
                    if id(node) in seen:
                        tree_problems.append("ciclo na arvore de paginas")
                        break
                    seen.add(id(node))
                    t = node.get("Type")
                    if t == "Pages":
                        kids = node.get("Kids")
                        if isinstance(kids, PDFRef):
                            kids = doc.resolve(kids)
                        if not isinstance(kids, list):
                            tree_problems.append("/Pages sem /Kids array: %r" % (kids,))
                            continue
                        # empilha na ordem inversa: pop() devolve os /Kids na ordem
                        # do documento, que e a ordem exigida pelo PDF
                        for k in reversed(kids):
                            try:
                                kn = doc.resolve(k)
                            except (PDFError, Unsupported) as e:
                                tree_problems.append("/Kids contem item que nao resolve: %s" % e)
                                continue
                            if not isinstance(kn, dict):
                                tree_problems.append("/Kids contem item que nao e dicionario")
                                continue
                            stack.append((kn, k if isinstance(k, PDFRef) else None, noderef))
                    elif t == "Page":
                        pages.append((node, noderef, parent_ref))
                    else:
                        tree_problems.append("no da arvore com /Type inesperado: %r" % (t,))
                # checagem de /Count recursiva
                def count_leaf(node, depth=0, seen2=None):
                    if depth > 64:
                        return -1
                    if node.get("Type") == "Page":
                        return 1
                    if node.get("Type") != "Pages":
                        return 0
                    kids = node.get("Kids")
                    if isinstance(kids, PDFRef):
                        try:
                            kids = doc.resolve(kids)
                        except PDFError:
                            return -1
                    if not isinstance(kids, list):
                        return -1
                    tot = 0
                    for k in kids:
                        try:
                            kn = doc.resolve(k)
                        except (PDFError, Unsupported):
                            return -1
                        if not isinstance(kn, dict):
                            return -1
                        c = count_leaf(kn, depth + 1)
                        if c < 0:
                            return -1
                        tot += c
                    return tot
                def check_counts(node, depth=0):
                    if depth > 64:
                        return
                    if node.get("Type") != "Pages":
                        return
                    real = count_leaf(node)
                    decl = node.get("Count")
                    if decl != real:
                        count_problems.append(
                            "/Type /Pages /Count = %r, folhas /Type /Page reais sob este no = %d"
                            % (decl, real))
                    kids = node.get("Kids")
                    if isinstance(kids, PDFRef):
                        try:
                            kids = doc.resolve(kids)
                        except PDFError:
                            return
                    if isinstance(kids, list):
                        for k in kids:
                            try:
                                kn = doc.resolve(k)
                            except (PDFError, Unsupported):
                                continue
                            if isinstance(kn, dict):
                                check_counts(kn, depth + 1)
                check_counts(pages_root)
                if not page_parents:
                    pass
        except (PDFError, Unsupported) as e:
            tree_problems.append("erro ao percorrer a arvore de paginas: %s" % e)
    else:
        tree_problems.append("/Root indisponivel")

    if tree_problems:
        rep.fail("C14", "arvore de paginas percorrivel a partir de /Root /Pages", tree_problems[:12])
    else:
        rep.ok("C14", "arvore de paginas percorrivel a partir de /Root /Pages",
               ["paginas encontradas: %d" % len(pages)])

    if count_problems:
        rep.fail("C15", "/Count igual ao numero real de /Type /Page alcancaveis", count_problems[:12])
    elif tree_problems:
        rep.skip("C15", "/Count igual ao numero real de paginas", ["arvore nao percorrivel"])
    else:
        rep.ok("C15", "/Count igual ao numero real de /Type /Page alcancaveis",
               ["%d pagina(s)" % len(pages)])

    # C16 /Parent
    parent_problems = []
    if pages and not tree_problems:
        def verify_parents(node, node_ref, parent_ref, depth=0):
            if depth > 64:
                return
            t = node.get("Type")
            if t == "Page":
                if parent_ref is None:
                    return
                pv = node.get("Parent")
                if pv is None:
                    parent_problems.append("/Page %r sem /Parent (esperado %r)"
                                           % (node_ref, parent_ref))
                elif not isinstance(pv, PDFRef) or not isinstance(parent_ref, PDFRef):
                    parent_problems.append("/Page %r: /Parent nao e referencia indireta: %r"
                                           % (node_ref, pv))
                elif pv != parent_ref:
                    parent_problems.append("/Page %r: /Parent = %r, no pai real = %r"
                                           % (node_ref, pv, parent_ref))
                return
            if t != "Pages":
                return
            kids = node.get("Kids")
            if isinstance(kids, PDFRef):
                try:
                    kids = doc.resolve(kids)
                except PDFError:
                    return
            if not isinstance(kids, list):
                return
            for k in kids:
                try:
                    kn = doc.resolve(k)
                except (PDFError, Unsupported):
                    continue
                if isinstance(kn, dict):
                    verify_parents(kn, k if isinstance(k, PDFRef) else None, node_ref, depth + 1)
        try:
            pv = root.get("Pages")
            verify_parents(pages_root, pv if isinstance(pv, PDFRef) else None, None)
        except Exception as e:
            parent_problems.append("erro: %s" % e)
    if parent_problems:
        rep.fail("C16", "cada /Page tem /Parent apontando de volta para o no pai",
                 parent_problems[:12])
    elif not pages:
        rep.skip("C16", "cada /Page tem /Parent", ["nenhuma pagina encontrada"])
    else:
        rep.ok("C16", "cada /Page tem /Parent apontando de volta para o no pai",
               ["%d paginas conferidas" % len(pages)])

    # C17 paginas orfas
    if xref_loaded and pages:
        reachable = set()
        for node, noderef, pref in pages:
            if isinstance(noderef, PDFRef):
                reachable.add(noderef.num)
        orphans = []
        for num in sorted(sorted(k for k, e in doc.xref.items() if e["type"] == "n")):
            try:
                o = doc.get_object(num)
            except (PDFError, Unsupported):
                continue
            if isinstance(o, dict) and o.get("Type") == "Page" and num not in reachable:
                orphans.append("objeto %d e /Type /Page mas nao esta na arvore" % num)
        if orphans:
            rep.fail("C17", "todo /Type /Page do arquivo esta na arvore de paginas", orphans[:12])
        else:
            rep.ok("C17", "todo /Type /Page do arquivo esta na arvore de paginas",
                   ["%d paginas na arvore" % len(pages)])
    else:
        rep.skip("C17", "todo /Type /Page do arquivo esta na arvore de paginas",
                 ["nenhuma pagina ou xref indisponivel"])

    # -- /Contents e /Length ---------------------------------------------
    contents_problems = []
    page_contents = []
    if pages:
        for idx, (node, noderef, pref) in enumerate(pages):
            cv = node.get("Contents")
            if cv is None:
                contents_problems.append("pagina %d sem /Contents" % (idx + 1))
                page_contents.append((idx, []))
                continue
            items = cv if isinstance(cv, list) else [cv]
            resolved = []
            for it in items:
                try:
                    o = doc.resolve(it) if isinstance(it, PDFRef) else it
                except (PDFError, Unsupported) as e:
                    contents_problems.append("pagina %d: /Contents nao resolve: %s" % (idx + 1, e))
                    continue
                if not isinstance(o, PDFStream):
                    contents_problems.append(
                        "pagina %d: /Contents nao e stream (e %s)" % (idx + 1, type(o).__name__))
                    continue
                resolved.append(o)
            page_contents.append((idx, resolved))
    if contents_problems:
        rep.fail("C18", "/Contents de cada pagina existe e e stream", contents_problems[:12])
    elif not pages:
        rep.skip("C18", "/Contents de cada pagina existe e e stream", ["nenhuma pagina"])
    else:
        rep.ok("C18", "/Contents de cada pagina existe e e stream",
               ["%d paginas, %d streams de conteudo"
                % (len(pages), sum(len(c) for _i, c in page_contents))])

    # C19 /Length
    if xref_loaded:
        len_problems = []
        for num, st in sorted(doc.streams.items()):
            if st.len_ok is False:
                len_problems.append(
                    "objeto %d: /Length = %r, bytes reais entre 'stream' e 'endstream' = %s"
                    % (num, st.declared, list(st.len_cands)))
            elif st.len_ok is None:
                len_problems.append("objeto %d: /Length ausente ou nao inteiro (%r)"
                                    % (num, st.declared_raw))
        if len_problems:
            rep.fail("C19", "/Length de cada stream igual ao numero real de bytes",
                     len_problems[:20])
        else:
            rep.ok("C19", "/Length de cada stream igual ao numero real de bytes",
                   ["%d streams conferidos" % len(doc.streams)])
    else:
        rep.skip("C19", "/Length de cada stream igual ao numero real de bytes",
                 ["xref nao carregada"])

    # C20 EOL depois de 'stream' e endstream no lugar
    if xref_loaded:
        eol_problems = []
        for num, st in sorted(doc.streams.items()):
            if st.eol == b"":
                eol_problems.append("objeto %d: 'stream' seguido imediatamente de dados "
                                    "(sem CRLF nem LF)" % num)
            elif st.eol == b"\r":
                eol_problems.append("objeto %d: 'stream' seguido de CR sozinho (exige CRLF ou LF)"
                                    % num)
            if st.len_ok and st.endstream_at is not None:
                pass
        if eol_problems:
            rep.fail("C20", "keyword 'stream' seguido de CRLF ou LF, e 'endstream' apos os dados",
                     eol_problems[:20])
        else:
            rep.ok("C20", "keyword 'stream' seguido de CRLF ou LF, e 'endstream' apos os dados",
                   ["%d streams conferidos" % len(doc.streams)])
    else:
        rep.skip("C20", "EOL apos o keyword 'stream'", ["xref nao carregada"])

    # C21 decodificacao dos filtros
    decoded = {}
    decode_problems = []
    unsupported = []
    if xref_loaded:
        for num, st in sorted(doc.streams.items()):
            filt = st.dict.get("Filter")
            if filt is None:
                decoded[num] = st.raw or b""
                continue
            try:
                decoded[num] = doc.decode(st)
            except Unsupported as e:
                unsupported.append("objeto %d: %s" % (num, e))
            except (PDFError, zlib.error) as e:
                decode_problems.append("objeto %d (/Filter %r): %s" % (num, filt, e))
        if decode_problems:
            rep.fail("C21", "todo stream com /Filter descomprime sem erro",
                     decode_problems[:20] + unsupported[:5])
        elif unsupported:
            rep.skip("C21", "todo stream com /Filter descomprime sem erro",
                     unsupported[:12])
        else:
            rep.ok("C21", "todo stream com /Filter descomprime sem erro",
                   ["%d streams decodificados" % len(decoded)])
    else:
        rep.skip("C21", "todo stream com /Filter descomprime sem erro", ["xref nao carregada"])

    # -- recursos e fontes ----------------------------------------------
    def page_resources(node):
        cur = node
        for _ in range(32):
            if not isinstance(cur, dict):
                return None
            rv = cur.get("Resources")
            if rv is not None:
                try:
                    return doc.resolve(rv) if isinstance(rv, PDFRef) else rv
                except (PDFError, Unsupported):
                    return None
            pv = cur.get("Parent")
            if not isinstance(pv, PDFRef):
                return None
            try:
                cur = doc.resolve(pv)
            except (PDFError, Unsupported):
                return None
        return None

    font_problems = []
    if pages:
        for idx, (node, noderef, pref) in enumerate(pages):
            res = page_resources(node)
            if not isinstance(res, dict):
                font_problems.append("pagina %d: /Resources ausente ou nao e dicionario"
                                     % (idx + 1))
                continue
            fonts = res.get("Font")
            if isinstance(fonts, PDFRef):
                try:
                    fonts = doc.resolve(fonts)
                except (PDFError, Unsupported) as e:
                    font_problems.append("pagina %d: /Resources /Font nao resolve: %s"
                                         % (idx + 1, e))
                    continue
            if fonts is None:
                continue
            if not isinstance(fonts, dict):
                font_problems.append("pagina %d: /Font nao e dicionario" % (idx + 1))
                continue
            for name, fref in fonts.items():
                try:
                    fo = doc.resolve(fref) if isinstance(fref, PDFRef) else fref
                except (PDFError, Unsupported) as e:
                    font_problems.append("pagina %d: fonte /%s nao resolve: %s"
                                         % (idx + 1, name, e))
                    continue
                if not isinstance(fo, dict):
                    font_problems.append("pagina %d: fonte /%s nao e dicionario"
                                         % (idx + 1, name))
                    continue
                if fo.get("Type") != "Font":
                    font_problems.append("pagina %d: fonte /%s tem /Type %r (esperado /Font)"
                                         % (idx + 1, name, fo.get("Type")))
                if fo.get("Subtype") is None:
                    font_problems.append("pagina %d: fonte /%s sem /Subtype" % (idx + 1, name))
    if font_problems:
        rep.fail("C23", "toda fonte em /Resources existe como objeto /Type /Font",
                 font_problems[:16])
    elif not pages:
        rep.skip("C23", "fontes em /Resources", ["nenhuma pagina"])
    else:
        rep.ok("C23", "toda fonte em /Resources existe como objeto /Type /Font",
               ["%d paginas conferidas" % len(pages)])

    # -- interpretacao dos content streams -------------------------------
    interp_errors = []
    all_runs = []
    font_use_problems = []
    if pages:
        for idx, streams in page_contents:
            if not streams:
                continue
            node = pages[idx][0]
            res = page_resources(node)
            interp = ContentInterp(doc, idx)
            for st in streams:
                num = st.objnum
                if num not in decoded:
                    interp_errors.append("pagina %d: stream %r nao decodificado" % (idx + 1, num))
                    continue
                try:
                    interp.run(decoded[num], res, idx)
                except (PDFError, Unsupported, ValueError) as e:
                    interp_errors.append("pagina %d: erro ao interpretar content stream: %s"
                                         % (idx + 1, e))
            interp_errors.extend(interp.errors)
            if interp.unknown_ops:
                interp_errors.append("pagina %d: operadores desconhecidos: %s"
                                     % (idx + 1, sorted(interp.unknown_ops.items())))
            fonts_declared = set()
            if isinstance(res, dict):
                f = res.get("Font")
                if isinstance(f, PDFRef):
                    try:
                        f = doc.resolve(f)
                    except (PDFError, Unsupported):
                        f = None
                if isinstance(f, dict):
                    fonts_declared = set(str(k) for k in f.keys())
            for fname in sorted(interp.fonts_used):
                if fname not in fonts_declared:
                    font_use_problems.append(
                        "pagina %d: Tf /%s nao existe em /Resources /Font (tem: %s)"
                        % (idx + 1, fname, sorted(fonts_declared)))
            all_runs.append((idx, interp))

    if interp_errors:
        rep.fail("C22", "content streams com sintaxe valida (q/Q, BT/ET, operandos)",
                 interp_errors[:20])
    elif not pages:
        rep.skip("C22", "content streams com sintaxe valida", ["nenhuma pagina"])
    else:
        rep.ok("C22", "content streams com sintaxe valida (q/Q, BT/ET, operandos)",
               ["%d pagina(s) interpretadas" % len(all_runs)])

    if font_use_problems:
        rep.fail("C24", "toda fonte usada em Tf existe em /Resources", font_use_problems[:16])
    elif not all_runs:
        rep.skip("C24", "toda fonte usada em Tf existe em /Resources", ["sem content streams"])
    else:
        rep.ok("C24", "toda fonte usada em Tf existe em /Resources", ["ok"])

    # C25 MediaBox
    box_problems = []
    page_boxes = []
    if pages:
        for idx, (node, noderef, pref) in enumerate(pages):
            box = None
            cur = node
            for _ in range(32):
                if not isinstance(cur, dict):
                    break
                bv = cur.get("MediaBox")
                if bv is not None:
                    try:
                        box = doc.resolve(bv) if isinstance(bv, PDFRef) else bv
                    except (PDFError, Unsupported):
                        box = None
                    break
                pv = cur.get("Parent")
                if not isinstance(pv, PDFRef):
                    break
                try:
                    cur = doc.resolve(pv)
                except (PDFError, Unsupported):
                    break
            if not (isinstance(box, list) and len(box) == 4):
                box_problems.append("pagina %d: /MediaBox ausente ou nao tem 4 numeros (%r)"
                                    % (idx + 1, box))
                page_boxes.append(None)
                continue
            try:
                nums = [float(doc.resolve(x) if isinstance(x, PDFRef) else x) for x in box]
            except (TypeError, ValueError, PDFError, Unsupported):
                box_problems.append("pagina %d: /MediaBox com valor nao numerico (%r)"
                                    % (idx + 1, box))
                page_boxes.append(None)
                continue
            x0, y0, x1, y1 = nums
            if x1 <= x0 or y1 <= y0:
                box_problems.append("pagina %d: /MediaBox sem area util %r" % (idx + 1, nums))
                page_boxes.append(None)
                continue
            page_boxes.append((x0, y0, x1, y1))
        if box_problems:
            rep.fail("C25", "cada pagina tem /MediaBox valido (4 numeros, area positiva)",
                     box_problems[:12])
        else:
            d = []
            for i, b in enumerate(page_boxes):
                d.append("pagina %d: MediaBox %.1f %.1f %.1f %.1f (%.1f x %.1f pt)"
                         % (i + 1, b[0], b[1], b[2], b[3], b[2] - b[0], b[3] - b[1]))
            rep.ok("C25", "cada pagina tem /MediaBox valido (4 numeros, area positiva)", d)
    else:
        rep.skip("C25", "/MediaBox", ["nenhuma pagina"])

    # C26 texto extraido
    text_ops_total = sum(i.text_ops for _k, i in all_runs)
    codes_total = sum(i.codes_total for _k, i in all_runs)
    codes_mapped = sum(i.codes_mapped for _k, i in all_runs)
    codes_unmapped = sum(i.codes_unmapped for _k, i in all_runs)
    approx_runs = sum(i.approx_runs for _k, i in all_runs)
    run_count = sum(len(i.runs) for _k, i in all_runs)
    total_chars = codes_mapped
    if not pages:
        rep.skip("C26", "texto extraido dos content streams", ["nenhuma pagina"])
    elif text_ops_total == 0:
        rep.fail("C26", "texto extraido dos content streams",
                 ["nenhum operador de texto (Tj/TJ/'/\") em %d pagina(s): "
                  "o documento nao tem texto" % len(pages)])
    elif codes_mapped == 0:
        rep.skip("C26", "texto extraido dos content streams",
                 ["%d trechos de texto e %d codigos de glifo, mas 0 mapeados para Unicode"
                  % (run_count, codes_total),
                  "a fonte nao declara /ToUnicode nem /Encoding mapeavel: os codigos "
                  "sao indices de glifo, o texto nao e verificavel"])
    else:
        d = ["%d caracteres decodificados de %d codigos de glifo, %d trechos de texto"
             % (codes_mapped, codes_total, run_count),
             "%d operadores de texto executados" % text_ops_total]
        if codes_unmapped:
            d.append("%d codigos sem mapeamento Unicode (impressos como U+FFFD)"
                     % codes_unmapped)
        sem_texto = [str(k + 1) for k, i in all_runs if i.text_ops == 0]
        if sem_texto:
            d.append("pagina(s) %s sem nenhum operador de texto (pode ser proposital)"
                     % ", ".join(sem_texto))
        rep.ok("C26", "texto extraido dos content streams", d)

    # C27 texto dentro da MediaBox
    if pages and page_boxes and total_chars:
        over = []
        skipped_approx = 0
        for idx, interp in all_runs:
            runs = interp.runs
            box = page_boxes[idx] if idx < len(page_boxes) else None
            if box is None:
                continue
            bx0, by0, bx1, by1 = box
            for r in runs:
                if r.approx:
                    skipped_approx += 1
                    continue
                if not r.text.strip():
                    continue
                if (r.x0 < bx0 - 1.0 or r.x1 > bx1 + 1.0
                        or r.y0 < by0 - 1.0 or r.y1 > by1 + 1.0):
                    over.append(
                        "pagina %d: texto %r em x[%.1f..%.1f] y[%.1f..%.1f] sai da "
                        "MediaBox x[%.1f..%.1f] y[%.1f..%.1f]"
                        % (idx + 1, r.text[:40], r.x0, r.x1, r.y0, r.y1, bx0, bx1, by0, by1))
        if over:
            rep.fail("C27", "texto estimado cabe dentro da MediaBox da pagina", over[:16])
        else:
            d = []
            if skipped_approx:
                d.append("%d trechos sem metrica de largura confiavel foram excluidos "
                         "desta conta" % skipped_approx)
            for idx, interp in all_runs:
                runs = interp.runs
                if not runs:
                    continue
                xs0 = min(r.x0 for r in runs)
                xs1 = max(r.x1 for r in runs)
                ys0 = min(r.y0 for r in runs)
                ys1 = max(r.y1 for r in runs)
                d.append("pagina %d: caixa do texto estimada x[%.1f..%.1f] y[%.1f..%.1f]"
                         % (idx + 1, xs0, xs1, ys0, ys1))
            rep.ok("C27", "texto estimado cabe dentro da MediaBox da pagina", d)
    else:
        rep.skip("C27", "texto estimado cabe dentro da MediaBox",
                 ["sem texto ou sem MediaBox"])

    # C30 texto UTF-8 cru dentro de fonte de 1 byte (vira mojibake no leitor)
    if all_runs:
        hits = []
        for idx, interp in all_runs:
            for r in interp.runs:
                if not r.codes1 or not r.raw:
                    continue
                if not any(b >= 0x80 for b in r.raw):
                    continue
                try:
                    dec = r.raw.decode("utf-8")
                except UnicodeDecodeError:
                    continue
                if not any(ord(c) > 0x7F for c in dec):
                    continue
                hits.append("pagina %d: bytes %r sao UTF-8 valido (%r) dentro de fonte de "
                            "1 byte (/%s)" % (idx + 1, r.raw[:24], dec[:24], r.font))
        if hits:
            rep.fail("C30", "texto nao-ASCII gravado com a codificacao da fonte", hits[:10])
        else:
            rep.ok("C30", "texto nao-ASCII gravado com a codificacao da fonte",
                   ["nenhuma sequencia UTF-8 crua em fonte de 1 byte"])
    else:
        rep.skip("C30", "texto nao-ASCII gravado com a codificacao da fonte",
                 ["sem texto interpretado"])

    # -- saida de texto ---------------------------------------------------
    result["pages"] = [{"index": i + 1, "mediabox": page_boxes[i] if i < len(page_boxes) else None}
                       for i in range(len(pages))]
    result["checks"] = rep.rows
    result["text"] = []
    for idx, interp in all_runs:
        runs = interp.runs
        result["text"].append({
            "page": idx + 1,
            "runs": [{"t": r.text, "x": round(r.x0, 2), "y": round(r.y0, 2),
                      "x1": round(r.x1, 2), "size": r.size, "font": r.font}
                     for r in runs],
        })
    return result


def layout_lines(runs):
    """Reconstroi as linhas a partir das posicoes dos trechos de texto.

    Nova linha quando a linha de base (y) muda mais que 0.3 do corpo da fonte.
    Espaco quando o trecho anterior termina 0.2 corpo antes do inicio deste
    (geradores que escrevem palavra por palavra com Td). A regra e explicita
    e determinista; nao depende de adivinhar o gerador.
    """
    lines = []
    cur = ""
    prev = None
    for r in runs:
        t = r["t"]
        if not t:
            continue
        if prev is not None:
            size = prev["size"] or 10.0
            if abs(r["y"] - prev["y"]) > max(1.0, 0.3 * size):
                lines.append(cur)
                cur = ""
            else:
                gap = r["x"] - prev["x1"]
                if gap > 0.15 * size and cur and not cur.endswith(" "):
                    cur += " "
        cur += t
        prev = r
    if cur:
        lines.append(cur)
    return lines


def emit_text(out, result, show_runs=False):
    pages = result["text"]
    if not pages:
        out.write("\n=== TEXTO EXTRAIDO: nenhum content stream interpretado ===\n")
        return
    total = sum(len("".join(r["t"] for r in p["runs"])) for p in pages)
    out.write("\n=== TEXTO EXTRAIDO DOS CONTENT STREAMS (%d caracteres) ===\n" % total)
    for p in pages:
        out.write("--- inicio pagina %d ---\n" % p["page"])
        if show_runs:
            for r in p["runs"]:
                out.write("  [x=%8.2f y=%8.2f xfim=%8.2f fonte=%s tam=%s] %r\n"
                          % (r["x"], r["y"], r["x1"], r["font"], r["size"], r["t"]))
        lines = layout_lines(p["runs"])
        if lines:
            for ln in lines:
                out.write(ln + "\n")
        else:
            out.write("(nenhum texto nesta pagina)\n")
        out.write("--- fim pagina %d ---\n" % p["page"])


def main(argv):
    if hasattr(sys.stdout, "reconfigure"):
        try:
            sys.stdout.reconfigure(encoding="utf-8", errors="replace", newline="\n")
        except Exception:
            pass
    if len(argv) < 2:
        sys.stderr.write("uso: pdfcheck.py <arquivo.pdf> [--json]\n")
        return 2
    path = argv[1]
    as_json = "--json" in argv[2:]
    show_runs = "--runs" in argv[2:]
    if not os.path.isfile(path):
        sys.stderr.write("arquivo nao encontrado: %s\n" % path)
        return 2
    with open(path, "rb") as f:
        data = f.read()

    rep = Report()
    try:
        result = validate(data, path, rep, as_json)
    except Exception as e:  # falha de parse que escapou de uma secao
        import traceback
        rep.fail("C00", "validacao executou ate o fim",
                 ["excecao nao tratada: %s: %s" % (type(e).__name__, e)] +
                 traceback.format_exc().splitlines()[-6:])
        result = {"text": [], "pages": [], "checks": rep.rows}

    out = sys.stdout
    if as_json:
        # modo maquina: a saida padrao e apenas o JSON
        c = rep.counts()
        doc = {"path": os.path.abspath(path), "bytes": len(data),
               "checks": rep.rows, "text": result.get("text", []),
               "notes": rep.notes,
               "pass": c["PASS"], "fail": c["FAIL"], "skip": c["SKIP"],
               "veredito": "APROVADO" if c["FAIL"] == 0 else "REPROVADO"}
        out.write(json.dumps(doc, indent=2, ensure_ascii=False) + "\n")
        return 0 if c["FAIL"] == 0 else 1
    out.write("=" * 78 + "\n")
    out.write("pdfcheck -- validador de conformidade PDF (parser proprio, sem biblioteca)\n")
    out.write("arquivo: %s\n" % os.path.abspath(path))
    out.write("tamanho: %d bytes\n" % len(data))
    out.write("=" * 78 + "\n")
    rep.print_report(out)
    emit_text(out, result, show_runs)
    c = rep.counts()
    out.write("\n")
    out.write("=" * 78 + "\n")
    out.write("RESUMO: %d PASS, %d FAIL, %d SKIP de %d checagens\n"
              % (c["PASS"], c["FAIL"], c["SKIP"], len(rep.rows)))
    if c["FAIL"]:
        out.write("VEREDITO: REPROVADO (%d checagem(ns) com FAIL)\n" % c["FAIL"])
    else:
        out.write("VEREDITO: APROVADO nas %d checagens executadas\n" % len(rep.rows))
    if c["SKIP"]:
        out.write("NAO VERIFICADO: %d checagem(ns) em SKIP (ver motivo em cada linha)\n"
                  % c["SKIP"])
    out.write("=" * 78 + "\n")
    return 0 if c["FAIL"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
