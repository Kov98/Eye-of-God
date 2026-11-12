import os, sqlite3
p = '.oracle/wal_out.db'
print('exists', os.path.exists(p), 'size', os.path.getsize(p) if os.path.exists(p) else 0)
with open(p, 'rb') as f:
    head = f.read(16)
print('header_hex', head.hex())
try:
    conn = sqlite3.connect(p)
    rows = conn.execute('SELECT COUNT(*) FROM msg').fetchone()
    print('rows', rows)
    conn.close()
except Exception as exc:
    print('erro', exc)
