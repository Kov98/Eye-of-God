# AGENTS.md — Eye of God

## Stack real deste projeto (verificado)

| Camada | Tecnologia | Volume |
|---|---|---|
| Core | **C++** | 1584 `.h`, 116 `.cpp` |
| UI desktop | **Qt Widgets** (`QApplication` em `mainwindow.cpp`) | — |
| Tooling / web | **TypeScript** | 1002 `.ts` |

**Não existe QML neste repositório** (0 arquivos `.qml`). Por isso a skill
`qt-qml` não se aplica aqui — usar `qt-cpp-review` e `qt-ui-design`.

## Skills obrigatórias por tipo de tarefa

Carregue a skill **antes** de escrever o código, não depois:

| Vou fazer | Carregar primeiro |
|---|---|
| **Deixar a UI parecida com o Visual Studio** | **`vs-fidelity-ui`** |
| Mexer em `.cpp`/`.h` do core ou da UI Qt | `qt-cpp-review` |
| Criar/auditar tela, layout, navegação | `qt-ui-design` |
| Polir UI, estados, micro-interação, espaçamento | `emil-design-eng` |
| Criar ou revisar animação | `animate` (criar) / `review-animations` (criticar) |
| Documentar classe ou arquivo C++ | `qt-cpp-docs` |
| Mexer no frontend `.ts` | `emil-design-eng` + `bexa` |
| Design vem do Figma | `figma-design-system` |
| Antes de commitar | `qt-cpp-review` como gate |

> **Alvo visual do projeto: fidelidade ao Visual Studio 2022 dark.**
> A paleta medida, as três camadas de fundo, a densidade e os trechos de QSS
> estão na skill `vs-fidelity-ui`. Não invente cores nem bordas: os valores
> foram extraídos por amostragem de pixel de um VS real.

Se a tarefa tem mais de uma linha da tabela, carregue todas as listadas — não
escolha só a mais óbvia.

## Regra de qualidade

1. **Compila antes de dizer pronto.** Não inferir de leitura de código.
2. **Gate de review.** `qt-cpp-review` roda 60+ regras determinísticas; corrija
   `[high]` e `[blocker]`, justifique o resto.
3. **UI tem evidência.** Screenshot ou medição (`design-audit`), não "achei bom".
4. **Animação tem propósito.** Se a resposta é "fica bonito", use
   `remove-animations`.

## Design

O design system canônico está no Figma:
`https://www.figma.com/design/GQ1NJwAXrEPaZ5zFVeLcpg` (`fileKey: GQ1NJwAXrEPaZ5zFVeLcpg`)

Tokens, componentes e o passo a passo estão na skill `figma-design-system`.
Nunca traduza design olhando screenshot quando o arquivo Figma existe.

## Notas

- Decisões técnicas vão para o vault Obsidian (vault `dsh`, tag `#decisao`).
- Servidores MCP disponíveis nesta sessão: `figma`, `notion`, `obsidian`,
  `devtools`, `wincu`.
