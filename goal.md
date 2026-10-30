# Goal: Cloud + Enterprise

Status: **fase 1 concluída e fase 2 iniciada em 2026-10-28; tenant real pendente**
Produto: Olho de Deus / Deep Engine
Base: C++17, CLI Windows, GUI Qt 6, SQLite, coleta local e sanitização.

## 1. Objetivo

Evoluir o Olho de Deus de uma estação forense local para uma plataforma
**cloud-aware e enterprise-ready**, sem perder o que já é diferencial:

- aquisição forense Windows transparente;
- CLI auditável;
- sanitização/exposição no mesmo produto;
- execução local quando não houver infraestrutura.

O alvo não é copiar o Oxygen inteiro. É entregar, em fases:

1. coleta read-only de **Microsoft 365** e **Google Workspace**;
2. normalização de evidência cloud no mesmo caso/timeline;
3. operação multiusuário com RBAC, auditoria e jobs;
4. deployment central (serviço headless + API) e local.

## 2. Baseline atual
Fase 0 já entregue: `cloud/core` (retry, redaction, vault em memória, proveniência), `WinHttpTransport`, `DpapiTokenVault`, PKCE/OAuth core, callback loopback, `cloud_auth` interativo, Graph client paginado e `cloud_collect` M365; teste end-to-end contra mock com perfil, mailbox, OneDrive, manifesto SHA-256 e timeline. O case DB cria as tabelas cloud_* (schema v2), o CLI/GUI importam manifesto/timeline em transação (incluindo metadados de anexos), a GUI consulta/recarrega o DB e lista objetos cloud em tabela, o coletor baixa anexos inline ou via `$value` com allowlist (até 50 MB) em CLI, GUI, fila e worker e o case-summary conta cloud. Há relatório HTML/JSON e viewer de objetos cloud; ainda não há IdP/tenant real. Auth e coleta M365 estão integrados ao CLI principal e à ferramenta Cloud da GUI. A timeline cloud já é mesclada no caso principal, o coletor grava `cloud/jobs/<job>.json` com status/checkpoint e a GUI/CLI têm verificação de manifesto e status do job. O coletor retoma fontes parciais pelo checkpoint nextLink, preservando timeline/manifesto. A UI atualiza o status do job a cada 1,2 s, tem pausa/retomada via NtSuspendProcess, cancelamento, histórico de jobs, recarga automática do último job e reload do caso/timeline ao terminar a coleta. RBAC, OIDC RS256, usuários locais PBKDF2 e sessões em memória existem; a GUI já tem login local, gates RBAC, gestão de usuários/papéis, trilha de auditoria com hash chain e login OIDC com ID token verificado, tudo no mesmo tema. A sessão local é persistida com DPAPI, restaurada no início e invalidada se o usuário for desativado; auditoria exporta com SHA-256. Serviço headless local, fila com lease/retry e worker existem; TLS direto, OIDC no serviço, fila priorizada, métricas de fila/workers, Prometheus, rate limit e worker com lease/concorrência local existem; falta validar com IdP real e operar em tenant real. Faltam login OIDC na GUI, persistência de sessão, serviço headless e API., schema SQLite, mock Graph sintético, ADR e contrato de CLI. O WinHTTP foi validado contra o mock local; ainda não há chamada real ao Graph/OAuth.

Já existe:

- CLI estático `OlhoDeDeus_deep.exe`;
- GUI Qt 6 com worktree, tabela, Hex View, timeline e detalhes;
- caso local em `cases/<case_id>/` com `manifest.json`, `timeline.jsonl`
  e relatórios;
- aquisição Windows: raw, VSS, NTFS/MFT, USN, registry, browser, EVTX,
  Prefetch, JumpList, LNK, Recycle, SRUM, Amcache, ShimCache;
- E01/AFF4, hashes SHA-256, manifesto e assinatura de manifesto;
- auditoria de exposição local e sanitização;
- console com logs coloridos e exit code.

Não existe hoje:

