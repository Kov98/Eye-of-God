# Olho de Deus — Deep Engine

Status: quinquagésimo terceiro corte. O binário original `OlhoDeDeus.exe` continua
intacto; o novo alvo é `OlhoDeDeus_deep.exe`, construído de `main.cpp`,
`deep.cpp`, `imaging.cpp`, `manifest_sign.cpp`, `wipe_hive.cpp`,
`case_db.cpp`, `artifact_*.cpp` e `artifacts_engine.cpp`.

## O que este corte entrega

### 1. Fonte raw e NTFS

- Leitura de imagem ou volume com `ReadFile` posicionado.
- Detecção automática do offset da partição NTFS.
- Boot sector, `$MFT`, fixups USA, atributos `$STANDARD_INFORMATION`,
  `$FILE_NAME`, `$DATA`, `$BITMAP`, `$INDEX_*`, runs residentes e não
  residentes, ADS e resolução de caminho com sequence number.
- Inventário `mft_inventory.jsonl` com registro, flags, timestamps, caminho,
  confiança do caminho e streams.

### 2. Recuperação de apagados

- Registros com `in_use = 0` e `$DATA` sobrevivente.
- Extração residente e não residente, inclusive ADS, com SHA-256.
- Arquivos em `recovered/` e `recovered.jsonl`.
- Comprimido/EFS é marcado como não suportado em vez de gerar lixo.

### 3. USN Journal

- Localiza `$Extend\$UsnJrnl:$J` pelo MFT.
- Lê stream esparso e interpreta `USN_RECORD_V2` e `V3`.
- `--deep-usn-file` interpreta um `$J` bruto.
- V4 (range tracking) ainda é ignorado.

### 4. Carving

- Percorre clusters livres pelo `$Bitmap`, em janelas de 8 MiB com overlap.
- Validação estrutural para JPEG, PNG, PDF, ZIP, SQLite, EVTX, Prefetch
  (`SCCA`), MP3, RIFF, MP4 e GIF.
- Slack de arquivo e slack de registro MFT.
- Saída `carved/`/`slack/` com offset, tamanho e SHA-256.

### 5. Sanitização

- `wipe-file`: arquivo ou árvore, ADS, múltiplas passadas, verificação,
  renomeação aleatória, exclusão e auditoria.
- `wipe-free`: clusters livres de imagem/volume, MFT não usado zerado,
  verificação do padrão final.
- `purge-usn`, `clear-logs`, `wipe-artifacts` (Prefetch, Recent, Jump Lists,
  WER, thumbcache) e `delete-vss`.
- `wipe-hive`: zera todas as células livres de um hive offline, incluindo
  chaves/valores apagados e blobs antigos; preserva chaves e valores em uso.
- `wipe-journal`: zera os runs de `$LogFile` e `$UsnJrnl:$J` em uma imagem
  NTFS, com auditoria.
- `wipe-browser`: sobrescreve e remove histórico, cookies, logins, Web Data,
  storage e caches de perfis Chrome/Edge; recusa perfil live com o navegador
  aberto.
- `sanitize-disk`: sobrescreve um disco físico inteiro com passes aleatórios,
  serial obrigatório, recusa disco do sistema e discos com volume montado;
  exige `--confirm WIPE-DISK` e admin.
- `nvme-sanitize`: envia o comando NVMe Admin Sanitize (block erase/crypto
  erase) via `IOCTL_STORAGE_PROTOCOL_COMMAND`; `--confirm NVME-SANITIZE`,
  serial e disco NVMe. Código compilado, não executado nesta máquina.
- `ata-erase`: ATA SECURITY ERASE PREPARE + ERASE UNIT (opcional enhanced)
  via `IOCTL_ATA_PASS_THROUGH_DIRECT`; só SATA, serial, senha ATA
  opcional, `--confirm ATA-ERASE`. Testado em VM com disco SATA virtual:
  comando aceito; apagamento físico real não é demonstrável no VirtualBox.
- `sanitize-disk` detecta SSD por seek penalty e avisa que overwrite não
  cobre firmware/overprovisioning; a auditoria grava `media`, `verified`,
  `verification_scope` e `residual_risk`.
