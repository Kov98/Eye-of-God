#!/usr/bin/env python3
"""Independent oracle for the Android backup (.ab) reader.

Nothing here shares code with the C++ under test:

  * tar            -> Python tarfile (GNU and PAX flavours)
  * compression    -> Python zlib (the format AOSP actually writes) plus gzip
  * key derivation -> hashlib.pbkdf2_hmac (OpenSSL), not BCrypt
  * AES-256-CBC    -> .NET System.Security.Cryptography via aes_helper.ps1

The generator writes the expected file hashes from the *source tree it built*,
not from the archive, so an error in the reader cannot be reproduced by the
fixture. Every variant is deterministic: same bytes on every run.
"""

import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")
AES_PS1 = os.path.join(HERE, "aes_helper.ps1")
TMP = os.path.join(HERE, "tmp")

PW_ASCII = "senha-de-teste-123"
PW_ACCENT = "Senha\u00c7\u00e3o2024"   # utf8 and 8bit encodings differ only here
ROUNDS = 10000
FIXED_MTIME = 1700000000


def shell():
    for name in ("pwsh", "powershell"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("PowerShell nao encontrado no PATH")


SHELL = shell()


def aes_encrypt(key, iv, data):
    os.makedirs(TMP, exist_ok=True)
    src = os.path.join(TMP, "plain.bin")
    dst = os.path.join(TMP, "cipher.bin")
    with open(src, "wb") as handle:
        handle.write(data)
    if os.path.exists(dst):
        os.remove(dst)
    result = subprocess.run(
        [SHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", AES_PS1,
         "-KeyHex", key.hex(), "-IvHex", iv.hex(), "-InFile", src, "-OutFile", dst],
        capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("AES .NET falhou: " + result.stderr)
    with open(dst, "rb") as handle:
        return handle.read()


def stream_bytes(seed, size):
    """Deterministic pseudo-random content (no dependency on random's algorithm)."""
    out = bytearray()
    counter = 0
    while len(out) < size:
        out += hashlib.sha256(("%s:%d" % (seed, counter)).encode()).digest()
        counter += 1
    return bytes(out[:size])


# ---------------------------------------------------------------------------
# tar building
# ---------------------------------------------------------------------------

def build_tar(entries, fmt):
    """entries: list of (kind, name, payload) with kind in dir/file/link."""
    buffer = io.BytesIO()
    seen_dirs = set()

    def add_parents(tf, name):
        parts = name.split("/")[:-1]
        for i in range(len(parts)):
            path = "/".join(parts[: i + 1])
            if path in seen_dirs:
                continue
            seen_dirs.add(path)
            info = tarfile.TarInfo(path)
            info.type = tarfile.DIRTYPE
            info.mode = 0o755
            info.mtime = FIXED_MTIME
            tf.addfile(info)

    with tarfile.open(fileobj=buffer, mode="w", format=fmt, encoding="utf-8") as tf:
        for kind, name, payload in entries:
            add_parents(tf, name)
            info = tarfile.TarInfo(name)
            info.mtime = FIXED_MTIME
            if kind == "dir":
                info.type = tarfile.DIRTYPE
                info.mode = int(payload)
                tf.addfile(info)
            elif kind == "file":
                data, mode = payload
                info.size = len(data)
                info.mode = mode
                tf.addfile(info, io.BytesIO(data))
            elif kind == "link":
                info.type = tarfile.SYMTYPE
                info.linkname = payload
                info.mode = 0o777
                tf.addfile(info)
            else:
                raise ValueError("tipo desconhecido: " + kind)
    return buffer.getvalue()


# ---------------------------------------------------------------------------
# .ab writing (the layout AOSP's PerformAdbBackupTask emits)
# ---------------------------------------------------------------------------

def pbkdf2(password_bytes, salt):
    return hashlib.pbkdf2_hmac("sha1", password_bytes, salt, ROUNDS, 32)


def password_bytes(password, encoding):
    if encoding == "utf8":
        return password.encode("utf-8")
    return bytes(ord(ch) & 0xFF for ch in password)


def master_key_as_password(master_key, encoding):
    """Java casts each byte to char (sign extension) before the PBKDF2 PBEKeySpec."""
    if encoding == "8bit":
        return master_key
    out = bytearray()
    for byte in master_key:
        code = byte if byte < 0x80 else (0xFF00 | byte)
        out += chr(code).encode("utf-8")
    return bytes(out)


def header_lines(version, compressed, algorithm, user_salt=None, checksum_salt=None,
                 user_iv=None, blob=None):
    lines = ["ANDROID BACKUP", str(version), "1" if compressed else "0", algorithm]
    if algorithm != "none":
        # AOSP writes uppercase hex; the reader must accept it as well as lowercase.
        lines += [user_salt.hex().upper(), checksum_salt.hex().upper(), str(ROUNDS),
                  user_iv.hex().upper(), blob.hex().upper()]
    return ("\n".join(lines) + "\n").encode("utf-8")


def container_bytes(tar_bytes, compressed, container):
    if not compressed:
        return tar_bytes
    if container == "zlib":
        return zlib.compress(tar_bytes, 9)
    if container == "gzip":
        import gzip
        return gzip.compress(tar_bytes, 9, mtime=0)
    raise ValueError(container)


def make_ab(name, tar_bytes, version=5, compressed=True, container="zlib",
            password=None, encoding="utf8"):
    salt_seed = name + ":usersalt"
    if password is None:
        payload = container_bytes(tar_bytes, compressed, container)
        return header_lines(version, compressed, "none") + payload

    user_salt = stream_bytes(salt_seed, 64)
    checksum_salt = stream_bytes(name + ":cksalt", 64)
    user_iv = stream_bytes(name + ":useriv", 16)
    master_key = stream_bytes(name + ":masterkey", 32)
    master_iv = stream_bytes(name + ":masteriv", 16)

    user_key = pbkdf2(password_bytes(password, encoding), user_salt)
    checksum = pbkdf2(master_key_as_password(master_key, encoding), checksum_salt)

    blob = bytes([len(master_iv)]) + master_iv
    blob += bytes([len(master_key)]) + master_key
    blob += bytes([len(checksum)]) + checksum
    blob_encrypted = aes_encrypt(user_key, user_iv, blob)

    payload = container_bytes(tar_bytes, compressed, container)
    ciphertext = aes_encrypt(master_key, master_iv, payload)

    header = header_lines(version, compressed, "AES-256", user_salt, checksum_salt, user_iv,
                          blob_encrypted)
    return header + ciphertext


# ---------------------------------------------------------------------------
# fixtures
# ---------------------------------------------------------------------------

GENERIC_TREE = [
    ("dir", "databases", 0o755),
    ("file", "databases/msgstore.db", (stream_bytes("msgstore", 300000), 0o600)),
    ("file", "databases/msgstore.db-wal", (stream_bytes("wal", 65536), 0o600)),
    ("file", "databases/msgstore.db-shm", (stream_bytes("shm", 32768), 0o600)),
    ("file", "files/key", (stream_bytes("wakey", 32), 0o600)),
    ("file", "files/empty.bin", (b"", 0o600)),
    ("file", "files/tiny.txt", (b"x", 0o644)),
    ("file", "files/modes/exec.sh", (stream_bytes("exec", 1024), 0o755)),
    ("file", "files/modes/readonly.txt", (stream_bytes("ro", 512), 0o444)),
    ("file", "files/deep/a/b/c/d/e/f/g/h/i/j/deep.txt", (stream_bytes("deep", 2048), 0o644)),
    ("file", "files/unicode/\u00e1\u00e9\u00ed\u00f3\u00fa-\u4e2d\u6587-\U0001F600.txt",
     (stream_bytes("unicode", 700), 0o644)),
    ("file", "files/long/" + "L" * 90 + "/" + "N" * 90 + ".txt",
     (stream_bytes("longname", 900), 0o644)),
    ("file", "files/big.bin", (stream_bytes("big", 4 * 1024 * 1024), 0o644)),
    ("dir", "whatsapp/Media", 0o755),
    ("file", "whatsapp/Media/WhatsApp Images/IMG-20240101-WA0001.jpg",
     (stream_bytes("img1", 200000), 0o644)),
    ("file", "whatsapp/Media/WhatsApp Video/VID-20240101-WA0001.mp4",
     (stream_bytes("vid1", 1048576), 0o644)),
    ("file", "whatsapp/Media/WhatsApp Audio/PTT-20240101-WA0001.opus",
     (stream_bytes("aud1", 40000), 0o644)),
    ("link", "whatsapp/link-to-msgstore", "databases/msgstore.db"),
]

# The layout 'adb backup com.whatsapp' produces: data/ is remapped to
# apps/<pkg>/{db,f,sp,r} and the package manifest is the first entry.
WA_BACKUP_TREE = [
    ("file", "apps/com.whatsapp/_manifest", (b"\x01\x02com.whatsapp\x00", 0o600)),
    ("file", "apps/com.whatsapp/db/msgstore.db", (stream_bytes("wa-msgstore", 250000), 0o600)),
    ("file", "apps/com.whatsapp/db/msgstore.db-wal", (stream_bytes("wa-wal", 49152), 0o600)),
    ("file", "apps/com.whatsapp/f/key", (stream_bytes("wa-key-32", 32), 0o600)),
    ("file", "apps/com.whatsapp/sp/com.whatsapp_preferences.xml",
     (b"<map><string name=\"version\">2.24.1.78</string></map>", 0o600)),
    ("file", "apps/com.whatsapp/r/databases/msgstore.db", (stream_bytes("decoy", 128), 0o600)),
]

SLIP_TREE = [
    ("file", "safe/ok.txt", (b"conteudo legitimo", 0o644)),
    ("file", "../evil.txt", (b"fora da raiz", 0o644)),
    ("file", "/abs.txt", (b"absoluto", 0o644)),
    ("file", "C:\\win.txt", (b"drive", 0o644)),
    ("file", "safe/nested/../escape.txt", (b"travessia", 0o644)),
]


def is_safe_member(name):
    """Mirrors the containment rule the reader must enforce."""
    path = name.replace("\\", "/")
    if not path or path.startswith("/"):
        return False
    if len(path) >= 2 and path[1] == ":":
        return False
    parts = [part for part in path.split("/") if part not in ("", ".")]
    if not parts or any(part == ".." for part in parts):
        return False
    return True


def expected_tsv(entries, safe_only=False):
    rows = []
    for kind, name, payload in entries:
        if kind != "file":
            continue
        if safe_only and not is_safe_member(name):
            continue
        data, mode = payload
        digest = hashlib.sha256(data).hexdigest()
        rows.append("%s\t%d\t%s\t%03o" % (name, len(data), digest, mode))
    rows.sort()
    return "\n".join(rows) + "\n"


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def write_text(path, text):
    write(path, text.encode("utf-8"))


def main():
    if os.path.isdir(FIXTURES):
        shutil.rmtree(FIXTURES)
    os.makedirs(FIXTURES)

    generic_tar = build_tar(GENERIC_TREE, tarfile.GNU_FORMAT)
    generic_pax = build_tar(GENERIC_TREE, tarfile.PAX_FORMAT)
    wa_tar = build_tar(WA_BACKUP_TREE, tarfile.GNU_FORMAT)
    slip_tar = build_tar(SLIP_TREE, tarfile.GNU_FORMAT)

    write_text(os.path.join(FIXTURES, "tree.expected.tsv"), expected_tsv(GENERIC_TREE))
    write_text(os.path.join(FIXTURES, "wa.expected.tsv"), expected_tsv(WA_BACKUP_TREE))
    # The names the reader must refuse, exactly as the tar header spells them.
    write_text(os.path.join(FIXTURES, "slip.rejected.txt"),
               "../evil.txt\n/abs.txt\nC:\\win.txt\nsafe/nested/../escape.txt\n")

    cases = []

    def emit(name, blob, expect_lines):
        write(os.path.join(FIXTURES, name + ".ab"), blob)
        write_text(os.path.join(FIXTURES, name + ".expect"),
                   "case=%s\n" % name + "\n".join(expect_lines) + "\n")
        cases.append(name)

    # 1-2: the format adb really writes (zlib), UTF-8 key encoding.
    emit("v5_aes_zlib",
         make_ab("v5_aes_zlib", generic_tar, 5, True, "zlib", PW_ASCII, "utf8"),
         ["ok=1", "password=" + PW_ASCII, "container=zlib", "integrity=1",
          "manifest=tree.expected.tsv", "rejected=0", "files=15"])
    emit("v5_aes_zlib_accent",
         make_ab("v5_aes_zlib_accent", generic_tar, 5, True, "zlib", PW_ACCENT, "utf8"),
         ["ok=1", "password=" + PW_ACCENT, "container=zlib", "integrity=1",
          "manifest=tree.expected.tsv", "rejected=0", "files=15"])
    # 3: version 1 with the legacy 8-bit PBKDF2 encoding (AOSP's PBKDF_FALLBACK).
    emit("v1_aes_zlib_8bit",
         make_ab("v1_aes_zlib_8bit", generic_tar, 1, True, "zlib", PW_ACCENT, "8bit"),
         ["ok=1", "password=" + PW_ACCENT, "container=zlib", "integrity=1",
          "manifest=tree.expected.tsv", "rejected=0", "files=15", "encoding=8bit"])
    # 4: encrypted but uncompressed -> PKCS7 padding on a raw tar.
    emit("v3_aes_plain",
         make_ab("v3_aes_plain", generic_tar, 3, False, "zlib", PW_ASCII, "utf8"),
         ["ok=1", "password=" + PW_ASCII, "container=", "integrity=0",
          "manifest=tree.expected.tsv", "rejected=0", "files=15"])
    # 5: PAX tar (different long-name mechanism).
    emit("v5_pax_aes",
         make_ab("v5_pax_aes", generic_pax, 5, True, "zlib", PW_ASCII, "utf8"),
         ["ok=1", "password=" + PW_ASCII, "container=zlib", "integrity=1",
          "manifest=tree.expected.tsv", "rejected=0", "files=15"])
    # 6: gzip instead of zlib, to prove the sniffing branch is real.
    emit("v5_aes_gzip",
         make_ab("v5_aes_gzip", generic_tar, 5, True, "gzip", PW_ASCII, "utf8"),
         ["ok=1", "password=" + PW_ASCII, "container=gzip", "integrity=1",
          "manifest=tree.expected.tsv", "rejected=0", "files=15"])
    # 7-8: unencrypted variants.
    emit("v2_none_zlib", make_ab("v2_none_zlib", generic_tar, 2, True, "zlib"),
         ["ok=1", "password=", "container=zlib", "integrity=1",
          "manifest=tree.expected.tsv", "rejected=0", "files=15"])
    emit("v5_none_plain", make_ab("v5_none_plain", generic_tar, 5, False, "zlib"),
         ["ok=1", "password=", "container=", "integrity=0",
          "manifest=tree.expected.tsv", "rejected=0", "files=15"])
    # 9: tar-slip archive: every unsafe member must be refused, the safe one kept.
    # Only the safe member may land on disk; every unsafe one must be reported.
    emit("v5_slip", make_ab("v5_slip", slip_tar, 5, True, "zlib"),
         ["ok=1", "password=", "container=zlib", "integrity=1",
          "manifest=slip.expected.tsv", "rejected_min=4", "files=1",
          "rejected_list=slip.rejected.txt"])
    # 10: the WhatsApp case fixture.
    emit("wa_case_v5",
         make_ab("wa_case_v5", wa_tar, 5, True, "zlib", PW_ASCII, "utf8"),
         ["ok=1", "password=" + PW_ASCII, "container=zlib", "integrity=1",
          "manifest=wa.expected.tsv", "rejected=0", "files=6"])

    write_text(os.path.join(FIXTURES, "slip.expected.tsv"),
               expected_tsv(SLIP_TREE, safe_only=True))

    # --- negative fixtures ------------------------------------------------
    good = make_ab("v5_aes_zlib", generic_tar, 5, True, "zlib", PW_ASCII, "utf8")
    header_end = good.index(b"\n", good.index(b"AES-256"))

    emit("bad_magic", b"ANDROID BACKUQ\n5\n1\nnone\n" + zlib.compress(generic_tar, 9),
         ["ok=0", "error_contains=magica invalida"])
    emit("bad_version", b"ANDROID BACKUP\n9\n1\nnone\n" + zlib.compress(generic_tar, 9),
         ["ok=0", "error_contains=nao suportada"])
    emit("bad_algo",
         b"ANDROID BACKUP\n1\n1\n1\n" + b"00" * 64 + b"\n" + b"00" * 64 + b"\n10000\n" +
         zlib.compress(generic_tar, 9),
         ["ok=0", "error_contains=linha 4"])
    emit("bad_salt",
         b"ANDROID BACKUP\n5\n1\nAES-256\n" + b"zz" * 64 + b"\n" + b"11" * 64 +
         b"\n10000\n" + b"22" * 16 + b"\n" + b"33" * 96 + b"\n",
         ["ok=0", "error_contains=nao hexadecimal"])
    emit("no_payload",
         header_lines(5, True, "none"),
         ["ok=0", "error_contains=sem payload"])
    emit("truncated_cipher",
         good[: len(good) - len(good) // 3],
         ["ok=0", "password=" + PW_ASCII])
    corrupt = bytearray(good)
    corrupt[-1] ^= 0xFF
    emit("corrupt_last_block", bytes(corrupt),
         ["ok=0", "password=" + PW_ASCII])
    # Adler-32 corruption on an unencrypted stream: the tar may extract, but the
    # integrity check must still refuse the archive.
    plain_zlib = bytearray(make_ab("v2_none_zlib", generic_tar, 2, True, "zlib"))
    plain_zlib[-1] ^= 0xFF
    emit("corrupt_adler", bytes(plain_zlib),
         ["ok=0", "password=", "error_contains=checksum"])

    # --- adb fixtures -----------------------------------------------------
    adb_dir = os.path.join(FIXTURES, "adb")
    adb_samples = {
        "devices_one": (
            "List of devices attached\n"
            "R58M12ABCDE            device usb:2-1 product:beyond1ltexx model:SM_G973F device:beyond1 transport_id:1\n"
            "\n",
            ["ok=1", "count=1", "dev=R58M12ABCDE|device|SM_G973F|beyond1ltexx|beyond1"]),
        "devices_unauthorized": (
            "List of devices attached\n"
            "0123456789ABCDEF       unauthorized usb:1-2 transport_id:2\n"
            "\n",
            ["ok=1", "count=1", "dev=0123456789ABCDEF|unauthorized|||"]),
        "devices_offline": (
            "List of devices attached\n"
            "emulator-5554          offline transport_id:4\n"
            "\n",
            ["ok=1", "count=1", "dev=emulator-5554|offline|||"]),
        "devices_mixed": (
            "* daemon not running; starting now at tcp:5037\n"
            "* daemon started successfully\n"
            "List of devices attached\n"
            "emulator-5554          device product:sdk_gphone64_x86_64 model:sdk_gphone64_x86_64 device:emu64xa transport_id:1\n"
            "0123456789ABCDEF       unauthorized usb:1-2 transport_id:2\n"
            "R58M12ABCDE            offline usb:2-1 transport_id:3\n"
            "ABC123                 no permissions (user in plugdev group; are your udev rules wrong?); see [http://developer.android.com/tools/device.html] usb:1-4 transport_id:5\n"
            "\n",
            ["ok=1", "count=4",
             "dev=emulator-5554|device|sdk_gphone64_x86_64|sdk_gphone64_x86_64|emu64xa",
             "dev=0123456789ABCDEF|unauthorized|||",
             "dev=R58M12ABCDE|offline|||",
             "dev=ABC123|no_permissions|||"]),
        "devices_none": (
            "List of devices attached\n\n",
            ["ok=1", "count=0"]),
        "devices_empty": ("", ["ok=0", "count=0", "error_contains=saida vazia"]),
        "devices_network": (
            "List of devices attached\n"
            "192.168.0.10:5555      device product:x model:y device:z transport_id:7\n"
            "R58M12ABCDE            recovery usb:2-1 transport_id:3\n"
            "\n",
            ["ok=1", "count=2",
             "dev=192.168.0.10:5555|device|y|x|z",
             "dev=R58M12ABCDE|recovery|||"]),
        "devices_crlf": (
            "List of devices attached\r\n"
            "R58M12ABCDE            device usb:2-1 model:SM_G973F device:beyond1 transport_id:1\r\n",
            ["ok=1", "count=1", "dev=R58M12ABCDE|device|SM_G973F||beyond1"]),
        "devices_server_banner": (
            "adb server version (41) doesn't match this client (39); killing...\n"
            "* daemon started successfully\n"
            "List of devices attached\n\n",
            ["ok=1", "count=0"]),
        "devices_garbage": ("adb: command not found\n",
                            ["ok=0", "count=0", "error_contains=cabecalho"]),
    }
    os.makedirs(adb_dir, exist_ok=True)
    for name, (text, expect) in adb_samples.items():
        write_text(os.path.join(adb_dir, name + ".txt"), text)
        write_text(os.path.join(adb_dir, name + ".expect"), "\n".join(expect) + "\n")

    # --- a pulled tree for the WhatsApp case builder -----------------------
    pulled = os.path.join(FIXTURES, "pulled")
    msgstore = stream_bytes("pulled-msgstore", 180000)
    wal = stream_bytes("pulled-wal", 40000)
    key = stream_bytes("pulled-key", 32)
    image = stream_bytes("pulled-img", 120000)
    video = stream_bytes("pulled-vid", 300000)
    write(os.path.join(pulled, "com.whatsapp", "databases", "msgstore.db"), msgstore)
    write(os.path.join(pulled, "com.whatsapp", "databases", "msgstore.db-wal"), wal)
    write(os.path.join(pulled, "com.whatsapp", "databases", "msgstore.db.crypt15"),
          b"cifrado, nao e o alvo")
    write(os.path.join(pulled, "com.whatsapp", "files", "key"), key)
    write(os.path.join(pulled, "sdcard", "WhatsApp", "Media", "WhatsApp Images",
                       "IMG-20240101-WA0001.jpg"), image)
    write(os.path.join(pulled, "sdcard", "WhatsApp", "Media", "WhatsApp Video",
                       "VID-20240101-WA0001.mp4"), video)
    write(os.path.join(pulled, "sdcard", "WhatsApp", "Media", ".nomedia"), b"")
    pulled_manifest = {
        "msgstore": hashlib.sha256(msgstore).hexdigest(),
        "wal": hashlib.sha256(wal).hexdigest(),
        "key": hashlib.sha256(key).hexdigest(),
        "image": hashlib.sha256(image).hexdigest(),
        "video": hashlib.sha256(video).hexdigest(),
    }
    write_text(os.path.join(FIXTURES, "pulled.expect"),
               "\n".join("%s=%s" % item for item in pulled_manifest.items()) + "\n")

    print("fixtures em: " + FIXTURES)
    print("variantes .ab: %d" % len(cases))
    print("amostras adb: %d" % len(adb_samples))
    print("arvore generica: %d arquivos" % sum(1 for e in GENERIC_TREE if e[0] == "file"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