- OAuth2, Graph API, Google Workspace API;
- coleta de mailbox, OneDrive/Drive, Teams, SharePoint ou audit log;
- armazenamento seguro de tokens;
- fila de jobs, retry/rate-limit e cancelamento;
- multiusuário, RBAC, SSO ou servidor central;
- criptografia em repouso no caso;
- paginação completa do timeline e índice de busca;
- laudo PDF/DOCX e cadeia de custódia assinada ponta a ponta.

## 3. Escopo inicial

### 3.1 Cloud — primeiro provedor

**Microsoft 365 / Microsoft Graph** como primeiro conector.

Escopo read-only inicial:

- perfil da conta e tenant;
- mailbox: mensagens, pastas, anexos e metadados;
- OneDrive: arquivos, versões e metadados;
- Teams/SharePoint: canais e arquivos (fase 1b);
- audit log unificado (fase 1b).

Escopos OAuth somente leitura, sujeitos a consentimento administrativo e
autorização legal. Nada de senha do usuário, bypass de MFA ou credential
harvesting.

### 3.2 Cloud — segundo provedor

**Google Workspace**:

- usuários e unidades;
- Gmail (metadados + mensagens autorizadas);
- Drive (arquivos, permissões e versões);
- audit logs;
- OAuth com delegação administrativa quando aplicável.

### 3.3 Enterprise

- casos múltiplos e persistentes;
- usuários, papéis (`admin`, `examiner`, `reviewer`, `auditor`);
- login local + OIDC/SAML;
- trilha de auditoria imutável (quem, o quê, quando, de onde);
- fila de jobs com retry, rate limit, cancelamento e retomada;
- criptografia em repouso para evidência e tokens;
- API REST versionada para automação;
- serviço headless para laboratório;
- backup/restore e retenção configurável.

## 4. Fora de escopo nesta fase

- quebrar criptografia ou contornar consentimento;
- aquisição mobile iOS/Android;
- drone, satélite ou rádio;
- marketplace de plugins de terceiros;
- treinamento de modelos de IA generativa;
- prometer cobertura de "todos os apps" do mercado.

Esses itens podem entrar depois, com dataset, demanda e validação próprios.

## 5. Arquitetura alvo

```text
cloud/
  core/                 # OAuth2, HTTP, retry, rate limit, redaction
  vault/                # tokens via DPAPI/Windows Credential Manager
  connectors/
    m365/               # Microsoft Graph
    google/             # Google Workspace
  normalize/            # evidência cloud -> modelo do caso
  service/              # worker headless + fila
  api/                  # REST versionada
  tests/
    fixtures/           # respostas gravadas, sem dados reais
    mock_server/        # servidor local para CI
```

### 5.1 Princípios

- **Read-only por padrão**; nenhuma ação de escrita em tenant.
- **Token nunca em log, timeline, stdout ou case DB em claro.**
- **Toda chamada externa tem timeout, retry com backoff e rate limit.**
- **Toda evidência tem hash, origem, horário e ID de job.**
- **Nenhum teste de CI usa tenant real ou dado pessoal.**
- **Falha parcial é registrada, não transformada em sucesso.**
- **CLI primeiro**: a GUI consome os mesmos comandos/API.

### 5.2 Modelo de dados

Novas tabelas/entidades:

- `cloud_tenant`: provedor, tenant id, região, consentimento;
- `cloud_account`: usuário, UPN/email, ids externos;
- `cloud_job`: tipo, escopo, status, tentativas, checkpoint;
- `cloud_object`: mailbox/file/message/event, id externo, pai, tamanho;
- `cloud_artifact`: conteúdo + hash + content-type + caminho no store;
- `cloud_permission`: ACL/ sharing;
- `cloud_audit`: chamadas, escopos, resultado, erro sanitizado.

Campos de proveniência obrigatórios: `provider`, `tenant`, `account`,
`external_id`, `collected_at_utc`, `api_endpoint`, `etag/version`,
`sha256`, `job_id`, `legal_basis_ref`.

### 5.3 Segurança

- OAuth2 Authorization Code + PKCE para interactive; device code apenas
  quando a política permitir.
- Tokens no **DPAPI** por usuário Windows; no servidor, KMS/HSM ou
  segredo gerenciado.
