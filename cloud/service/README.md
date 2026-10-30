# Cloud service (headless, loopback)

API local read-only para operação do módulo Cloud. O processo escuta apenas
`127.0.0.1` e não deve ser exposto diretamente à rede.

## Build

```powershell
.\build_service.ps1
```

Saída: `cloud_service.exe`.

## Run

```powershell
.\cloud_service.exe --case C:\casos\OLHO-001 --port 8766 --tls-cert cert.pem --tls-key key.pem
```

Opções:

- `--case DIR` — diretório do caso (obrigatório)
- `--port N` — porta loopback, padrão `8766`
- `--identity FILE` — padrão `%LOCALAPPDATA%\OlhoDeDeus\identity.json`
- `--audit FILE` — padrão `%LOCALAPPDATA%\OlhoDeDeus\audit.jsonl`
- `--rate-limit N` — requisições/minuto por bearer (padrão 120)
- `--oidc-issuer URL` / `--oidc-client-id ID` — habilitam OIDC
- sem OIDC configurado, `/v1/session/oidc` retorna `503`
- `--tls-cert/--tls-key` — habilita HTTPS

## Autenticação

`POST /v1/session` com usuário/senha local retorna um bearer token de 8 h.
Todos os endpoints exceto `/health` e o próprio login exigem:

```text
Authorization: Bearer <token>
```

Sessões vivem em memória; reiniciar o serviço exige novo login.

## Endpoints

| Método | Rota | Permissão |
|---|---|---|
| GET | `/health` | pública |
| POST | `/v1/session` | credenciais locais |
| POST | `/v1/session/oidc` | ID token OIDC |
| DELETE | `/v1/session` | sessão |
| GET | `/v1/jobs` | `ViewCase` |
| POST | `/v1/jobs` | `RunCollection` |
| GET | `/v1/queue` | `ViewCase` |
| DELETE | `/v1/queue/{id}` | `CancelJob` |
| GET | `/v1/metrics` | `ViewCase` |
| GET | `/v1/metrics/prometheus` | `ViewCase` |
| GET | `/v1/audit` | `ViewAudit` |
| POST | `/v1/verify` | `VerifyEvidence` |

### Login local

```json
{"username":"admin","password":"..."}
```

### Login OIDC

```json
{"issuer":"https://issuer","client_id":"client","id_token":"..."}
```

O serviço faz discovery/JWKS e valida RS256, issuer, audience e expiração.
Discovery e JWKS ficam em cache por 1 h para reduzir dependência de rede.
Se a validação falhar por chave ausente/assinatura, o serviço força um
refresh do JWKS e tenta novamente uma vez (rotação de chave sem restart).
Issuer e client ID são fixos na configuração do serviço; um corpo com
valores diferentes retorna `400`.
Papel padrão: `Reviewer`; claims `roles`/`groups` podem elevar o papel.

### Métricas

`GET /v1/metrics` retorna totais da fila (`queued`, `running`, `completed`,
`failed`, `cancelled`, leases expirados), workers online/stale/stopped e
integridade da auditoria. `.../prometheus` retorna o mesmo em formato
Prometheus 0.0.4 (`text/plain`).

### Verify

```json
{"account":"analista@example.test"}
```

`account` é opcional; sem ele, todos os manifestos M365 do caso são verificados.

## Worker

```powershell
.\cloud_worker.exe --case C:\casos\OLHO-001
```

A fila usa lock de arquivo exclusivo e escrita atômica; o worker lê `cloud/queue`, aplica lease, executa
`OlhoDeDeus_deep.exe --cloud-m365-collect`, grava `completed`/`failed` e faz retry com backoff até `max_attempts`.
Use `--watch` para polling contínuo, `--max-parallel N` para concorrência local e `--deep EXE` para apontar outro coletor.

## Segurança

- bind fixo em `127.0.0.1`
- rate limit fixo por token (429 com `retry_after`)
- nenhuma senha ou token é escrito em log
- nenhum token aparece na resposta de jobs/auditoria
- não há endpoint de coleta, wipe ou escrita no tenant
- TLS opcional com `--tls-cert/--tls-key`; sem TLS, use apenas localmente ou atrás de reverse proxy
- a API não substitui a GUI para operações destrutivas

## Limitações

- não há persistência de sessão do serviço
- a fila durável é consumida pelo `cloud_worker.exe` local; não há worker remoto
- não há rate limit distribuído
- auditoria local não é WORM
