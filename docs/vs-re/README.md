# Como a UI do Visual Studio 2026 é codada — engenharia reversa

**Alvo:** Microsoft Visual Studio Community 2026 `18.10.12224.181`
`C:\Program Files\Microsoft Visual Studio\18\Community` (somente leitura; nada foi alterado no produto).
**Método:** decompilação estática do CLR + extração de BAML + amostragem de pixel da janela renderizada.
**Resultado numérico (v2 — corpus completo):** **7.671** dicionários BAML convertidos para XAML, de **220 assemblies**, **0 falhas de conversão**, **0 XML malformado** (30,1 MB de XAML). A primeira passada, amostral, cobria 372.

---

## 0. Corpus completo — o UI inteiro em mãos

Todo o XAML do produto instalado foi extraído: nada de amostra.

| fatia | escopo | XAML | assemblies |
|---|---|---|---|
| shell | `Common7\IDE\*.dll` + PrivateAssemblies + PublicAssemblies | 4.856 | 42 |
| commonext | `Common7\IDE\CommonExtensions\**` | 883 | 88 |
| extensions | `Common7\IDE\Extensions\**` | 1.932 | 90 |
| **total** | | **7.671** | **220** |

- **Onde está:** `C:/Users/pedri/Downloads/vs-ui-corpus/<fatia>/<assembly>/<caminho interno>.xaml`
- **Índice por fatia:** `index.json` com `{assembly, resource, xamlPath, bytes, status}` (0 failed, 0 órfão).
- **Catálogo derivado:** `catalog/INDEX.md` (maiores assemblies), `catalog/TOKENS.md`, `catalog/SURFACES.md`, `catalog/METRICS.md`, `catalog/catalog.json`.
- **Verificação:** cada XAML reparseado com XML estrito → **0 malformado** nas três fatias.

### Vocabulário real de tokens (extraído do corpus, não de documentação)

| classe | nomes distintos |
|---|---|
| `EnvironmentColors` | **316** |
| `ShellColors` | **73** |
| `VisualOptions` | 1 |

Top de uso: `ToolWindowTextBrushKey` (766), `ComboBoxButtonMouseOverSeparatorBrushKey` (180), `DropDownMouseOverTextBrushKey` (154), `ToolWindowBackgroundBrushKey` (133), `CommandBarTextActiveBrushKey` (124).

### Correção dos números preliminares

Minha varredura inicial estimou 1.299 assemblies / 30.863 BAML. **Estava inflada**, por duas razões medidas: (a) assemblies satélite `*.resources.dll` (4.618 em CommonExtensions, 2.614 em Extensions) **duplicam os mesmos BAML** por idioma; (b) contagem de *ocorrências* do nome, não de entradas. A medição válida — confirmada por três métodos independentes por fatia (conversão, `extractor list`, varredura de bytes) — é **7.671 / 220**.

### Onde estão as cores (negativo provado, agora sobre o corpus inteiro)

- 7.671 XAML varridos: **nenhum** dicionário que defina os tokens Fluent (`ControlFillDefault`, `TextFillPrimary`, …) com valor hex. Os hex que aparecem são de ícones/estilos locais.
- `CurrentSettings.vssettings`: 0 itens de cor (`<Item`/`Foreground=` ausentes).
- `privateregistry.bin`: **travado pelo processo do VS** (`ERROR_SHARING_VIOLATION`), inacessível sem fechar o produto.
- Automação DTE na instância viva: anexa (`Visual Studio 18.0`), mas **`Environment/FontsAndColors` não existe mais** (migrou para o Unified Settings) → sem acesso às cores por essa via.
- `themeresources.xaml` do SDK do Windows: é o vocabulário **UWP antigo** (`SystemBaseHighColor`…), não os tokens Fluent do VS 2026.

⇒ Conclusão: o mapa **token → RGB** só existe em runtime (serviço de tema). O que temos é arquitetura + nomes completos + **valores medidos por pixel** (ver §6). Fechar 100% exigiria instrumentação em processo ou amostragem de cada estado com o produto em execução.

