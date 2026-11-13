# Filtros da Timeline (aba Timeline)

Barra de filtro em cima do grafico da Timeline. Existe por um motivo pratico: um
caso de WhatsApp com 200 mil mensagens nao diz nada quando tudo vira uma mancha
roxa no grafico.

## O que a barra tem

| Controle | Efeito |
|---|---|
| combo de provedor | so os eventos daquele provedor (`WhatsApp`, `Telegram`, `Google`...) |
| combo de canal | so aquele canal dentro do provedor escolhido |
| `Periodo` + duas datas | janela de datas, limites **inclusivos**, na data **local** do evento |
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
6. **Escopo: o grafico.** O filtro age no grafico da Timeline. A aba Tabela e o
   painel Detalhes seguem mostrando a janela inteira - por isso o texto do
   contador comeca com `grafico:`.
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
(`1`/`0`). O log imprime `barra: grafico: 500 de 2519 carregados (filtro esconde 2019)`.

## Limites conhecidos

- O filtro nao persiste entre sessoes nem troca de caso junto com o layout.
- Nao existe exportar "a lista filtrada" em CSV; a exportacao existente
  (`Exibir > Timeline`) exporta a janela carregada.
- Nao ha filtro por texto/ID de evento - so provedor, canal, periodo e nuvem.
- A aba Tabela nao e filtrada (ver escopo acima); se isso virar necessidade, o
  caminho e uma tabela propria da Timeline, nao reaproveitar a tabela generica.
