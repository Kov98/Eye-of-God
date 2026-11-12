import subprocess, sys, re, os
os.chdir(r'C:\Users\pedri\Downloads\Eye-of-God-main')
out = subprocess.run([sys.executable, r'ferramentas\pdfcheck\pdfcheck.py', r'.oracle\laudo_acentos.pdf'],
                     capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
# words that must survive as accents
checks = {'acao':'ação','coracao':'coração','pericia':'perícia','Jose':'José','Sao Paulo':'São Paulo',
          'conclusao':'conclusão','endereco':'endereço','unico':'único','acentuacao':'acentuação'}
found = {}
for k, v in checks.items():
    found[v] = (v in out)
print('--- acentos preservados ---')
for v, ok in found.items():
    print('  %-14s %s' % (v, 'OK' if ok else 'AUSENTE'))
q = out.count('?')
print('interrogacoes no texto extraido:', q)
print('todos presentes:', all(found.values()))