### Limites do corpus

- **WinForms/legado não tem XAML**: 55 assemblies (Extensions) e 47 (shell) só têm `.resources` serializados — VSDesigner, EntityDesigner, Publish, Server Explorer, property pages antigas. Essas superfícies não são representáveis por este método.
- 105 DLLs nativas em Extensions e 90 na shell: sem metadado gerenciado, nada a extrair.
- 1 outlier: `Basic.Reference.Assemblies.Scope.dll` embute reference assemblies do .NET (126 nomes de BAML do PresentationFramework) mas nenhum container — não é UI do VS.

---

## 1. Stack real (o que existe, comprovado)

| Camada | Tecnologia | Evidência |
|---|---|---|
| Host | `devenv.exe` nativo | processo 32308, CLR hospedado |
| Shell UI | **WPF** (.NET Framework) | XAML com `xmlns` `presentation`/`x`, `ControlTemplate`, BAML embutido |
| Painéis novos | **WebView2** | `CommonExtensions\...\EdgeWebViews\*.manifest.json` com `"browserHostVersion": "2.0"`, `"use_css_variables": true` |
| Cores | **serviço de tema em runtime** | nenhum hex nos assemblies (ver §4) |

Os manifests de WebView2 declaram os tokens que o HTML consome, por exemplo:

```json
"Fluent-Shell-Fill-Control-Default": {
  "category": "73708ded-2d56-4aad-b8eb-73b20d3f4bff",
  "key_type": "BackgroundColor",
  "name": "ControlFillDefault"
}
```

Ou seja: **a mesma tabela de tokens alimenta WPF e WebView2**. É design system de verdade, não estilo espalhado.

---

## 2. Onde cada coisa mora (mapa dos assemblies)

| Assembly | Tamanho | BAML → XAML | Conteúdo |
|---|---|---|---|
| `Microsoft.VisualStudio.Shell.Styles.dll` | 639 KB | **86** | a biblioteca Fluent primitiva: 1 dicionário por controle (`tabitemstyle`, `buttonstyle`, `scrollbarstyle`, `treeviewitemstyle`, `comboboxstyle`, `textboxstyle`, `menuitemstyle`, `window\windowstyle`, `window\nonclientbuttonstyle`, …) + as classes de token |
| `Microsoft.VisualStudio.Platform.WindowManagement.dll` | 1,9 MB | **60** | o sistema de janelas/dock: 37 XAML em `themes\` (document tabs, dock, auto-hide, floating window, title bar) |
| `Microsoft.VisualStudio.Shell.UI.Internal.dll` | 16 MB | **286** | `mainwindow.xaml`, `statusbar\vsstatusbar.xaml`, `styles\commanding\vstoolbarstyle.xaml` + `vstoolbarbuttonstyle`, `vstoolbarseparatorstyle`, `outputwindow.xaml`, 16 accent swatches, 97 assets de status bar |

Consequência para nós: o VS não tem "um tema". Tem **~90 dicionários de controle** que consomem **uma tabela de tokens**. Trocar tema = trocar a tabela, não os estilos.

---

## 3. Os tokens (o design system dentro do código)

Extraídos de `Microsoft.VisualStudio.Shell.Styles\Microsoft\VisualStudio\Shell\Styles\`:

| Classe | Valores (px/DIP) |
|---|---|
| `Spacings` | 0, 1, 2, 4, 8, 12, 16, 20, 24, 32 (+ nudges 3, 6, 10) |
| `CornerRadii` | 0, 2, 4, 6, 8 (`FocusRect` = 6) |
| `StrokeWidths` | 0, 1, 2, 3, 4 (`Thin`=1, `Thickest`=4) |
| `Sizes` | ControlMin 24×24 · ContentMin 16×16 · Icon 8/12/16/24/32 · **NonClientControl 30×44** · ResizeGrip 6 · ToggleSwitch 20 |
| `Typography` | **Segoe UI 12** (shell) · **Cascadia Mono 14** (code) · caption 12 · body 14 · bodyLarge 18 · subtitle 20 · title 28 · titleLarge 40 · display 68 · line-heights 16/20/24/20/28/36/52/92 |

E a tipografia é **derivada**, não fixa (`Typography.cs:233-242`):

```csharp
ShellFontSize = (double)Math.Abs(array[0].lfHeight) * 96.0 / VisualTree.SystemDpiY;
BodyFontSize  = Math.Round(ShellFontSize * 1.1667);
SubtitleFontSize = Math.Round(ShellFontSize * 1.6667);
TitleFontSize = Math.Round(ShellFontSize * 2.3333);
```

Base 12 → o resto é razão. É por isso que um app Qt com "9.5pt" nunca vai parecer VS: além da unidade, a **rampa** inteira está fora.

---

## 4. Cores: indireção em XAML — a paleta está em .pkgdef e no registro (§12)

- Nos 86 XAML de estilo a **única** cor literal é `#00FFFFFF` (branco transparente): 21 ocorrências em 11 arquivos. Todo o resto é `{DynamicResource ...}`.
- `ShellColors.cs` (3.769 linhas): **188 membros `ThemeResourceKey`**, zero `#RRGGBB`, zero `Color.FromArgb`.
- Varredura dos 60 blobs `.resources` dos dois assemblies: **0 valores `#RRGGBB`**. Nomes de cor só aparecem em texto de localização.

