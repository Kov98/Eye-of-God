# Handoff — implementação do tema VS 2026 no Olho de Deus (Qt6)

> Sessão de origem: engenharia reversa do UI do VS (concluída).
> Esta sessão NÃO implementa. Tudo abaixo é o que a próxima precisa para executar sem re-derivar nada.

## 0. Decisões já tomadas (não reabrir)

1. **Qt6 nativo** (Opção 2). Motivo registrado: motor já em C++, dev solo, prazo de v1.0, e fidelidade vem do dado (867 tokens), não do framework.
2. **Gerador offline**: script lê tokens e emite QSS + header C++. Sem loader JSON em runtime.
3. **Converter** o QSS atual em template (não reescrever do zero).
4. **Escopo da 1ª rodada: só tema** (cor, tipografia, densidade, estados). Chrome estrutural (title bar 30px, botões 44px) fica para rodada própria.
5. Tema claro/azul fora de escopo.

## 1. Acervo de dados (já extraído, verificado)

| arquivo | conteúdo | uso |
|---|---|---|
| `docs/vs-re/theme/tokens-dark-flat.json` | **867 tokens** `{nome: AARRGGBB}` (Fluent-Shell 94 + Environment 773) | fonte das cores |
| `docs/vs-re/theme/tokens-dark-by-category.json` | 84 categorias / 3.100 tokens (inclui **Text Editor**, onde vive a cor do editor) | cores por área |
| `docs/vs-re/theme/tokens-shell-dark.json` | 236 tokens do `Shell.Dark.pkgdef` com alpha e cor composta | conferência |
| `docs/vs-re/theme/palettes-all-themes-18.json` | 18 temas (se um dia quiser claro) | fora de escopo |
| `docs/vs-re/catalog/TOKEN_USAGE.md` | **token -> controle -> estado** (394 tokens, 172 controles) | montar o template |
| `docs/vs-re/styles/` (24 XAML) | código real de tab/button/scrollbar/tree/menu/toolbar/status | referência de anatomia |
| `docs/vs-re/README.md` | relatório completo, 14 seções, inclui os erros que cometi | contexto |
| `C:/Users/pedri/Downloads/vs-ui-corpus/` | 7.671 XAML do produto | consulta |
| `C:/Users/pedri/Downloads/vs-ui-src/` | 295 mil `.cs` + 4,1 mil `.xaml` decompilados | consulta de implementação |

**Não redistribuir binários do VS.** O acervo é conhecimento (medidas, paleta, código de referência), não licença.

## 2. Mapa papel -> token (derivado por inversão, não por palpite)

A inversão funcionou assim: para cada token, compor sobre a base `#1C1C1C` e comparar com a cor medida na janela real do VS.

| cor medida | token | valor |
|---|---|---|
| `#1C1C1C` chrome | `EnvironmentBackground` | literal |
| `#1F1F1F` output | `Background` | literal |
| `#262626` command bar / status | `ControlAltFillTertiary` | branco alpha 11/255 |
| `#282828` painéis / docks | `CardBackgroundFillDefault` | branco alpha 13/255 |
| `#232323` linha alternada | `ControlFillTertiary` | branco alpha 8/255 |
| `#333333` hover de command bar | `CommandBarHover` | literal |
| `#454545` borda de seleção | `ControlSolidFillDefault` | literal |
| `#9184EE` accent | `AccentFillDefault` | literal |

**Fórmula de composição** (para todo token com alpha < 255):

`
canal_final = round(28 + (alpha/255) * (canal_token - 28))     base = 0x1C = 28
`

### Cores que NÃO têm token (são composição) — não procure, componha

`#272727`, `#2B2B2B`, `#303030`, `#353535`, `#3A3A3A` não existem em nenhuma das 84 categorias.
São branco translúcido sobre a base. O gerador deve compor e **validar contra a tabela de ouro**.

### Tabela de ouro (o teste de regressão do tema)

`
#1C1C1C chrome    #1E1E1E editor     #1F1F1F output    #232323 grade alt
#262626 command   #272727 tab baixo  #282828 painel    #2B2B2B tab cima
#303030 hover     #333333 hover bar  #353535 seleção   #3A3A3A hover 3
#454545 borda     #9184EE accent     #0E0E0E divisor   #141414 sombra
`

O gerador **falha** se um papel composto não bater exatamente. Esse é o gate.

## 3. Anatomia e geometria (do código do VS)

| item | valor |
|---|---|
| base tipográfica | Segoe UI **12px** (não pt); mono **Cascadia Mono 14px** |
| rampa | caption 12, body 14, bodyLarge 18, subtitle 20, title 28, display 68 |
| ControlMin | 24x24 · ContentMin 16x16 |
| raio | 0/2/4/6/8 (`M` = 4 é o padrão de controle) |
| stroke | 1 (Thin) / 2 (Thick) / 3 / 4 |
| ícones | 8/12/16/24/32 (16 é o padrão de toolbar) |
| non-client | 30 de altura x 44 de largura |
| faixas medidas | title 30 · command bar 29 + gap 6 + 25 · tab strip 22+16 · status 24 · scrollbar 8 · splitter 1 |
| anel de foco | 2px, raio 6, offset -4 (Qt não faz offset negativo em QSS: fazer no paintEvent ou aceitar sem offset) |
| menu/flyout | raio 8 externo / 6 interno, borda 1px, padding = ShadowMargin |
| indicador de aba ativa | 3px (Indicator.ControlWidth), barra vertical na lateral |