- Tudo exige `--confirm WIPE` ou a palavra `APAGAR` e grava
  `destruction_audit.jsonl`.

### 6. Artefatos

- **Registro live** (`artifact_registry.cpp`): Run/RunOnce, RunMRU, TypedPaths,
  RecentDocs, UserAssist, USBSTOR, NetworkList, Uninstall.
- **Navegador** (`artifact_browser.cpp`): Chrome/Edge/Firefox; histórico,
  visitas e downloads; copia o banco para `%TEMP%` antes de abrir read-only.
- **Browser secrets** (`artifact_browser_secrets.cpp`): opt-in `--browser-secrets`;
  DPAPI para a chave do `Local State`, AES-256-GCM para cookies e senhas
  `v10/v11`; percorre `Default` e `Profile *`; `v20` app-bound é registrado sem
  decifrar.
- **EVTX offline** (`artifact_evtx.cpp`): `EvtQuery` por arquivo, XML,
  EventData, canal e provider; `--image-evtx` extrai os `.evtx` do MFT de uma
  imagem NTFS e importa direto para o banco.
- **Recycle Bin** (`artifact_recycle.cpp`): `$I`/`$R`, nome original, tamanho
  e FILETIME; `--recycle-recover` copia o conteúdo de `$R` para uma pasta de
  recuperação, preservando arquivos e diretórios.
- **LNK** (`artifact_lnk.cpp`): target, argumentos, diretório, ícone, hotkey;
  quando `IShellLinkW::GetPath` vem vazio, o `LinkTargetIDList` é resolvido via
  shell e preenche `target_path` (`target_source: shell_item`).
- **Jump Lists** (`artifact_jumplist.cpp`): CFB, streams LNK e entradas
  `DestList` completas — path, hostname, pinned, FILETIME, entry number e
  quatro DROIDs.
- **Prefetch** (`artifact_prefetch.cpp`): SCCA v17/23/26/30/31 e MAM/XPRESS
  Huffman; executável, hash, runs, últimos horários, volumes e arquivos;
  `--image-prefetch` extrai os `.pf` do MFT e importa direto.
- **Shellbags** (`artifact_shellbags.cpp`): BagMRU, GUID raiz, itens 0x31/0x32,
  volume 0x2F e hex do blob.
- **Hive regf** (`artifact_registry_hive.cpp`): `nk`, `vk`, `lf/lh/li/ri`,
  `db`, tipos REG_SZ/EXPAND_SZ/BINARY/DWORD/QWORD/MULTI_SZ e células livres.
- **Amcache** (`artifact_amcache.cpp`): normaliza `Root\InventoryApplication*`,
  `Root\File` e `Root\Programs`.
- **BAM/DAM** (`artifact_bam.cpp`): valores de
  `Services\bam|dam\State\UserSettings\<SID>`, FILETIME de execução, flags e
  SID; `Version`/`SequenceNumber` são descartados.
- **ShimCache** (`artifact_shimcache.cpp`): lê o valor `AppCompatCache`
  diretamente de um SYSTEM hive (`--shimcache-hive`) ou de um binário exportado
  (`--shimcache-file`); formatos Win10/11 `10ts`, Win8.x `00ts`/`10ts`, Win7
  32/64, Vista/2008 32/64 e XP; path, FILETIME, flags e execução.
- **VSS** (`artifact_vss.cpp`): enumera shadow copies com `vssadmin list
  shadows` e `--vss-list`; `--vss-read N` abre
  `\?\GLOBALROOT\Device\HarddiskVolumeShadowCopyN` pelo mesmo motor NTFS.
  Ambos exigem admin e uma sombra existente.

### 7. Banco do caso

- SQLite com `meta`, `artifacts` e `timeline`, índices por tipo, fonte e tempo.
- Toda coleta grava JSONL e, por padrão, `<saída>\case.db`.
- `--case-query <db> <sql>` devolve JSON; `--case-summary <db>` mostra contagens.


### Selo de integridade do caso

