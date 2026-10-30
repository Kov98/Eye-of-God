# Cloud CLI — especificação da fase 0/1

Binário: `OlhoDeDeus_deep.exe`  
Nenhum comando cloud escreve no tenant. Todos exigem autorização registrada.

## Autenticação interativa (fase 1)

Enquanto o comando `--cloud-m365-auth` não é integrado ao binário principal,
o fluxo está disponível como `cloud_auth.exe`:

```text
cloud_auth.exe --tenant <tenant-id> --client-id <app-id> \
  --scope "User.Read Mail.Read Files.Read.All" \
  --vault-key <case>/<user> [--no-browser]
```

O CLI abre o navegador (ou imprime a URL com `--no-browser`), recebe o
callback em `127.0.0.1`, valida o `state`, troca o code por token via PKCE e
salva no DPAPI. Nenhum token ou code é impresso.

Para mock local, use `--mock-http` e endpoints explícitos. Nunca use
`--mock-http` contra tenant real.
## Autenticação Microsoft 365

```text
OlhoDeDeus_deep.exe --cloud-m365-auth \
  --tenant <tenant-id> \
  --client-id <app-id> \
  --scopes "User.Read Mail.Read Files.Read.All" \
  --vault-key <case-id>/<user>
```

Regras:

- OAuth2 Authorization Code + PKCE no fluxo interativo.
- Device code somente se `--allow-device-code` for explícito.
- `client_secret` nunca em argumento; usar `OLHO_M365_CLIENT_SECRET` ou
  cofre do sistema.
- O token é salvo no vault; o case DB guarda apenas a referência.
- Saída: status, escopos concedidos, expiração; nunca o token.

## Coleta interativa (fase 1)

Enquanto `--cloud-m365-collect` não é integrado ao binário principal:

```text
cloud_collect.exe --case <case-dir> --account <upn> --tenant <tenant-id> \
  --vault-key <key> --legal-basis <reference> --job <id> \
  [--since YYYY-MM-DD] [--until YYYY-MM-DD] [--max-pages 50]
```

O token vem do DPAPI; a coleta grava `cloud/m365/<account>/raw/`,
`manifest.json` e `timeline.jsonl`. Falhas por fonte tornam o resultado
`partial` e retornam exit code 6.
## Coleta Microsoft 365

```text
OlhoDeDeus_deep.exe --cloud-m365-collect \
  --case <case-dir> \
  --tenant <tenant-id> \
  --user <upn> \
  --out <case-dir>/cloud/m365 \
  --since 2026-01-01 \
  --until 2026-01-31 \
  --legal-basis <reference> \
  --job <job-id>
```

Escopos iniciais:

- `User.Read` — perfil;
- `Mail.Read` — mensagens e metadados;
- `Files.Read.All` — OneDrive;
- `AuditLog.Read.All` — fase 1b, somente com consentimento.

O comando deve:

1. validar autorização e escopos antes da rede;
2. criar checkpoint de job;
3. paginar com `@odata.nextLink`;
4. respeitar `Retry-After`, 429 e 5xx;
5. gravar cada objeto com SHA-256 e proveniência;
6. registrar falhas parciais sem marcar sucesso;
7. emitir `manifest.json`, `timeline.jsonl` e `cloud_audit.jsonl` no caso.

## Status e verificação

Comando integrado no binário principal:

```text
OlhoDeDeus_deep.exe --cloud-verify --case <case-dir> [--account <upn>]
```

Valida cada arquivo do manifesto contra o SHA-256, tamanho, base legal,
job journal e timeline; retorna 7 se houver divergência.

```text
OlhoDeDeus_deep.exe --cloud-status --case <case-dir>
OlhoDeDeus_deep.exe --cloud-verify --case <case-dir>
```

`--cloud-verify`:

- recalcula SHA-256 dos artefatos;
- valida o encadeamento do audit log;
- mostra jobs incompletos, falhas e escopos usados;
- retorna código diferente de zero se algo não conferir.

## Exit codes

| Código | Significado |
|---:|---|
| 0 | sucesso completo |
| 2 | argumentos inválidos |
| 3 | autorização ausente/expirada |
| 4 | consentimento/escopo insuficiente |
| 5 | falha de rede/API após retries |
| 6 | falha parcial; verificar manifesto |
| 7 | verificação de integridade falhou |

## Segurança de logs

Nunca imprimir:

- `Authorization`, `access_token`, `refresh_token`, `id_token`;
- `client_secret`, `code`, cookies;
- corpo de resposta com dados pessoais em nível debug.

O log de auditoria registra ação, caso, tenant, job, resultado, código de
erro e timestamp — não o token.
