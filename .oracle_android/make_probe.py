import zlib, hashlib, sys
data = (b"the quick brown fox jumps over the lazy dog. " * 400) + bytes(range(256)) * 40
raw_tar_like = data
z = zlib.compress(raw_tar_like, 9)
open(r".oracle_android\out\probe.zlib", "wb").write(z)
open(r".oracle_android\out\probe.expected", "wb").write(raw_tar_like)
print("in", len(raw_tar_like), "deflated", len(z), "sha256", hashlib.sha256(raw_tar_like).hexdigest()[:16])
