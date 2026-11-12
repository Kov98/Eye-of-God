# Achados sobre o gerador do lead (`forensic_report.cpp`)

Medições feitas chamando `write_pdf` pelo `driver_laudo.cpp` com casos sintéticos,
e conferindo o PDF resultante com o `pdfcheck.py`. Nenhum arquivo do gerador foi alterado.

Compilação usada (g++ 16.1.0, ucrt64):

    g++ -std=c++17 -O1 -I. ferramentas/pdfcheck/driver_laudo.cpp forensic_report.cpp \
        -o ferramentas/pdfcheck/out/driver_laudo.exe -lcrypt32 -lbcrypt

## Resumo

| caso | páginas | texto extraído | checagens de conformidade |
|---|---|---|---|
| normal (14 evidências, 9 recuperados, 2 notas) | 3 | 5460 caracteres | 30 PASS, 0 FAIL |
| grande (120 evidências, 40 recuperados) | 7 | 16302 caracteres | 30 PASS, 0 FAIL |
| vazio (sem evidência, sem recuperados) | 2 | 624 caracteres | 30 PASS, 0 FAIL |
| acentos reais (WinAnsi) | 2 | 1205 caracteres | 30 PASS, 0 FAIL |
| hostil (parênteses, barra invertida, token sem espaço) | 2 | 2036 caracteres | 29 PASS, **1 FAIL (C27)** |

Estrutura do PDF: aprovada nas 30 checagens em todos os casos, menos o C27 do caso hostil.
xref com entradas de 20 bytes, offsets corretos, `/Length` exato em todos os streams,
`/Count` e `/Parent` corretos, `/Size` coerente, fonte declarada e usada, texto dentro da
página nos casos normal/grande/vazio/acentos.

## Defeito 1: texto sem espaço vaza a página (C27)

Um texto de 180 caracteres sem nenhum espaço em `recovered_item.detail` é escrito de
x = 50.0 até x = 1010.5 pt numa página de 595 pt — 415 pt fora da página, à direita.
Uma nota de 240 caracteres sem espaço vai até x = 1223.1 pt (628 pt fora).

Números conferidos com a métrica AFM da Helvetica: 180 caracteres 'X' a 8 pt, largura 667/1000
por glifo, dão 180 * 0.667 * 8 = 960.5 pt; 50.0 + 960.5 = 1010.5. A quebra de linha do gerador
só corta em espaço, então um token único atravessa a margem.

Consequência: no laudo assinado esse trecho é cortado pelo leitor (não aparece) ou aparece fora
da página. Um hash, um caminho de arquivo longo ou uma URL produzem esse caso na prática.

O que o validador aponta:

    [FAIL] C27 texto estimado cabe dentro da MediaBox da pagina
            pagina 2: texto 'XXXX...' em x[50.0..1010.5] y[573.0..581.0]
            sai da MediaBox x[0.0..595.0] y[0.0..842.0]

## Defeito 2: acento vira '?' no texto de recuperados e nas notas

Entrada:

    acao, coracao, pericia, laudo, evidencia, Jose, Sao Paulo, conclusao, endereco, unico
    (com acentos reais: ação, coração, perícia, ...)

Extraído do PDF:

    a??o, cora??o, per?cia, laudo, evid?ncia, Jos?, S?o Paulo, conclus?o, endere?o, ?nico
    - Conclus?o: n?o houve viola??o do lacre.

9 das 10 palavras ficam com '?'. São bytes 0x3F no arquivo, não é falha do extrator: o caminho
que escreve `recovered_item.detail` e `notes` troca todo byte > 126 por '?'.

O mesmo gerador trata os outros textos corretamente: em `case_info` e no caminho de evidência os
acentos saem em WinAnsi de 1 byte (0xE3 para ã, 0xE7 para ç), que é o correto para a fonte
base-14 com `/WinAnsiEncoding`. Ou seja: duas políticas diferentes no mesmo arquivo, e a que
vale para o texto das provas recuperadas perde o dado.

Isso não é erro de sintaxe PDF: nenhuma checagem de conformidade pega. Só aparece comparando o
texto extraído com o texto de entrada.

## O que passou (e por que importa)

- Escapes de string literal: entrada `Caso (2026)/0001 \ teste` chega ao PDF e sai na extração
  como `Caso (2026)/0001 \ teste`. Parênteses e barra invertida escapados `\(`, `\)`, `\\`.
- Acentuação em `case_info` e caminhos: WinAnsi de 1 byte, correto para fonte base-14.
- 120 itens de evidência e 40 recuperados: 7 páginas, `/Count 7`, todos os `/Parent` corretos,
  nenhum texto fora da página.
- Documento vazio: continua sendo um PDF 1.4 válido, com as duas páginas de cabeçalho/encerramento.
- Paginação e rodapé ("Caso 2026-0001 pagina N de M") consistentes com o número real de páginas.
- Nenhum stream com `/Filter` (o gerador não comprime), então `/Length` é o único contrato de
  tamanho e ele está exato nos 3+3+3+1+2 streams gerados.

## Como reproduzir

    cd ferramentas/pdfcheck
    python make_fixtures.py          # fixtures do próprio validador
    ..\..\ferramentas\pdfcheck\out\driver_laudo.exe out\laudo_hostil.pdf hostil
    python pdfcheck.py out\laudo_hostil.pdf
