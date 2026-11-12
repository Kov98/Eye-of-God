# Plano de integração — tema VS 2026 no Olho de Deus

Status: proposta (aguardando 4 decisões). Nada foi alterado neste plano.

## Objetivo

Substituir o tema atual (duas paletas append-only no QSS + 30 cores hardcoded em C++) por um
tema **gerado** a partir dos 867 tokens do VS 2026, com uma única fonte de verdade e verificação
por build + medição de pixel.

## Arquitetura proposta (gerador offline)

```
tools/theme/
  mapping.json        entrada curada: seletor/paper Qt -> token VS + propriedade + escala
  gen_theme.py        gerador (~250 linhas, sem dependencia externa)
  (saidas)
resources/forensic.qss     QSS gerado: 1 paleta, px, sem camadas
theme_vs2026.hpp           header C++ gerado: constexpr + helpers QColor
resources/theme.resolved.json  mapa resolvido (auditoria / diff entre versoes)
```

`gen_theme.py` le `docs/vs-re/theme/tokens-dark-flat.json` (867 tokens) + `mapping.json`, e falha
alto quando um token pedido nao existe (nada de cor silenciosamente errada).

## Fases

### Fase 0 — contrato de mapeamento (a unica parte que exige julgamento)
Escrever `mapping.json` cobrindo: 41 objectNames do app + classes Qt usadas
(QTabBar, QTreeView, QTableView, QHeaderView, QToolBar, QStatusBar, QMenu, QMenuBar, QLineEdit,
QComboBox, QPushButton, QCheckBox, QScrollBar, QSplitter, QToolTip, QDockWidget) x estados
(default/hover/pressed/selected/disabled/focus).
Referencia pronta: `docs/vs-re/catalog/TOKEN_USAGE.md` (mostra qual token o VS usa em cada estado).
Entrega: ~120 entradas, cada uma com `token`, `propriedade` e, quando for dimensionamento, `escala`.

### Fase 1 — QSS gerado
Gerar e substituir `resources/forensic.qss`.
**Obrigatorio preservar** os valores que hoje fazem dimensionamento:
- `QTabBar::tab` min-width/padding, `QToolButton#tabClose` 14px
- `QTreeView#workTree::item` height 22px
- `QCheckBox::indicator` 15x15, `QComboBox::drop-down` 22px
- `QScrollBar` espessura 8px, `QSplitter::handle` 1px
- `QToolButton#titleMin/Max/Close` 30x44 (nao-client do VS)
Aceite: comparacao automatica de conjuntos de seletores antes/depois = zero seletor perdido.

### Fase 2 — C++ sem hex
Criar `theme_vs2026.hpp`; trocar os **26** `QColor("#...")` e os **4** `setStyleSheet` inline.
Pontos: `ContourTabBar`, `DocumentFrame`, `DocumentTopLine`, `ToolTile`, `TimelineView`, `HexView`.
Cores semanticas de dominio (severidade do console: #4CAF50/#F44336/#B0B0B0) nao entram no tema:
vao para `theme::semantic::*` e ficam explicitas.
Aceite: `grep 'QColor("#' mainwindow.cpp` = 0.

### Fase 3 — tipografia e densidade
- base 12px (hoje `setFont(QFont("Segoe UI", 10))` em pontos, aplicado depois do QSS)
- mono 14px (Cascadia Mono) no console e no HexView
- alturas de faixa do VS: title 30, command bar 29+6+25, tab strip 22+16, status 24
- icones 16/24 conforme `Sizes`

### Fase 4 — estados e foco
- hover: `ControlFillSecondary` (botoes) e `SubtleFillSecondary` (linhas/abas) - dois valores
- pressed: `ControlFillTertiary`; selecao + borda `ControlStrokeSecondary`
- foco: 2px, raio 6, offset -4. Qt nao suporta offset negativo em QSS: implementar no
  `paintEvent` do widget custom ou aceitar sem offset (decisao registrada).

### Fase 5 — build e verificacao
`build_deep.ps1` -> rodar -> capturar janela -> medir pixel -> comparar com a tabela do VS.
Checklist: 3 niveis de fundo distintos, zero borda em painel, tudo em px, estados definidos,
scrollbar 8px sem setas, indicador de aba 3px, tabela alternando #1F1F1F/#232323.

### Fase 6 — distribuicao
Gerar tambem `dist_full/resources/forensic.qss` na mesma pipeline (hoje tem versao antiga de 9,9 KB).
`style_sheet()` devolve `{}` quando o arquivo falta: propor embutir QSS compilado ou falhar visivelmente.

## Riscos e mitigacao

| risco | mitigacao |
|---|---|
| QSS carrega dimensionamento; remover quebra layout | coluna `escala` obrigatoria no mapping + comparacao de seletores |
| widgets custom pintam por cima do QSS | header unico; zero hex solto |
| `mainwindow.cpp` esta em edicao pelo usuario (4.259 -> 4.093 linhas) | nao editar em paralelo; coordenar janela de trabalho |
| `dist_full` divergente | mesma pipeline gera as duas copias |
| pt vs px / DPI | migrar para px e validar em 100% e 125% |

## Criterio de pronto

1. QSS gerado, 1 paleta, zero seletor perdido.
2. `grep` de hex em `mainwindow.cpp` = 0 (exceto `theme::semantic`).
3. Build passa e o app roda.
4. Screenshot com os 3 niveis de fundo + medicao de pixel batendo com o VS.
5. `dist_full` sincronizado.
