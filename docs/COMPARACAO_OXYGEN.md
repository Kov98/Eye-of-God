# Olho de Deus vs. Oxygen Forensic Detective

Comparativo honesto, baseado no código atual do Olho de Deus e em material
público do Oxygen. Não é um benchmark: não executei os dois produtos no mesmo
caso, com o mesmo hardware e o mesmo conjunto de evidências.

## Veredito curto

O **Oxygen Forensic Detective** é uma plataforma comercial madura, ampla e
validada para mobile, cloud, computer e drone. O **Olho de Deus** ainda é uma
estação Windows focada, transparente e customizável, com um diferencial real:
**aquisição/forense + sanitização/exposição no mesmo produto**.

Eles não estão no mesmo estágio. O objetivo realista não é "bater o Oxygen em
tudo"; é ser superior no nicho Windows + privacidade/sanitização e crescer em
profundidade forense.

## Matriz

| Dimensão | Olho de Deus | Oxygen Forensic Detective |
|---|---|---|
| **Posicionamento** | estação Windows CLI + GUI, código próprio, sem licença comercial | suíte enterprise all-in-one para mobile, cloud, computer e drone |
| **UI** | Qt 6, três colunas, worktree, tabela, hex, timeline, detalhes, abas de ferramentas | UI consolidada, orientada a casos, múltiplos módulos e visualizações |
| **Aquisição Windows** | raw, VSS, NTFS/MFT, USN, recovery, artefatos, E01/AFF4 | computer acquisition e parsing amplo, integrado ao ecossistema |
| **Mobile iOS/Android** | não implementado | ponto forte histórico do produto |
| **Cloud / apps** | não implementado | extração e parsing de cloud/app data é um dos focos |
| **Drone / mídia** | não implementado | suportado no ecossistema Oxygen |
| **Parsers de artefatos** | registry, browser, EVTX, Prefetch, JumpList, LNK, Recycle, SRUM, Amcache, ShimCache | catálogo muito maior, com atualização comercial frequente |
| **Timeline** | funcional, amostrada (80 pontos no gráfico), 3.000 eventos na tabela | timeline analítica madura, correlacionável com múltiplas fontes |
| **Hex / file viewer** | Hex View do evento selecionado | file viewer/hex com navegação de evidência e offsets reais |
| **Busca e filtros** | busca recursiva na árvore + filtro de tabela | busca global, filtros e consultas por caso |
| **Analytics** | básico: tabela, detalhes, hash, linha do tempo | social graph, mapas, análise de comunicação, faces, OCR, entidades, etc. |
| **Relatórios** | Markdown/JSON/CLI; GUI ainda não tem gerador de laudo | templates de relatório, exportação e apresentação para corte |
| **Cadeia de custódia** | manifest/hash SHA-256, logs de comando, arquivos de saída | fluxo de custódia e auditoria enterprise |
| **Colaboração** | um usuário/local | multiusuário, equipes e revisão |
| **Escala** | caso mais recente, até 3.000 eventos carregados, sem paginação completa | projetado para casos grandes e volumes maiores |
| **Sanitização / wipe** | **diferencial**: `--mode sanitize`, ATA/NVMe, wipe, VSS, resíduos, exposure audit | não é o foco público do Oxygen |
| **Transparência** | CLI aberto, comandos visíveis, fácil auditar o que foi executado | produto fechado, fluxo proprietário |
| **Licenciamento** | próprio, sem mensalidade | licença comercial |
| **Validação forense** | testes funcionais/VM; sem certificação ou validação formal | produto comercial com suporte, treinamento e uso judicial amplo |
| **Instalação/enterprise** | pacote portátil + runtime Qt; sem MSI/assinatura | instalador, licenciamento, suporte e atualizações |

## Onde o Olho de Deus já é forte

1. **Windows profundo**: MFT, USN, VSS, registry, EVTX, Prefetch, SRUM,
   Amcache, ShimCache, Recycle, LNK, JumpList.
2. **Sanitização no mesmo binário**: ATA erase, NVMe sanitize, wipe de
   arquivos/hives/journals, limpeza de resíduos e auditoria de exposição.
3. **Transparência**: cada ação da GUI vira um comando de CLI; o console
   mostra o comando e o exit code.
4. **Portabilidade**: não exige banco proprietário nem servidor; roda local.
5. **Controle**: código próprio, sem depender de roadmap de fornecedor.

## Onde o Oxygen ganha claramente

1. **Mobile e cloud**: iOS, Android, apps, backups, contas e drones.
2. **Amplitude de parsers**: centenas de fontes e atualizações comerciais.
3. **Analytics**: visualizações, correlação, entidades, mapas e gráficos.
4. **Relatórios e court-readiness**: laudos, templates e fluxo de revisão.
5. **Escala e enterprise**: casos grandes, equipes, auditoria e suporte.
6. **Maturidade**: validação, documentação, treinamento e base instalada.

## Notas sugeridas

| Critério | Olho de Deus | Oxygen |
|---|---:|---:|
| UI/UX | 6,5 | 9,0 |
| Aquisição Windows | 8,0 | 8,5 |
| Mobile/cloud | 0,5 | 9,5 |
| Parsers/artefatos | 6,0 | 9,5 |
| Analytics | 3,0 | 9,5 |
| Relatórios | 3,0 | 9,0 |
| Sanitização/exposição | 8,0 | 2,0* |
| Transparência/CLI | 9,0 | 5,0 |
| Maturidade enterprise | 2,0 | 9,5 |

\* O Oxygen não posiciona sanitização destrutiva como núcleo; a nota é uma
comparação de escopo, não uma medida de qualidade.

## Próximos passos para aproximar do Oxygen

1. **Caso + paginação**: seletor de casos, consultas paginadas e índice no
   `timeline.jsonl`, sem carregar 3.000 itens de uma vez.
2. **File/Hex real**: abrir arquivo do manifesto e navegar por offset real,
   não só pelos bytes do evento.
3. **Laudo**: gerador PDF/DOCX com hash, metodologia, achados e anexos.
4. **Cadeia de custódia**: log assinado, usuário, timestamps e inventário.
5. **Performance**: SQLite/FTS, processamento em streaming e virtualização da
   tabela.
6. **Mobile/cloud**: só vale a pena com parsers e datasets reais; é um
   produto dentro do produto.
7. **Enterprise**: MSI, assinatura, atualização, telemetria opt-in e
   validação documentada.

## Fontes públicas consultadas

- Oxygen Forensic Detective — visão do produto:
  <https://www.oxygenforensics.com/en/products/oxygen-forensic-detective/>
- Oxygen Forensic Detective v17 — atualizações:
  <https://www.oxygenforensics.com/news/oxygen-forensic-detective-v-17-updates/>
- Extração de dispositivos suportados:
  <https://www.oxygenforensics.com/blog/ofd-supported-device-extraction/>
- Forensic Focus — v17.3.1:
  <https://www.forensicfocus.com/news/introducing-oxygen-forensic-detective-v-17-3-1/>

## Conclusão

Se o objetivo é **forense móvel/cloud de nível enterprise**, o Oxygen é a
referência e o Olho de Deus ainda está muito distante.

Se o objetivo é **forense Windows + sanitização/privacidade com transparência
de CLI**, o Olho de Deus já tem uma proposta que o Oxygen não cobre bem. Esse
é o nicho onde vale investir profundidade, validação e usabilidade — não em
tentar copiar todos os módulos do Oxygen de uma vez.
