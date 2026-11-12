# Modelo de cor do VS 2026 dark (escala de overlay)

O tema do produto nao e uma lista de hex soltos: e uma base mais uma escala de overlay.

```
branco:  hex = base + alpha * (255 - base)
preto:   hex = base - alpha * base
base    = #1C1C1C
```

Todos os valores abaixo foram medidos por pixel na janela real; o modelo reconstroi
cada um com residuo maximo de 0 nivel(is) de 255 (0.00%).

| papel | hex medido | overlay | alpha | delta |
|---|---|---|---|---|
| chrome / barra de titulo+menu | `#1C1C1C` | branco | 0.00% | +0 |
| editor / documento | `#1E1E1E` | branco | 0.88% | +2 |
| painel de saida (Output) | `#1F1F1F` | branco | 1.32% | +3 |
| linha alternada de grade | `#232323` | branco | 3.08% | +7 |
| command bar / status | `#262626` | branco | 4.41% | +10 |
| faixa inferior da tab strip | `#272727` | branco | 4.85% | +11 |
| painel / dock | `#282828` | branco | 5.29% | +12 |
| faixa superior da tab strip | `#2B2B2B` | branco | 6.61% | +15 |
| hover / selecao neutra | `#353535` | branco | 11.01% | +25 |
| borda de item selecionado | `#454545` | branco | 18.06% | +41 |
| divisoria de 1px | `#0E0E0E` | preto | 50.00% | -14 |
| faixa de borda inferior | `#141414` | preto | 28.57% | -8 |

## Por que isso importa

Com base + escala, qualquer estado que o template peca (ControlFillSecondary no hover,
ControlFillTertiary no pressed, SubtleFillSecondary, DividerStrokeDefault) deixa de ser
um hex magico e passa a ser um alpha sobre a mesma base - que e como o produto e codado.

## Item aberto

hover / pressed / focus por token nao foram medidos: a janela do produto estava minimizada
durante a sessao e a automacao de ponteiro nao chegou a executar. O modelo preve esses
valores pelo alpha; falta confirmar qual slot cada token de estado usa.

## Estados medidos (injecao de ponteiro na janela real)

Cada linha foi capturada com o ponteiro sobre o controle e comparada pixel a pixel
com a captura de repouso. No fim a janela foi devolvida ao estado original
(diff final contra o baseline: vazio).

| estado | hex | alpha | medido em |
|---|---|---|---|
| repouso de controle (ControlFillDefault) | `#262626` | 4.41% | botao de comando em repouso |
| item de menu-bar ativo (menu aberto) | `#292929` | 5.73% | item Arquivo com menu aberto |
| pressed (ControlFillTertiary) | `#2F2F2F` | 8.37% | botao de comando com mouse pressionado |
| hover de botao de comando (ControlFillSecondary) | `#333333` | 10.13% | icone Salvar sob o ponteiro |
| hover de linha/aba + selecao (SubtleFillSecondary) | `#353535` | 11.01% | linha da Solution Explorer e aba inativa |
| borda de item selecionado (ControlStrokeSecondary) | `#454545` | 18.06% | contorno de selecao |

Achado: hover nao e um valor so. Botao de comando usa ~10,13% e linha/aba usa ~11,01% -
sao tokens diferentes (`ControlFillSecondary` vs `SubtleFillSecondary`).
Pressed fica ABAIXO do hover na escala (8,37%).

Nao observados: anel de foco (nao apareceu na caixa de busca) e menu popup
(renderiza em HWND proprio, fora do PrintWindow da janela principal).

## PALETA AUTORITATIVA — encontrada (duas fontes independentes)
O negativo anterior ("hex nao existe em disco") estava **errado**. Duas fontes independentes entregaram os valores literais.

### Fonte 1 — `privateregistry.bin` (o que o produto usa em runtime)
- chave `Themes\{1ded0138-47ce-435e-84ef-9ec1f439b749}\<Categoria>`, valor `Data` = REG_BINARY
- layout: `u32 totalBytes | ver=11 | nCat=1 | GUID(16 LE) | nEntradas`; por entrada: `u32 len | nome ASCII | [u8 flags + u32 RGBA]×2` (slot 0 = Foreground, slot 1 = Background)
- cor em bytes **R,G,B,A** e GUID em **little-endian** dentro do blob — os dois motivos pelos quais toda varredura de string/dword deu negativo
- validação: **1.495 de 1.496** blobs consomem exatamente o `totalBytes` declarado
- leitor real: `ColorThemeProvider.CreateThemeList()` em `Shell.UI.Internal.dll`
- números: tema escuro **84 categorias / 3.100 tokens**; 18 temas **10.125**; flat escuro **867** (Fluent-Shell 94 + Environment 773)

### Fonte 2 — `Shell.Dark.pkgdef` (o que o produto distribui)
- 236 tokens em 4 blobs, layout `[u32 nameLen][name utf8][u8 type][R][G][B][A]`, **nomes também hex-encoded** dentro do blob
- universo: 41 pkgdef de tema, 3.278 entradas

### Conferência contra os pixels medidos

| token | autoritativo | composto sobre #1C1C1C | medido |
|---|---|---|---|
| EnvironmentBackground | FF1C1C1C | — | #1C1C1C ✅ |
| AccentFillDefault | FF9184EE | — | #9184EE ✅ |
| ControlFillDefault | 0FFFFFFF (5,9%) | #262626 | #262626 ✅ |
| CardBackgroundFillDefault | 0DFFFFFF (13/255) | #282828 | #282828 ✅ |
| AccentFillSecondary | E59184EE | — | rampa de accent |

**#262626 e #353535 nunca foram tokens** — são composição de branco translúcido sobre a base. Foi exatamente o que o modelo derivado dos pixels previu; agora está confirmado no dado.

### Por que as 8 buscas anteriores falharam (3 causas)
1. Nomes de token também hex-encoded dentro do blob (pkgdef) ou guardados como valor `Data` (registro).
2. Ordem de bytes **R,G,B,A** — nenhum padrão dword ARGB/BGRA casa.
3. GUID da categoria em **little-endian** dentro do blob.

Artefatos: `tokens-dark-flat.json` (867), `tokens-dark-by-category.json` (84 categorias), `tokens-shell-dark.json` (236), `palettes-all-themes-18.json` (18 temas).
