# formatguard

Parsers Rust para os dois formatos onde a entrada e hostil por definicao num aparelho
apreendido: o msgstore do WhatsApp (protobuf) e um backup iOS (Apple binary plist
"bplist00"). O C++ do produto ja parseia os dois; esta biblioteca entra como camada que
nao derruba a estacao do perito quando a entrada e malformada.

Nao e uma reescrita: e um crate novo, com ABI C estreita, que o C++ linka estatico.

    rust/formatguard/
      Cargo.toml                      crate-type = ["cdylib", "staticlib", "rlib"]
      include/formatguard.h           header escrito a mao (a ABI e o contrato)
      src/lib.rs                      raiz: deny(unsafe_code) e o conjunto de lints
      src/error.rs                    codigos estaveis (FgStatus) + FgError
      src/limits.rs                   limites explicitos de profundidade e alocacao
      src/plist.rs                    parser bplist00 (forbid(unsafe_code))
      src/protobuf.rs                 parser wire format protobuf (forbid(unsafe_code))
      src/json.rs                     serializacao canonica e deterministica
      src/abi.rs                      unico modulo com unsafe; fronteira do FFI
      src/bin/fgdump.rs               CLI: dump canonico de um arquivo
      src/bin/robust.rs               campanha de robustez (milhoes de entradas)
      tests/                          plist, protobuf, abi, robustness, corpus
      tools/gen_corpus.py             oraculo independente (plistlib + codificador Python)

## Build

    cargo build --release

Saidas:

    target/release/libformatguard.a    linkar estatico (nenhuma DLL ao lado do exe)
    target/release/formatguard.dll     opcional
    rust/formatguard/include/formatguard.h

Link com o g++ do MSYS2 UCRT64 (x86_64-pc-windows-gnu, o mesmo alvo do Rust aqui):

    g++ -std=c++17 -O2 app.cpp \
        -I rust/formatguard/include \
        rust/formatguard/target/release/libformatguard.a \
        -lbcrypt -lntdll -luserenv -lws2_32 -ladvapi32 -lole32 -loleaut32 -luuid \
        -lshlwapi -luser32 -lgdi32 -lcomdlg32 -lcomctl32 -lwinspool -lmsimg32 \
        -lwsock32 -lsetupapi -lversion -lshell32 -lshcore -lcfgmgr32 \
        -lsynchronization -lwinhttp

Essa lista de bibliotecas do sistema e o que o std do Rust arrasta no alvo windows-gnu.

## ABI

    int32_t fg_parse_plist(const uint8_t* data, size_t len, fg_plist** out);
    int32_t fg_parse_protobuf(const uint8_t* data, size_t len, fg_proto** out);
    int32_t fg_plist_to_json(const fg_plist* h, char** out, size_t* out_len);
    int32_t fg_proto_to_json(const fg_proto* h, char** out, size_t* out_len);
    void    fg_free_plist(fg_plist*);
    void    fg_free_proto(fg_proto*);
    void    fg_free_string(char*);
    const char* fg_strerror(int32_t code);
    const char* fg_last_error(void);
    const char* fg_version(void);
    int32_t fg_abi_version(void);

Tipos opacos: o C++ nunca ve a estrutura. Zero = sucesso, negativo = codigo de erro
estavel (ver include/formatguard.h). Memoria devolvida so e liberada pelas funcoes
fg_free_* (o alocador e o do Rust; free() do CRT seria um bug).

## JSON canonico

A mesma arvore sempre vira exatamente os mesmos bytes, o que permite comparar
implementacoes por igualdade de texto:

| tipo | JSON |
|---|---|
| null / bool | null / true, false |
| integer | numero decimal |
| real | expansao decimal EXATA do double (nunca arredondada) |
| date | objeto com a chave $date e o valor em segundos desde 2001-01-01T00:00:00Z |
| data | objeto com a chave $data e o valor em hex minusculo |
| string / UTF-16 | string JSON (bytes que nao sao UTF-8 valido viram escape unicode U+00XX) |
| uid | objeto com a chave $uid |
| array / set | lista / objeto com a chave $set e uma lista |
| dict | objeto JSON na ordem do arquivo; chave que comeca com $ sai com o escape de U+0024 |

Real como expansao exata e o unico jeito de duas implementacoes independentes concordarem
por texto: o "menor decimal que volta ao mesmo double" tem empate -- Rust e C++ escolhem
digitos diferentes em ~0,023% dos doubles (medido em 300 mil valores).

