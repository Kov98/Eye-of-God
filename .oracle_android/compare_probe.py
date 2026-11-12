import hashlib
import sys

expected = open(r".oracle_android/out/probe.expected", "rb").read()
import zlib
print("adler32 esperado: %d" % zlib.adler32(expected))
print("sha256 esperado: %s" % hashlib.sha256(expected).hexdigest())
for name in (r".oracle_android/out/probe.got.direct", r".oracle_android/out/probe.got",
             r".oracle_android/out/probe.got2"):
    try:
        got = open(name, "rb").read()
    except FileNotFoundError:
        print(name, "ausente")
        continue
    same = got == expected
    print("%-40s %6d bytes  sha=%s  igual=%s" % (name, len(got), hashlib.sha256(got).hexdigest()[:16], same))
    if not same:
        for i in range(min(len(got), len(expected))):
            if got[i] != expected[i]:
                print("  primeiro byte divergente: offset %d (0x%02x != 0x%02x)" % (i, got[i], expected[i]))
                print("  esperado:", expected[max(0, i - 8):i + 24].hex())
                print("  obtido:  ", got[max(0, i - 8):i + 24].hex())
                break
