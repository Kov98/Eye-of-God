#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Valida o manifesto JSON gerado por backup_write_manifest_json."""

import json
import sys

PATH = ".oracle_ios/out/out_enc_manifest.json"


def main(path=PATH):
    raw = open(path, "rb").read()
    text = raw.decode("utf-8")
    data = json.loads(text)

    problems = []
    if data.get("module") != "olho::ios":
        problems.append("module")
    if data.get("encrypted") is not True:
        problems.append("encrypted")
    if not data.get("device_name"):
        problems.append("device_name vazio")
    files = data.get("files", [])
    extracted = [f for f in files if f["status"] == "extracted"]
    if len(files) != data["summary"]["entries_seen"]:
        problems.append("contagem de registros difere de entries_seen")
    for item in extracted:
        if len(item["sha256"]) != 64:
            problems.append("sha256 invalido em " + item["file_id"])
        if len(item["sha256_ciphertext"]) != 64:
            problems.append("sha256_ciphertext invalido em " + item["file_id"])
        if item["size"] != data["summary"]["bytes_written"] and False:
            problems.append("tamanho")
    total = sum(f["size"] for f in extracted)
    if total != data["summary"]["bytes_written"]:
        problems.append("soma de bytes difere do summary")

    status = {}
    for item in files:
        status[item["status"]] = status.get(item["status"], 0) + 1
    print("manifesto:", path)
    print("  device_name:", ascii(data.get("device_name")))
    print("  registros:", len(files), status)
    print("  bytes extraidos:", total)
    print("  problemas:", problems if problems else "nenhum")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
