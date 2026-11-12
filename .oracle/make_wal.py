import os, shutil, sqlite3

base = '.oracle'
src = os.path.join(base, 'wal_src.db')
for suffix in ('', '-wal', '-shm'):
    p = src + suffix
    if os.path.exists(p):
        os.remove(p)

conn = sqlite3.connect(src)
conn.execute('PRAGMA journal_mode=WAL')
conn.execute('PRAGMA wal_autocheckpoint=0')
conn.execute('CREATE TABLE msg (id INTEGER PRIMARY KEY, texto TEXT)')
for i in range(1, 21):
    conn.execute('INSERT INTO msg (id, texto) VALUES (?, ?)', (i, 'mensagem %d' % i))
conn.commit()
conn.execute('DELETE FROM msg WHERE id = 7')
conn.execute('UPDATE msg SET texto = ? WHERE id = 3', ('mensagem 3 editada',))
conn.commit()

# Copia o par db+wal COM a conexao aberta: fechar faria checkpoint.
db_copy = os.path.join(base, 'wal_db.db')
wal_copy = db_copy + '-wal'
for p in (db_copy, wal_copy, db_copy + '-shm'):
    if os.path.exists(p):
        os.remove(p)
shutil.copyfile(src, db_copy)
shutil.copyfile(src + '-wal', wal_copy)
size_db = os.path.getsize(db_copy)
size_wal = os.path.getsize(wal_copy)
conn.close()

# Limpa apenas o par de origem; o par copiado e o fixture do teste.
for p in (src, src + '-wal', src + '-shm'):
    if os.path.exists(p):
        os.remove(p)

# Lista de frames para o teste comparar com a nossa varredura.
import json, struct
with open(wal_copy, 'rb') as f:
    wal = f.read()
magic, version, page_size, ckpt, salt1, salt2, c1, c2 = struct.unpack('>8I', wal[:32])
frames = []
off = 32
while off + 24 + page_size <= len(wal):
    pgno, db_size = struct.unpack('>2I', wal[off:off+8])
    frames.append({'pgno': pgno, 'db_size': db_size})
    off += 24 + page_size
last_commit = 0
for i, fr in enumerate(frames):
    if fr['db_size'] != 0:
        last_commit = i + 1
for i, fr in enumerate(frames):
    fr['after_last_commit'] = i >= last_commit
json.dump(frames, open(os.path.join(base, 'wal_frames.json'), 'w'), indent=1)

print('db_bytes', size_db)
print('wal_bytes', size_wal)