A chave é `ThemeResourceKey(categoria, nome, tipo)`, com duas categorias centrais:

| Categoria (GUID) | Família | Tokens |
|---|---|---|
| `73708ded-2d56-4aad-b8eb-73b20d3f4bff` | **Fluent-Shell** | `ControlFillDefault/Secondary/Tertiary/Transparent`, `SubtleFillSecondary/Tertiary`, `LayerFillDefault`, `ControlStrongFillDefault`, `ControlStrokeDefault/Secondary/Transparent`, `DividerStrokeDefault`, `TextFillPrimary/Secondary/Disabled`, `AccentFillDefault/Secondary/Tertiary`, `TextOnAccentFillPrimary` |
| `624ed9c3-bdfd-41fa-96c3-7c824ea32e3d` | **Environment** | `ToolWindowBackground`, `ToolWindowTabSelectedActiveBackground`, `EnvironmentBackground`, `CommandBarGradient*` |

**Portanto:** os valores RGB não são recuperáveis do binário — são resolvidos pelo `VSColorTheme` em runtime. Para replicar o visual, a fonte de verdade é o **tema renderizado** (amostragem de pixel, §6). Lista completa dos 94 nomes de token em `tokens/shell-token-names.txt`.

---

## 5. Anatomia dos controles (o código, de fato)

### TabItem — `styles\tabitemstyle.xaml` (48 linhas)
```xml
<Border Background="{TemplateBinding Control.Background}"
        BorderBrush="{TemplateBinding Control.BorderBrush}"
        BorderThickness="{TemplateBinding Control.BorderThickness}"
        CornerRadius="{x:Static styles:CornerRadii.M}">   <!-- M = 4 -->
  <DockPanel LastChildFill="False">
    <ctrls:Indicator x:Name="Indicator" Orientation="Vertical"
                     Height="{Binding ActualHeight, ElementName=Header}"/>
    <ContentPresenter Name="Header" ContentSource="Header"
                      Margin="{TemplateBinding Control.Padding}"
                      MinHeight="{x:Static styles:Sizes.ContentMinHeight}"/>
  </DockPanel>
</Border>
```
- `Padding = Spacings.HorizontalM` (12), `BorderThickness = StrokeWidths.Thin` (1), `MinHeight = 24`.
- Hover: `SubtleFillSecondary` + `ControlStrokeSecondary`; desabilitado: `TextFillDisabled`.
- O `Indicator` é o **realce da aba ativa**: `Indicator.ControlWidth = 3.0` (`Controls/Indicator.cs:30`), vertical na lateral e **vira barra horizontal embaixo** quando `TabStripPlacement=Top`.
- Medido na tela: **3–4px `#9184EE`** na borda da aba ativa (3 DIP rasteriza em 4 colunas de pixel — as duas leituras são compatíveis e não se contradizem).