- O banco é um SQLite vivo (o VFS grava em qualquer deslocamento), então o selo
  fica **ao lado** do arquivo: um HMAC-SHA256 por bloco de 4 KiB, chaveado por 32
  bytes derivados da chave do caso (keystream CTR em `0xF0000000000`, um
  deslocamento que nunca coincide com página real). Cobre `case.db`, `case.db-journal`
  e `case.db-wal`; a verificação aponta o **número do bloco** alterado.
- **Tudo o que a ferramenta extrai para dentro do caso** ganhou selo de pasta,
  `media.seal` (o nome é histórico, do primeiro caso de uso): um SHA-256 por
  arquivo (hash em fluxo, 1 MiB por vez), recursivo, em ordem estável de caminho,
  com um HMAC da lista inteira. Cobre `recovered/` (export do `wa_recover`),
  `media/`, `cloud/` (manifestos e downloads dos conectores) e `social/`
  (Facebook, Instagram, Takeout, Telegram) — cada pasta com o seu selo.
  Detecta conteúdo trocado, tamanho diferente, arquivo removido, arquivo novo na
  pasta e arquivo renomeado; aponta o caminho relativo.
- **`case.seal`, o índice do caso**: um selo mora dentro do que ele sela, então
  apagar a pasta levava o selo junto e o caso parecia nunca ter sido selado. O
  índice fica na raiz e guarda nome + digest de **cada** selo. Com ele, selo
  apagado, pasta selada removida e selo novo fora do índice viram falha com o
  caminho apontado — e não mais ambigüidade. O índice é recusado se não houver
  nenhum selo para indexar (índice vazio daria impressão de caso coberto).
- **O selo entra sozinho no fim da aquisição**: ao terminar a recuperação de
  WhatsApp (Android), a coleta de nuvem ou a importação de export social, a GUI
  roda o selo do caso — banco, pastas e índice — sem depender de alguém lembrar do
  menu (Projeto → Selar caso). Prova não selada no minuto seguinte é prova que
  ninguém consegue provar depois.
- **O selo do banco é streaming**: o `case.db` é lido em blocos de 4 KiB, o HMAC de
  cada bloco vai direto para o `.tmp`, e o arquivo **nunca** é carregado inteiro
  para a memória. Um banco de 128 MB mostra `... 19338/32768 blocos - 75.5 MB /
  128.0 MB` (selo de 1.048.592 B = 32 B por bloco); um de 8 GB não derruba o
  processo por falta de RAM. Se o banco mudar de tamanho durante a leitura, o selo
  **não é publicado** — banco vivo não tem tamanho estável.
- **Progresso e escrita atômica**: o selo avisa `... N/M arquivos - X GB / Y GB`
  **no máximo 4 vezes por segundo** (o fim sempre aparece), com duas dimensões:
  contar só arquivos deixaria um vídeo de 8 GB parado em `1/1`; contar só bytes
  não diria quantos faltam. A GUI mostra ao vivo, linha a linha, enquanto o filho
  roda — e grava em `.tmp`
  antes de publicar com rename. Se a publicação falha — arquivo travado, disco
  cheio —, o selo **anterior continua válido e inteiro**, e o temporário é
  removido: um selo pela metade é pior que selo nenhum, porque parece
  corrupção da prova em vez do que é.
- HMAC por bloco no banco (precisa dizer *onde*) e lista de digests na mídia
  (basta dizer *qual*): 32 MB de selo para responder a mesma pergunta num vídeo
  de 4 GB não se justifica.
- **O laudo confere o selo antes de existir**: `olho_report` verifica o banco e
  **todas** as pastas seladas do caso (recuperação e aquisição de nuvem/social) e,
  se algo não confere, **recusa** imprimir (exit 4, nenhum arquivo escrito) —
  um PDF nascido de prova adulterada dá aparência de perícia ao que já foi mexido.
  `--allow-broken-seal` emite o laudo **documentando** a violação, com a frase
  "SELO DE INTEGRIDADE VIOLADO" no corpo; sem selo ou sem chave o laudo sai e diz
  isso por escrito ("ausente" / "presente, NAO verificado").
- CLI: `--case-seal/--case-check --case DIR [--media DIR]` cobrem os dois;
  `--media-seal/--media-check --media DIR --key K` selam só a mídia.
  Pasta de mídia não selada é reportada como "sem selo", não como adulteração.
### 8. Aquisição raw e assinatura

