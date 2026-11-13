# Estado do Olho de Deus — o que está provado e o que não está

Documento de decisão, não de marketing. Cada linha da primeira seção tem **número
medido e comando reproduzível**; as seções seguintes dizem, sem rodeio, o que ainda
não foi exercitado de verdade.

Última medição: suíte completa em **13/11/2026**, `run_tests.ps1` → **43 de 43 etapas
verdes**, exit 0.

---

## 1. Provado (com o número e onde está a prova)

### 1.1 Suíte e oráculos

| Capacidade | Prova | Número |
|---|---|---|
| Suíte completa | `run_tests.ps1` | **43/43 etapas, exit 0** |
| Selo do caso (HMAC por bloco) | `.oracle\seal_test.cpp` | 71 verificações; adulterar → exit 5 |
| Selo: produto sela e detecta | estágio da suíte | `sela=True verifica=True adulterado_exit=5` |
| Selo: mídia extraída, intruso e remoção | estágio | `intacto=True intruso_exit=5` / `pasta_removida_exit=5` |
| Selo: progresso + escrita atômica | estágio | `sem_tmp=True travado_exit=5 selo_sobreviveu=True` |
| Laudo recusa selo violado | estágio | `adulterado_exit=4 nuvem_exit=4 selo_apagado_exit=4` |
| Timeline do caso | `.oracle_timeline\run_all.ps1` | **87 verificações, 0 falhas** |
| Galeria + EXIF + efêmera | `.oracle_gallery\run_all.ps1` | 12 verificações, 0 falhas |
| iOS backup + keybag | `.oracle_ios\run_all.ps1` | 73 de 73 |
| Android ADB + `.ab` | `.oracle_android\run_oracle.ps1` | 243 verificações, 0 falhas |
| Export social (ZIP/DYI) | `.oracle_social\run_all.ps1` | 96 de 96 (zip 30, dyi 65, cli 0) |
| WhatsApp ponta a ponta | `.oracle\wa_recover_e2e.ps1` | 17 verificações, 0 falhas |
| PDF: validador detecta quebra | `ferramentas\pdfcheck\selftest.py` | 25 de 25 quebras |
| PDF: conformidade do laudo | estágio da suíte | 30 PASS, 0 FAIL, 0 SKIP |
| Assinatura + carimbo + custódia | estágio | assinatura VÁLIDA, carimbo VÁLIDO, adulteração exit 1 |
| Dropbox: OAuth+PKCE+listagem / coleta / fluxo | 3 estágios | DROPBOX OK / E2E OK / FLOW OK — **transporte simulado, não a API viva** |

### 1.2 Crash de fechamento (corrigido)

| Antes | Depois |
|---|---|
| 10/25 fechamentos no binário de debug e 6/15 no de produto morriam com `0xC0000005` | **0/60** rodadas de `--screenshot`, **0/6** fechamentos reais (`WM_CLOSE`) e **0/15** aberturas em 7 casos × 2 abas = **81 fechamentos limpos** |

