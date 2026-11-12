#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_fixtures.py -- gera os PDFs de teste do pdfcheck.

Escreve, sem nenhuma biblioteca de PDF:
  out/valid_classic.pdf        PDF 1.4 conforme, xref em tabela, conteudo cru
  out/valid_flate.pdf          mesmo documento, content streams com FlateDecode
  out/broken/NN_nome.pdf       20 arquivos com UM defeito injetado cada

Cada defeito e injetado por construcao (nao por acidente): o offset da xref,
o /Length, o /Count, o /Parent etc. sao escritos errados de proposito.

Uso: python make_fixtures.py [dir_saida]
"""

import os
import re
import sys
import zlib


# ---------------------------------------------------------------------------
# construtor de PDF
# ---------------------------------------------------------------------------
def stream_bytes(extra, data, length=None, eol=b"\n", tail_eol=b"\n"):
    """Objeto stream com /Length calculado (ou mentido, se length for dado)."""
    L = len(data) if length is None else length
    head = ("<< " + extra + " /Length " + str(L) + " >>").encode("latin-1")
    return head + b"\nstream" + eol + data + tail_eol + b"endstream"


def build_pdf(objects, root_num=1, header=b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n",
              add_eof=True, trailer_extra="", xref_shift=None,
              xref_entry_len=20, startxref_value=None, truncate_bytes=None,
              size_value=None, free_last_entry=False, raw_tail=b"",
              xref_subsection_count=None):
    """objects: lista de bytes; o indice 0 vira o objeto 1."""
    out = bytearray(header)
    offsets = {}
    for i, body in enumerate(objects, start=1):
        offsets[i] = len(out)
        out += b"%d 0 obj\n" % i + body + b"\nendobj\n"
    xref_pos = len(out)
    nents = len(objects) + 1
    declared = xref_subsection_count if xref_subsection_count is not None else nents
    out += b"xref\n0 %d\n" % declared
    out += b"0000000000 65535 f \n"
    for i in range(1, len(objects) + 1):
        off = offsets[i]
        if xref_shift and i in xref_shift:
            off += xref_shift[i]
        if free_last_entry and i == len(objects):
            out += b"%010d 00000 f \n" % 0
        elif xref_entry_len == 19:
            off_b = b"%010d 00000 n\n" % off
            out += off_b
        elif xref_entry_len == 21:
            out += b"%010d 00000 n  \n" % off
        else:
            out += b"%010d 00000 n \n" % off
    size = size_value if size_value is not None else nents
    out += b"trailer\n<< /Size %d /Root %d 0 R" % (size, root_num)
    if trailer_extra:
        out += b" " + trailer_extra.encode("latin-1")
    out += b" >>\n"
    out += raw_tail
    sv = startxref_value if startxref_value is not None else xref_pos
    out += b"startxref\n%d\n" % sv
    if add_eof:
        out += b"%%EOF\n"
    if truncate_bytes is not None:
        out = out[:truncate_bytes]
    return bytes(out), xref_pos


# ---------------------------------------------------------------------------
# documento de referencia (um laudo)
# ---------------------------------------------------------------------------
PAGE1 = b"""BT
/F1 16 Tf
1 0 0 1 60 790 Tm
(LAUDO PERICIAL - DOCUMENTO DE PROVA) Tj
ET
BT
/F1 10 Tf
60 750 Td
14 TL
(Caso: 2026-0001) Tj
T*
(Perito: Joao da Silva) Tj
T*
(Hash SHA-256 do dispositivo: 3f9a7c) Tj
T*
(Data da coleta: 2026-02-11 14:22:07) Tj
ET
BT
/F1 10 Tf
60 660 Td
[(Texto com kerning) -180 (entre palavras, operador TJ)] TJ
ET
BT
/F1 10 Tf
60 630 Td
(Aspas: \\(teste\\) barra: \\\\ octal: \\101\\102\\103) Tj
ET
BT
/F1 10 Tf
60 600 Td
<4C6175646F20656D20686578206465> Tj
ET
(Linha escrita com o operador quote) '
"""

PAGE2 = b"""BT
/F1 16 Tf
1 0 0 1 60 790 Tm
(ANEXO I - CADEIA DE CUSTODIA) Tj
ET
BT
/F1 10 Tf
60 750 Td
14 TL
(1. Apreensao em 2026-02-11 14:22, local declarado pelo solicitante.) Tj
T*
(2. Acondicionamento em embalagem lacrada numero 0091.) Tj
T*
(3. Transferencia para o laboratorio as 16:05 do mesmo dia.) Tj
T*
(4. Lacre conferido na abertura: integro.) Tj
ET
BT
/F1 10 Tf
60 660 Td
(Assinatura do perito: ______________________________) Tj
ET
"""

CATALOG = b"<< /Type /Catalog /Pages 2 0 R >>"
PAGES = (b"<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 "
         b"/MediaBox [0 0 595 842] >>")
FONT = b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"


def page_obj(contents_ref=b"6 0 R", parent=b"2 0 R", font=b"4 0 R"):
    return (b"<< /Type /Page /Parent " + parent +
            b" /Resources << /Font << /F1 " + font + b" >> >> "
            b"/MediaBox [0 0 595 842] /Contents " + contents_ref + b" >>")


def reference_objects(flate=False, length_lie=None, contents1=b"6 0 R",
                      contents2=b"7 0 R", parent1=b"2 0 R", parent2=b"2 0 R",
                      font1=b"4 0 R", font2=b"4 0 R", count=b"2",
                      catalog_type=True, page1=PAGE1, page2=PAGE2,
                      flate_corrupt=False, tf_bad=False, unbalanced=False):
    c1 = page1
    c2 = page2
    if tf_bad:
        c1 = c1.replace(b"/F1 10 Tf", b"/F9 10 Tf", 1)
    if unbalanced:
        c1 = c1.replace(b"ET\nBT", b"BT\n", 1)
    objs = []
    objs.append(b"<< " + (b"/Type /Catalog " if catalog_type else b"") + b"/Pages 2 0 R >>")
    objs.append(b"<< /Type /Pages /Kids [3 0 R 5 0 R] /Count " + count +
                b" /MediaBox [0 0 595 842] >>")
    objs.append(page_obj(contents1, parent1, font1))
    objs.append(FONT)
    objs.append(page_obj(contents2, parent2, font2))
    if flate:
        d1 = zlib.compress(c1, 9)
        d2 = zlib.compress(c2, 9)
        if flate_corrupt:
            d1 = b"\x78\x9c" + bytes((b ^ 0x5A) for b in d1[2:40]) + d1[40:]
        objs.append(stream_bytes("/Filter /FlateDecode", d1, length_lie))
        objs.append(stream_bytes("/Filter /FlateDecode", d2, None))
    else:
        if flate_corrupt:
            raise ValueError("flate_corrupt exige flate=True")
        objs.append(stream_bytes("", c1, length_lie))
        objs.append(stream_bytes("", c2, None))
    return objs


# ---------------------------------------------------------------------------
def main(argv):
    outdir = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
    brokendir = os.path.join(outdir, "broken")
    os.makedirs(brokendir, exist_ok=True)
    manifest = []

    def emit(name, data, expect, desc, valid=False):
        """expect: lista de ids de checagem que devem ficar em FAIL."""
        if isinstance(expect, str):
            expect = [expect]
        path = os.path.join(outdir if valid else brokendir, name)
        with open(path, "wb") as f:
            f.write(data)
        manifest.append({"file": path, "expect": expect, "desc": desc, "valid": valid})
        return path

    # --- 1. valido, xref em tabela ------------------------------------
    o = reference_objects()
    data, _x = build_pdf(o)
    emit("valid_classic.pdf", data, None, "PDF 1.4 conforme, xref em tabela", valid=True)

    # --- 2. valido, conteudo comprimido com FlateDecode ---------------
    o = reference_objects(flate=True)
    data, _x = build_pdf(o)
    emit("valid_flate.pdf", data, None, "conforme, content streams com FlateDecode", valid=True)

    # ------------------------------------------------------------------
    # quebras injetadas
    # ------------------------------------------------------------------
    o = reference_objects()
    data, xp = build_pdf(o, xref_shift={5: 9})
    emit("01_xref_offset_errado.pdf", data, "C07",
         "offset do objeto 5 na xref somado de +9")

    o = reference_objects(length_lie=999)
    data, xp = build_pdf(o)
    emit("02_length_mentindo.pdf", data, "C19", "objeto 6 declara /Length 999 (real ~500)")

    o = reference_objects()
    data, xp = build_pdf(o, root_num=99)
    emit("03_root_inexistente.pdf", data, "C10", "trailer /Root 99 0 R, objeto nao existe")

    o = reference_objects()
    data, xp = build_pdf(o)
    cut = xp + 24          # no meio da tabela xref
    emit("04_truncado_no_xref.pdf", data[:cut], ["C03", "C04", "C05"],
         "arquivo cortado %d bytes dentro da tabela xref (leva startxref e EOF junto)" % cut)

    o = reference_objects()
    data, xp = build_pdf(o, add_eof=False)
    emit("05_sem_eof.pdf", data, "C03", "%%EOF removido")

    o = reference_objects(flate=True, flate_corrupt=True)
    data, xp = build_pdf(o)
    emit("06_flate_corrompido.pdf", data, "C21",
         "/Filter /FlateDecode com bytes corrompidos no meio")

    o = reference_objects(count=b"7")
    data, xp = build_pdf(o)
    emit("07_count_mentindo.pdf", data, "C15", "/Count 7 com 2 paginas reais")

    o = reference_objects(parent1=b"1 0 R")
    data, xp = build_pdf(o)
    emit("08_parent_errado.pdf", data, "C16",
         "/Parent da pagina 1 aponta para o catalogo (1 0 R)")

    o = reference_objects(contents1=b"88 0 R")
    data, xp = build_pdf(o)
    emit("09_contents_inexistente.pdf", data, "C18", "/Contents 88 0 R nao existe")

    o = reference_objects(font1=b"77 0 R")
    data, xp = build_pdf(o)
    emit("10_fonte_inexistente.pdf", data, "C23", "/Resources /Font /F1 77 0 R nao existe")

    o = reference_objects()
    data, xp = build_pdf(o, header=b"")
    emit("11_sem_cabecalho.pdf", data, "C02", "primeiros bytes nao sao %PDF-1.x")

    o = reference_objects()
    data, xp = build_pdf(o, startxref_value=999999)
    emit("12_startxref_errado.pdf", data, "C04", "startxref aponta para 999999 (fora do arquivo)")

    o = reference_objects()
    data, xp = build_pdf(o, xref_entry_len=19)
    emit("13_xref_entrada_19_bytes.pdf", data, "C06",
         "entradas da xref com 19 bytes (EOL de 1 byte em vez de 2)")

    o = reference_objects(catalog_type=False)
    data, xp = build_pdf(o)
    emit("14_catalog_sem_type.pdf", data, "C11", "objeto /Root sem /Type /Catalog")

    o = reference_objects()
    data, xp = build_pdf(o)
    # reconstroi o stream 6 com 'stream' colado nos dados
    objs2 = list(o)
    objs2[5] = stream_bytes("", PAGE1, None, eol=b"")
    data, xp = build_pdf(objs2)
    emit("15_stream_sem_eol.pdf", data, "C20",
         "keyword 'stream' seguido imediatamente pelos dados (sem CRLF/LF)")

    o = reference_objects()
    data, xp = build_pdf(o)
    ghost = b"20 0 obj\n<< /Type /Page >>\nendobj\n"
    idx = data.rfind(b"startxref")
    data = data[:idx] + ghost + data[idx:]
    emit("16_objeto_fantasma.pdf", data, "C13",
         "objeto 20 escrito no arquivo mas ausente da xref")

    o = reference_objects(page1=b"BT\n/F1 10 Tf\n60 -80 Td\n(Texto fora da pagina) Tj\nET\n")
    data, xp = build_pdf(o)
    emit("17_texto_fora_da_pagina.pdf", data, "C27",
         "texto desenhado em y=-80, fora da MediaBox [0 0 595 842]")

    o = reference_objects()
    data, xp = build_pdf(o, size_value=5)
    emit("18_size_errado.pdf", data, "C09", "trailer /Size 5 com 7 objetos (entradas=8)")

    o = reference_objects(unbalanced=True)
    data, xp = build_pdf(o)
    emit("19_bt_sem_et.pdf", data, "C22", "BT sem ET correspondente no content stream")

    o = reference_objects(tf_bad=True)
    data, xp = build_pdf(o)
    emit("20_fonte_nao_declarada.pdf", data, "C24",
         "/F9 usado em Tf mas /Resources /Font so declara /F1")

    o = reference_objects()
    data, xp = build_pdf(o, xref_subsection_count=40)
    emit("21_xref_declara_mais_entradas.pdf", data, "C05",
         "xref diz '0 40' mas so 8 entradas existem; startxref e EOF preservados")

    o = reference_objects()
    data, xp = build_pdf(o)
    dup = b"3 0 obj\n" + page_obj(b"6 0 R", b"2 0 R", b"4 0 R") + b"\nendobj\n"
    idx = data.find(b"xref\n0 8\n")
    data = data[:idx] + dup + data[idx:]
    new_xref = data.find(b"xref\n0 8\n")          # startxref tem que ser reescrito
    data = re.sub(rb"startxref\n\d+\n", b"startxref\n%d\n" % new_xref, data, count=1)
    emit("22_objeto_duplicado.pdf", data, "C28",
         "objeto 3 escrito duas vezes; a xref aponta para o primeiro")

    graf = b"0.2 0.2 0.8 rg 60 700 400 100 re f\n0 0 0 RG 2 w 60 200 m 500 200 l S\n"
    o = reference_objects(page1=graf, page2=graf)
    data, xp = build_pdf(o)
    emit("23_documento_sem_texto.pdf", data, "C26",
         "as duas paginas so tem operadores graficos: nenhum Tj/TJ no documento")

    o = reference_objects()
    o.append(b"<< /Filter /Standard /V 2 /R 3 /O <00> /U <00> /P -1 >>")
    data, xp = build_pdf(o, trailer_extra="/Encrypt 8 0 R")
    emit("24_pdf_criptografado.pdf", data, "C29",
         "trailer com /Encrypt: o validador nao consegue ler o conteudo e recusa aprovar")

    # UTF-8 cru dentro de string literal de fonte de 1 byte: "acao" com cedilha
    utf8_page = (b"BT\n/F1 12 Tf\n60 780 Td\n(a\xc3\xa7\xc3\xa3o e cora\xc3\xa7\xc3\xa3o) Tj\nET\n")
    o = reference_objects(page1=utf8_page)
    data, xp = build_pdf(o)
    emit("25_texto_utf8_cru.pdf", data, "C30",
         "acentos em UTF-8 (2 bytes) dentro de fonte de 1 byte: renderiza como mojibake")

    import json
    with open(os.path.join(outdir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    print("fixtures geradas em %s" % outdir)
    for m in manifest:
        print("  %-40s %s  %s" % (os.path.basename(m["file"]),
                                  "VALIDO" if m["valid"] else m["expect"], m["desc"]))
    print("total: %d validos, %d quebrados"
          % (sum(1 for m in manifest if m["valid"]),
             sum(1 for m in manifest if not m["valid"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
