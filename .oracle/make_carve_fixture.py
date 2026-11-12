import sqlite3, os

D = r'C:\Users\pedri\Downloads\Eye-of-God-main\.oracle'
db = os.path.join(D, 'carvedb.db')
if os.path.exists(db): os.remove(db)

c = sqlite3.connect(db)
c.execute('PRAGMA page_size=4096')
c.execute('CREATE TABLE message(_id INTEGER PRIMARY KEY, key_remote_jid TEXT, data TEXT, timestamp INTEGER)')
rows = []
for i in range(1, 401):
    rows.append((i, '5511999%04d@s.whatsapp.net' % i,
                 'APAGADA-MARCADOR-%d mensagem que foi removida do banco' % i, 1700000000 + i))
c.executemany('INSERT INTO message(_id,key_remote_jid,data,timestamp) VALUES(?,?,?,?)', rows)
c.commit()

# Delete a contiguous block so whole pages retire, plus a scattered subset so
# freeblocks inside live pages get exercised too.
victims = list(range(50, 201)) + [250, 260, 270, 280, 290, 300]
c.executemany('DELETE FROM message WHERE _id=?', [(v,) for v in victims])
c.commit()
live = c.execute('SELECT COUNT(*) FROM message').fetchone()[0]
c.close()

open(os.path.join(D, 'victims.txt'), 'w').write('\n'.join(str(v) for v in victims))
print('db=%d bytes, live=%d, deletadas=%d' % (os.path.getsize(db), live, len(victims)))
