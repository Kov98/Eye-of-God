# Crashes e bugs conhecidos — Olho de Deus (GUI)

> **Status deste documento: registro apenas.** Kai pediu, em 05/11/2026, que nada
> fosse corrigido agora — só documentado. Nenhuma linha de código foi alterada
> depois desse pedido. Este arquivo é a memória do problema para quando a
> correção for autorizada.

---

## 1. Build documentado

| Campo | Valor |
|---|---|
| Binário | `OlhoDeDeus_gui.exe` |
| Tamanho | 4.551.935 bytes |
| Gravado em | 2026-11-05 20:45:04 |
| SHA-256 | `9d3b8cf6f7d49a30794d5c66b0fe2405eb7ef97074180ce4335397691adad642` |
| Qt | 6.12.0.0 (MSYS2 UCRT64, `C:\msys64\ucrt64\bin`) |
| Build | `build_deep.ps1` (g++ 16.1, UCRT64) |

Todo diagnóstico futuro deve citar o SHA-256 acima. Sem isso não se sabe qual
código falhou.

---

## 2. Resumo do histórico (30 dias, fonte: Log de Eventos do Windows)

**29 falhas do `OlhoDeDeus_gui.exe` em 30 dias, com 9 assinaturas distintas.**

| Ocorrências | Exceção | Módulo + deslocamento | Leitura |
|---|---|---|---|
| 7x | `0xc000041d` | `USER32.dll +0x173b9` | exceção não tratada **dentro de um callback** (procedimento de janela). Típico de crash dentro de um slot Qt chamado pelo Windows. |
| 7x | `0xc0000005` | `USER32.dll +0x173b9` | mesmo ponto de despacho, sem a marca de callback. |
| 5x | `0xc0000005` | `Qt6Widgets.dll +0x18a1d8` | violação de acesso dentro do Qt Widgets (widget inválido, ponteiro pendurado). |
| 3x | `0xc0000005` | `Qt6Widgets.dll +0x19383b` | idem — **inclui as duas falhas de hoje, 20:55 e 20:57**. |
| 3x | `0xc0000374` | `ntdll.dll +0x112165` | **corrupção de heap** detectada pelo alocador. É a família mais grave: o defeito real pode estar a horas de distância do ponto que estoura. |
| 1x cada | `0xc0000005` | `OlhoDeDeus_gui.exe +0x20bea / +0x20eba / +0x20e56 / +0x20fca` | falha dentro do nosso próprio código (4 ocorrências, todas em 05/11 de manhã). |

Distribuição por dia:

```
2026-10-28: 19
2026-10-29:  1
2026-10-30:  1
2026-10-31:  1
2026-11-04:  1
2026-11-05:  6
```

### O que isso já descarta

O defeito **não** nasceu do trabalho de nuvem/GUI de hoje à noite:

- a assinatura `Qt6Widgets.dll +0x19383b` já aparecia em **04/11 12:57**, antes de qualquer alteração minha nesta sessão;
- as 4 falhas dentro do exe são de **05/11 pela manhã** (09:30–12:14), também antes;
- as falhas de USER32 e a corrupção de heap são de **outubro**.

O que **não** pode ser descartado sem dump: uma interação nova (por exemplo, o
timer de autorização que criei) pode ter mudado a frequência de um defeito que
já existia. Frequência não é causa.

---

## 3. As duas ocorrências de hoje (20:55:26 e 20:57:15)

Detalhe do log:

```
Nome do aplicativo com falha: OlhoDeDeus_gui.exe
Módulo com falha: Qt6Widgets.dll, versão 6.12.0.0
Código de exceção: 0xc0000005
Deslocamento de falha: 0x000000000019383b
```

Contexto honesto: as duas aconteceram **durante as minhas tentativas de abrir e
fechar o app por script**, enquanto eu procurava a janela para te mostrar a
página Cloud. Encerramento forçado (`Stop-Process`) normalmente **não**
gera relatório APPCRASH — então a leitura mais provável é crash real, mas não
posso afirmar qual dos dois eventos foi espontâneo e qual foi induzido por mim.

Sintoma observado no momento: uma janela de **70×43** ainda com o título padrão
`OlhoDeDeus_gui` (antes de `setWindowTitle` ser aplicado), que
desapareceu em seguida. Isso aponta para falha **durante a construção da
interface**, não depois dela pronta.

Contraponto: uma instância posterior (pid 9384) ficou **45 s viva** sem cair.
Ou seja, a falha não é determinística por inicialização.

---

## 4. Lacuna de evidência (por que ainda não dá para apontar a linha)

