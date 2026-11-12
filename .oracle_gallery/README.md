# .oracle_gallery - prova de aceite da GalleryView (task-4)

Tudo aqui e ferramenta de verificacao; o produto sao ../galleryview.hpp e
../galleryview.cpp. Nenhum arquivo existente do repositorio foi modificado.

## Reproduzir tudo com um comando

    .\.oracle_gallery\run_all.ps1

Isso compila com -Wall -Wextra (falha se houver um unico warning), renderiza as
imagens base, escreve as fixtures com EXIF montado a mao, roda o oraculo e
compara o resultado com expected.json.

## Abrir a galeria (o que o lead fotografa)

    .\.oracle_gallery\build\harness.exe .\.oracle_gallery\fixtures

Variantes usadas para os prints:

    .\.oracle_gallery\build\harness.exe .\.oracle_gallery\fixtures --shot shot_grid.png
    .\.oracle_gallery\build\harness.exe .\.oracle_gallery\fixtures --shot shot_exif.png --open 10
    .\.oracle_gallery\build\harness.exe .\.oracle_gallery\fixtures --shot shot_zoom.png --open 10 --zoom 9
    .\.oracle_gallery\build\harness.exe .\.oracle_gallery\fixtures_empty --shot shot_empty.png

--open N abre o item N da lista filtrada na vista ampliada. --zoom N envia N
notches de roda de verdade (120/8 grau cada) para o widget, em vez de forcar uma
escala na mao: e por isso que a captura mostra 337% em vez dos 48% do fit.

## Arquivos

| arquivo | papel |
|---|---|
| gen_base.cpp | renderiza 4 JPEGs sem EXIF (so para haver scan data valido) |
| make_fixtures.py | monta APP1/TIFF a mao (II e MM), PNG/BMP a mao, arquivos truncados e hostis, report.json |
| exif_oracle.cpp | dump do parser + varredura real do widget na mesma pasta |
| view_once_oracle.cpp | relatorio -> marca de efemera -> pixels do selo (4 pastas) |
| scan_bench.cpp | mede varredura, primeira pintura e memoria |
| verify.py | compara o dump com expected.json e conta os campos |
| build.ps1 | compilacao com porte de warning |
| run_all.ps1 | pipeline completo |
| fixtures/ | 23 arquivos, incluindo 10 hostis |
| fixtures_empty/ | pasta sem midia, para o estado vazio |
| expected.json | o que o parser tem que devolver |
| exif_dump.txt | saida do oraculo (gerado) |
| shot_*.png | capturas (geradas) |

## Midia efemera ("ver uma vez") - contrato com o motor de recuperacao

O `wa_recover` escreve `report.json` ao lado da pasta `media/`. O contrato que a
galeria le:

```json
{
  "midia_view_once": 1,
  "midia_view_once_tabela": true,
  "midia_view_once_arquivos": 1,
  "midia": [
    {"file": "1_media1.jpg", "status": "ok", "tipo": "jpg", "bytes": 2032,
     "tabela": "message_media", "chave": "media_key", "mime": "image/jpeg",
     "origem": ".../media1.jpg.enc"},
    {"file": "2_media2.jpg", "status": "ok", "tipo": "jpg", "bytes": 2032,
     "tabela": "message_media", "chave": "media_key", "mime": "image/jpeg",
     "origem": ".../media2.jpg.enc", "view_once": true}
  ]
}
```

`file` e o basename do arquivo gravado em `media/`; `view_once` so aparece quando e
verdade. O relatorio e procurado na pasta varrida e, se nao houver, no diretorio
pai: o motor escreve em `recovered/report.json` e a GUI aponta a galeria para
`recovered/media`.

Onde a marca aparece: selo `VER 1X` no canto da miniatura, etiqueta
`MIDIA EFEMERA (VER UMA VEZ)` na vista ampliada, linha `Efemera` em vermelho no
painel, contagem `N ver-uma-vez` na barra de status e o filtro `Ver uma vez (N)`.

`view_once_oracle.exe` fecha o circuito: 12 verificacoes que cobrem relatorio ->
contagem -> **pixels** (conta os pixels do selo na captura do widget). Um caso sem
marca tem que pintar zero, senao o teste nao prova nada.
## O que foi provado

* 77 de 77 campos EXIF corretos, incluindo os dois byte orders (II e MM), GPS em
  graus/minuto/segundo convertido para decimal (S e W negativos), altitude abaixo
  do nivel do mar, e a data normalizada.
* 23 de 23 digests SHA-256 corretos contra hashlib.
* 10 arquivos hostis/truncados: nenhum derruba o processo (oraculo sai com codigo
  0) e cada um devolve um motivo legivel.
* EXIF sobrevive a truncamento do scan JPEG (truncated_scan.jpg: pixels cortados,
  EXIF lido).

## Limites conhecidos

* EXIF so para JPEG (jpg/jpeg/jpe/jfif). TIFF/HEIC/PNG nao sao lidos; o painel
  diz isso em vez de inventar campos.
* Video e audio nao tem miniatura (o projeto nao embarca decodificador): aparecem
  com selo da extensao, e a vista ampliada explica a ausencia.
* O zoom/pan foi exercitado por evento de roda sintetico (a captura de 337% prova
  a matematica); arrasto de mouse nao foi automatizado.
* O teto de 512 miniaturas em cache esta no codigo e o pior caso e aritmetico
  (512 x 160 x 120 x 4 B = 39,3 MB); nao foi medido puxando 512 miniaturas, porque
  a API publica nao expoe rolagem.

## Numeros medidos (esta maquina)

| cenario | varredura | 1a pintura | pico de RSS |
|---|---|---|---|
| 1.200 arquivos (PNG 40x30) | 168 ms | 13 ms | 44,7 MB |
| 22 fixtures (JPEG ate 1600x1200) | 10 ms | 39 ms | 67,8 MB |

python .oracle_gallery\make_fixtures.py --stress 1200 recria a pasta de estresse
que o scan_bench.exe consome.
