# Cloud / Enterprise — fase 0

Este diretório é a fundação do Olho de Deus para coleta cloud read-only e
operação enterprise. O plano completo está em [`../goal.md`](../goal.md) e a
decisão arquitetural em
[`../docs/adr/0001-cloud-architecture.md`](../docs/adr/0001-cloud-architecture.md).

## Estado atual

- `core/cloud_core.*`: transporte abstrato, retry/backoff, redaction, token
  vault em memória, proveniência e auditoria.
- `core/winhttp_transport.*`: transporte WinHTTP real, TLS padrão, sem
  redirects, com smoke test contra o mock.
- `core/dpapi_token_vault.*`: cofre DPAPI por usuário Windows.
- `core/oauth_pkce.*`: PKCE S256, authorize URL, troca de code e refresh
  contra transporte mock; sem client secret.
- `core/oauth_callback.*`: listener loopback 127.0.0.1 para receber o code.
- `core/rbac.*`: papéis admin/examiner/reviewer/auditor e permissões
  explícitas (collect, cancel, verify, audit, manage users/connectors).
- `core/oidc.*`: discovery OIDC, JWKS e validação RS256 de JWT
  (issuer, audience, exp, nbf, clock skew, kid).
- `enterprise/identity.*`: usuários locais com PBKDF2-HMAC-SHA256
  (120k iterações, salt por usuário) e papéis.
- `enterprise/session.*`: sessões em memória com token aleatório,
  validade e revogação; só o SHA-256 do token fica no mapa.
- `enterprise/audit.*`: log JSONL append-only com hash chain SHA-256,
  detecção de edição/reordenação e visualização na GUI.
- GUI OIDC: discovery, PKCE, callback loopback, ID token RS256 e
  mapeamento de roles via claims `roles`/`groups`.
- `enterprise/session_vault.*`: sessão DPAPI por usuário, restaurada
  no início e apagada no logout.
- `cli/cloud_auth.cpp`: fluxo interativo completo com DPAPI e state.
- `connectors/m365/graph_client.*`: Graph read-only com paginação.
- `connectors/m365/m365_collector.*`: profile + mailbox + OneDrive,
  manifesto SHA-256, timeline cloud, job journal e retomada parcial
  por `nextLink`.
- `cli/cloud_collect.cpp`: coleta usando token do DPAPI.
- `tests/cloud_core_test.cpp`: testes unitários sem rede.
- `tests/mock_graph_server.py`: Microsoft Graph sintético.
- `tests/fixtures/`: dados 100% sintéticos.
- `schema/001_cloud.sql`: schema SQLite inicial.
- `CLI.md`: contrato dos comandos cloud.

**Ainda não existe** chamada a tenant real, conector Google, UI cloud ou
serviço headless. Auth e coleta M365 foram executados de ponta a ponta contra
o mock local; o próximo passo é apontar para um tenant de teste autorizado.

## Build e testes

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build_cloud.ps1
```

Auth de teste contra o mock (não use dados reais):

```powershell
.\cloud_auth.exe --tenant test --client-id client --scope User.Read `
  --vault-key test/mock --no-browser --mock-http `
  --authorize-endpoint http://127.0.0.1:8765/authorize `
  --token-endpoint http://127.0.0.1:8765/oauth/token
```

Coleta M365 contra o mock:

```powershell
.\cloud_collect.exe --case C:\temp\case --account analyst@example.test `
  --tenant tenant-1 --vault-key test/mock --legal-basis test-warrant `
  --job job-1 --base-url http://127.0.0.1:8765/v1.0 --mock-http
```

O CLI imprime a authorize URL e espera o callback em 127.0.0.1. O token
é salvo no DPAPI; nada é impresso.

O script compila `cloud_core`, `winhttp_transport`, `dpapi_token_vault` e
o smoke client; executa a suíte. Não requer tenant, credencial ou internet.
O smoke client é executado manualmente contra o mock, como no teste abaixo.

## Mock Graph

```powershell
python cloud\tests\mock_graph_server.py --port 8765
.\cloud_transport_smoke.exe http://127.0.0.1:8765/v1.0/me
```

Rotas sintéticas:

- `GET /v1.0/me`
- `GET /v1.0/me/messages`
- `GET /v1.0/me/drive/root/children`
- `POST /oauth/token` (code `demo-code`)\n- `GET /health`

Todas exigem `Authorization: Bearer test-token`. O servidor não registra
headers, justamente para não vazar o token de teste.

## Regras que não podem regredir

1. Nenhum token em log, case DB, timeline ou stdout.
2. Toda chamada externa tem timeout, retry e rate limit.
3. Toda evidência tem hash, origem, job e base legal.
4. Nenhum dado real em fixture/CI.
5. Nenhum write no tenant.
6. Coleta sem autorização registrada é bloqueada antes da rede.

## Estado da fase 1

Auth e coleta M365 foram executados de ponta a ponta contra o mock, incluindo
retomada de uma fonte parcial a partir do checkpoint:

- PKCE + callback loopback + state;
- token salvo no DPAPI;
- Graph read-only com paginação;
- perfil, mailbox e OneDrive;
- manifesto com SHA-256, base legal, job e status;
- timeline cloud com mensagens e arquivos;
- nenhum token no manifesto/log.

## Próximo corte

1. integrar `cloud_auth`/`cloud_collect` ao CLI principal;
2. wizard cloud na GUI;
3. mesclar timeline cloud no caso/timeline principal;
4. job persistente, pausa/retomada e checkpoint;
5. tenant Microsoft 365 real autorizado.