- `imaging.cpp`: raw em blocos de 4 MiB, SHA-256 incremental, split e manifesto
  `<imagem>.manifest.json`.
- `manifest_sign.cpp`: ECDSA P-256/SHA-256 com CNG. `--keygen` gera `.priv` e
  `.pub`; `--sign-manifest` grava `.sig`; `--verify-manifest` valida hash,
  fingerprint da chave e assinatura.
- `--image-create ... --sign-key <priv>` assina o manifesto após verificar a
  imagem.
- `aff4.cpp`: container **AFF4-L directory** com `version.txt`,
  `container.description`, `information.turtle`, ImageStream de 32 KiB sem
  compressão e índice big-endian; SHA-256 no RDF. `--aff4-verify` reconstrói e
  compara o hash.
- `--aff4-zip`: o mesmo container em **um arquivo ZIP64**, com headers ZIP64 e
  comentário com a URI do volume; `--aff4-zip-verify` lê o ZIP64, valida CRC e
  reconstrói o AFF4 sem depender de Python.

## Interface CLI e desktop

- **CLI**: `OlhoDeDeus_deep.exe` — menu console, 47 opções e todos os comandos.
- **GUI**: `OlhoDeDeus_gui.exe` — janela Win32 nativa, sem console; deve ficar na
  mesma pasta do CLI, pois os botões executam o CLI e capturam stdout/stderr.
- `OlhoDeDeus_deep.exe --gui` também abre a janela a partir do CLI.
- A GUI é escura, abre maximizada, usa Segoe UI, escala com a tela e trilho
  lateral: **Pericia, Avancado, Sanitizacao, Auditoria, Wipe e Kernel**.
  O log fica fixo embaixo e os campos crescem com a janela.
- Cobre scan, artefatos, raw, AFF4, E01, exposição, limpeza, VSS, EVTX/
  Prefetch de imagem, recycle, case, keygen/sign/verify, deep USN, wipe,
  kernel version/read e ajuda CLI; o campo **Comando CLI** cobre qualquer
  comando restante.
- Wipe/sanitize na GUI exigem confirmação e o checkbox de modo sanitização;
  os comandos de disco continuam também no CLI com confirmação digitada.

## E01 (libewf)

- libewf antigo compilado do fonte com MSVC (`lfcnassif/libewf_64bit`),
  usando zlib local; binários em `third_party\libewf`:
  `ewfacquire.exe`, `ewfexport.exe`, `libewf.dll`, `zlib.dll`.
- `--e01-create <origem> --out BASE` grava um **E01 real** (segmento 200 MiB).
- `--e01-export <E01> --out RAW` exporta para raw.
- `--e01-scan <E01> --out DIR` exporta para temp e roda o motor NTFS direto.
- Teste: imagem de 50 MB → **E01 de 52.451.510 bytes**; export → SHA-256
  `9c5b6fa9…a8a9`, idêntico à origem; scan → 108 registros, 18 recuperados.
- Os quatro binários precisam ficar em `libewf\\` ao lado do executável.

## Exposição residual (audit + limpeza local)

- `--exposure-audit --out DIR`: read-only; encontra OneDrive/Google Drive/Dropbox,
  caches de sync do Chrome/Edge, Windows.old, FileHistory, lixeira, pagefile/
  hiberfil, snapshots de VM, VSS e gateway padrão. Gera `exposure_report.md`
  e `exposure_audit.jsonl`.
- `--clean-local-residue --out DIR --confirm CLEAN-RESIDUE`: apaga shadow
  copies, Windows.old, FileHistory e lixeira; `--include-sync-cache` também
  remove caches de sync. Backups de ferramentas não são apagados sem decisão
  explícita.
- Checklist assistido no relatório: roteador, OneDrive/Microsoft, Google,
  Dropbox, snapshots e logs remotos.
- Snapshots de VM são detectados, mas **não apagados automaticamente**;
  exigem decisão manual. VSS/lixeira podem falhar sem admin e isso fica no log.
- Teste em VM: audit encontrou 5 itens; cleanup removeu Windows.old e
  FileHistory. VSS/lixeira retornaram erro e foram registrados como falha,
  sem inventar sucesso.