- Redaction de `Authorization`, `access_token`, `refresh_token`, cookies
  e query strings sensíveis.
- TLS 1.2+ obrigatório; certificate pinning opcional.
- Criptografia do case store (AES-256-GCM) com chave por caso.
- Logs de auditoria encadeados por hash.
- Permissões mínimas; consentimento administrativo documentado.

### 5.4 Legal / compliance

Antes de coletar:

- base legal registrada (consentimento, ordem judicial, política interna);
- identificação do custodiante e do tenant;
- escopo e período;
- retenção e descarte;
- região/jurisdição e transferência internacional;
- aprovação do responsável legal.

O produto deve **bloquear** coleta sem referência de autorização registrada.

## 6. Roadmap

### Fase 0 — Fundação e decisões (1–2 semanas)

Entregáveis:

- ADR de arquitetura cloud;
- threat model e modelo de dados;
- escolha do cliente HTTP/JSON;
- layout `cloud/` + targets de build;
- especificação do comando `cloud-m365-collect`;
- dataset de fixtures sintéticas;
- política de autorização e retenção.

Critério de saída:

- nenhum código de rede ainda; build atual continua verde;
- revisão de segurança aprovada.

### Fase 1 — Conector Microsoft 365 read-only (3–5 semanas)

Entregáveis:

- OAuth2 device code em tenant de teste;
- vault DPAPI;
- cliente Graph com retry/429/backoff;
- coleta de perfil, mailbox e OneDrive;
- export para `cases/<id>/cloud/m365/`;
- manifesto + SHA-256 por objeto;
- CLI `--cloud-m365-auth`, `--cloud-m365-collect`, `--cloud-m365-status`;
- logs sanitizados e exit codes;
- testes com mock Graph e fixtures.

Critério de saída:

- coleta reproduzível em tenant de teste;
- zero token em log;
- retomada após falha;
- manifesto verificável;
- nenhum write no tenant.

### Fase 2 — UI cloud e timeline (2–3 semanas)

Entregáveis:

- wizard de conexão/consentimento;
- seleção de usuário, escopo e período;
- job com progresso, pausa, cancelamento e retry;
- resultados na worktree, tabela, timeline e detalhes;
- status de consentimento e expiração;
- exportação de relatório cloud.

Critério de saída:

- operador consegue executar tudo sem CLI;
- token não aparece na UI;
- falhas aparecem com código e mensagem acionável.

### Fase 3 — Google Workspace + enterprise core (4–6 semanas)

Entregáveis:

- conector Google;
- usuários/papéis e login OIDC;
- trilha de auditoria imutável;
- fila de jobs persistente;
- case store criptografado;
- API REST inicial;
- modo serviço headless.

Critério de saída:

- dois examinadores no mesmo caso com papéis distintos;
- auditoria de todas as ações;
- jobs sobrevivem a restart;
- API documentada com OpenAPI.

### Fase 4 — Hardening e operação (3–4 semanas)

Entregáveis:

- MSI/instalador, assinatura de código;
- backup/restore;
- métricas e health check;
- testes de carga;
- documentação de operação e resposta a incidentes;
- validação funcional documentada;
- revisão de licenças e compliance.

## 7. Backlog priorizado

| Prioridade | Item | Valor |
|---|---|---|
| P0 | ADR, threat model, schema cloud | evita retrabalho |
| P0 | OAuth2 + vault DPAPI | base de qualquer conector |
| P0 | Graph read-only + fixtures | primeira evidência cloud |
| P0 | redaction e auditoria | evita vazamento de token |
| P1 | UI wizard + progresso | usabilidade enterprise |
| P1 | Google Workspace | segundo provedor |
| P1 | RBAC + OIDC + audit trail | enterprise |
| P1 | case store criptografado | requisito de segurança |
| P2 | API REST + service headless | integração/automação |
| P2 | relatório PDF/DOCX cloud | court-readiness |
| P2 | Teams/SharePoint/audit log | cobertura M365 |
| P3 | mobile/cloud apps de terceiros | somente com demanda |