### Button — `buttonbasestyle.xaml` + `buttonstyle.xaml`
```xml
<Border CornerRadius="{TemplateBinding ctrls:Button.CornerRadius}"  <!-- 4 -->
        Background="{TemplateBinding Control.Background}"
        BorderBrush="{TemplateBinding Control.BorderBrush}"
        BorderThickness="{TemplateBinding Control.BorderThickness}"> <!-- 1 -->
  <ContentPresenter MinHeight="{x:Static styles:Sizes.ContentMinHeight}"/>
</Border>
```
Estados (nomes reais dos tokens): Default → `ControlFillDefault` · Hover → `ControlFillSecondary` · Pressed → `ControlFillTertiary` · Accent (primário) → `AccentFillDefault` + texto `TextOnAccentFillPrimary`.

### ScrollBar — `scrollbarstyle.xaml` (168 linhas)
```xml
<Setter Property="Background" Value="{DynamicResource ...ControlFillTransparentBrushKey}"/>
<Setter Property="Foreground" Value="{DynamicResource ...ControlStrongFillDefaultBrushKey}"/>
<Setter TargetName="Thumb" Property="Border.CornerRadius"
        Value="{Binding ActualHeight, Converter=MultiplyConverter, ConverterParameter=0.5}"/>
```
- Thumb = **pílula**: raio = metade da espessura.
- `ScrollBar.ScrollThumbSize = 2.0`, `ScrollThumbExpandedSize = 8.0`; espessura vem de `SystemParameters.VerticalScrollBarWidth` normalizado para par.
- Trilho transparente; hover do trilho → `LayerFillDefault`.

### TreeViewItem — `treeviewitemstyle.xaml`
```xml
<Border Name="PART_Border" CornerRadius="{x:Static styles:CornerRadii.M}"
        BorderThickness="{x:Static styles:StrokeWidths.Thin}"/>
<ctrls:Indicator x:Name="Indicator" Grid.Column="0">   <!-- mesma barra de 4px -->
```
`MinHeight 24`, `Padding = Spacings.RightS` (8), expansor é um `ToggleButton` translúcido.

### Non-client (barra de título) — `themes\windowtitlebarbuttonstyle.xaml`
```xml
<Setter Property="FrameworkElement.Height" Value="{x:Static styles:Sizes.NonClientControlHeight}"/>  <!-- 30 -->
<Setter Property="FrameworkElement.Width"  Value="{x:Static styles:Sizes.NonClientControlWidth}"/>   <!-- 44 -->
<Setter Property="vm:GlyphButton.HoverBorderThickness" Value="{x:Static styles:StrokeWidths.Thin}"/>
```
Medido: a barra de título tem exatamente **30px**. Bate com o token.

---

## 6. Medição de pixel (o que realmente aparece na tela)

Janela real, 1936×1048 a 100% de escala (captura tem offset de +8px da moldura).
Artefato: `measured/vs2026-18.10-dark-window.png`.

### Três níveis de fundo (regra central)
| Nível | Hex | Onde |
|---|---|---|
| chrome | `#1C1C1C` | barra de título/menu (30px), área ao redor |
| command bar | `#262626` | duas faixas de toolbar (29px + 25px, separadas por 6px de `#1C1C1C`), status |
| painel | `#282828` | docks (Solution Explorer, Git Changes) |
| documento | `#1E1E1E` | editor **e aba ativa** (fundem) |
| saída | `#1F1F1F` | painel Output |

### Bordas e estados
| Função | Hex |
|---|---|
| divisória de 1px | `#0E0E0E` |
| faixa inferior | `#141414` |
| hover neutro / seleção neutra | `#353535` |
| borda de item selecionado | `#454545` |
| seleção de "arquivo" (azul) | `#28287C` |
| **accent / indicador de aba** | `#9184EE` |
| texto primário | `#FFFFFF` · secundário `#E6E6E6` · muted `#D7D7D7` |