Prova em [CRASHES-E-BUGS.md](CRASHES-E-BUGS.md) `9: a causa era um `QHash<QDockWidget*, …>`
membro sendo lido dentro do lambda de `destroyed`, depois de destruído. Gates:
`.oracle_crash\verify_crash_fix.ps1`, `verify_close_path.ps1`, `sweep_cases.ps1`.

### 1.3 Exports (todos com sidecar, conferidos por fora)

| Export | Prova independente | Número |
|---|---|---|
| Timeline CSV | ids do CSV × `timeline.jsonl` cru | **500 × 500**, iguais |
| Timeline JSONL | **linha a linha** contra a fonte | 500 linhas, `Compare-Object` vazio |
| Galeria CSV | **sha256 de cada arquivo** recalculado no disco | 4 arquivos, 0 divergentes, 0 ausentes |
| Chat CSV | linhas + multiconjunto de textos do `conversations.json` | **351 = 243 vivas + 108 recuperadas**, 0 diferenças |
| Laudo cita recorte | `olho_report --recorte` | exit 0 citando o sha256; **1 byte alterado → exit 4 sem PDF** |
| Recorte por pasta | `--recorte <pasta>` | 2 anexos citados, exit 0 |
| Laudo gerado pela GUI | `--print-report-args` e execução dos argumentos reais | exit 0, recorte citado |

### 1.4 Desempenho medido

| Operação | Número |
|---|---|
| Selo de banco de 256 MB | 1,08 s = **238 MB/s**; verificação 0,56 s = **454 MB/s** (32 B por bloco) |
| Timeline 200 mil eventos — teto na ordem dos arquivos | **86 ms** |
| Timeline 200 mil — corte cronológico (heap) | **959 ms** (a primeira versão O(n) fazia 6.476 ms) |
| Parser puro de 200 mil eventos | **1.067 ms** |

### 1.5 Binários

`OlhoDeDeus_gui.exe` 5.620.657 · `OlhoDeDeus_deep.exe` 9.216.840 ·
`olho_report.exe` 2.444.278 · `wa_recover.exe` 1.567.574.

---

## 2. Implementado, mas **nunca exercitado de verdade**

| Item | O que existe | O que falta |
|---|---|---|
| Carimbo de tempo RFC 3161 | cliente e verificação completos | **TSA real**: hoje só `SELFTEST (nao confiavel)` |
| Conector Dropbox | OAuth+PKCE, listagem, coleta, importação | **API viva**: os testes usam transporte simulado |
| Google Drive/Photos, Telegram, Meta | código de importação e fluxo | **credenciais e app aprovado** — nunca falaram com o serviço |
| Aquisição iPhone / Android | módulos e oráculos sobre fixtures reais de backup e `.ab` | **aparelho físico**: nenhum cabo foi plugado |
| Interface gráfica | modo evidência (`--screenshot`), dumps e gates | **teste de UI interativa**: clique e arraste são sintetizados via `QMouseEvent`, não por automação de tela |
| Escala | 200 mil eventos medidos | **1 milhão+** e caso multi-provedor simultâneo |
| Concorrência | um caso por vez, um job por vez | dois casos abertos ao mesmo tempo |

## 3. Bloqueado por dependência externa (só destrava com o Kai)

1. **Carimbo real** — certificado A1/A3 (ICP-Brasil ou outra AC) + URL do TSA.
2. **Google Drive/Photos, Telegram, Meta** — credenciais OAuth e app aprovado em cada
   plataforma.
3. **Validação em hardware** — um iPhone e um Android com os respectivos cabos.

## 4. Como reproduzir

```powershell
powershell -File run_tests.ps1                            # 43 etapas
powershell -File .oracle_timeline\run_all.ps1             # 87 verificações
powershell -File .oracle_gallery\run_all.ps1              # 12 verificações
powershell -File .oracle_crash\verify_crash_fix.ps1       # 0 crash em 60 fechamentos
powershell -File .oracle_crash\verify_close_path.ps1      # 0 crash em 6 fechamentos reais
powershell -File .oracle_crash\verify_timeline_filter.ps1 # filtros, exports, laudo, clique, arraste
```

## 5. Índice dos documentos

- [CRASHES-E-BUGS.md](CRASHES-E-BUGS.md) — histórico de crash e a correção provada da seção 9
- [TIMELINE-FILTROS.md](TIMELINE-FILTROS.md) — filtros, clique, arraste, período e export da Timeline
- [EXPORTS.md](EXPORTS.md) — contrato de publicação, formatos e recital no laudo
- [COMPARACAO_OXYGEN.md](COMPARACAO_OXYGEN.md) — comparação com o concorrente
- [theme-integration-PLAN.md](theme-integration-PLAN.md) — plano do tema Visual Studio

## 6. Mudanças desta sessão

| Commit | O quê |
|---|---|
| `f2cf63c` | correção do crash de fechamento (hash de docks) |
| `18b58f1` | evidência extra: fechamento interativo e varredura de casos |
| `24c9604` | filtros da Timeline (provedor/canal/período/nuvem) |
| `af2ff20` | lista própria da Timeline, filtrada junto com o gráfico |
| `b457d18` | export CSV com sidecar de procedência |
| `575f678` | clique no gráfico seleciona o evento |
| `45bb96e` | arraste recorta o período + granularidade de minuto |
| `7b0d15c` | export JSONL (linha original) |
| `530bd7c` | laudo cita o recorte e confere o hash |
| `0316196` | correções do gate de revisão (QSaveFile, UTC, rebuilds) |
| `a34591f` | export da Galeria + caminho único de publicação |
| `ef06575` | export do Chat |
| `715f06e` | submenu Exibir > Exportar |
| `47915a9` | `--recorte` por pasta + botão de voltar o período |
| `ac0c640` | laudo da GUI cita os exports do caso automaticamente |
