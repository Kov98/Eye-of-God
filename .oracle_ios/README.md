# .oracle_ios — verificacao do modulo iOS (task-1)

Tudo aqui existe para provar o comportamento de `ios_keybag.*` e `ios_backup.*`
contra um **oraculo independente**. Nada neste diretorio e compilado para o produto.

## Como reproduzir

    powershell -ExecutionPolicy Bypass -File .oracle_ios\run_all.ps1

O script encadeia as cinco etapas e escreve o log completo em `test_run.log`.
Codigo de saida 0 = todos os checks passaram.

| Etapa | O que faz |
|---|---|
| 1 | auto-teste do oraculo (RFC 3394 4.1/4.6, NIST SP 800-38A F.2.5, PBKDF2-HMAC-SHA256, ida-e-volta do key wrap) |
| 2 | gera `fixture/` com o oraculo: keybag, `Manifest.plist`, `Manifest.db`, conteudo cifrado |
| 3 | compila as unidades do modulo com `-Wall -Wextra` e exige zero warnings |
| 4 | link com `third_party/sqlite/sqlite3.o` e `-lbcrypt` |
| 5 | roda `build/test_ios.exe` |

## Quem e o oraculo

Cada camada e provada por uma implementacao que **nao** e o codigo C++ sob teste:

| Camada | Oracao | Natureza |
|---|---|---|
| binary plist | `plistlib` (CPython 3.13) | escritor binario independente, formato Apple |
| Manifest.db | `sqlite3` (CPython) | banco real |
| PBKDF2-HMAC-SHA256 | `hashlib.pbkdf2_hmac` (OpenSSL) **e** laco manual em .NET | duas implementacoes independentes que precisam concordar entre si |
| AES-256-CBC | `System.Security.Cryptography.Aes` (.NET Framework 4.8) | biblioteca de terceiro |
| AES Key Wrap (RFC 3394) | implementacao a mao em PowerShell sobre AES-ECB do .NET | linguagem diferente, sem CNG |
| vetores de referencia | RFC 3394, NIST SP 800-38A, RFC 4231, vetores publicados de PBKDF2 | verdade externa, nao auto-consistencia |

O gerador nunca importa nem reutiliza o codigo C++.

## Matriz de prova

| Afirmacao | Como foi provada | Resultado |
|---|---|---|
| SHA-256 / HMAC-SHA256 corretos | vetores RFC 4231 e publicados | 2/2 |
| PBKDF2-HMAC-SHA256 correto | 3 vetores publicados (1, 2 e 4096 iteracoes) | 3/3 |
| AES Key Wrap RFC 3394 correto | vetores 4.1 e 4.6, ida e volta em 5 tamanhos, rejeicao de dado corrompido | 4/4 |
| AES-256-CBC correto | NIST SP 800-38A F.2.5 encrypt e decrypt | 2/2 |
| Parser binary plist | 19 asseracoes sobre o `Manifest.plist` escrito por plistlib (inteiro negativo, inteiro de 8 bytes, real, data, data binario vazio, string vazia, UTF-16 com acento/CJK/par substituto, dicionario aninhado em 3 niveis) | 19/19 |
| Parser keybag | VERS/TYPE/UUID/SALT/ITER/HMCK lidos, uma secao CLAS por classe, WPKY de cada classe | 8/8 |
| Derivacao de chave | passcode key == PBKDF2 do `hashlib`; class keys == as do oraculo, para 5 classes | 2/2 |
| Senha errada detectada | integrity check do RFC 3394 recusa | 1/1 |
| Unwrap da file key | 13 de 13 chaves iguais as do oraculo, com o layout de `EncryptionKey` identificado | 13/13 |
| Decriptacao | 13 de 13 arquivos identicos ao original (memcmp byte a byte + sha256) | 13/13 |
| Backup nao cifrado | 13 de 13 arquivos identicos ao original | 13/13 |
| Manifesto JSON | 16 registros (13 extracted + 2 directory + 1 symlink), sha256 conferido, UTF-8 valido | ok |
| Robustez | 8000 chamadas com blob aleatorio nos dois parsers: sem crash, falha sempre com mensagem | ok |