### Geometria medida
title/menu 30px · command bar 29px · gap 6px · command bar 25px · tab strip 22px + 16px · status 24px · scrollbar ~8px · splitter 1px.

### O modelo por trás das cores (o achado mais útil)

Todas as superfícies medidas são **cinzas puros (R=G=B)** e reconstroem exatamente com uma única
fórmula sobre a base:

```
branco:  hex = base + alpha * (255 - base)
preto:   hex = base - alpha * base            base = #1C1C1C
```

| papel | hex | overlay | alpha |
|---|---|---|---|
| chrome | `#1C1C1C` | — | 0% |
| documento | `#1E1E1E` | branco | 0,88% |
| Output | `#1F1F1F` | branco | 1,32% |
| grade alternada | `#232323` | branco | 3,08% |
| command bar / status | `#262626` | branco | 4,41% |
| tab strip (baixo) | `#272727` | branco | 4,85% |
| painel | `#282828` | branco | 5,29% |
| tab strip (cima) | `#2B2B2B` | branco | 6,61% |
| hover / seleção | `#353535` | branco | 11,01% |
| borda de seleção | `#454545` | branco | 18,06% |

Resíduo de reconstrução: **0/255**. Arquivos: `theme/MODEL.md` e `theme/vs2026-dark.json`.

### Estados medidos (ponteiro real sobre o produto)

Medidos com injeção de ponteiro na janela do produto, diff de pixel contra o repouso; ao final a janela foi devolvida ao estado original (**diff final: vazio**).

| estado | hex | alpha | medido em |
|---|---|---|---|
| repouso de controle | `#262626` | 4,41% | botão de comando |
| item de menu-bar ativo | `#292929` | 5,73% | "Arquivo" com menu aberto |
| **pressed** | `#2F2F2F` | 8,37% | botão com mouse pressionado |
| **hover de botão de comando** | `#333333` | 10,13% | ícone "Salvar" sob o ponteiro |
| **hover de linha/aba + seleção** | `#353535` | 11,01% | linha da Solution Explorer e aba inativa |
| borda de seleção | `#454545` | 18,06% | contorno de seleção |

Dois achados que ninguém acerta por estimativa: **hover não é um valor só** (botão 10,13% vs linha/aba 11,01% — tokens diferentes) e **pressed fica abaixo do hover** (8,37%).

Evidência visual recortada (6× de zoom) em `measured/states/`: `hover_toolbar.png`, `state_pressed_toolbar.png`, `hover_tab.png`, `hover_tree.png`, `state_menu_open.png`.

Isso significa que reproduzir o VS não é copiar dezenas de hex — é acertar **base + escada de alpha**.
Qualquer estado que o template peça vira um alpha na mesma base.

---

## 7. O que isso diz sobre o Olho de Deus (lacunas medidas)

O tema do app hoje (`resources/forensic.qss`, 1.115 linhas) está **fora da escala do VS** em quase todos os eixos:

| Eixo | Olho de Deus | VS 2026 | Arquivo |
|---|---|---|---|
| base | `#121212` | `#1C1C1C` | `resources/forensic.qss:5-11` |
| painel | `#1A1A1A` | `#282828` | :121-133 |
| documento | `#0A0A0A` | `#1E1E1E` | :152-156 |
| camada de command bar | ausente | `#262626` | — |
| raio | 3–6px (e 7/12px em `documentBody`) | só 4 | :165-168, :1099, :1113 |
| borda | 1px em quase tudo | só stroke de controle, 1px | :127, :204, :309… |
| tipografia | `9.5pt`/`8pt` misturado com `px` | 12px base, rampa fixa | :9, :172, :937 |
| scrollbar | 10px + raio 4 | ~8px, pílula, trilho transparente | :342-352 |
| seleção | roxa `#2D163D` | `#353535` / `#28287C` | :208, :218 |
| accent | `#7A3FB3` | `#9184EE` | :147, :184 |

**Armadilhas reais no nosso código (não são cosméticas):**