- **Não existe dump retido**: `C:\ProgramData\Microsoft\Windows\WER` tem
  `dumps=0`. Os `.dmp` citados nas mensagens eram temporários e já
  foram apagados.
- **Não há símbolos**: o build não usa `-g`, e os binários são
  `-static` para as bibliotecas do sistema, mas o Qt vem em DLL, então os
  deslocamentos apontam para dentro da Qt e não para nosso código.
- Consequência: hoje só é possível dizer *onde* quebrou (módulo + offset), não
  *por quê*.

### Para tornar diagnosticável (quando autorizado, não agora)

1. Habilitar dump local, uma chave de registro, sem tocar no código:
   `HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps`
   com `DumpFolder` e `DumpType=2` (dump completo).
2. Recompilar com `-g -O1` e manter o `.exe` correspondente para os
   símbolos.
3. Abrir o dump no gdb (`gdb OlhoDeDeus_gui.exe arquivo.dmp`) e ler a pilha
   do thread que falhou.

---

## 5. Hipóteses (não verificadas — ordem de probabilidade)

Nenhuma foi testada. Ficam registradas para não se perder o raciocínio.

| # | Hipótese | Por que faz sentido | Teste mais barato |
|---|---|---|---|
| H1 | Ponteiro de widget pendurado: um `QWidget*` guardado em membro continua sendo usado depois de o widget pai ser destruído | explica as 8 falhas em `Qt6Widgets.dll` e as 14 em USER32 (despacho para slot com `this` inválido) | rodar com dump + `QT_FATAL_WARNINGS`, e uma varredura de `delete`/`deleteLater` em `mainwindow.cpp` |
| H2 | Corrupção de heap por escrita fora de limite em algum parser (cópias de buffer) | as 3 falhas `0xc0000374` são a assinatura clássica | recompilar com ASan (`-fsanitize=address`) e exercitar as telas |
| H3 | Uso de widget fora da thread da interface | encaixa em USER32/callback e em qualquer trabalho que eu tenha movido para thread | procurar `std::thread`/`QThread` que toquem em `QWidget*` |
| H4 | Ciclo de vida na saída: destrutor juntando thread, ou widget destruído antes do timer | explicaria falha ao fechar, não ao abrir | fechar o app 20× e contar crashes |
| H5 | Algo no carregamento do caso (mesmo caminho de dados em todas as telas) | o app sempre abre com caso carregado nesta máquina | abrir sem caso e comparar |

---

## 6. Bugs de comportamento já observados por mim (não crashes)

1. **Painel Cloud — troca de provedor não verificada visualmente.** O seletor
   `Microsoft 365`/`Dropbox` troca campos e botões, mas eu nunca consegui
   capturar o estado Dropbox: o foco do Windows ficou com outro aplicativo e o
   envio de teclas foi recusado. Pode haver erro de layout não visto.
2. **O provedor não persiste.** Reabrir o app volta para Microsoft 365, mesmo que
   o último job inspecionado fosse Dropbox.
3. **`cloud_apply_provider()` esconde linhas de dois formulários específicos**
   (conexão e coleta). Se o layout for reorganizado, a ocultação para de casar
   com os campos certos, silenciosamente.
4. **Autenticação sem retorno**: se o navegador não chamar o callback, a GUI fica
   em "aguardando…" até o limite (300 s). Já não congela a janela, mas também
   não oferece botão de desistir.
5. **Não existe "coletar agora" na GUI**: a tela só enfileira o trabalho; quem
   executa é o worker.
6. **Fechar o app durante uma coleta nunca foi testado** — a thread de trabalho é
   juntada no destrutor, o que pode segurar o fechamento.

---

## 7. Relatado pelo Kai (preencher)

> Ele disse: "tem coisa que tá crashando o software, e alguns bugs". Os crashes
> acima são os que o Windows registrou. Os *bugs* que ele viu **não foram
> descritos ainda** — anotar aqui, com tela e passo a passo, antes de mexer:

- [ ] (descrever)
- [ ] (descrever)

---

## 8. Anexo — como esta evidência foi coletada

```powershell
# assinaturas de falha do binário nos últimos 30 dias
Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error';
    StartTime=(Get-Date).AddDays(-30)} |
  Where-Object { $_.Message -match 'OlhoDeDeus_gui' } |
  ForEach-Object {
    $mod = ([regex]::Match($_.Message, 'módulo com falha: ([^,]+)')).Groups[1].Value
    $off = ([regex]::Match($_.Message, 'Deslocamento de falha: (0x[0-9a-fA-F]+)')).Groups[1].Value
    $cod = ([regex]::Match($_.Message, 'Código de exceção: (0x[0-9a-fA-F]+)')).Groups[1].Value
    $_.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss') + ' | ' + $cod + ' | ' + $mod + ' +' + $off
  }
```

