# pdfcheck — validador de conformidade PDF (independente)

Validador escrito para conferir o PDF que o **gerador do lead** (`forensic_report.cpp`) produz.
Não usa biblioteca de PDF: o formato é lido a mão, byte a byte, só com a stdlib do CPython 3.13.

Motivo de existir: um PDF malformado abre no Edge mesmo com a tabela xref errada, porque o
leitor reconstrói o arquivo. "Abre no meu visualizador" não é prova de nada. Este programa lê a
estrutura declarada e compara com o que está realmente escrito no arquivo.

## Arquivos

| arquivo | função |
|---|---|
| `pdfcheck.py` | validador: estrutura + extração de texto dos content streams |
| `make_fixtures.py` | gera 2 PDFs conformes e 25 PDFs com um defeito injetado cada |
| `selftest.py` | roda o validador em todos os fixtures e exige o FAIL da checagem alvo |
| `render.ps1` | abre o PDF no Edge headless, salva PNG e mede se a imagem tem conteúdo |
| `out/` | artefatos gerados (fixtures, PNGs, logs). Nada em `out/` é fonte |

## Como rodar

    python ferramentas/pdfcheck/pdfcheck.py <arquivo.pdf>            # relatório + texto extraído
    python ferramentas/pdfcheck/pdfcheck.py <arquivo.pdf> --runs     # + cada trecho com coordenadas
    python ferramentas/pdfcheck/pdfcheck.py <arquivo.pdf> --json     # saída para máquina

    python ferramentas/pdfcheck/make_fixtures.py                     # regera os fixtures
    python ferramentas/pdfcheck/selftest.py                          # prova que detecta defeito

    powershell -NoProfile -File ferramentas/pdfcheck/render.ps1 -Pdf <arq.pdf> -Png <saida.png>

Exit code do validador: `0` = nenhuma checagem FAIL; `1` = ao menos um FAIL.
`pwsh` não está instalado nesta máquina; use `powershell` para o render.ps1.

## Checagens

Cada linha do relatório é `[PASS]`, `[FAIL]` ou `[SKIP]` (SKIP = não foi possível
verificar, com o motivo declarado na própria linha).

| id | verifica |
|---|---|
| C01 | arquivo existe e não está vazio |
| C02 | cabeçalho `%PDF-1.x` no byte 0 |
| C03 | `%%EOF` no fim do arquivo (só espaço em branco depois) |
| C04 | `startxref` presente, apontando para dentro do arquivo e para `xref` ou `N G obj` |
| C05 | seção cross-reference parseia (tabela clássica, xref em stream, arquivo híbrido `/XRefStm` e cadeia `/Prev`) |
| C06 | cada entrada de xref tem exatamente 20 bytes: 10 dígitos, espaço, 5 dígitos, espaço, `n`/`f`, EOL de 2 bytes |
| C07 | abrir o arquivo em cada offset da xref encontra `N G obj` do objeto declarado; detecta deslocamento uniforme |
| C08 | todo objeto em uso parseia como objeto indireto completo |
| C09 | trailer `/Size` presente, `>= maior objeto + 1` e igual ao número de entradas (arquivo sem `/Prev`) |
| C10 | trailer `/Root` presente, é referência indireta e resolve |
| C11 | objeto `/Root` tem `/Type /Catalog` e `/Pages` |
| C12 | toda referência indireta do documento resolve para objeto existente |
| C13 | nenhum objeto completo escrito no arquivo está fora da xref (inclusive objeto marcado livre) |
| C28 | cada número de objeto aparece uma única vez no arquivo |
| C14 | árvore de páginas percorrível a partir de `/Root /Pages` |
| C15 | `/Count` de cada nó `/Type /Pages` igual ao número real de folhas `/Type /Page` sob ele |
| C16 | cada `/Page` tem `/Parent` apontando para o nó pai real |
| C17 | todo `/Type /Page` do arquivo está na árvore (nenhuma página órfã) |
| C18 | `/Contents` de cada página existe e é stream |
| C19 | `/Length` de cada stream igual ao número real de bytes entre `stream` e `endstream` |
| C20 | keyword `stream` seguido de CRLF ou LF (nunca dados colados) |
| C21 | todo stream com `/Filter` descomprime (FlateDecode, ASCIIHex, ASCII85, RunLength, LZW + preditores PNG/TIFF) |
| C22 | content stream com sintaxe válida: operandos por operador, `q`/`Q` e `BT`/`ET` balanceados |
| C23 | toda fonte em `/Resources /Font` existe como objeto `/Type /Font` com `/Subtype` |
| C24 | toda fonte usada em `Tf` existe em `/Resources /Font` |
| C25 | cada página tem `/MediaBox` com 4 números e área positiva |
| C26 | texto extraído dos content streams (`Tj`, `TJ`, `'`, `"`) — reprova documento sem texto |
| C27 | caixa estimada do texto cabe dentro da `/MediaBox` da página |
| C29 | documento sem `/Encrypt` (com criptografia nada do conteúdo é verificável: reprova em vez de aprovar sem ver) |
| C30 | texto não-ASCII gravado com a codificação da fonte (pega UTF-8 cru dentro de fonte de 1 byte, que o leitor desenha como dois glifos errados) |

