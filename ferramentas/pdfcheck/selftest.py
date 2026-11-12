#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
selftest.py -- prova que o pdfcheck detecta defeito.

Passo 1: roda pdfcheck nos PDFs validos. Exige exit 0 e zero FAIL.
Passo 2: roda pdfcheck em cada PDF quebrado. Exige exit != 0 e que a
         checagem alvo (a que corresponde ao defeito injetado) esteja em FAIL.
Resultado: tabela + "N de N quebras detectadas" + exit 0/1.

Uso: python selftest.py
"""

import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PDFCHECK = os.path.join(HERE, "pdfcheck.py")
OUTDIR = os.path.join(HERE, "out")
FAIL_RE = re.compile(r"^\[FAIL\] (C\d\d) (.*)$")


def run_check(pdf_path):
    p = subprocess.run([sys.executable, PDFCHECK, pdf_path],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    fails = []
    for line in p.stdout.splitlines():
        m = FAIL_RE.match(line.strip())
        if m:
            fails.append((m.group(1), m.group(2)))
    return p.returncode, fails, p.stdout, p.stderr


def main():
    man_path = os.path.join(OUTDIR, "manifest.json")
    if not os.path.isfile(man_path):
        sys.stderr.write("manifest ausente; rode make_fixtures.py primeiro\n")
        return 2
    with open(man_path, encoding="utf-8") as f:
        manifest = json.load(f)

    print("=" * 100)
    print("SELFTEST DO VALIDADOR -- cada defeito injetado tem que produzir FAIL")
    print("=" * 100)
    print("%-34s %-6s %-6s %-8s %s" % ("arquivo", "alvo", "exit", "detectou", "checagens em FAIL"))
    print("-" * 100)

    valid_ok = 0
    valid_total = 0
    broken_ok = 0
    broken_total = 0
    errors = []

    for m in manifest:
        name = os.path.basename(m["file"])
        rc, fails, out, err = run_check(m["file"])
        ids = [f[0] for f in fails]
        if m["valid"]:
            valid_total += 1
            if rc == 0 and not fails:
                valid_ok += 1
                print("%-34s %-6s %-6d %-8s %s" % (name, "-", rc, "OK", "(nenhum)"))
            else:
                print("%-34s %-6s %-6d %-8s %s" % (name, "-", rc, "FALSO+", ",".join(ids)))
                errors.append("%s: valido mas reprovado em %s" % (name, ",".join(ids)))
        else:
            broken_total += 1
            want = m["expect"] if isinstance(m["expect"], list) else [m["expect"]]
            hit = any(w in ids for w in want)
            label = "/".join(want)
            if rc != 0 and hit:
                broken_ok += 1
                print("%-34s %-6s %-6d %-8s %s" % (name, label, rc, "SIM", ",".join(ids)))
            else:
                print("%-34s %-6s %-6d %-8s %s" % (name, label, rc, "NAO",
                                                  ",".join(ids) if ids else "(nenhum)"))
                errors.append("%s: esperava FAIL em %s, obtive %s (exit %d)"
                              % (name, want, ids, rc))

    print("-" * 100)
    print("PDFs validos aprovados:      %d de %d" % (valid_ok, valid_total))
    print("QUEBRAS DETECTADAS:          %d de %d" % (broken_ok, broken_total))
    if errors:
        print("")
        print("FALHAS DO PROPRIO SELFTEST:")
        for e in errors:
            print("  - %s" % e)
    print("")
    ok = (valid_ok == valid_total) and (broken_ok == broken_total)
    print("SELFTEST: %s" % ("PASSOU" if ok else "FALHOU"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
