# Filtros da Timeline (aba Timeline)

Barra de filtro em cima do grafico da Timeline. Existe por um motivo pratico: um
caso de WhatsApp com 200 mil mensagens nao diz nada quando tudo vira uma mancha
roxa no grafico.

## O que a barra tem

| Controle | Efeito |
|---|---|
| combo de provedor | so os eventos daquele provedor (`WhatsApp`, `Telegram`, `Google`...) |
| combo de canal | so aquele canal dentro do provedor escolhido |
| `Periodo` + duas datas | janela de **data e hora** (minuto), limites **inclusivos**, no horario **local** do evento |
| `So nuvem` | so eventos vindos de `cloud/` |
| `x` | limpa o filtro inteiro |
| contador a direita | numero curto (`500 de 2519`); tooltip e log com o texto completo |

## Semantica (o que o filtro promete)

1. **Filtro de apresentacao.** Ele reprojeta o que ja esta em memoria
   (`timeline_result_`) e nao rele o caso: trocar de provedor e instantaneo mesmo
   com 200 mil linhas carregadas.
2. **Comparacao sem caixa.** Os produtores nao combinam maiusculas entre si
   (`WhatsApp` no `wa_recover`, `whatsapp` num export), entao a comparacao ignora
   caixa.
3. **Data local, limites inclusivos.** `from` e `to` comparam a data local do
   evento; um evento as 23h de 14/11 em UTC pode ser 14/11 ou 15/11 local, e quem
   manda e o fuso da maquina que produziu o laudo.
4. **Evento sem data.** Sem janela de periodo ele continua visivel (nao ha criterio
   para descartar). Com janela ligada ele sai e fica **contado** em `no_time`, que
   aparece no tooltip: `... / 3 sem data fora do periodo`.
5. **O denominador e o que esta carregado.** O contador diz `500 de 2519` onde 2519
   e a janela carregada (teto de exibicao, 5.000 por padrao), nao
   necessariamente o total do caso. O total do caso continua no rotulo da Tabela e
   na linha `[case] timeline: ...` do console.
6. **Escopo: a linha do tempo.** O filtro age no grafico E na lista da propria
   aba Timeline (a lista mostra todos os eventos que passaram; o grafico amostra
   para desenhar). A aba Tabela continua com a janela inteira, porque ela tambem
   lista artefatos do sistema de arquivos. Selecionar uma linha da lista alimenta o
   painel Detalhes e o Hex View.
7. **Grafico vazio tem duas causas, com textos diferentes.** "Nenhum evento de
   timeline neste caso" so aparece quando `timeline.jsonl` nao existe; quando o
   filtro e que escondeu tudo, o cartao diz "O filtro escondeu os 2.519 eventos
   carregados" e ensina a limpar.

## Medicao independente

Fixture `.oracle_crash/filtro_caso`: 2.519 eventos (2.019 de WhatsApp em 14/11 e
500 de Telegram em `cloud/telegram` em 15/11). Os numeros esperados vieram de
PowerShell lendo o `timeline.jsonl` cru - nao do codigo C++:

| filtro | esperado (PowerShell) | GUI (`barra:` no log) |
|---|---|---|
| nenhum | 2519 | 2519 eventos, sem filtro |
| `provider=Telegram` | 500 | 500 de 2519 carregados |
| `channel=WhatsApp/Messages` | 2019 | 2019 de 2519 carregados |
| `from=2023-11-15;to=2023-11-15` | 500 | 500 de 2519 carregados |
| `cloud=1` | 500 | 500 de 2519 carregados |
| `from=2023-11-20;to=2023-11-20` | 0 | 0 de 2519 carregados |

Oraculo do modulo: `powershell -File .oracle_timeline\run_all.ps1` ->
**40 verificacoes, 0 falhas** (16 delas do filtro), com `-Wall -Wextra` sem warning.

## Como repetir pela linha de comando

O modo evidencia aceita o filtro na mesma especificacao que a barra usa, aplicada
nos mesmos widgets (nao existe caminho paralelo de teste):

```powershell
.\OlhoDeDeus_gui.exe --screenshot saida.png --case .oracle_crash\filtro_caso ^
    --tab Timeline --filter "provider=Telegram"
```