## Extração de texto

Imprime **todo** o texto dos content streams, por página, na ordem do documento. Sem isso não há
como provar que o laudo carrega os dados do caso.

O que o interpretador executa: `BT`/`ET`, `Tf`, `Td`/`TD`/`Tm`/`T*`, `TL`/`Tc`/`Tw`/`Tz`/`Ts`,
`Tj`/`TJ`/`'`/`@"`, `q`/`Q`/`cm`, `Do` (entra em Form XObject com os recursos dele).

Escapes de string literal decodificados: `\(`, `\)`, `\\`, `\n`, `\r`, `\t`, `\b`, `\f`, `\ddd` (octal),
continuação de linha com `\` no fim. Strings hexadecimais `<...>` também são decodificadas
(com o padding de meio byte exigido pela norma).

Fontes: fonte simples é lida como byte = código; fonte Type0/Identity-H é lida como código de
2 bytes. `/ToUnicode` (`beginbfchar`/`beginbfrange`) é aplicado quando existe; `FEFF`/`FFFE` em
string literal é tratado como UTF-16.

Reconstrução de linha e palavra (regra explícita, determinística):
nova linha quando a linha de base muda mais que `max(1.0, 0.3 * corpo)`; espaço quando o trecho anterior
termina mais de `0.15 * corpo` antes do início do seguinte. A lista crua de trechos, com coordenadas,
sai com `--runs` — é a fonte da verdade quando a linha reconstruída parecer estranha.

Largura de glifo: `/Widths` (fonte simples), `/W` + `/DW` (CID), tabela AFM da Helvetica quando a
fonte simples não declara `/Widths` e o nome é Helvetica/Arial. Trecho cuja largura não tem dado
confiável é marcado e **fica fora** da conta do C27 (o relatório diz quantos ficaram fora).

## Autoteste — prova de que o validador detecta defeito

`make_fixtures.py` escreve os PDFs do zero (gerador próprio, independente do gerador do lead) e
injeta um defeito por arquivo. `selftest.py` exige, para cada arquivo quebrado, exit != 0 **e** a
checagem alvo em FAIL; e exige exit 0 nos PDFs conformes.

Resultado nesta máquina: **25 de 25 quebras detectadas, 2 de 2 PDFs conformes aprovados.**

| fixture | defeito injetado | checagem que reprova |
|---|---|---|
| valid_classic.pdf | nenhum (PDF 1.4, xref em tabela) | nenhuma — aprovado |
| valid_flate.pdf | nenhum (content streams com FlateDecode) | nenhuma — aprovado |
| 01_xref_offset_errado | offset do objeto 5 somado de +9 | C07 |
| 02_length_mentindo | `/Length 999` num stream de ~500 bytes | C19 |
| 03_root_inexistente | `/Root 99 0 R` não existe | C10 |
| 04_truncado_no_xref | arquivo cortado dentro da tabela xref | C03, C04 |
| 05_sem_eof | `%%EOF` removido | C03 |
| 06_flate_corrompido | FlateDecode com bytes corrompidos | C21 |
| 07_count_mentindo | `/Count 7` com 2 páginas | C15 |
| 08_parent_errado | `/Parent` da página 1 aponta para o catálogo | C16 |
| 09_contents_inexistente | `/Contents 88 0 R` não existe | C18 |
| 10_fonte_inexistente | `/Font /F1 77 0 R` não existe | C23 |
| 11_sem_cabecalho | primeiros bytes não são `%PDF-` | C02 |
| 12_startxref_errado | `startxref` para 999999 | C04 |
| 13_xref_entrada_19_bytes | entradas com 19 bytes (EOL de 1 byte) | C06 |
| 14_catalog_sem_type | `/Root` sem `/Type /Catalog` | C11 |
| 15_stream_sem_eol | `stream` colado nos dados | C20 |
| 16_objeto_fantasma | objeto 20 escrito no arquivo, fora da xref | C13 |
| 17_texto_fora_da_pagina | texto em y=-80, fora da MediaBox | C27 |
| 18_size_errado | `/Size 5` com 7 objetos | C09 |
| 19_bt_sem_et | `BT` sem `ET` | C22 |
| 20_fonte_nao_declarada | `/F9` usado em `Tf`, resources só têm `/F1` | C24 |
| 21_xref_declara_mais_entradas | seção `0 40` com 8 entradas | C05 |
| 22_objeto_duplicado | objeto 3 escrito duas vezes | C28 |
| 23_documento_sem_texto | páginas só com operadores gráficos | C26 |
| 24_pdf_criptografado | trailer com `/Encrypt` | C29 |
| 25_texto_utf8_cru | acentos em UTF-8 (2 bytes) dentro de fonte de 1 byte | C30 |

## Testar o gerador do lead

`driver_laudo.cpp` é um chamador do gerador (`write_pdf` de `forensic_report.hpp`) com
quatro casos sintéticos. Ele não faz parte do produto e não altera o gerador: só o compila e chama.

    g++ -std=c++17 -O1 -I. ferramentas/pdfcheck/driver_laudo.cpp forensic_report.cpp \
        -o ferramentas/pdfcheck/out/driver_laudo.exe -lcrypt32 -lbcrypt

    driver_laudo.exe out/laudo_normal.pdf normal    # caso típico, 3 páginas
    driver_laudo.exe out/laudo_grande.pdf grande    # 120 itens de evidência
    driver_laudo.exe out/laudo_vazio.pdf  vazio     # sem evidência e sem recuperados
    driver_laudo.exe out/laudo_hostil.pdf hostil    # parênteses, barra invertida, token sem espaço
    driver_laudo.exe out/laudo_acentos.pdf acentos  # acentos reais (WinAnsi)

Medido nesta máquina, com o gerador do repositório:

| laudo | páginas | texto extraído | checagens |
|---|---|---|---|
| laudo_normal | 3 | 5460 caracteres | 30 PASS, 0 FAIL |
| laudo_grande (120 evidências) | 7 | 16302 caracteres | 30 PASS, 0 FAIL |
| laudo_vazio | 2 | 624 caracteres | 30 PASS, 0 FAIL |
| laudo_acentos | 2 | 1205 caracteres | 30 PASS, 0 FAIL |
| laudo_hostil | 2 | 2036 caracteres | 29 PASS, **1 FAIL (C27)** |

Detalhe do C27 no laudo_hostil: um texto de 180 caracteres sem nenhum espaço foi escrito em
x de 50.0 até 1010.5 pt numa página de 595 pt (415 pt fora da página); uma nota de 240 caracteres
sem espaço vai até 1223.1 pt. Os acentos, os parênteses e a barra invertida passam corretos
(escapados e codificados em WinAnsi).

## Render (Edge headless) — resultado medido nesta máquina

`render.ps1` abre o arquivo no Edge, salva PNG, lê as dimensões do IHDR e mede a cor dominante
da imagem e a fração de pixels que dela diferem (uma tela chapada tem fração 0).

Medido com Edge em `C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe`:

| entrada | PNG | cor dominante | pixels fora | veredito |
|---|---|---|---|---|
| valid_classic.pdf | 10009 bytes, 1240x1754 | rgb(230,230,230) | 0 de 30225 (0%) | RENDER VAZIO (exit 2) |
| 01_xref_offset_errado.pdf | 1240x1754 | rgb(230,230,230) | 0% | RENDER VAZIO (exit 2) |
| 02_length_mentindo.pdf | 1240x1754 | rgb(51,51,51) | 0% | RENDER VAZIO (exit 2) |
| edge_page.html (controle) | 22804 bytes, 1240x1754 | rgb(255,255,255) | 179 de 30225 (0.592%) | RENDER OK (exit 0) |

Conclusão medida, não suposta: **nesta máquina o Edge headless não desenha o conteúdo de PDF**
(ele não carrega a extensão visualizadora; a tela sai chapada). O mesmo script, mesmas flags, num
HTML desenha conteúdo. Portanto o render **não serve como prova de que o laudo aparece** aqui —
e, como a tela chapada é idêntica para PDF válido e para PDF quebrado, ela também não distingue
um do outro. O PNG do render não é evidência de conformidade; a evidência é o relatório do
`pdfcheck.py`.

## O que NÃO é verificado

- Streams de imagem: `/DCTDecode` (JPEG), `/JPXDecode`, `/JBIG2Decode`, `/CCITTFaxDecode` → a
  checagem C21 sai `SKIP` com o motivo. O conteúdo de imagem não é decodificado nem medido.
- Imagem inline (`BI ... ID ... EI`): os bytes são pulados, não interpretados.
- PDF criptografado (`/Encrypt`): reprovado de propósito (C29); nenhuma outra checagem de conteúdo vale.
- Assinatura digital, `/AcroForm`, anexos, metadados `/Info`, cores, fontes embutidas (o programa
  confere que a fonte existe e tem `/Subtype`, não confere o programa da fonte).
- Fidelidade visual: nada aqui compara o PDF com uma imagem de referência.
- Texto de fonte CID **sem** `/ToUnicode`: os códigos são índices de glifo; o validador marca os
  trechos como não mapeados (U+FFFD) e a C26 sai `SKIP` dizendo que o texto não é verificável.
- Amostragem de espaço/linha na reconstrução: heurística declarada acima; `--runs` mostra os
  trechos crus com coordenadas.

## Corpus real já passado por este validador

| arquivo | tamanho | resultado |
|---|---|---|
| PDF gerado pelo Edge (Chromium, 33 objetos, fonte Type0/Identity-H com `/ToUnicode`) | 47 KB | 30 PASS, 0 FAIL; 1 página, 303 caracteres extraídos corretamente |
| Apresentação PowerPoint/Impress | 4.6 MB | 15 páginas, 2905 caracteres; 29 PASS, 1 SKIP (`/DCTDecode`) |
| APROVAR PELO CELULAR.pdf (arquivo híbrido `/XRefStm` + `/Prev`) | 361 KB | 5 páginas, 4692 caracteres; 29 PASS, 1 SKIP |
| CC EM MÃOS.pdf (híbrido) | 120 KB | 2 páginas, 963 caracteres; 30 PASS |
| CURSO APROVÇÃO 2024.pdf | 1.4 MB | 5 páginas, 6976 caracteres; 30 PASS |
| Comprovante endereço.pdf | 267 KB | 11 PASS, 6 FAIL, 13 SKIP: a tabela xref está deslocada em 1 objeto (a entrada do objeto 2 aponta para `1 0 obj`); abre em visualizador porque o leitor reconstrói o arquivo |
| Curso Básico 171 Carder....pdf | 9.9 MB | 2 PASS, 3 FAIL, 24 SKIP: não é PDF, é um arquivo texto ("WARNING: linker: ...") com extensão .pdf |
| .oracle_gallery/fixtures/docs/IMG-20260120-WA0010.pdf (fixture do próprio repositório) | 4 KB | 2 FAIL: não é PDF — tem cabeçalho `%PDF-1.7`, uma rampa de bytes e `%%EOF`, sem um único objeto |

## Checklist para quem escreve o gerador

O que mais reprova gerador feito à mão, na ordem em que aparece:

1. `/Length` de cada stream tem que ser o número exato de bytes entre `stream` + LF e o LF que
   precede `endstream`. Não conte esse EOL final.
2. Cada linha da xref: 10 dígitos, espaço, 5 dígitos, espaço, `n`, e EOL de **2 bytes** (CRLF,
   espaço+CR ou espaço+LF). EOL de 1 byte desalinha a tabela inteira.
3. O offset gravado é o do primeiro byte do `N` de `N 0 obj`.
4. `/Size` = maior número de objeto + 1 = número de entradas da tabela.
5. `/Count` do nó `/Pages` = número real de páginas; todo `/Page` precisa de `/Parent` de volta.
6. `%%EOF` depois do `startxref`; `startxref` aponta para o `x` de `xref`.
7. Fonte: declare `/Type /Font` e `/Subtype`; use `/Widths` ou um nome de fonte padrão. Se usar
   fonte com códigos de 2 bytes, emita `/ToUnicode`, senão o texto do laudo não é verificável.
8. Não emita `/Encrypt` no laudo.
