# Barra de comandos — medições de pixel (2026-05-11)

Referência: `ref62.png` = faixa 1919x62 do chrome do Visual Studio 2026 (linhas 0..61:
menu 0..29, contorno 30, preenchimento 31..58, contorno 59).
Captura nossa: `olho-chrome-v19.png` (1920x1032, PrintWindow).

## 1. Borda esquerda do bloco (grip)

Linha y=45, valores de cinza em x=0..13:

| x | 0..5 | 6 | 7 | 8 | 9 | 10 | 11.. |
|---|---|---|---|---|---|---|---|
| VS | 28 (chrome) | **14** | **19** | **19** | **19** | 38 | 38 |
| Olho (antes) | 28 | 14 | *28* | 19 | 19 | *28* | 38 |
| Olho (agora) | 28 | 14 | 19 | 19 | 19 | 38 | 38 |

Antes o grip era desenhado em `block.left() + 1` (x=7.5) com 3.0 de largura; com
antialiasing isso dava **meio pixel** de chrome em x=7 e em x=10 (0x1C nos dois) e só
**2px cheios** de grip. O VS tem contorno em x=6, grip inteiro em x=7..9 e
preenchimento a partir de x=10. Correção: `block.left() + 0.5` (x=7.0) → perfil
idêntico ao VS, byte a byte na linha 45.

## 2. Separador de grupo

Varredura de colunas monocromáticas em #383838 (56), linhas 37..52:

- VS: **82, 223, 312, 408, 618, 1207, 1316, 1373, 1430, 1513, 1648**
- Olho antes: 134, 311, 439 (só nas fronteiras de categoria → a primeira só caía em x=134)
- Olho agora: **62, 143, 224, 305, 338, 466, 741, 822, 855, 918, 975**

O separador é 1px x **16px** em **#383838**, centrado nos 28px de preenchimento
(linhas 37..52), com 4px de respiro de cada lado. Implementação:
`CommandBar::add_separator()`. O ritmo do VS (primeiro separador depois de 2 botões)
agora vem de marcadores explícitos na ordem — `"|"` = separador de grupo,
`"||"` = quebra de segmento (9px: contorno 1px + 4px chrome + contorno 1px + 3px grip,
medida no VS em x=462..470).

## 3. Canto direito (chevron de opções)

Antes: dois QToolButton com glifo de texto. `⚑` (U+2691) caía na fonte de emoji
colorida do Windows (bandeirinha verde/branca de 12px) e `▾` (U+25BE) saía pequeno
demais; além disso o Qt desenhava o *menu-indicator* (setinha no canto inferior
direito) por cima, virando um rabisco.

Agora: um único botão 22x24 com ícone **desenhado em QPixmap** (16px lógicos, DPR 2,
traço 1.4 com ponta redonda) e `QToolButton::menu-indicator{image:none;width:0;height:0}`.
As ações do pin removido (ocultar/redefinir barra) já vivem no menu de opções.

## 3.1 Passo (pitch) dos ícones — corrige a estimativa de 32px

Centros dos glifos achados por varredura de colunas (linhas 33..57), agrupados com
tolerância de 2px:

- **VS**: 24.5, 44.5, 66.5 | 97, 117.5, 138, 159.5, 181.5 | 207.5, 239, 258.5, 280, 300.5 | 329 ...
  → passo **dentro do grupo: 20–22px**; salto entre grupos (com o separador): **26–31px**
- **Olho**: 21.5, 45.5 | 79, 102.5, 126.5 | 159.5, 183.5, 207.5 ...
  → passo **dentro do grupo: 23.5–24px**; salto entre grupos: **33px**

Ou seja: o VS é **mais apertado** que o nosso em ~2px por botão e ~3px por grupo.
A estimativa anterior de "32px no VS" estava errada — ela vinha de supor 2 botões
antes do separador em x=82, mas são **3** (centros 24.5 / 44.5 / 66.5). Alargar o
botão para 30 (passo 32) afastaria do VS, não aproximaria.

## 3.2 Barra segmentada (uma toolbar por classe)

O VS nao desenha uma faixa unica: sao varias toolbars encostadas, cada uma com o
seu bloco arredondado e o seu grip. Medido em x=462..470 (linha 45):

| x | 455..461 | 462 | 463..466 | 467 | 468..470 | 471.. |
|---|---|---|---|---|---|---|
| VS | 38 fill | **14** contorno | **28** chrome (4px) | **14** contorno | **19** grip (3px) | 38 fill |
| Olho | 134 | 135 | 136..139 | 140 | 141..143 | 144 |
| Olho barra 2 | — | 111 | 112..115 | 116 | 117..119 | 120 |

Assinatura **idêntica** à do VS: contorno 1px + 4px de chrome + contorno 1px + grip 3px.
As curvas internas tambem batem (rampa de 2-3px nas linhas 65..68 / 89..92 da barra 2).