Chaves aceitas: `provider`, `channel`, `from`, `to` (`yyyy-MM-dd`), `cloud`
(`1`/`0`). O log imprime `barra: timeline: 500 de 2519 carregados (filtro esconde 2019)`.

## Clicar no grafico

Clicar numa barra seleciona o evento correspondente na lista - e a lista alimenta
o painel Detalhes e o Hex View. Antes o grafico era so desenho: um pico as 09:18
nao levava a lugar nenhum.

- **Tolerancia de 8 px** e vale o evento **mais proximo**, nao o primeiro da
  varredura.
- Clicar no vazio **nao escolhe nada** (devolve -1). Num laudo, abrir o evento
  errado e pior que nao abrir nada.
- O ponteiro vira mao quando ha evento embaixo (affordance antes do clique).
- O grafico desenha no maximo ~80 pontos (amostragem), a lista mostra todos: o
  clique devolve a **linha da lista**, nao o indice do ponto amostrado.

Desenho e clique usam a mesma conta de `timeline_geometry.hpp` (area, primeiro
instante e janela). Duas copias divergiriam na primeira correcao, e o defeito
seria silencioso - o grafico continuaria bonito e o clique passaria a abrir o
vizinho. Os 13 checks de geometria do oraculo cobrem bordas, tolerancia, empate e
vazio.

## Zoom no eixo do tempo (roda do mouse)

A roda sobre o grafico aproxima/afasta o tempo em torno do ponteiro (1,25x por
passo). A ancora (o instante sob o cursor) fica parada, o eixo passa a rotular a
**janela visivel** e o evento fora dela nao e desenhado.

**Zoom e lente, nao filtro.** Ele nao altera o periodo da barra, nao muda a lista e
nao muda o export: a lista continua com os 500 eventos filtrados e o CSV/JSONL
continua sendo "a lista filtrada". Confundir os dois faria um gesto de navegacao
alterar evidencia exportada — que e exatamente o tipo de coisa que nao pode
acontecer num laudo.

Limites: minimo de **1 s** de janela, maximo = a janela do caso (aproximar ate o
fim e o mesmo que voltar). O botao **↺** (voltar o periodo a janela do caso) tambem
**desfaz o zoom** — os dois significam "voltar a ver o caso inteiro".

## Arrastar no grafico recorta o periodo

Arrastar o botao esquerdo no grafico (cursor vira cruz sobre a area) aplica o
intervalo de tempo coberto na janela de periodo da barra. O retangulo do arraste
aparece enquanto o botao esta pressionado.

- **Regra de 3 px**: menos que isso e clique (abre o evento), mais que isso e
  arraste. Decidir no `press` obrigaria a escolher entre um gesto e outro antes do
  operador terminar o movimento.
- **Arrastar da direita para a esquerda vale igual** (as bordas sao ordenadas).
- Arraste que sai da area e **limitado a janela** do caso.
- O botao **↺** ao lado do `✕` volta o **periodo** a janela do caso (primeiro ao
  ultimo evento) mantendo provedor/canal/nuvem — o `✕` zera tudo, e as vezes o
  operador só quer desfazer o recorte.
- A precisao e de **segundo** no filtro e de **minuto** no campo (o campo mostra
  `yyyy-MM-dd HH:mm`); o intervalo exato vai no console: `[timeline] arraste:
  periodo 2023-11-15 05:00:00 -> 2023-11-15 05:51:02`.

### Por que o filtro passou a ter hora

Antes `Periodo` era por dia. Num caso de WhatsApp com 2.519 eventos em duas horas,
filtrar por dia nao recorta nada - e arrastar no grafico seria decorativo. A janela
(`timeline_filter::from/to`) agora e `QDateTime` e compara o instante local do
evento. A granularidade do campo e minuto; o do arraste, segundo.

## Exportar o que esta filtrado (CSV)

Menu **Exibir > Timeline: exportar o que esta filtrado (CSV)**. Sai um `.csv` com
o que a lista mostra e um `.meta.json` ao lado com a procedencia.

**Colunas:** `timestamp_utc, when_local, provider, channel, event_id, record_id,
from_cloud, size_bytes, sha256, raw_json`.

