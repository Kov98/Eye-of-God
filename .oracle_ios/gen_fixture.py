#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Gera a fixture do modulo iOS do Olho de Deus.

Nada aqui importa ou reutiliza o codigo C++ sob teste. Cada camada usa uma
implementacao independente:

  binary plist  -> plistlib (CPython)
  Manifest.db   -> modulo sqlite3 (CPython)
  PBKDF2        -> hashlib.pbkdf2_hmac (OpenSSL) + conferencia em .NET
  AES/KeyWrap   -> PowerShell + System.Security.Cryptography (gen_crypto.ps1)

Saida:
  fixture/enc/          backup cifrado pronto para o leitor C++
  fixture/plain/        backup nao cifrado
  fixture/expected/     arquivos originais (comparacao byte a byte)
  fixture/expect.tab    expectativas em formato tabulado, lido pelo teste C++
"""

import base64
import hashlib
import hmac
import json
import os
import plistlib
import random
import shutil
import sqlite3
import subprocess
import sys
import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "fixture")
PS_SCRIPT = os.path.join(HERE, "gen_crypto.ps1")

PASSCODE = "senha-de-teste-\u00e7\u00e3\u00e9-2024"
SALT = bytes(range(0x10, 0x24))            # 20 bytes
ITERATIONS = 10000
KEYBAG_UUID = bytes.fromhex("00112233445566778899aabbccddeeff")
CLASS_IDS = [1, 2, 3, 4, 11]


def rand_bytes(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


def file_id_for(domain, relative_path):
    return hashlib.sha1((domain + "-" + relative_path).encode("utf-8")).hexdigest()


def run_crypto(ops):
    """Executa o oraculo PowerShell/.NET e devolve {id: bytes}."""
    job_path = os.path.join(HERE, "_crypto_job.json")
    out_path = os.path.join(HERE, "_crypto_out.json")
    with open(job_path, "w", encoding="utf-8") as handle:
        json.dump({"ops": ops}, handle)
    command = [
        "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", PS_SCRIPT, "-Job", job_path, "-Out", out_path,
    ]
    completed = subprocess.run(command, capture_output=True, text=True)
    if completed.returncode != 0:
        raise SystemExit("oraculo PowerShell falhou:\n" + completed.stdout + completed.stderr)
    with open(out_path, "r", encoding="utf-8-sig") as handle:
        payload = json.load(handle)
    results = {}
    for item in payload["results"]:
        if not item.get("ok", False):
            raise SystemExit("op %s falhou: %s" % (item.get("id"), item.get("error")))
        if "b64" in item:
            results[item["id"]] = base64.b64decode(item["b64"])
        else:
            results[item["id"]] = bytes.fromhex(item["hex"])
    return results


def tlv(tag, data):
    return tag.encode("ascii") + len(data).to_bytes(4, "big") + data


def build_keybag(class_wpky, passcode_key):
    """Monta o blob TLV. O HMCK cobre os bytes anteriores ao proprio bloco."""
    head = (tlv("VERS", (4).to_bytes(4, "big")) +
            tlv("TYPE", (1).to_bytes(4, "big")) +
            tlv("UUID", KEYBAG_UUID))
    mac = hmac.new(passcode_key, head, hashlib.sha1).digest()
    blob = head + tlv("HMCK", mac)
    blob += tlv("WRAP", (0).to_bytes(4, "big"))
    blob += tlv("SALT", SALT)
    blob += tlv("ITER", ITERATIONS.to_bytes(4, "big"))
    for class_id in CLASS_IDS:
        blob += tlv("CLAS", class_id.to_bytes(4, "big"))
        blob += tlv("WRAP", (0).to_bytes(4, "big"))
        blob += tlv("WPKY", class_wpky[class_id])
        blob += tlv("KTYP", (0).to_bytes(4, "big"))
        blob += tlv("PBKY", hashlib.sha256(b"pbky-%d" % class_id).digest())
    blob += tlv("END", b"")
    return blob, mac


def mbfile_archive(meta, extra_objects):
    """NSKeyedArchiver real (o mesmo formato que o iTunes grava em Files.file)."""
    objects = ["$null", None, {
        "$classes": ["MBFile", "NSObject"],
        "$classname": "MBFile",
    }]
    portador = {
        "$class": plistlib.UID(2),
        "RelativePath": meta["relative_path"],
        "Mode": meta["mode"],
        "Flags": meta["flags"],
        "Size": meta["size"],
    }
    if meta.get("protection_class"):
        portador["ProtectionClass"] = meta["protection_class"]
    if meta.get("target"):
        portador["Target"] = meta["target"]
    objects[1] = portador
    objects.extend(extra_objects)
    return plistlib.dumps({
        "$version": 100000,
        "$archiver": "NSKeyedArchiver",
        "$top": {"root": plistlib.UID(1)},
        "$objects": objects,
    }, fmt=plistlib.FMT_BINARY, sort_keys=False)


def write_manifest_db(path, rows):
    if os.path.exists(path):
        os.remove(path)
    connection = sqlite3.connect(path)
    connection.execute(
        "CREATE TABLE Files (fileID TEXT PRIMARY KEY, domain TEXT, "
        "relativePath TEXT, flags INTEGER, file BLOB)")
    connection.execute("CREATE TABLE Properties (key TEXT PRIMARY KEY, value BLOB)")
    connection.executemany(
        "INSERT INTO Files (fileID, domain, relativePath, flags, file) VALUES (?,?,?,?,?)",
        rows)
    connection.execute("INSERT INTO Properties VALUES ('salt', ?)", (SALT,))
    connection.execute("INSERT INTO Properties VALUES ('iterations', ?)",
                       (str(ITERATIONS).encode(),))
    connection.commit()
    connection.close()


def main():
    if not os.path.isfile(PS_SCRIPT):
        raise SystemExit("gen_crypto.ps1 ausente")
    if os.path.isdir(FIXTURE):
        shutil.rmtree(FIXTURE)
    for name in ("enc", "plain", "expected"):
        os.makedirs(os.path.join(FIXTURE, name))

    rng = random.Random(0x0DEADBEEF)

    # ---- conjunto de arquivos -------------------------------------------------
    # (domain, relative_path, protection_class, tamanho, layout do EncryptionKey)
    plan = [
        ("HomeDomain", "Library/SMS/sms.db", 4, 4096, "class-prefix-be"),
        ("HomeDomain", "Documents/vazio.txt", 4, 0, "class-prefix-be"),
        ("HomeDomain", "Documents/um_byte.bin", 4, 1, "class-prefix-be"),
        ("HomeDomain", "Documents/quinze.bin", 3, 15, "class-prefix-be"),
        ("HomeDomain", "Documents/dezesseis.bin", 3, 16, "class-prefix-le"),
        ("CameraRollDomain", "Media/DCIM/100APPLE/IMG_0001.JPG", 3, 65537, "class-prefix-be"),
        ("AppDomain-com.example.app", "Documents/dados.bin", 1, 1000, "class-prefix-be"),
        ("HomeDomain", "Library/Preferences/com.apple.mobiletest.plist", 2, 512,
         "class-prefix-be"),
        ("MediaDomain", "Media/Recordings/\u00e1udio \u00e7\u00e3o.bin", 11, 300,
         "no-prefix"),
        ("HomeDomain", "Library/Caches/com.apple.mobile.installation.plist", 4, 1024,
         "class-prefix-be"),
        ("SysContainerDomain-com.apple.mobilesafari", "Library/Safari/History.db", 3, 8192,
         "class-prefix-be"),
        ("CameraRollDomain", "Media/grande.bin", 4, 262144, "class-prefix-be"),
        ("RootDomain", "Library/compartilhado.txt", 4, 77, "class-prefix-le"),
    ]

    files = []
    for domain, relative, klass, size, layout in plan:
        payload = rand_bytes(rng, size)
        files.append({
            "domain": domain,
            "relative_path": relative,
            "protection_class": klass,
            "size": size,
            "layout": layout,
            "file_id": file_id_for(domain, relative),
            "plain": payload,
            "file_key": rand_bytes(rng, 32),
        })

    directories = [
        ("HomeDomain", "Documents", 0o40755),
        ("CameraRollDomain", "Media/DCIM", 0o40755),
    ]
    symlinks = [
        ("HomeDomain", "Library/atalho", "../Documents/um_byte.bin", 0o120755),
    ]

    # ---- chaves e crypto ------------------------------------------------------
    passcode_key = hashlib.pbkdf2_hmac("sha256", PASSCODE.encode("utf-8"), SALT,
                                       ITERATIONS, 32)
    class_keys = {class_id: rand_bytes(rng, 32) for class_id in CLASS_IDS}

    ops = [{
        "id": "pbkdf2:passcode",
        "op": "pbkdf2",
        "password_b64": base64.b64encode(PASSCODE.encode("utf-8")).decode(),
        "salt_b64": base64.b64encode(SALT).decode(),
        "iterations": ITERATIONS,
        "length": 32,
    }]
    for class_id in CLASS_IDS:
        ops.append({
            "id": "wrap:class:%d" % class_id,
            "op": "kw_wrap",
            "kek_b64": base64.b64encode(passcode_key).decode(),
            "plain_b64": base64.b64encode(class_keys[class_id]).decode(),
        })
    for item in files:
        ops.append({
            "id": "wrap:file:%s" % item["file_id"],
            "op": "kw_wrap",
            "kek_b64": base64.b64encode(class_keys[item["protection_class"]]).decode(),
            "plain_b64": base64.b64encode(item["file_key"]).decode(),
        })
        ops.append({
            "id": "enc:%s" % item["file_id"],
            "op": "aes_cbc_encrypt",
            "key_b64": base64.b64encode(item["file_key"]).decode(),
            "iv_b64": base64.b64encode(bytes(16)).decode(),
            "data_b64": base64.b64encode(item["plain"]).decode(),
        })

    out = run_crypto(ops)

    oracle_passcode = out["pbkdf2:passcode"]
    if oracle_passcode != passcode_key:
        raise SystemExit("PBKDF2 divergiu entre hashlib e .NET\n  %s\n  %s"
                         % (passcode_key.hex(), oracle_passcode.hex()))

    class_wpky = {c: out["wrap:class:%d" % c] for c in CLASS_IDS}
    for item in files:
        item["wrapped_key"] = out["wrap:file:%s" % item["file_id"]]
        item["cipher"] = out["enc:%s" % item["file_id"]]

    keybag_blob, keybag_mac = build_keybag(class_wpky, passcode_key)

    # ---- Manifest.plist -------------------------------------------------------
    manifest = {
        "Version": 200,
        "Date": datetime.datetime(2024, 3, 7, 12, 30, 15),
        "SystemDomainsVersion": 3.25,
        "IsEncrypted": True,
        "WasPasscodeSet": True,
        "IsFullBackup": True,
        "NegativeInt": -12345,
        "BigInt": 2 ** 53 + 7,
        "UnicodeName": "Backup \u2713 \u4e2d\u6587 \U0001F600",
        "EmptyText": "",
        "EmptyData": b"",
        "RawData": b"\x00\x01\x02\xfe\xff",
        "Floats": [1.5, -2.25, 0.0],
        "Nested": {"a": {"b": [1, True, False]}},
        "BackupKeyBag": keybag_blob,
        "Lockdown": {
            "DeviceName": "iPhone 15 Pro de Teste \u2713",
            "ProductVersion": "17.4.1",
            "BuildVersion": "21E236",
            "ProductType": "iPhone16,1",
            "UniqueDeviceID": "00008120-000000000000000E",
        },
        "Applications": {
            "com.example.app": {
                "CFBundleIdentifier": "com.example.app",
                "CFBundleVersion": "1.0",
                "ContainerContentClass": 4,
            }
        },
    }
    manifest_path = os.path.join(FIXTURE, "enc", "Manifest.plist")
    with open(manifest_path, "wb") as handle:
        plistlib.dump(manifest, handle, fmt=plistlib.FMT_BINARY, sort_keys=False)

    # ---- conteudo cifrado + Manifest.db --------------------------------------
    # Um arquivo usa o embrulho NSKeyedArchiver classico (campo apontando para um
    # objeto {"NS.data": ...}); os outros carregam os bytes direto no campo.
    nsdata_ids = {files[9]["file_id"]}

    def encryption_field(item):
        klass = item["protection_class"].to_bytes(4, "big")
        little = item["protection_class"].to_bytes(4, "little")
        layout = item["layout"]
        if layout == "class-prefix-be":
            payload = klass + item["wrapped_key"]
        elif layout == "class-prefix-le":
            payload = little + item["wrapped_key"]
        elif layout == "no-prefix":
            payload = item["wrapped_key"]
        else:
            raise SystemExit("layout desconhecido: " + layout)
        if item["file_id"] in nsdata_ids:
            return plistlib.UID(3), [{"NS.data": payload}]
        return payload, []

    rows = []
    for item in files:
        payload_field, extra = encryption_field(item)
        meta = {
            "relative_path": item["relative_path"],
            "mode": 0o100644,
            "flags": 1,
            "size": item["size"],
            "protection_class": item["protection_class"],
        }
        blob = mbfile_archive_with_key(meta, extra, payload_field)
        rows.append((item["file_id"], item["domain"], item["relative_path"], 1, blob))

        sharded = os.path.join(FIXTURE, "enc", item["file_id"][:2])
        os.makedirs(sharded, exist_ok=True)
        with open(os.path.join(sharded, item["file_id"]), "wb") as handle:
            handle.write(item["cipher"])

        original = os.path.join(FIXTURE, "expected", item["domain"], item["relative_path"])
        os.makedirs(os.path.dirname(original), exist_ok=True)
        with open(original, "wb") as handle:
            handle.write(item["plain"])

    for domain, relative, mode in directories:
        meta = {"relative_path": relative, "mode": mode, "flags": 2, "size": 0}
        rows.append((file_id_for(domain, relative), domain, relative, 2,
                     mbfile_archive(meta, [])))
        os.makedirs(os.path.join(FIXTURE, "expected", domain, relative), exist_ok=True)

    for domain, relative, target, mode in symlinks:
        meta = {"relative_path": relative, "mode": mode, "flags": 4, "size": 0,
                "target": target}
        rows.append((file_id_for(domain, relative), domain, relative, 4,
                     mbfile_archive(meta, [])))

    write_manifest_db(os.path.join(FIXTURE, "enc", "Manifest.db"), rows)
    with open(os.path.join(FIXTURE, "enc", "Status.plist"), "wb") as handle:
        plistlib.dump({"UUID": "11111111-2222-3333-4444-555555555555",
                       "SnapshotState": "finished",
                       "IsFullBackup": True,
                       "Version": "3.3"}, handle, fmt=plistlib.FMT_BINARY)
    with open(os.path.join(FIXTURE, "enc", "Info.plist"), "w", encoding="utf-8") as handle:
        handle.write('<?xml version="1.0" encoding="UTF-8"?>\n'
                     '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" '
                     '"http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
                     '<plist version="1.0"><dict>'
                     '<key>Device Name</key><string>iPhone de Teste</string>'
                     '<key>Product Version</key><string>17.4.1</string>'
                     '</dict></plist>\n')

    # ---- backup nao cifrado ---------------------------------------------------
    plain_manifest = {
        "Version": 200,
        "Date": datetime.datetime(2024, 3, 7, 12, 30, 15),
        "IsEncrypted": False,
        "IsFullBackup": True,
        "Lockdown": {"DeviceName": "iPhone sem cifra", "ProductVersion": "16.7"},
    }
    with open(os.path.join(FIXTURE, "plain", "Manifest.plist"), "wb") as handle:
        plistlib.dump(plain_manifest, handle, fmt=plistlib.FMT_BINARY, sort_keys=False)

    plain_rows = []
    for item in files:
        meta = {
            "relative_path": item["relative_path"],
            "mode": 0o100644,
            "flags": 1,
            "size": item["size"],
        }
        plain_rows.append((item["file_id"], item["domain"], item["relative_path"], 1,
                           mbfile_archive(meta, [])))
        sharded = os.path.join(FIXTURE, "plain", item["file_id"][:2])
        os.makedirs(sharded, exist_ok=True)
        with open(os.path.join(sharded, item["file_id"]), "wb") as handle:
            handle.write(item["plain"])
    for domain, relative, mode in directories:
        meta = {"relative_path": relative, "mode": mode, "flags": 2, "size": 0}
        plain_rows.append((file_id_for(domain, relative), domain, relative, 2,
                           mbfile_archive(meta, [])))
    for domain, relative, target, mode in symlinks:
        meta = {"relative_path": relative, "mode": mode, "flags": 4, "size": 0,
                "target": target}
        plain_rows.append((file_id_for(domain, relative), domain, relative, 4,
                           mbfile_archive(meta, [])))
    write_manifest_db(os.path.join(FIXTURE, "plain", "Manifest.db"), plain_rows)

    # ---- expectativas ---------------------------------------------------------
    lines = ["# gerado por gen_fixture.py - chave<TAB>valor"]
    lines.append("K\tpasscode\t" + PASSCODE)
    lines.append("K\tsalt_hex\t" + SALT.hex())
    lines.append("K\titerations\t" + str(ITERATIONS))
    lines.append("K\tpasscode_key_hex\t" + passcode_key.hex())
    lines.append("K\tkeybag_len\t" + str(len(keybag_blob)))
    lines.append("K\tkeybag_sha256\t" + hashlib.sha256(keybag_blob).hexdigest())
    lines.append("K\tkeybag_hmac_hex\t" + keybag_mac.hex())
    lines.append("K\tplist_version\t200")
    lines.append("K\tplist_date_epoch\t" +
                 str((datetime.datetime(2024, 3, 7, 12, 30, 15) -
                      datetime.datetime(2001, 1, 1)).total_seconds()))
    lines.append("K\tplist_unicode\t" + "\u0042ackup \u2713 \u4e2d\u6587 \U0001F600")
    lines.append("K\tplist_negative\t-12345")
    lines.append("K\tplist_bigint\t" + str(2 ** 53 + 7))
    lines.append("K\tplist_real\t3.25")
    lines.append("K\tdevice_name\tiPhone 15 Pro de Teste \u2713")
    lines.append("K\tproduct_version\t17.4.1")
    lines.append("K\tfile_count\t" + str(len(files)))
    lines.append("K\tdir_count\t" + str(len(directories)))
    lines.append("K\tsymlink_count\t" + str(len(symlinks)))
    lines.append("K\tnsdata_wrapped_file\t" + sorted(nsdata_ids)[0])
    for class_id in CLASS_IDS:
        lines.append("C\t%d\t%s\t%s" % (class_id, class_wpky[class_id].hex(),
                                        class_keys[class_id].hex()))
    for item in files:
        lines.append("F\t%s\t%d\t%s\t%d\t%s\t%s\t%s\t%s\t%s" % (
            item["file_id"], item["protection_class"], item["layout"], item["size"],
            item["file_key"].hex(), hashlib.sha256(item["plain"]).hexdigest(),
            hashlib.sha256(item["cipher"]).hexdigest(),
            item["domain"], item["relative_path"]))
    with open(os.path.join(FIXTURE, "expect.tab"), "w", encoding="utf-8",
              newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")

    summary = {
        "files": len(files),
        "directories": len(directories),
        "symlinks": len(symlinks),
        "classes": len(CLASS_IDS),
        "keybag_bytes": len(keybag_blob),
        "plaintext_bytes": sum(item["size"] for item in files),
        "ciphertext_bytes": sum(len(item["cipher"]) for item in files),
        "passcode_key": passcode_key.hex(),
    }
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    return 0


def mbfile_archive_with_key(meta, extra_objects, payload_field):
    """Igual a mbfile_archive, mas inclui o campo EncryptionKey."""
    objects = ["$null", None, {
        "$classes": ["MBFile", "NSObject"],
        "$classname": "MBFile",
    }]
    portador = {
        "$class": plistlib.UID(2),
        "RelativePath": meta["relative_path"],
        "Mode": meta["mode"],
        "Flags": meta["flags"],
        "Size": meta["size"],
        "EncryptionKey": payload_field,
    }
    if meta.get("protection_class"):
        portador["ProtectionClass"] = meta["protection_class"]
    if meta.get("target"):
        portador["Target"] = meta["target"]
    objects[1] = portador
    objects.extend(extra_objects)
    return plistlib.dumps({
        "$version": 100000,
        "$archiver": "NSKeyedArchiver",
        "$top": {"root": plistlib.UID(1)},
        "$objects": objects,
    }, fmt=plistlib.FMT_BINARY, sort_keys=False)


if __name__ == "__main__":
    sys.exit(main())