Coletado em 05/11/2026, na máquina do Kai, com o build do SHA-256 acima.

---

# Anexo — "Gerenciador do Caso" some quando fechado (05/11/2026 21:03)

Segundo defeito relatado pelo Kai, na mesma regra: **registrar, não corrigir**.

## Sintoma

Fechar o painel **Gerenciador do Caso** (o dock da esquerda, com a árvore do
caso) usando o ✕ da barra de título faz o painel desaparecer — e **não existe
nenhum caminho na interface para trazê-lo de volta**. Pior: o estado fechado é
gravado em disco, então ele continua sumido depois de fechar e reabrir o
programa.

## Causa no código (verificada, não é hipótese)

1. O dock é criado com o botão de fechar habilitado —
   `mainwindow.cpp:4456-4461`:
   ```cpp
   dock->setFeatures(QDockWidget::DockWidgetMovable |
                     QDockWidget::DockWidgetFloatable |
                     QDockWidget::DockWidgetClosable);
   ```
2. O menu **Exibir** (`mainwindow.cpp:4135-4148`) oferece apenas
   Tabela / Hex View / Timeline e **Console**. O menu **Janela**
   (`4162-4169`) repete Console + abas. **Não há ação para
   `tree_dock_` nem para `details_dock_`.**
   Ou seja: fechado o dock, ele fica sem porta de volta.
3. O estado é persistido em `layout.ini` **ao lado do executável**
   (`layout_settings_path()`, `4425-4428`), gravado em
   `saveState()` (`4432-4434`) e restaurado por
   `restoreState()` quando `layout/version == 2` (`4445-4448`).
   É por isso que o sumiço sobrevive ao reinício: a ausência do dock virou
   layout salvo.

Mesmo risco se aplica a **Detalhes** (`details_dock_`) — também
fechável e também sem ação no menu.

## Evidência observada nesta máquina

| Momento | Observação |
|---|---|
| 20:45 | app aberto **sem** o painel da esquerda (captura `.oracle/gui_cloud.png`) |
| 20:54:47 | `layout.ini` gravado, 1.146 bytes, com `windowState` e `version=2`; seção `[autohide] docks=detailsDock, bottomDock` |
| 21:03 | `layout.ini` copiado para `layout.ini.bak-20261105-210319` e removido -> app reaberto **com** o Gerenciador do Caso visível (captura `.oracle/gui_casos.png`) |

## Contorno para o usuário (sem tocar em código)

1. Feche o Olho de Deus.
2. Renomeie ou apague `layout.ini` **na mesma pasta do `OlhoDeDeus_gui.exe`
   (renomear é reversível; o backup desta sessão está em `layout.ini.bak-20261105-210319`).
3. Abra de novo: o layout padrão volta, com o painel.

Consequência do contorno: as posições de dock e o auto-hide voltam ao padrão.

## Correção proposta (NÃO aplicada)

No menu Exibir, acrescentar as ações dos docks em vez de escrever `show()`
à mão — `toggleViewAction()` já traz texto, atalho e estado sincronizado com
o ✕:

```cpp
view->addAction(tree_dock_->toggleViewAction());     // "Gerenciador do Caso"
view->addAction(details_dock_->toggleViewAction());  // "Detalhes"
view->addAction(bottom_dock_->toggleViewAction());   // "Console"
```

Alternativa mínima, se a intenção for impedir o fechamento de vez: tirar
`DockWidgetClosable` de `make_dock` — mas isso tira um recurso que o
usuário pode querer. Preferir a ação de menu.

---

## 9. crash no desmonte da janela — RESOLVIDO em 12/11/2026

Ate aqui este documento tinha 29 APPCRASH em 30 dias **sem** saber onde o processo
morria (secao 4). O modo evidencia `--screenshot` mudou isso: ele carrega um caso,
salva um PNG e **sai sozinho** — e a saida e o ponto onde o processo sempre passa.
Repetindo a mesma execucao, o crash aparece.

### Como reproduzir

```powershell
# qualquer --case, ou nenhum; o crash nao depende do conteudo do caso
.\OlhoDeDeus_gui.exe --screenshot saida.png --case cases\qualquer
echo $LASTEXITCODE
```

### Resultado medido (15 execucoes, 3 casos diferentes)

| Execucoes | Falhas | Codigo | PNG |
|---|---|---|---|
| 15 | **6** | -1073741819 = 0xC0000005 (access violation) | **sempre gravado, 118-143 KB, 1440x900** |

