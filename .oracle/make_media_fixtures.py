import sqlite3, os

D = r'C:\Users\pedri\Downloads\Eye-of-God-main\.oracle'
keys = []
for line in open(os.path.join(D, 'media_keys.txt'), encoding='utf-8'):
    line = line.strip()
    if not line: continue
    name, hx = line.split()
    keys.append((name, bytes.fromhex(hx)))

# Two plaintext images with distinct, verifiable payloads.
payloads = {}
for i, (name, _) in enumerate(keys, start=1):
    body = b'\xff\xd8\xff\xe0' + b'\x00\x10JFIF\x00' + bytes([0x41 + i]) * 2000 + ('IMAGE-%d' % i).encode()
    if len(body) % 16: body += b'\x00' * (16 - len(body) % 16)
    p = os.path.join(D, 'orig%d.jpg' % i)
    open(p, 'wb').write(body)
    payloads[name] = len(body)

db = os.path.join(D, 'mediadb.db')
if os.path.exists(db): os.remove(db)
c = sqlite3.connect(db)
c.execute('CREATE TABLE message_media (message_row_id INTEGER PRIMARY KEY, chat_row_id INTEGER,'
          ' file_path TEXT, media_key BLOB, media_name TEXT, mime_type TEXT, file_size INTEGER,'
          ' view_once INTEGER DEFAULT 0)')
# Tabela dedicada a midia efemera, como nas versoes que separam esse estado.
c.execute('CREATE TABLE message_view_once (message_row_id INTEGER PRIMARY KEY,'
          ' message_media_row_id INTEGER, played INTEGER)')
c.execute('CREATE TABLE messages (_id INTEGER PRIMARY KEY, data TEXT)')
c.execute("INSERT INTO messages(data) VALUES('mensagem sem midia')")
for i, (name, key) in enumerate(keys, start=1):
    # A primeira midia e "ver uma vez": o aplicativo a apaga depois da leitura.
    once = 1 if i == 1 else 0
    c.execute('INSERT INTO message_media(chat_row_id,file_path,media_key,media_name,mime_type,'
              'file_size,view_once) VALUES(?,?,?,?,?,?,?)',
              (100 + i, 'Media/WhatsApp Images/' + name, key, name, 'image/jpeg', payloads[name], once))
    if once:
        c.execute('INSERT INTO message_view_once(message_media_row_id,played) VALUES(?,1)', (i,))
c.commit(); c.close()
print('fixtures ok:', ', '.join('%s=%dB' % (n, s) for n, s in payloads.items()))
