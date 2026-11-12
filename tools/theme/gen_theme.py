
import json, os, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(ROOT, '..', '..'))

def load(name):
    with open(os.path.join(ROOT, name), encoding='utf-8') as f:
        return json.load(f)

tokens = load('tokens.json')
roles  = load('roles.json')
golden = load('golden.json')
scale  = load('scale.json')

BASE = (0x1C, 0x1C, 0x1C)

def parse_hex(h):
    v = int(h.lstrip('#'), 16)
    if len(h.lstrip('#')) == 8:
        return ((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255)
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255, 255)

def resolve(role, depth=0):
    if depth > 8:
        raise SystemExit('recursao em ' + role)
    r = roles[role]
    if 'literal' in r:
        c = parse_hex(r['literal'])
        return c[:3], 'literal ' + r['literal'] + ' (' + r.get('source', '?') + ')'
    name = r['token']
    if name not in tokens:
        raise SystemExit('TOKEN AUSENTE: ' + name + ' (papel ' + role + ')')
    rgb = parse_hex(tokens[name])
    a = rgb[3]
    if a == 255:
        return rgb[:3], 'token ' + name
    parent = r.get('over')
    base = resolve(parent)[0] if parent else BASE
    comp = tuple(int(round(base[i] + (a / 255.0) * (rgb[i] - base[i]))) for i in range(3))
    return comp, 'token %s alpha=%d sobre %s' % (name, a, parent or 'base #1C1C1C')

resolved = {}
errors = []
for role in roles:
    comp, src = resolve(role)
    hx = '#%02X%02X%02X' % comp
    resolved[role] = {'hex': hx, 'source': src}
    if role in golden and hx.upper() != golden[role].upper():
        errors.append('%s: golden %s != gerado %s   [%s]' % (role, golden[role], hx, src))

if errors:
    print('FALHA NA VALIDACAO CONTRA O GOLDEN:')
    for e in errors:
        print('  ' + e)
    sys.exit(1)

def scale_get(path):
    node = scale
    for part in path.split('.'):
        if part not in node:
            raise SystemExit('escala ausente: ' + path)
        node = node[part]
    return str(node)

tpl_path = os.path.join(ROOT, 'template.qss')
qss = open(tpl_path, encoding='utf-8').read()

for role, info in resolved.items():
    qss = qss.replace('{{' + role + '}}', info['hex'])

import re
for m in sorted(set(re.findall(r'\{\{([a-zA-Z0-9_.]+)\}\}', qss))):
    qss = qss.replace('{{' + m + '}}', scale_get(m))

left = re.findall(r'\{\{[^}]+\}\}', qss)
if left:
    print('placeholders nao resolvidos:', left[:10])
    sys.exit(1)

qss_out = os.path.join(REPO, 'resources', 'forensic.qss')
with open(qss_out, 'w', encoding='utf-8') as f:
    f.write(qss)

def cap(s):
    return s[0].upper() + s[1:]

h = []
h.append('// GERADO por tools/theme/gen_theme.py - NAO EDITAR A MAO.')
h.append('// Cores do Visual Studio 2026 (tema escuro), validadas contra a tabela golden.')
h.append('#pragma once')
h.append('#include <QColor>')
h.append('')
h.append('namespace theme {')
for role, info in resolved.items():
    r, g, b = (int(info['hex'][i:i+2], 16) for i in (1, 3, 5))
    h.append('constexpr const char* k%s = "%s";' % (cap(role), info['hex']))
    h.append('inline QColor %s() { return QColor(%d, %d, %d); }' % (role, r, g, b))
h.append('')
h.append('// Semantico: NAO e tema (severidade do console e alertas de dominio).')
h.append('namespace semantic {')
h.append('constexpr const char* kOk = "#4CAF50";')
h.append('constexpr const char* kError = "#F44336";')
h.append('constexpr const char* kNormal = "#E6E6E6";')
h.append('constexpr const char* kEcho = "#8A8A8A";')
h.append('inline QColor ok() { return QColor(0x4C, 0xAF, 0x50); }')
h.append('inline QColor error() { return QColor(0xF4, 0x43, 0x36); }')
h.append('inline QColor normal() { return QColor(0xE6, 0xE6, 0xE6); }')
h.append('inline QColor echo() { return QColor(0x8A, 0x8A, 0x8A); }')
h.append('}  // namespace semantic')
h.append('}  // namespace theme')
hpp_out = os.path.join(REPO, 'theme_vs2026.hpp')
with open(hpp_out, 'w', encoding='utf-8') as f:
    f.write('\n'.join(h) + '\n')

with open(os.path.join(ROOT, 'theme.resolved.json'), 'w', encoding='utf-8') as f:
    json.dump({'roles': resolved, 'golden_ok': True}, f, indent=1)

print('OK: %d papeis validados contra o golden' % len(golden))
print('QSS ->', qss_out, '(%d linhas)' % len(qss.splitlines()))
print('HPP ->', hpp_out, '(%d linhas)' % len(h))
for role in ('chromeFixed', 'commandBar', 'surface', 'document', 'outputPane', 'hoverRow', 'pressed', 'accent', 'focusOuter'):
    print('  %-14s %s  <- %s' % (role, resolved[role]['hex'], resolved[role]['source']))
