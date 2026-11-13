# Exports do produto — contrato comum

Todo export que sai da GUI (Timeline, Galeria) passa por um único caminho em
`export_publish.cpp`. A regra é a mesma de um selo de caso: **publicação atômica,
hash do conteúdo e sidecar de procedência**.

## O contrato

```cpp
export_result publish_export(const QString& path, const QByteArray& bytes,
                             const QString& format, const QString& hash_key,
                             qint64 rows, const QJsonObject& extra);
```

1. **Publicação atômica** — `QSaveFile` (temporário + commit). Um export
   interrompido não deixa arquivo pela metade com o nome final, e o export anterior
   continua intacto até o commit. (`remove` + `rename`, que é o jeito óbvio, tem
   uma janela em que **nenhum** dos dois existe.)
2. **sha256 do arquivo inteiro**, em hex minúsculo, devolvido no resultado e gravado
   no sidecar.
3. **Sidecar `<arquivo>.meta.json`** com `format`, `generated_at_utc` (UTC), `rows`,
   `bytes`, `<hash_key>`, mais os campos de procedência do produtor (`filter`,
   `scope`, `payload`, `case_dir`/`directory`).

## Formatos registrados

| format | produtor | hash_key | conteúdo |
|---|---|---|---|
| `olho-timeline-csv/1` | Timeline (filtrada) | `sha256_csv` | CSV RFC 4180, BOM, CRLF: `timestamp_utc, when_local, provider, channel, event_id, record_id, from_cloud, size_bytes, sha256, raw_json` |
| `olho-timeline-jsonl/1` | Timeline (filtrada) | `sha256_jsonl` | a **linha original** de cada evento, sem envelope e sem BOM |
| `olho-galeria-csv/1` | Galeria (grade filtrada) | `sha256_csv` | `arquivo, caminho, bytes, sha256, tipo, extensao, exif_data, view_once` |
| `olho-chat-csv/1` | Chat (conversas carregadas) | `sha256_csv` | `fonte, conversa, quando, quando_epoch, texto, apagada, origem` |

O `scope` sempre diz o que o arquivo **é**: "janela carregada da Timeline (não o
banco do caso)" ou "grade da Galeria (o que está na tela)". Nenhum export se diz
"caso inteiro" quando é um recorte.

## Onde os exports nascem

Os quatro exports da GUI (Timeline CSV/JSONL, Chat, Galeria) sugerem
`<caso>/exports/` como destino, e a pasta é criada na hora. Isso não é estética:
**o laudo gerado pela GUI procura exatamente ali**.

`AcquireView::report_arguments()` monta a linha do `olho_report` e, quando
`<caso>/exports` (ou `<caso>/recovered/exports`) tem `.csv`/`.jsonl`, acrescenta
`--recorte <pasta>`. O botão de laudo loga `[laudo] os recortes exportados do caso
vão ser citados e verificados`, e divergência continua recusando o PDF (exit 4).

## Recital no laudo

`olho_report --recorte ARQUIVO` aceita qualquer um dos formatos acima: escolhe a
chave de hash pelo `format` do sidecar, calcula o sha256 do arquivo e escreve a
citação no corpo. `--recorte` também aceita uma **pasta**: ela vale como lista de
todos os `.csv`/`.jsonl` dentro dela (ordenados por nome), para um caso com vários
anexos não exigir um argumento por arquivo. Divergência **recusa o laudo com exit 4** e nenhum PDF é escrito
(`--allow-broken-recorte` existe para documentar a divergência). Ver
[TIMELINE-FILTROS.md](TIMELINE-FILTROS.md).

## Como cada um é verificado

| export | verificação no gate | resultado medido |
|---|---|---|
| Timeline CSV | 500 ids iguais aos do `timeline.jsonl` cru; sha256 do sidecar = `Get-FileHash` | 500 / 500 |
| Timeline JSONL | **linha a linha** contra o cru (`Compare-Object` vazio) | 500 / 500, 0 diferenças |
| Galeria CSV | **sha256 de cada arquivo** recalculado no disco pelo PowerShell | 4 arquivos, 0 divergentes, 0 ausentes |
| Chat CSV | linhas e **multiconjunto de textos** contra o `conversations.json` | 351 = 243 vivas + 108 recuperadas, 0 diferenças |
| recital | laudo cita; 1 byte alterado → exit 4 sem PDF | exit 0 / exit 4 |

## Limites conhecidos

- O export da Galeria **calcula o sha256 na hora** dos arquivos que ainda não estavam
  no cache. Numa pasta com milhares de fotos isso demora — é o preço de um manifesto
  que prova algo em vez de sair com a coluna vazia.
- O export da Timeline cobre a **janela carregada** (teto de exibição), não o banco do
  caso. Para o caso inteiro existe `deep::forensic::export_case_timeline()`, outro
  caminho, com outro conjunto de colunas.
- O export do **Chat** reflete o que o visualizador carregou: mensagens sem texto
  são descartadas pelo loader, e das entradas em `apagadas` o visualizador guarda o
  **último texto não vazio** de `textos[]` (os anteriores são jids). O CSV herda
  exatamente essa regra, e o gate a replica a partir do JSON para comparar.
- Export não é assinado: quem assina é o laudo. O que o export garante é
  **integridade do anexo** (sha256 conferível), não autoria.
