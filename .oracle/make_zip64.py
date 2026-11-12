import zipfile, os

# A) muitos arquivos: forca o ZIP64 no diretorio central (contagem > 65534)
count_path = os.path.join('.oracle', 'zip64_count.zip')
if os.path.exists(count_path):
    os.remove(count_path)
with zipfile.ZipFile(count_path, 'w', zipfile.ZIP_STORED, allowZip64=True) as z:
    for i in range(70000):
        z.writestr('e%05d.txt' % i, 'x' * (i % 7))

# B) uma entrada com force_zip64: forca o campo extra 0x0001 na entrada
extra_path = os.path.join('.oracle', 'zip64_extra.zip')
if os.path.exists(extra_path):
    os.remove(extra_path)
payload = (b'linha de teste zip64 ' * 500)
with zipfile.ZipFile(extra_path, 'w', zipfile.ZIP_DEFLATED, allowZip64=True) as z:
    with z.open('big.txt', 'w', force_zip64=True) as f:
        f.write(payload)
    z.writestr('small.txt', 'abc')

with open(os.path.join('.oracle', 'zip64_payload.bin'), 'wb') as f:
    f.write(payload)

print('count_entries', zipfile.ZipFile(count_path).namelist().__len__())
print('count_size', os.path.getsize(count_path))
print('extra_size', os.path.getsize(extra_path))
print('payload_size', len(payload))
