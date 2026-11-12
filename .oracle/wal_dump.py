import struct

with open('.oracle/wal_db.db-wal', 'rb') as f:
    wal = f.read()
magic, version, page_size, ckpt_seq, salt1, salt2, ck1, ck2 = struct.unpack('>8I', wal[:32])
print('magic', hex(magic), 'page_size', page_size, 'salt', salt1, salt2)
frame = 32
index = 0
while frame + 24 + page_size <= len(wal):
    pgno, db_size, f1, f2, c1, c2 = struct.unpack('>6I', wal[frame:frame+24])
    index += 1
    print('frame', index, 'pgno', pgno, 'db_size', db_size, 'salt_ok', (f1 == salt1 and f2 == salt2))
    frame += 24 + page_size
print('frames', index, 'trailing_bytes', len(wal) - frame)