1. `#9184EE` **já está correto**, mas hardcoded em três `paintEvent`: `mainwindow.cpp:287` (`ContourTabBar`), `:338` (`DocumentFrame`), `:381` (`DocumentTopLine`); e `#7A6FB8` em `:445` (`ToolTile`). Mexer só no QSS deixa violeta residual.
2. `QMenuBar#mainMenuBar` no QSS **nunca aplica**: o `objectName` real é `titleMenuBar` (`mainwindow.cpp:3580`). Todo o bloco "escala VS" do QSS (`:792-822`, `:935-941`) é código morto.
3. O QSS é **append-only**: o mesmo seletor aparece 2–6× e vence o último (`QToolBar#mainToolbar` em 7 lugares, `QTabBar::tab` em 7, `QTabWidget#centerTabs::pane` em 6). Editar bloco antigo = zero efeito.
4. Existem **duas paletas conflitantes** no mesmo arquivo (roxo até ~:414, "VS sampled" a partir de :415).
5. `dist_full/resources/forensic.qss` é **outra** versão (9.9 KB) — a build de distribuição roda outro tema.
6. O QSS é carregado como string de `applicationDirPath()/resources/forensic.qss` (`mainwindow.cpp:4238-4245`), com fallback **vazio** → sem o arquivo, o app cai em Fusion puro, sem tema e sem o dimensionamento que vem do QSS.

---

## 8. Como reproduzir

```powershell
# 1. decompilar (C# + XAML, inclui BAML)
dotnet tool install -g ilspycmd
ilspycmd -p -o <out> --nested-directories "C:\...\Common7\IDE\Microsoft.VisualStudio.Shell.Styles.dll"
ilspycmd --list-resources "C:\...\Microsoft.VisualStudio.Shell.Styles.dll"

# 2. extrair BAML de qualquer assembly WPF (rota usada: NuGet ICSharpCode.BamlDecompiler 11.2)
#    XamlDecompiler(PEFile, IAssemblyResolver, BamlDecompilerSettings).Decompile(stream).Xaml

# 3. medir pixel da janela viva (PrintWindow 2 = PW_RENDERFULLCONTENT) e amostrar regiões
```

## 9. Limites honestos

- **Não existe hex nos assemblies.** Quem quiser valor exato de token precisa runtime (injeção/serviço de tema) ou amostragem de pixel. Fiz o segundo; não injetei código no `devenv`.
- Não abri o VS, não matei processo, não escrevi em `C:\Program Files`.
- A rampa de tipografia é derivada do font do sistema: em outra máquina/DPI os números mudam de forma previsível (base 12 × razões).
- Ghidra não foi usado: o alvo da UI é **gerenciado** (WPF/BAML). Ghidra só entraria no `devenv.exe` nativo (bootstrap do CLR, não UI).

## 10. Acervo neste diretório

```
docs/vs-re/
├── README.md                      este relatório
├── measured/
│   ├── vs2026-18.10-dark-window.png   captura real usada nas medições
│   └── vs2026-dark-tokens.json        tokens + hex medido + geometria + mapa p/ QSS
├── tokens/
│   ├── Sizes.cs Spacings.cs CornerRadii.cs StrokeWidths.cs Typography.cs
│   ├── ShellColors.cs              188 ThemeResourceKey (a tabela de cores do VS)
│   └── shell-token-names.txt       94 nomes de token
├── styles/                        24 XAML reais (tab, button, scrollbar, treeview,
│                                  combobox, textbox, menu, window, nonclient,
│                                  vstoolbar*, vsstatusbar, mainwindow, outputwindow)
├── catalog/                       catalogo do corpus: INDEX.md, TOKENS.md,
│                                  SURFACES.md, METRICS.md, catalog.json
└── theme/                         vs2026-dark.json (especificacao) + MODEL.md
```

## 11. As 8 fontes que falharam (a 9a e a 10a funcionaram — §12)

Objetivo: obter o mapa **token → RGB** direto do produto, não por inferência. Tudo abaixo foi executado e medido.