## 8. Testes e validação

- **Unitários**: normalização, redaction, retry, checkpoint, hashing.
- **Integração**: mock server local; fixtures de Graph/Google; sem rede real.
- **Contrato**: schemas JSON, OpenAPI, migrações SQLite/Postgres.
- **Segurança**: token nunca em log; TLS; DPAPI; permissões de arquivo.
- **Carga**: mailbox grande, rate limit, retomada, memória.
- **Legal**: bloqueio sem autorização; retenção; exportação mínima.
- **Manual**: tenant de teste; operador independente; checklist.

## 9. Métricas de sucesso

- 100% das chamadas externas com timeout/retry/rate limit;
- 0 tokens em logs ou case DB em claro;
- 100% dos objetos com SHA-256 e proveniência;
- retomada de job sem duplicar evidência;
- coleta M365 de 1 usuário (mailbox + OneDrive) concluída em tenant de teste;
- 2 papéis funcionando com auditoria;
- instalação limpa em VM sem dependências não documentadas.

## 10. Riscos

| Risco | Impacto | Mitigação |
|---|---|---|
| Consentimento/admin approval demorado | atrasa conector | tenant de teste e app registrado cedo |
| Mudança de API Graph/Google | quebra parser | versionamento, fixtures, testes de contrato |
| Token vazado | crítico | DPAPI/KMS, redaction, revisão de segurança |
| Volume de dados | performance | streaming, paginação, store por objeto |
| Requisito legal/jurisdição | bloqueio | autorização registrada e retenção configurável |
| Escopo amplo demais | nada entrega | fases e critérios de saída |
| Dependência de lib HTTP | build/deploy | decisão ADR e bundle documentado |
| Dados reais em teste | violação | fixtures sintéticas e mock server |

## 11. Decisões necessárias

1. **Primeiro provedor:** Microsoft 365 (recomendado) ou Google Workspace?
2. **Deployment inicial:** local-only (recomendado) ou serviço central?
3. **Tenant de teste:** existe tenant Microsoft 365/Google disponível e
   autorizado para desenvolvimento?
4. **Base legal:** qual modelo de autorização será exigido no MVP?
5. **Licenciamento:** o produto será distribuído? Isso afeta ícones GPLv3,
   Qt LGPL e dependências.

## 12. Definição de pronto da fase Cloud/Enterprise

- Um caso pode conter evidência local **e** cloud no mesmo timeline.
- Um operador consegue autenticar, escolher escopo, coletar, pausar,
  retomar e verificar a coleta sem tocar em token.
- Todo objeto cloud tem hash, origem, job e base legal.
- Falhas parciais são visíveis e não geram falso sucesso.
- Um segundo usuário com papel de revisor consegue auditar o caso.
- Build, instalador e documentação permitem reproduzir o ambiente.

## 13. Próxima ação imediata

Começar a **Fase 0**:

1. criar `cloud/` e ADR `docs/adr/0001-cloud-architecture.md`;
2. definir schema cloud e migração;
3. criar mock server + fixtures M365;
4. especificar comandos CLI;
5. implementar `cloud_core` (OAuth abstraction, HTTP, retry, redaction)
   com testes unitários, sem chamar tenant real.

O primeiro código não deve ser um conector completo: deve ser a fundação
segura que permite plugar M365 e Google sem reescrever o produto.

## Pendências de código fechadas

- vault key: stdin + DPAPI na fila;
- lease: geração/fencing, heartbeat e cancelamento;
- symlink/junction: canonical containment;
- reimport: contadores de existentes;
- anexos: streaming até 2 GiB;
- rate limit, auditoria e OIDC endurecidos.

## Fase de arquitetura

- worker coleta in-process; sem processo filho/argv/segredo via stdin;
- schema v4 com índice único e triggers de SHA-256;
- API valida tipos JSON;
- cancelamento por página/anexo;
- fila DPAPI e streaming 2 GiB.

- GUI enfileira toda coleta no store DPAPI via JobQueue; botão antigo de coleta direta removido;

- cancelamento por evento nomeado + fallback por marcador;