Numeros da fixture: 13 arquivos, 342914 bytes de plaintext, 343072 bytes de ciphertext,
2 diretorios, 1 symlink, 5 classes de protecao, keybag de 755 bytes, ITER = 10000.

## O que NAO esta provado

1. **Nenhum backup real da Apple foi lido.** Toda a fixture foi montada por mim a partir
   da descricao do formato. Se a Apple divergir em algum detalhe estrutural, o teste nao
   detecta: ele valida o C++ contra a minha leitura do formato, nao contra o iTunes.
2. **HMCK.** Implementei quatro convencoes plausiveis (SHA1/SHA256 x cobre-ate-o-tag /
   cobre-ate-o-fim-dos-dados) e reporto qual fechou. A fixture usa a convencao 0 por
   construcao, entao isso prova consistencia, nao fidelidade a Apple.
3. **`Properties.salt` / `Properties.iterations` do `Manifest.db`** sao lidos e
   reportados, mas nao usados na derivacao. Nao consegui confirmar empiricamente qual e
   o papel deles sem um backup real.
4. **Classe de protecao por arquivo em backup real** nao foi observada; apenas simulada.
5. **Nada de dispositivo**: sem iPhone, sem AFC, sem lockdown, sem escrow keybag.

## Achados de formato que divergem da descricao da tarefa

- **WRAP e um inteiro de 4 bytes (tipo de embrulho), nao a chave embrulhada.** A chave
  embrulhada e o bloco WPKY. O parser trata exatamente assim.
- **Numeracao das classes.** A tarefa chama a classe 4 de
  NSFileProtectionCompleteUntilFirstUserAuthentication. Na numeracao CPProtectionClass que
  conheco, 3 = UntilFirstUserAuthentication e 4 = None. Para nao depender do rotulo, o
  codigo desembrulha **todas** as classes presentes no keybag; a fixture exercita 1, 2, 3,
  4 e 11.
- **Layout do `EncryptionKey`.** Nao ha garantia de um unico layout. O resolvedor tenta
  `[u32 BE classe][chave embrulhada]`, `[u32 LE classe][chave embrulhada]` e
  `[chave embrulhada]` com a classe vinda do campo `ProtectionClass`, e aceita o que
  passar no integrity check do RFC 3394 com uma class key existente. Os tres formatos
  estao na fixture e os tres passam.
- **Embrulho NSKeyedArchiver.** O campo `EncryptionKey` pode vir como bytes diretos ou
  como referencia a um objeto `{"NS.data": ...}`; ambos cobertos.
- **Padding.** O conteudo cifrado e PKCS#7. O codigo prefere truncar pelo `Size` registrado
  no manifesto e so cai no unpad PKCS#7 quando o tamanho nao esta disponivel.
- **`Info.plist` e XML**, nao binary plist. O parser recusa com mensagem explicita em vez
  de fingir que leu.
- **`Manifest.mbdb` (pre-iOS 10)** nao e suportado: erro explicito.
- **BCrypt escreve o IV de volta no buffer do chamador.** `BCryptEncrypt`/`BCryptDecrypt`
  recebem `pbIV` como `PUCHAR` e gravam o IV encadeado la dentro. Sem copia local, a
  segunda chamada com o mesmo vetor de IV usa lixo. Foi o unico bug de cripto que o
  oraculo pegou e que nao aparecia em teste de ida-e-volta.

## Arquivos

    gen_crypto.ps1      oraculo criptografico (.NET) e auto-teste de vetores externos
    gen_fixture.py      monta keybag, Manifest.plist, Manifest.db e conteudo cifrado
    test_main.cpp       harness de verificacao (73 checks)
    check_manifest.py   valida o manifesto JSON de extracao
    run_all.ps1         roda tudo em sequencia
    fixture/            gerado; pode ser apagado e recriado a qualquer momento
    out/                saida da extracao durante os testes
    build/              objetos e o executavel do harness