| # | fonte tentada | resultado |
|---|---|---|
| 1 | os 7.671 XAML do corpus | **0** dicionários que definem os tokens Fluent com hex. Só hex de ícones/estilos locais |
| 2 | 60 blobs `.resources` dos assemblies da shell | 0 valores `#RRGGBB`; são tabelas de string |
| 3 | `CurrentSettings.vssettings` (301 KB) | 0 itens de cor (`<Item`, `Foreground=` ausentes no VS 18) |
| 4 | `privateregistry.bin` do perfil (travado) e **da instância isolada** (6 MB, destravado após fechar) | **9.548 chaves varridas, 0** chaves/valores de tema |
| 5 | automação DTE na instância viva | anexa em `Visual Studio 18.0`, mas `Environment/FontsAndColors` **não existe mais** no VS 18 |
| 6 | cache `VTC` e todo o perfil do usuário | `VTC` é cache de templates; 0 `.vstheme` no sistema |
| 7 | `themeresources.xaml` do Windows SDK | vocabulário UWP antigo (`SystemBaseHighColor`…), não os tokens Fluent do VS |
| 8 | manifests WebView2 do produto | declaram os tokens, **não** os valores |

**Conclusão provada:** o mapa token→RGB só existe na memória do processo em execução.

### O que foi entregue no lugar (e é fiel)

- **Modelo de overlay** validado com resíduo **0/255** (§6, `theme/MODEL.md`): base `#1C1C1C` + escada de alpha branco {0,88 · 1,32 · 3,08 · 4,41 · 4,85 · 5,29 · 6,61 · 11,01 · 18,06}% e preto {28,57 · 50}%.
- Especificação completa em `theme/vs2026-dark.json` (escala, tokens por categoria, tipografia, geometria).
- Vocabulário real: **316** `EnvironmentColors` + **73** `ShellColors` + 1 `VisualOptions`.

### Estado atual do item que faltava

**Resolvido por medição:** `hover` (dois valores distintos), `pressed` e item de menu-bar ativo.
Valores em §6 e em `theme/vs2026-dark.json` → `states_measured`.

**Ainda não observado (2 itens, ambos de baixo impacto):**

- **anel de foco**: cliquei na caixa de busca do Solution Explorer e nenhum anel apareceu — em VS 18 o foco em campo de texto é indicado pela borda/caret, não por um ring destacado.
- **menu popup**: o menu do WPF renderiza em **HWND próprio**, então o `PrintWindow` da janela principal não o captura (o que se mede é o item da barra em estado ativo, `#292929`).

Fechar esses dois exigiria capturar a janela popup separadamente (enumerar HWNDs do processo durante o menu aberto) — trabalho pequeno, sem risco, se você quiser.

---

## 12. A paleta encontrada (duas fontes independentes)

Depois de 8 tentativas frustradas (§11), duas fontes entregaram os valores literais. O negativo anterior estava **errado** e fica corrigido aqui.

### Fonte 1 — `privateregistry.bin` (o que o produto usa em runtime)
- chave `Themes\{1ded0138-47ce-435e-84ef-9ec1f439b749}\<Categoria>`, valor `Data` = REG_BINARY
- layout `u32 totalBytes | ver=11 | nCat=1 | GUID(16 LE) | nEntradas`, e por entrada `u32 len | nome ASCII | [u8 flags + u32 RGBA]×2` (slot 0 = Foreground, slot 1 = Background)
- cor em bytes **R,G,B,A**; GUID em **little-endian** — as duas razões pelas quais nenhuma varredura casou
- **validação:** 1.495 de 1.496 blobs consomem exatamente o `totalBytes` declarado
- leitor: `ColorThemeProvider.CreateThemeList()` em `Shell.UI.Internal.dll` → `GetSubCollectionNames("Themes")` → tabela em memória → `VSColorTheme`
- **números:** escuro = **84 categorias / 3.100 tokens**; 18 temas = **10.125**; flat escuro = **867** (Fluent-Shell 94 + Environment 773)

