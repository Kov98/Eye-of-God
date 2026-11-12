import os, random, shutil, time

D = r'C:\Users\pedri\Downloads\Eye-of-God-main\.oracle\caso_grande'
if os.path.exists(D):
    shutil.rmtree(D)
os.makedirs(D)

random.seed(42)
t0 = time.time()
count = 0
NAMES = ['relatorio', 'anexo', 'captura', 'log', 'evidencia', 'foto']
EXTS = ['.txt', '.json', '.log', '.csv', '.jpg', '.bin']

for d in range(100):
    sub = os.path.join(D, 'dir_%03d' % d)
    os.makedirs(sub, exist_ok=True)
    for i in range(100):
        name = '%s_%03d%s' % (NAMES[i % len(NAMES)], i, EXTS[(d + i) % len(EXTS)])
        size = random.randint(200, 2000)
        body = bytes(random.getrandbits(8) for _ in range(size))
        # jpg recebe uma assinatura real para o verificador de tipo aceitar
        if name.endswith('.jpg'):
            body = b'' + bytes([0xFF, 0xD8, 0xFF, 0xE0]) + body[4:]
        if name.endswith(('.txt', '.json', '.log', '.csv')):
            body = (b'linha de evidencia ' * (size // 19 + 1))[:size]
        with open(os.path.join(sub, name), 'wb') as f:
            f.write(body)
        count += 1

# um arquivo plantado no meio, para o verificador de tipo achar na escala
with open(os.path.join(D, 'dir_050', 'documento_plantado.pdf'), 'wb') as f:
    f.write(b'%PDF-1.7\n' + bytes(range(1, 200)) + b'\n%%EOF\n')
count += 1

total = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(D) for f in fs)
print('arquivos=%d  bytes=%d  geracao=%.1fs' % (count, total, time.time() - t0))