## Workstation Qt 6

- Janela `#121212`, paineis laterais `#1A1A1A`, workspace `#0A0A0A`,
  divisórias `#2D2D2D`.
- Accent **roxo** em foco/seleção (`#7A3FB3` e `#2D163D`); sem azul/ciano.
- Ferramenta ativa: fundo `#2A2A2A`, texto branco e barra esquerda roxa.
- Abas com cantos superiores arredondados, inativas `#1A1A1A`/texto `#888`,
  ativa `#0A0A0A`/branca/linha superior roxa; botão X discreto.
- Ícones coloridos distintos do VirtualBox: máquina, settings, HDD, snapshot, delete, CPU; abas de dados e ações do Kernel usam ícones próprios.
- Tabela: zebra `#0F0F0F`/`#141414`, sem linhas verticais, cabeçalho
  maiúsculo, seleção `#2D163D`, hover de **linha inteira** animado.
- Auditoria e Kernel em duas colunas, com checklist, escopo, status e
  avisos; sem formulário perdido no topo.
- Console preto com logs verdes/vermelhos/cinza; status discreto.
- Toolbar somente com ícones 20×20 e tooltips.

### Mídia de WhatsApp e mídia efêmera ("ver uma vez")

- `wa_recover.exe` abre o `msgstore.db` com a chave do aparelho, decifra o banco
  (SQLCipher, com replay de WAL), decifra os `.enc` de mídia e escreve
  `report.json` com **uma entrada por arquivo** (`midia[]`), incluindo a marca
  `view_once`.
- A marca nasce no banco: `discover_media` reconhece a coluna `view_once` e a
  tabela dedicada `message_view_once`. Como o aplicativo apaga o arquivo depois da
  primeira visualização, a linha do banco pode ser a única prova de que a mídia
  existiu.
- `wa_recover` escreve **`timeline.jsonl`** junto do export, com as mesmas chaves
  dos importadores de nuvem e rede social (`timestamp_utc`, `provider`, `channel`,
  `event_id`, `record_id`, `event_data`). A aba **Timeline** da GUI varre o caso
  inteiro atrás de `timeline.jsonl` — raiz (engine de disco), `recovered/`
  (WhatsApp), `social/` e `cloud/`. Antes ela olhava só a raiz e a nuvem, então
  uma aquisição de WhatsApp deixava a Timeline vazia mesmo com centenas de
  mensagens no caso.
- O carregamento da Timeline vive em **`timeline_index.cpp`** (com oráculo próprio
  em `.oracle_timeline/`): varre o caso inteiro, ordena **cronologicamente entre
  provedores** e devolve `total`/`truncated`. O limite de exibição é 5.000 e a GUI
  mostra `5000 de 200000 eventos (limite de exibicao)` — nunca o número cortado
  como se fosse o todo. Medido: 5.000 eventos em **85 ms**, 200 mil parseados sem
  limite custariam **1,14 s**, e por isso acima do teto o carregador só conta.
- **A Timeline carrega em thread própria, com progresso e seleção preservada**: o
  worker publica um contador de linhas por átomo e o timer mostra
  `carregando timeline... 120.000 linhas`; ao terminar, a linha que o operador
  estava olhando é reselecionada (a tabela é reconstruída do zero). Antes,
  "carregar mais" jogava a seleção para o topo. O resultado volta por QTimer (mesmo
  padrão da coleta e do import social) e a janela nunca congela — o corte por data
  custa ~1 s em 200 mil eventos. Pedido de carga durante outra carga fica pendente
  em vez de bloquear ou se perder; o destrutor faz join da thread.
- **Paginação por ação, sem recarregar o caso**: `Exibir → Timeline: carregar mais`
  multiplica o teto por 5 (5.000 → 25.000 → 125.000 → 200.000) e chama só
  `refresh_timeline()` — tabela, contador, gráfico e mensagens; manifesto, árvores e
  galeria não são relidos. O limite vale por caso.
