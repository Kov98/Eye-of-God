import sqlite3, os
D = r'C:\Users\pedri\Downloads\Eye-of-God-main\.oracle'
db = os.path.join(D, 'full.db')
if os.path.exists(db): os.remove(db)

keys = []
for line in open(os.path.join(D, 'media_keys.txt'), encoding='utf-8'):
    line = line.strip()
    if line:
        n, hx = line.split()
        keys.append((n, bytes.fromhex(hx)))

c = sqlite3.connect(db)
c.execute('PRAGMA page_size=4096')
c.execute('CREATE TABLE message_media (message_row_id INTEGER PRIMARY KEY, chat_row_id INTEGER,'
          ' file_path TEXT, media_key BLOB, media_name TEXT, mime_type TEXT, file_size INTEGER)')
c.execute('CREATE TABLE message(_id INTEGER PRIMARY KEY, key_remote_jid TEXT, data TEXT, timestamp INTEGER)')
rows = [(i, '5511999%04d@s.whatsapp.net' % i,
         'APAGADA-MARCADOR-%d conversa removida do aparelho' % i, 1700000000 + i)
        for i in range(1, 401)]
c.executemany('INSERT INTO message(_id,key_remote_jid,data,timestamp) VALUES(?,?,?,?)', rows)
for i, (name, key) in enumerate(keys, start=1):
    c.execute('INSERT INTO message_media(chat_row_id,file_path,media_key,media_name,mime_type,file_size)'
              ' VALUES(?,?,?,?,?,?)', (100+i, 'Media/WhatsApp Images/' + name, key, name, 'image/jpeg', 2032))
c.commit()

victims = list(range(50, 201)) + [250, 260, 270, 280, 290, 300]
c.executemany('DELETE FROM message WHERE _id=?', [(v,) for v in victims])
c.commit(); c.close()
open(os.path.join(D, 'victims_full.txt'), 'w').write('\n'.join(str(v) for v in victims))
print('full.db=%d bytes, apagadas=%d, midias=%d' % (os.path.getsize(db), len(victims), len(keys)))