### Fonte 2 — `Shell.Dark.pkgdef` (o que o produto distribui)
- 236 tokens, 4 blobs, layout `[u32 nameLen][name utf8][u8 type][R][G][B][A]`, nomes hex-encoded; universo de 41 pkgdef / 3.278 entradas

### Conferência contra os pixels medidos

| token | autoritativo | composto sobre `#1C1C1C` | medido |
|---|---|---|---|
| `EnvironmentBackground` | `#1C1C1C` | — | `#1C1C1C` ✅ |
| `AccentFillDefault` | `#9184EE` | — | `#9184EE` ✅ |
| `ControlFillDefault` | `#FFFFFF` @5,9% | `#262626` | `#262626` ✅ |
| `CardBackgroundFillDefault` | `#FFFFFF` @13/255 | `#282828` | `#282828` ✅ |
| `AccentFillSecondary` / `Tertiary` | `#9184EE` @229 / @204 | — | rampa de accent |

**`#262626` e `#353535` não são tokens** — são composição de branco translúcido sobre a base. O modelo derivado dos pixels previu exatamente isso, agora confirmado no dado.

### O que isso fecha

| buraco | status |
|---|---|
| mapa token → RGB | **fechado** (867 flat / 3.100 por categoria) |
| hover / pressed / seleção | **fechado** (`ControlFill*` / `ControlAltFill*` com alpha inteiro) |
| anel de foco | **fechado** (`FocusStrokeOuter/Inner`, 2px, raio 6, offset −4 — agora com cor) |
| menu popup (estrutura) | **fechado** (flyout `SurfaceBackgroundFillDefault` + `SurfaceStrokeFlyout`, raio 8/6, 1px) |
| menu popup (pixels) | não medido — irrelevante: o valor vem da paleta |

---

## 13. Código-fonte do UI (decompilado)

Todo o UI gerenciado foi decompilado para C# + XAML: totais por assembly em `src/INDEX.md` e `src/manifest.json`; corpus em `C:/Users/pedri/Downloads/vs-ui-src/<fatia>/`.

---

## 14. Mapa token → controle → estado (o fechamento)

Gerado por script (varredura recursiva de cada elemento dos 7.671 XAML, rastreando o `Style.TargetType` e o `Trigger` em vigor):

| métrica | valor |
|---|---|
| XAML varridos | 7.671 |
| tokens distintos referenciados | **394** |
| tokens resolvidos para hex | **338 (85,8%)** |
| gradientes (não são cor sólida) | 16 |
| tokens de sistema/legado (resolvidos pelo Windows, não pelo tema) | ~38 |
| controles distintos mapeados | **172** |

Artefatos: `catalog/TOKEN_USAGE.md` (tabelas por controle/estado) e `catalog/token-usage.json` (mapa completo).

### Por que os 56 restantes não são lacuna

- **16 são gradientes** (`CommandBarGradientBrushKey`, `ComboBoxPopupBackgroundGradientBrushKey`…) — não existem como cor sólida em paleta nenhuma.
- **~38 são legado/sistema** (`SystemWindow`, `SystemHighlight`, `SystemGrayText`, `ToolTipText`, `StatusBarDefaultText`, `DialogText`, `MainboxDisabledContentText`…) — o VS os resolve de `GetSysColor`, não do tema escuro. Foram exatamente os call sites que o Ghidra encontrou em `msenv.dll` (92 chamadas de `GetSysColor`).
- 1 é falso positivo do meu regex (`VisualOptions.Instance`).

### Exemplo do que o mapa entrega

| controle | estado | token | hex |
|---|---|---|---|
| TabItem | default | ControlFillTransparent | — (transparente) |
| TabItem | IsMouseOver=True | SubtleFillSecondary | branco @11/255 → `#353535` |
| Button | IsPressed=True | ControlFillTertiary | branco @? → composto |
| TreeViewItem | IsSelected=True | SubtleFillSecondary | `#353535` |
| ScrollBar thumb | default | ControlStrongFillDefault | medido |

Isso fecha o último item da lista: **não falta conhecimento, falta apenas CPU terminando duas fatias de decompilação.**