Distribuicao das 6 falhas: 4x caso do motor (cases/case_20261027_181702_408Z),
1x caso de WhatsApp (.oracle/wa_tl), 1x pasta vazia. Ou seja: **nao** e especifico
de um caso.

### O que isso ja prova

- O crash acontece **no desmonte**, depois de window.grab() e do save()
  retornarem: a mensagem "screenshot: ... salvo=1" sai no stdout **antes** da
  morte, e o PNG esta integro no disco.
- Nao e o carregamento assincrono da Timeline: um caso vazio (sem timeline
  nenhuma) tambem caiu.
- Nao e dupla carga: o modo evidencia suprime o load_case() automatico do
  construtor (g_gui_evidence_mode) e o crash continua.

Os tres suspeitos que estavam aqui (QFileSystemModel, `delete auto_hide_`,
ordem com o QApplication) foram descartados: a falha foi localizada na pilha.

### Causa raiz (provada, nao hipotese)

O relatorio que a propria GUI grava no crash (`crash-<pid>.txt`,
`write_crash_report` em mainwindow.cpp:6068) passou a incluir o `exe_base` e o
endereco absoluto de cada frame, e isso fecha o caso em tres passos:

1. `codigo=0xc0000005 endereco=<exe_base>+0x25766` — o RVA sai direto da conta.
2. `addr2line -f -C -e OlhoDeDeus_gui_dbg.exe 0x140025766` devolve
   `QHashPrivate::Data<QHashPrivate::Node<QDockWidget*, ui::DockTitleBar*> >::Bucket::offset()`
   em `qhash.h:313`, embutida em `findBucketWithHash()`.
3. Esse tipo de hash aparece **uma unica vez** no repositorio: `dock_title_bars_`
   (mainwindow.cpp:929).

Com marcadores temporarios de desmonte no stderr, o ponto exato da morte aparece:

| marcador | resultado |
|---|---|
| `~MainWindow: corpo inicio` … `corpo fim (threads juntadas)` | ok |
| `destroyed treeDock: antes do remove` / `remove ok` | ok |
| `destroyed detailsDock: antes do remove` | **morre aqui** |

Como o defeito funciona: `dock_title_bars_` era **membro** do MainWindow e o
`remove()` rodava dentro do lambda ligado a `QObject::destroyed` do dock.
Membros morrem em `~MainWindow`; os docks so sao apagados depois, em
`QObject::~QObject`. O lambda, portanto, rodava com o hash **ja destruido**: o
primeiro `remove()` ainda acertava a memoria liberada, o segundo caia.

### Correcao aplicada

O hash membro foi removido. `dock_title_bar()` passa a achar a barra pela arvore
do proprio dock (`dynamic_cast` sobre `dock->findChildren<QWidget*>()`), e o
`connect(dock, &QObject::destroyed, …)` sumiu junto. Sem mapa guardando ponteiro
de widget, nao sobra objeto para sobreviver ao dono.

`make_dock()` ganhou uma invariante explicita: se `dock_title_bar(dock)` nao
devolver a barra recem-criada, sai um `qWarning` — sem isso o painel perderia pin
e menu do titulo em silencio.

### Medicao depois da correcao

| binario | rodadas | falhas |
|---|---|---|
| `OlhoDeDeus_gui_dbg.exe` (debug) | 40 | **0** |
| `OlhoDeDeus_gui.exe` (produto) | 20 | **0** |

Antes, o mesmo comando e o mesmo caso (`.oracle/wa_tl`): 10 de 25 no debug e 6 de
15 no produto.

Tres caminhos de fechamento foram exercitados depois da correcao:

| caminho | script | rodadas | falhas |
|---|---|---|---|
| `--screenshot` (debug + produto) | `verify_crash_fix.ps1` | 60 | 0 |
| fechar a janela de verdade (`WM_CLOSE`) | `verify_close_path.ps1` | 6 | 0 |
| 7 casos x 2 abas + caso vazio | `sweep_cases.ps1` | 15 | 0 |

**81 fechamentos, 0 crashes, 0 relatorios gravados.** Os tres scripts saem com
codigo 1 se qualquer execucao falhar, entao servem de gate.

### Reproducao isolada: por que ela NAO derrubava o processo

`.oracle_crash/dock_hash_lifetime.cpp` repete o padrao (hash membro + lambda de
`destroyed`, com e sem churn de heap) em 4 modos e 20 rodadas cada: **0 falhas**.
Num processo pequeno o bloco liberado continua mapeado, entao ler memoria morta
nao gera excecao. Foi o heap movimentado da janela real que transformou o UB em
`0xC0000005`. Fica registrado como limite do repro isolado — nao como prova
contraria.


