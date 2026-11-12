import struct

def pages(path, size):
    with open(path, 'rb') as f:
        data = f.read()
    return [data[i*size:(i+1)*size] for i in range(len(data)//size)]

ps = 4096
base = pages('.oracle/wal_db.db', ps)
with open('.oracle/wal_db.db-wal', 'rb') as f:
    wal = f.read()
frames = []
off = 32
while off + 24 + ps <= len(wal):
    pgno, db_size = struct.unpack('>2I', wal[off:off+8])
    frames.append((pgno, db_size, wal[off+24:off+24+ps]))
    off += 24 + ps

def has(buf, needle):
    return needle.encode() in buf

print('base_pages', len(base), 'base_p1_has_schema', has(base[0], 'CREATE TABLE'))
for i, (pgno, dbsize, data) in enumerate(frames, 1):
    print('frame', i, 'pgno', pgno, 'dbsize', dbsize,
          'schema', has(data, 'CREATE TABLE'), 'msg', has(data, 'mensagem 3 editada'))
print('wal_out size', __import__('os').path.getsize('.oracle/wal_out.db') if __import__('os').path.exists('.oracle/wal_out.db') else 0)