Emendas medidas: barra 1 em **136, 322, 459, 743, 959, 1025** (7 blocos);
barra 2 em **111, 297, 363, 405, 593** (6 blocos). Cada emenda ocupa 18px
(5px de respiro + 9px de emenda + 4px de respiro), e esses 5px/4px caem DENTRO
dos blocos (padding), nao no chrome.

Regra de agrupamento: `CommandBar::segment_of(id, category)` em `commandbar.hpp`.
Trocar a divisao = trocar essa funcao (nenhum outro ponto do codigo depende dela).

### Dois bugs achados na implementacao

1. **Geometria velha no paint.** O `paintEvent` deriva os limites dos blocos da
   geometria dos spacers. Sem ativar o layout, o paint rodava antes dos filhos
   ganharem posicao: das 6 emendas da barra 1, so 2 apareciam (medido). Correcao:
   `row_->activate(); update();` no fim do `rebuild()` + repaint em
   `QEvent::LayoutRequest`.
2. **Tarja de chrome de 1px.** O spacer era um `QWidget` puro e a regra global
   `QWidget { background: #1C1C1C; }` do QSS pintava uma linha de 1px na altura do
   meio de cada emenda (linha y=44 inteira em 0x1C). Correcao: `SegmentSpacer` com
   `paintEvent` vazio.

## 3.3 Ligar/desligar cada barra (modelo VS)

`CommandBar` guarda `hidden_segments_` e persiste em `toolbar.ini` como
`hiddenBars=<csv>` (dentro do grupo do layout, ou seja: por linha de barra).
O `rebuild()` pula os itens da classe desligada **antes** de decidir emenda,
entao nao sobra buraco nem emenda orfa.

Duas portas de entrada:
- botao direito na barra → "Barras nesta linha" (check por classe) + "Personalizar..."
  + "Ocultar esta barra";
- menu **Exibir > Barras de Ferramentas** → uma entrada por classe.

O bloco "Engine" (1 comando, "Rodar engine") foi fundido em **Ferramentas**:
bloco de um comando so nao existe no VS.

Emendas medidas com a mudanca:

| estado | barra 1 | blocos |
|---|---|---|
| padrao | 136, 441, 725, 941, 1007 | 6 |
| `hiddenBars=Ferramentas` | 136, 420, 636, 702 | 5 |

(barra 2 inalterada nos dois estados: a visibilidade e por linha, nao global)

## 3.4 Arrastar o bloco pelo grip

O "pega" e o grip preto de 3px na borda esquerda de cada bloco, igual ao VS:

- `mousePressEvent` → `segment_at_grip(x)` acerta a faixa `[left-0.5, left+5)`;
- `mouseMoveEvent` → a partir de 3px de deslocamento vira arrasto
  (`dragging_`), cursor `ClosedHandCursor`, e `gap_at(x)` calcula o vao de
  insercao comparando x com o meio de cada bloco;
- durante o arrasto o **grip do bloco arrastado fica roxo (#9184EE)** e um
  **caret roxo de 2px** marca onde ele cai (medido: grip em x=446..448 e caret
  em x=942..943 no teste);
- `mouseReleaseEvent` → `move_segment(from, to)` remonta `order_` agrupando
  por classe (ordem interna de cada classe preservada), `save()` + `rebuild()`.

Hooks de auto-teste (sem mouse): `OLHO_REORDER="from,to"` executa o reorder no
startup e `OLHO_DRAG="from,gap"` congela o visual do arrasto.

Faixa de pega do grip: `[left-1, left+8)` (9px). Antes eram 5px e o primeiro
botao comecava em x=10, ou seja so 4px agarraveis — ninguem acertava. Margem
esquerda da linha foi de 10 para 15px, o que tambem acertou a densidade do VS:
**primeiro glifo em x=18 no VS, x=19 aqui** (era 14). Emendas viraram
`141, 446, 730, 946, 1012` (mesmos 6 blocos, +5px de deslocamento).

Gesto sintetico (press → move → release enviados por `QCoreApplication::sendEvent`)
reordena de fato: bloco 0 → vao 3 fez `order` comecar em `tool.sanitize`.

Verificado: bloco 0 → indice 3 fez `order` comecar em `tool.sanitize`
(Ferramentas), e o reorder de volta restaurou `tool.acquisition` na frente;
emendas depois do ida-e-volta: `136, 441, 725, 941, 1007` (identicas ao estado
original). **Nao verificado:** o gesto fisico com mouse real (esta sessao nao tem
automacao de mouse) e arrastar um bloco de uma linha para a outra.

## 4. Faixas (sem regressão) — x=900

```
30  #0E0E0E     contorno superior
31..58 #262626  preenchimento da barra 1
59  #0E0E0E     contorno inferior
60..63          chrome
64  #0E0E0E     divisor
65..92          barra 2
93  #0E0E0E     divisor
94..97          4px band
98  #0E0E0E     divisor
99  #9184EE     linha de acento
```