- **Dois modos de corte**, escolhidos no menu `Exibir`:
  - *ordem de arquivo* (padrão): para de interpretar ao encher a página — **86 ms**
    em 200 mil eventos, mas a primeira página não é "os mais antigos do caso";
  - *por data* (`Timeline: cortar por data`): interpreta o caso inteiro e mantém os N
    mais antigos numa heap máxima — **959 ms** em 200 mil eventos, primeira página
    é o começo da história. A primeira versão varria a janela a cada troca e levava
    **6.476 ms**; a heap resolveu.
- Na Galeria: selo `VER 1X` na miniatura, etiqueta `MIDIA EFEMERA (VER UMA VEZ)`
  na vista ampliada, linha `Efemera` em vermelho no painel, contagem na barra de
  status e filtro `Ver uma vez (N)`.
- `wa_recover.exe` passou a ter alvo próprio no `build_deep.ps1` (antes era
  construído à mão e ficava fora de qualquer build reproduzível).


## Cloud / Enterprise — fase 1

O binário principal agora expõe:

```text
OlhoDeDeus_deep.exe --cloud-m365-auth --tenant ID --client-id ID [--no-browser] [--mock-http]
OlhoDeDeus_deep.exe --cloud-m365-collect --case DIR --account UPN --tenant ID \
  --vault-key KEY --legal-basis REF --job ID [--since YYYY-MM-DD] [--until YYYY-MM-DD]
```

A GUI ganhou a ferramenta **Cloud**, com autenticação, coleta, escopo, base legal
e job. O token fica no DPAPI; a coleta é read-only e grava manifesto SHA-256,
raw pages e timeline cloud em `cases/<id>/cloud/m365/<account>/`.

Ainda não houve coleta em tenant real; o fluxo completo foi validado contra o
mock Graph local (`cloud_auth` + `cloud_collect` + GUI). A GUI já mescla as
timelines cloud no caso e o coletor grava `cloud/jobs/<job>.json` com status,
checkpoint e erros. O CLI tem `--cloud-verify`, e a GUI tem **Atualizar status** e **Verificar evidencia**.

A ferramenta Cloud tem login local com PBKDF2, trilha de auditoria com hash chain
e login OIDC com PKCE, callback loopback e verificacao RS256 do ID token;
o administrador inicial e criado no primeiro acesso e a identidade fica em
`%LOCALAPPDATA%\OlhoDeDeus\identity.json`. Admin pode criar/ativar usuários
e trocar papéis pela janela **Usuarios**, no mesmo tema escuro do restante da interface. O core tambem valida OIDC RS256,
mas o login OIDC na GUI ainda nao esta ligado. Falta servico headless.

## Headless service

`cloud_service.exe --case DIR --port 8766 [--tls-cert cert.pem --tls-key key.pem]` escuta somente 127.0.0.1 e expõe
`/health`, `GET /v1/config` (p?blico, issuer/client OIDC), `/v1/session`, `/v1/jobs`, `/v1/queue` (e `GET /v1/config` público para issuer/client OIDC), `/v1/metrics`, `/v1/metrics/prometheus`, `/v1/audit` e `/v1/verify`.
O serviço aceita `--rate-limit N` e, para OIDC, exige `--oidc-issuer URL` +
`--oidc-client-id ID`; o JWKS fica em cache por 1 h, e uma rotação de chave força refresh
automático na próxima validação; sem configuração,
o endpoint OIDC retorna 503.
O `cloud_worker.exe --case DIR` consome a fila com prioridade e `--max-parallel`, lock atômico, lease/fencing e executa o coletor **in-process**, atualizando
o status de cada job. O login
local retorna bearer token de 8 h; sessões ficam em memória. Detalhes em
`cloud/service/README.md`.

## Build atual

| Arquivo | Tamanho | SHA-256 |
|---|---:|---|
| `OlhoDeDeus_deep.exe` | 8.076.595 | `2cddcd2235b3bf826f2a848a2cb69e326509d1124a58fecfc15d0c551f38d1c7` |
| `OlhoDeDeus_gui.exe` | 1.905.769 | `24b5bd65ce9b3c15187001f4b86e2b100e8e0b587a483f618471fbc1ca9d3530` |
| `cloud_service.exe` | 12.490.518 | `965b618052bf09588568c79f01d73ee47186b3cfbf6f2538e24e978b3d99b6b7` |
| `cloud_worker.exe` | 4.205.053 | `425b3d168ec197db1e6e654b831eb5d2e4b546e8867a98cf3688b7b374d6fab4` |
## Importar cloud no case DB

