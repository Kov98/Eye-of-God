# ADR 0001 — Arquitetura Cloud (fase 0)

Status: **Aceito para a fase 0**  
Data: 2026-10-28  
Contexto: Olho de Deus é Windows/C++17, CLI estático, GUI Qt 6 e caso
local em SQLite/arquivos. Não há camada de rede cloud, OAuth, fila de jobs
ou multiusuário.

## Decisão

1. **Microsoft 365 primeiro**, via Microsoft Graph, somente leitura.
2. **Local-first**: o mesmo executável coleta e grava no caso local; o modo
   serviço/enterprise vem depois sobre a mesma biblioteca.
3. **Transporte abstrato** (`HttpTransport`) com implementação WinHTTP na
   produção e `MockHttpTransport` nos testes. Não acoplar o domínio ao
   WinHTTP.
4. **OAuth2 Authorization Code + PKCE** para uso interativo e device code
   apenas quando a política do tenant permitir. Sem senha do usuário.
5. **Vault de tokens por interface**: DPAPI/Windows Credential Manager na
   produção; memória apenas em testes. Tokens nunca entram no case DB,
   timeline, stdout ou relatório.
6. **JSON**: vendorizar `nlohmann/json` (MIT) quando o conector real começar;
   o core da fase 0 não depende de parser externo.
7. **Persistência local**: SQLite no caso atual, com schema `cloud_*` e
   migrações versionadas. Postgres/objeto store ficam para a fase enterprise.
8. **CLI primeiro**: a GUI chama os mesmos comandos/API; nenhuma lógica de
   coleta dentro do Qt.
9. **Autorização obrigatória**: coleta sem `legal_basis` registrada é
   bloqueada antes de qualquer chamada de rede.

## Alternativas consideradas

- **libcurl**: portátil, mas adiciona dependência e DLLs; WinHTTP é nativo
  no alvo Windows. A interface permite trocar depois.
- **Coleta via PowerShell/Graph SDK**: rápida para protótipo, mas difícil de
  auditar, empacotar e testar; rejeitada para o produto.
- **Servidor central primeiro**: atrasa a primeira evidência cloud e aumenta
  o escopo de segurança; rejeitado para a fase 1.
- **Guardar token no SQLite**: simples, inseguro; rejeitado.

## Consequências

Positivas:

- testes determinísticos sem tenant real;
- troca de provedor/transporte sem reescrever o domínio;
- rastreabilidade e redaction desde o primeiro commit;
- caminho claro para serviço headless.

Negativas:

- WinHTTP limita a portabilidade imediata para Linux/macOS;
- DPAPI exige usuário Windows e tratamento de roaming;
- duas implementações de vault (local/enterprise) precisam de testes;
- nlohmann/json precisa ser vendorizado e atualizado.

## Requisitos de segurança

- TLS 1.2+; nunca desabilitar validação de certificado.
- Timeout, backoff e respeito a `Retry-After`.
- Redaction de `Authorization`, `access_token`, `refresh_token`,
  `id_token`, `client_secret`, `code`, cookies e query strings sensíveis.
- Escopos mínimos e consentimento administrativo documentado.
- Auditoria append-only com hash encadeado.
- Nenhum dado real em fixtures/CI.

## Próximos passos

- implementar `WinHttpTransport` atrás da interface;
- implementar `DpapiTokenVault`;
- adicionar `nlohmann/json` vendorizado;
- criar o primeiro comando `--cloud-m365-auth`;
- testes de contrato com Graph mock.
