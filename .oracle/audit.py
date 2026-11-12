import json, os, hashlib, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

D = r'C:\Users\pedri\Downloads\Eye-of-God-main\.oracle'
R = os.path.join(D, 'recovered')

print('--- arquivos gerados ---')
for root, _, files in os.walk(R):
    for f in sorted(files):
        p = os.path.join(root, f)
        print('  %-36s %7d bytes' % (os.path.relpath(p, R), os.path.getsize(p)))

print('--- midia: identica ao original? ---')
orig = {}
for name in ('orig1.jpg', 'orig2.jpg'):
    orig[name] = hashlib.sha256(open(os.path.join(D, name), 'rb').read()).hexdigest()
media_dir = os.path.join(R, 'media')
if os.path.isdir(media_dir):
    for m in sorted(os.listdir(media_dir)):
        h = hashlib.sha256(open(os.path.join(media_dir, m), 'rb').read()).hexdigest()
        match = [k for k, v in orig.items() if v == h]
        print('  %-28s %s' % (m, ('IDENTICA a ' + match[0]) if match else 'DIFERENTE'))

print('--- relatorio ---')
rep = json.load(open(os.path.join(R, 'report.json'), encoding='utf-8'))
for k in ('cipher', 'paginas', 'wal_frames', 'midia_ok', 'midia_falha',
          'apagadas_confirmadas', 'paginas_freelist'):
    print('  %-22s %s' % (k, rep.get(k)))

victims = set(int(x) for x in open(os.path.join(D, 'victims_full.txt')).read().split())
rec = set(r['rowid'] for r in rep['registros'] if r['rowid'] in victims)
ghosts = [r for r in rep['registros'] if r['rowid'] not in victims]
print('--- recuperacao ---')
print('  apagadas recuperadas: %d de %d (%.1f%%)' % (len(rec), len(victims), 100.0 * len(rec) / len(victims)))
print('  linhas que NAO eram vitimas: %d' % len(ghosts))
if rep['registros']:
    s = rep['registros'][0]
    print('  amostra: rowid=%s origem=%s tabela=%s' % (s['rowid'], s['origem'], s['tabela']))
    print('           texto=%s' % (s['textos'][:2],))