```text
OlhoDeDeus_deep.exe --cloud-db-import --case <case-dir> [--db <case.db>]
```

O importador cria/usa `case.db`, grava tenant, conta, autorização, job,
objetos, artefatos e anexos (metadados ou binário inline) e atualiza o índice SQL em uma
transação. O manifesto e os hashes originais continuam sendo a fonte de
verdade. A coleta aceita `--download-attachments` (inline ou `$value` com allowlist de hosts, até 50 MB); a GUI
tem o checkbox **Baixar anexos inline**. A GUI expõe **Importar DB**, **Consultar DB** e **Objetos DB** (tabela
filtrável de message/file/attachment/raw_page/event), recarrega o caso após
importar, e `--case-summary` mostra cloud_jobs, cloud_objects e cloud_artifacts.

## Hardening recente

- o worker coleta in-process, sem CreateProcess, argv ou stdin de segredo;
- a GUI enfileira coletas no store DPAPI do worker; o botão "Coletar" virou **Enfileirar coleta**, sem spawnar CLI;
- a GUI autentica M365 in-process (PKCE + callback + DPAPI), sem CLI;
- a GUI conecta ao `cloud_service` por HTTP local, enfileira via `/v1/queue` (e `GET /v1/config` público para issuer/client OIDC), lista a fila, cancela via `DELETE /v1/queue/{id}` e pausa/retoma via `POST .../pause|resume`;
- a fila guarda `vault_ref` opaco; o segredo fica no `SecretStore` DPAPI local;
- lease tem geração/fencing, heartbeat e cancelamento por evento nomeado (marcador como fallback);
- anexos usam streaming em disco, com SHA-256 incremental e limite configurável (padrão 2 GiB);
- importador verifica SHA-256/tamanho reais, rejeita symlink/junction para fora do caso e reporta objetos/artefatos existentes;
- rate limit, auditoria e discovery OIDC têm proteções adicionais;
- schema v4 exige um artefato por objeto, valida SHA-256 por trigger e a API valida tipos JSON (400);
## Secret GC

```powershell
OlhoDeDeus_deep.exe --secret-gc (--case <caso> | --cases-root <dir>) [--older-days 7] [--force]
```

Mantém as `vault_ref` referenciadas pela fila do caso e remove refs órfãs
mais antigas que o limite. `--force` usa idade zero.

> [!warning]
> O store é global por usuário. Rode com cautela se houver vários casos;
> refs de outros casos não são preservadas automaticamente.
## Relatório cloud

```text
OlhoDeDeus_deep.exe --cloud-report --case <case-dir> --out <arquivo> [--format html|json]
```

O relatório é gerado localmente a partir de manifesto, timeline, jobs e
verificação SHA-256; não faz rede. A GUI expõe **Relatorio**, que salva e abre
o HTML/JSON.
## Release assinado

```powershell
.\build_release.ps1 -PrivateKey .\release.priv -PublicKey .\release.pub
```

O script:

- recalcula `SHA256SUMS.txt`;
- gera `release_manifest.json`;
- cria ZIP em `releases/<timestamp>/`;
- grava `.zip.sha256`;
- assina o ZIP com ECDSA P-256;
- verifica a assinatura com a chave pública antes de concluir.

Sem chave, o release é gerado apenas com hashes. O par de chaves não é
distribuído pelo produto; o operador deve gerar e custodiar o seu.

## CI e release gates

```powershell
.\ci\build_and_test.ps1
.\ci\build_and_test.ps1 -Package -PrivateKey <release.priv> -PublicKey <release.pub>
.\ci\verify_release.ps1 -PublicKey <release.pub>
```

## Authenticode

```powershell
.\ci\sign_authenticode.ps1 -Files <exe> -PfxPath <codesign.pfx>
.\ci\verify_authenticode.ps1 -Directory dist_full -ExpectedThumbprint <thumb>
```

## Repositório

Commit inicial: `697002c` (416 arquivos).
Builds, releases, casos, VM, análise e material de chave são ignorados.