- A coluna `sha256` e o hash do **conteudo cru** da linha - o mesmo hash que o
  painel Detalhes mostra para o evento. Planilha e tela podem ser conferidas uma
  contra a outra sem abrir o app.
- `raw_json` vai junto: o CSV e a evidencia, nao um resumo dela.
- Todo campo sai entre aspas com as internas dobradas (RFC 4180), BOM UTF-8 e
  CRLF - abre no Excel sem virar ANSI e sem quebrar em virgula.
- Grava em `.tmp` e publica por rename: export interrompido nao deixa CSV meio
  escrito com nome final.

**Sidecar `.meta.json`:** formato (`olho-timeline-csv/1`), `generated_at_utc`,
`case_dir`, `rows`, `bytes`, `sha256_csv`, `filter` (a especificacao aplicada) e
`scope` = "janela carregada da Timeline (nao o banco do caso)".

### JSONL (para reprocessar)

Menu **Exibir > Timeline: exportar o que esta filtrado (JSONL)**. Sai a **linha
original** de cada evento, uma por linha, sem envelope e **sem BOM** - nada e
re-serializado, entao quem reprocessa recebe exatamente o que o produtor gravou.

- E o formato conferivel: o gate compara o arquivo exportado com as linhas do
  `timeline.jsonl` cru, **linha a linha** (`Compare-Object` vazio). O CSV nao
  permite isso porque reinterpreta os campos.
- Fim de linha normalizado para `\n` (o leitor da Timeline descarta o EOL da
  fonte, entao CRLF viraria ambiguedade).
- O sidecar `.meta.json` e o mesmo do CSV, com `sha256_jsonl`, `rows`, `filter`,
  `scope` e `payload` dizendo que o conteudo e a linha original.
- Lista vazia gera arquivo vazio (0 byte) valido, nao um arquivo com cabecalho.

**Por que nao reusa o export do banco:** `deep::forensic::export_case_timeline()`
exporta o **caso inteiro** a partir do `case.db`. Este exporta a **janela
carregada e filtrada**. Sao coisas diferentes, e um CSV que dissesse "caso
inteiro" enquanto o operador tem um filtro ligado seria um laudo errado.

## Levar o recorte para o laudo

O `olho_report` aceita um ou mais `--recorte ARQUIVO` (o `.csv` ou `.jsonl`
exportado, com o `.meta.json` ao lado):

```powershell
.\olho_report.exe cases\meu-caso --out laudo.pdf --recorte saida\timeline-filtrada.csv
```

Para cada recorte o laudo:

1. le o sidecar e escolhe a chave certa (`sha256_csv` ou `sha256_jsonl`, pelo
   `format` registrado);
2. calcula o **sha256 do arquivo** e compara com o sidecar;
3. escreve no corpo: `Recorte anexado: <arquivo> - 500 evento(s), filtro
   provider=Telegram, sha256 CONFIRMADO (1779ba3bc643...)` e, na linha seguinte, o
   `scope` do recorte ("janela carregada da Timeline, nao o banco do caso").

**Divergencia recusa o laudo**: exit **4**, nenhum PDF escrito, com a mensagem do
que falhou (arquivo ausente, sidecar ausente/ilegivel, ou hash diferente). E a
mesma regra do selo de integridade, e pelo mesmo motivo: citar um anexo que nao
bate da aparencia de prova a um recorte que ja foi mexido. Para DOCUMENTAR a
divergencia existe `--allow-broken-recorte`, que emite o laudo com a marca
`RECORTE ANEXADO DIVERGENTE` no corpo.

Verificado pelo gate: laudo com recorte sai e cita o hash; recorte com um byte
alterado e recusado com exit 4 sem PDF; recorte JSONL usa `sha256_jsonl`.

## Limites conhecidos

- O filtro nao persiste entre sessoes nem troca de caso junto com o layout.
- Nao existe exportar "a lista filtrada" em CSV; a exportacao existente
  (`Exibir > Timeline`) exporta a janela carregada.
- Nao ha filtro por texto/ID de evento - so provedor, canal, periodo e nuvem.
- A aba Tabela nao e filtrada (ver escopo acima); se isso virar necessidade, o
  caminho e uma tabela propria da Timeline, nao reaproveitar a tabela generica.
