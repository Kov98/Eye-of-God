#!/usr/bin/env python3
"""Compares the exif_oracle dump against expected.json.

Prints the headline the task asks for, field by field:
    N de M campos EXIF corretos
and exits non-zero when anything disagrees.
"""

import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load_dump(path):
    files = {}
    shas = {}
    meta = {}
    items = []
    current = None
    with open(path, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if line.startswith("#FILE\t"):
                current = {"fields": {}}
                files[line.split("\t", 1)[1]] = current
                continue
            if line.startswith("#SHA256\t"):
                parts = line.split("\t")
                if len(parts) >= 3:
                    shas[parts[1]] = parts[2]
                continue
            if line.startswith("#ITEM\t"):
                items.append(line.split("\t", 1)[1])
                continue
            if line.startswith("#SCAN\t"):
                meta["SCAN"] = line[len("#SCAN\t"):]
                continue
            if current is not None and line.startswith("#PRESENT\t"):
                current["present"] = int(line.split("\t")[1])
                continue
            if current is not None and line.startswith("#VALID\t"):
                current["valid"] = int(line.split("\t")[1])
                continue
            if current is not None and line.startswith("#ENDIAN\t"):
                current["endian"] = line.split("\t")[1]
                continue
            if current is not None and line.startswith("#ERROR\t"):
                current["error"] = line.split("\t", 1)[1]
                continue
            if current is not None and line.startswith("#FIELDS\t"):
                current["field_count"] = int(line.split("\t")[1])
                continue
            if current is not None and line.startswith("#ISO_DATE\t"):
                current["iso_date"] = line[len("#ISO_DATE\t"):]
                continue
            if line.startswith("#"):
                parts = line[1:].split("\t")
                if len(parts) >= 2:
                    meta[parts[0]] = parts[1]
                continue
            if current is not None and "\t" in line:
                key, value = line.split("\t", 1)
                current["fields"][key] = value
    return files, shas, meta, items


def main():
    dump_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "exif_dump.txt")
    fixtures = os.path.join(HERE, "fixtures")
    with open(os.path.join(HERE, "expected.json"), encoding="utf-8") as handle:
        expected = json.load(handle)

    files, shas, meta, items = load_dump(dump_path)
    problems = []
    correct = 0
    total = 0
    per_key = {}

    print("== EXIF: parser output vs hand-written bytes ==")
    for name in sorted(expected["exif"]):
        spec = expected["exif"][name]
        got = files.get(name)
        if got is None:
            total += 1
            problems.append("%s: nao apareceu no dump" % name)
            continue
        if "present" in spec:
            total += 1
            if got.get("present") == spec["present"]:
                correct += 1
                per_key["present"] = per_key.get("present", 0) + 1
            else:
                problems.append("%s: present esperado %s, veio %s"
                                % (name, spec["present"], got.get("present")))
            continue
        total += 1
        if got.get("endian") == spec["endian"]:
            correct += 1
            per_key["byte order"] = per_key.get("byte order", 0) + 1
        else:
            problems.append("%s: byte order esperado %s, veio %s"
                            % (name, spec["endian"], got.get("endian")))
        if "iso_date" in spec:
            total += 1
            if got.get("iso_date") == spec["iso_date"]:
                correct += 1
                per_key["iso date"] = per_key.get("iso date", 0) + 1
            else:
                problems.append("%s: data normalizada esperada %r, veio %r"
                                % (name, spec["iso_date"], got.get("iso_date")))
        for key in sorted(spec):
            if key in ("endian", "present", "iso_date"):
                continue
            total += 1
            want = spec[key]
            have = got["fields"].get(key)
            if have == want:
                correct += 1
                per_key[key] = per_key.get(key, 0) + 1
            else:
                problems.append("%s / %s: esperado %r, veio %r" % (name, key, want, have))

    print("%d de %d campos EXIF corretos" % (correct, total))
    for key in sorted(per_key):
        print("   %-22s %d ok" % (key, per_key[key]))

    print("")
    print("== SHA-256: file_sha256() vs hashlib ==")
    sha_ok = 0
    sha_total = 0
    for name in sorted(expected["sha256"]):
        want = expected["sha256"][name]
        have = shas.get(name)
        sha_total += 1
        if have == want:
            sha_ok += 1
        else:
            problems.append("sha256 %s: esperado %s, veio %s" % (name, want, have))
    print("%d de %d digests SHA-256 corretos" % (sha_ok, sha_total))

    print("")
    print("== arquivos hostis/truncados (nao podem derrubar o processo) ==")
    for name in sorted(files):
        if name in expected["exif"]:
            continue
        got = files[name]
        print("   %-22s present=%d valid=%d  %s"
              % (name, got.get("present", -1), got.get("valid", -1), got.get("error", "")))

    print("")
    print("== widget GalleryView ==")
    print("   scan: %s" % meta.get("SCAN", "sem linha #SCAN"))
    print("   itens varridos: %s" % meta.get("PATHS", "?"))
    print("   arquivos no dump: %s" % meta.get("FILES", "?"))
    print("   jpegs no dump: %s" % meta.get("JPEG_COUNT", "?"))
    print("   open_index(0)=%s  open_index(99999)=%s  load_directory(inexistente)=%s"
          % (meta.get("OPEN_FIRST"), meta.get("OPEN_OOR"), meta.get("MISSING_DIR")))

    if meta.get("OPEN_FIRST") != "1":
        problems.append("open_index(0) deveria ter aberto o primeiro item")
    if meta.get("OPEN_OOR") != "0":
        problems.append("open_index fora de faixa deveria ter retornado falso")
    if meta.get("MISSING_DIR") != "0":
        problems.append("load_directory de pasta inexistente deveria ter retornado falso")

    # Independent check of the digest the widget's hasher would produce.
    for name in sorted(expected["sha256"]):
        path = None
        for root, _dirs, names in os.walk(fixtures):
            if name in names:
                path = os.path.join(root, name)
                break
        if path is None:
            continue
        with open(path, "rb") as handle:
            if hashlib.sha256(handle.read()).hexdigest() != expected["sha256"][name]:
                problems.append("fixture %s mudou depois de gerada" % name)

    print("")
    if problems:
        print("FALHAS (%d):" % len(problems))
        for problem in problems:
            print("   - %s" % problem)
        return 1
    print("OK: nenhuma divergencia")
    return 0


if __name__ == "__main__":
    sys.exit(main())