## 4. Fatos do app que quebram implementação ingênua

1. **O QSS carrega dimensionamento, não só cor.** Preservar obrigatoriamente:
   - `QTabBar::tab` min-width (74 e 88 no arquivo, camadas diferentes) e paddings
   - `QToolButton#tabClose` 14x14 (alvo de clique)
   - `QTreeView#workTree::item` height 22px
   - `QCheckBox::indicator` 15x15
   - `QComboBox::drop-down` width 22px
   - `QScrollBar` espessura 8px (já ajustada)
   - `QSplitter::handle` 1px
2. **QSS é append-only e tem 2 paletas** (a roxa antiga e a camada VS que apliquei). Mesmo seletor aparece até 7x; vence o último.
3. **Seletores mortos** (não têm produtor de objectName): `QMenuBar#mainMenuBar` (o id real é `titleMenuBar`), `#brandTitle`, `#brandSub`, `#brandMark`, `#menuLogo`, `#titleText`, `#titleControls`, `#compactBrand`, `#compactTitle`, `QFrame#divider`, `QLabel#modeInline` (widget nunca atribuído), `QSpinBox`, `QTextEdit`.
4. **Ordem de inicialização**: `QApplication` -> `setStyle("Fusion")` -> `setStyleSheet(load_style_sheet())` -> `setFont(QFont("Segoe UI", 10))` (**pontos**, aplicado DEPOIS do QSS).
5. **Carregamento**: string lida de `applicationDirPath()/resources/forensic.qss`; fallback `{}` (sem arquivo, o app roda em Fusion puro, sem tema E sem o dimensionamento do item 1).
6. **Cor hardcoded em C++**: **26** `QColor("#...")` + **4** `setStyleSheet` inline, todos em `mainwindow.cpp`, concentrados em: `ContourTabBar`, `DocumentFrame`, `DocumentTopLine`, `ToolTile`, `TimelineView`, `HexView` (+ helper de título de seção).
7. **Cores semânticas de domínio não entram no tema**: severidade do console (`#4CAF50` sucesso, `#F44336` erro, `#B0B0B0` normal) — isolar em `theme::semantic`.
8. **dist_full/resources/forensic.qss está desatualizado** (9,9 KB vs 39 KB atual) — a distribuição roda outro tema. Gerar as duas cópias na mesma pipeline.
9. **Build**: `build_deep.ps1` -> g++ MSYS2 ucrt64 + pkg-config Qt6Widgets -> `OlhoDeDeus_gui.exe` na raiz. **O link falha com "Permission denied" se o exe estiver rodando** — fechar antes.
10. **Label pinta fundo se o QWidget global declarar background** — precisa de `QLabel { background: transparent; }` DEPOIS da regra global (foi um bug real nesta sessão).

## 5. Desenho do gerador

`
tools/theme/
  tokens.json     cópia dos 867 (o app não depende de docs/vs-re)
  roles.json      papel semântico -> token (+ pai de composição)
  golden.json     as 16 cores medidas (o teste)
  template.qss    QSS com {{placeholders}}
  gen_theme.py    compõe, valida contra golden, emite os dois artefatos
        -> resources/forensic.qss
        -> theme_vs2026.hpp      (substitui os 26 QColor)
`

Regras do gerador:
- se um token pedido não existir -> **erro**, nunca fallback silencioso;
- se o composto não bater no golden -> **erro**;
- emitir também `resources/theme.resolved.json` (mapa resolvido, para diff entre versões).

## 6. Sequência com critério de aceite

| # | passo | aceite |
|---|---|---|
| 1 | `roles.json` + `golden.json` | gerador valida 16/16 contra o ouro |
| 2 | `template.qss` (converter o atual) | comparação de conjuntos de seletores: zero perdido |
| 3 | gerar QSS + build | app abre, 3 níveis de fundo visíveis |
| 4 | `theme_vs2026.hpp` + trocar os 26 literais e 4 inline | `grep 'QColor("#' mainwindow.cpp` = 0 (fora de `theme::semantic`) |
| 5 | tipografia 12px + mono 14px + faixas | screenshot lado a lado |
| 6 | estados (hover duplo, pressed, seleção) + foco | medir pixel do app e comparar com a tabela VS |
| 7 | `dist_full` | duas cópias idênticas |

**Verificação de pixel**: existe script pronto nesta sessão (`%TEMP%\vsre\capture.ps1` captura janela; a medição é Python/Pillow amostrando regiões). Reimplementar é trivial: capturar e comparar contra o golden.

## 7. Fora de escopo (registrado)

- **Ícones**: o VS usa monikers monocromáticos recoloridos por estado; o app usa PNG colorido. **Este é o maior ganho visual restante** e não está no plano do tema — merece rodada própria (tint por estado via QIcon/QSvgRenderer).
- Chrome estrutural (title bar 30px, botões 44px).
- Tema claro.
- Comportamento/fluxos.

## 8. Armadilhas de processo

- `mainwindow.cpp` é o arquivo mais disputado: **um escritor por vez**. Nesta sessão ele mudou de 4.259 para 4.093 linhas enquanto eu trabalhava.
- Não rodar build com o app aberto (lock do exe).
- Não editar `resources/forensic.qss` à mão depois de adotar o gerador — o próximo build sobrescreve.