## Limites (src/limits.rs, nao configuraveis de proposito)

| limite | valor |
|---|---|
| profundidade de aninhamento (plist e grupos protobuf) | 64 |
| objetos de plist visitados | 1 048 576 |
| payload copiado por parsing | 64 MiB |
| saida JSON | 256 MiB |
| entrada (len) | 4 GiB |

## Verificacao (numeros medidos)

| prova | resultado |
|---|---|
| cargo build --release | exit 0; libformatguard.a 17 663 806 bytes |
| cargo clippy --all-targets -- -D warnings | 0 achados |
| cargo test --release | 40 testes, 0 falhas (6 ABI, 2 corpus, 18 plist, 10 protobuf, 2 robustez, 2 doc) |
| diferencial contra ios_backup.cpp, corpus legitimo | 55 arquivos: 12 aceitos com JSON identico, 43 recusados pelos dois, 0 divergencias |
| diferencial, mutacoes estruturadas | 4000 arquivos: 481 aceitos com JSON identico, 3519 recusados pelos dois, 0 divergencias, 0 crashes do C++ |
| oraculo independente plistlib (CPython 3.13) | 9 plists gerados, JSON identico nos 9 |
| oraculo independente de protobuf (codificador Python) | 10 vetores, JSON identico nos 10 |
| robustez | 25 000 000 entradas -> 50 000 000 chamadas de parse, 0 panics, 0 falhas de ABI |
| contrato de real | 299 899 doubles: Rust, C++ e CPython com 0 divergencias |
| prova de ida (g++ inclui o header e linka a staticlib) | .oracle_rust/results/demo.exe imprime o JSON de Manifest.plist |

Reproduzir tudo:

    powershell -ExecutionPolicy Bypass -File .oracle_rust\run.ps1

## Defeitos do parser C++ encontrados por este trabalho

Ver .oracle_rust/results/probe_findings.txt:

1. ios_backup.cpp: a checagem "units * 2 > data_.size() - pos" transborda com comprimento
   2^63 e a string UTF-16 e decodificada com 2^63 unidades. Um bplist de 55 bytes mata o
   processo com std::length_error nao capturado. O Rust devolve -9 (BadLength).
2. ios_backup.cpp nao tem orcamento de nos: referencias compartilhadas (sem ciclo) expandem
   2^N com N <= 64. Um arquivo de 153 bytes fez o C++ visitar 2 097 151 nos em 341 ms, e o
   custo dobra por nivel. O Rust recusa em 1 048 576 nos.

## O que NAO esta provado

1. Nenhum aparelho real. O corpus legitimo e a fixture do .oracle_ios (escrita por
   plistlib) mais o que eu gerei. Nenhum Manifest.plist de um iPhone de verdade passou por
   aqui; um backup real com mais de 1 milhao de nos seria recusado pelo orcamento (o limite
   e uma constante de uma linha, mas nao foi calibrado com dado real).
2. O protobuf nao tem oraculo de terceiro. Nao ha parser protobuf em C++ neste repositorio
   e a biblioteca protobuf do Google nao esta instalada: o esperado dos 10 vetores veio de
   um codificador Python que eu escrevi. Isso prova consistencia entre duas implementacoes
   minhas do wire format, nao fidelidade a um schema do WhatsApp. Campos length-delimited
   NAO sao adivinhados como mensagens aninhadas: quem conhece o schema chama
   fg_parse_protobuf no payload.
3. panic = "abort". A rede de seguranca da ABI (catch_unwind) exige panic = "unwind". Se o
   produto compilar o crate com abort, um panic inesperado mata o processo em vez de virar
   FG_ERR_INTERNAL.
4. Nenhum teste de concorrencia real. As funcoes sao puras (nenhum estado global alem do
   fg_last_error por thread), mas nao rodei N threads chamando a ABI ao mesmo tempo.
5. Miri nao rodou. Os parsers sao forbid(unsafe_code) (nao ha o que o miri checar ali) e o
   unsafe da ABI nao e alcancavel pelo miri, que nao executa FFI. O componente miri nao
   esta instalado neste toolchain.
6. Alocacao em caso de OOM. Os limites evitam alocacao absurda por construcao, mas uma
   falha de alocacao (abort do alocador) nao e tratavel em Rust e nao foi testada.
7. Nomes de arquivo com acento exigiram _wfopen no harness; o parser nao ve caminhos, mas
   isso mostra que o consumo da ABI em Windows precisa cuidar de UTF-8.
