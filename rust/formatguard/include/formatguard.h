/*
 * formatguard.h — parsers Rust para entrada hostil, com ABI C estreita.
 *
 * O que e isto: uma biblioteca para o C++ do "Olho de Deus" chamar quando o dado vem de
 * um aparelho apreendido (msgstore do WhatsApp e backup iOS). Ela parseia dois formatos
 * — Apple binary plist "bplist00" e wire format protobuf — e devolve, no pior caso de
 * entrada malformada, um codigo de erro. Nao existe entrada capaz de fazer panic, abortar
 * ou ler fora do buffer.
 *
 * Build:
 *   cd rust/formatguard
 *   cargo build --release                              # gera cdylib e staticlib
 *   target/release/libformatguard.a                    # linkar estatico (recomendado)
 *   target/release/formatguard.dll                     # opcional, se preferir DLL
 *
 * Link estatico com g++ (MSYS2 UCRT64), alvo x86_64-pc-windows-gnu:
 *   g++ -std=c++17 -O2 app.cpp \
 *       rust/formatguard/target/release/libformatguard.a \
 *       -lbcrypt -lntdll -luserenv -lws2_32 -ladvapi32 -lole32 -loleaut32 \
 *       -luuid -lshlwapi -luser32 -lgdi32 -lcomdlg32 -lcomctl32 -lwinspool \
 *       -lmsimg32 -lwsock32 -lsetupapi -lversion -lshell32 -lshcore \
 *       -lcfgmgr32 -lsynchronization -lwinhttp
 *   (a lista de bibliotecas do sistema e o que o std do Rust arrasta no alvo windows-gnu)
 *
 * Regras de propriedade e seguranca:
 *   1. Memoria devolvida por esta biblioteca so e liberada por fg_free_plist,
 *      fg_free_proto ou fg_free_string. Nunca chame free()/delete do CRT do chamador:
 *      o alocador e o do Rust.
 *   2. Strings devolvidas por fg_strerror, fg_last_error e fg_version sao estaticas ou
 *      por thread. Nunca liberar.
 *   3. fg_last_error vale ate a proxima chamada que falhe na mesma thread.
 *   4. Todas as funcoes sao thread-safe. O unico estado e fg_last_error, que e por thread.
 *   5. Ponteiro nulo e recusado com FG_ERR_NULL_ARGUMENT (exceto nos fg_free_*, que
 *      aceitam nulo como no-op, e exceto data == NULL com len == 0, que e entrada vazia).
 *
 * Limites (compilados na biblioteca, ver src/limits.rs):
 *   profundidade de aninhamento ....... 64 niveis (plist e grupos protobuf)
 *   objetos de plist .................. 1 048 576
 *   payload copiado ................... 64 MiB por parsing (data/string/bytes)
 *   saida JSON ........................ 256 MiB
 *   entrada ........................... 4 GiB (len)
 */

#ifndef FORMATGUARD_H
#define FORMATGUARD_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Versao do contrato da ABI. Muda so quando alguma assinatura muda de forma
 * incompativel. Consulte em tempo de execucao com fg_abi_version(). */
#define FG_ABI_VERSION 1

/* Tipos opacos: o chamador so conhece o ponteiro. */
typedef struct fg_plist fg_plist;
typedef struct fg_proto fg_proto;

/* Codigos devolvidos por toda funcao int32_t. Os numeros sao estaveis e podem ser
 * comparados com == no C++. */
typedef enum fg_status {
    FG_OK = 0,
    FG_ERR_NULL_ARGUMENT = -1,   /* ponteiro nulo de entrada ou de saida           */
    FG_ERR_EMPTY_INPUT = -2,     /* zero bytes onde um plist era exigido           */
    FG_ERR_INPUT_TOO_LARGE = -3, /* len acima de 4 GiB                             */
    FG_ERR_BAD_SIGNATURE = -4,   /* assinatura bplist00 ausente/versao diferente   */
    FG_ERR_TRUNCATED = -5,       /* buffer terminou no meio de um campo            */
    FG_ERR_BAD_TRAILER = -6,     /* trailer do plist inconsistente                 */
    FG_ERR_BAD_OFFSET_TABLE = -7,/* tabela de offsets fora do buffer               */
    FG_ERR_BAD_MARKER = -8,      /* marcador de tipo desconhecido                  */
    FG_ERR_BAD_LENGTH = -9,      /* comprimento declarado maior que o buffer       */
    FG_ERR_DEPTH_LIMIT = -10,    /* aninhamento acima de 64                        */
    FG_ERR_CYCLE = -11,          /* referencia circular entre objetos              */
    FG_ERR_BUDGET = -12,         /* orcamento de nos/bytes estourado               */
    FG_ERR_BAD_REFERENCE = -13,  /* referencia a objeto inexistente                */
    FG_ERR_UNSUPPORTED = -14,    /* tipo ou largura nao suportada                  */
    FG_ERR_BAD_UTF16 = -15,      /* string UTF-16 invalida                         */
    FG_ERR_BAD_VARINT = -16,     /* varint truncado ou com mais de 10 bytes        */
    FG_ERR_BAD_WIRE_TYPE = -17,  /* wire type 6 ou 7                               */
    FG_ERR_BAD_FIELD_NUMBER = -18,/* numero de campo 0 ou > 2^29-1                  */
    FG_ERR_BAD_GROUP = -19,      /* grupo sem END_GROUP ou com numero divergente   */
    FG_ERR_JSON_LIMIT = -20,     /* saida JSON acima de 256 MiB                    */
    FG_ERR_INTERNAL = -99        /* panic capturado na fronteira C (nunca deveria) */
} fg_status;

/* ---------------------------------------------------------------- parsing */

/* Parseia um Apple binary plist ("bplist00").
 * Sucesso: devolve FG_OK e grava o resultado em *out.
 * Falha:   devolve codigo negativo e *out fica NULL.
 * O resultado deve ser liberado com fg_free_plist. */
int32_t fg_parse_plist(const uint8_t* data, size_t len, fg_plist** out);

/* Parseia um buffer protobuf (wire format, sem schema).
 * Buffer vazio (len == 0) e uma mensagem valida sem campos: devolve FG_OK.
 * O resultado deve ser liberado com fg_free_proto. */
int32_t fg_parse_protobuf(const uint8_t* data, size_t len, fg_proto** out);

/* --------------------------------------------------------------- liberacao */

/* Libera um resultado de fg_parse_plist. Aceita NULL. */
void fg_free_plist(fg_plist* handle);

/* Libera um resultado de fg_parse_protobuf. Aceita NULL. */
void fg_free_proto(fg_proto* handle);

/* Libera um buffer devolvido por fg_plist_to_json/fg_proto_to_json. Aceita NULL. */
void fg_free_string(char* text);

/* ------------------------------------------------------------------- JSON */

/* Serializa em JSON canonico (UTF-8, terminado em NUL).
 * *out_len recebe o tamanho em bytes, sem contar o NUL.
 * Sucesso: FG_OK, *out aponta para um buffer liberavel com fg_free_string.
 * Falha:   codigo negativo, *out NULL e *out_len 0.
 *
 * Formato (identico ao descrito em src/json.rs):
 *   null                    -> null
 *   bool                    -> true | false
 *   integer                 -> <decimal>
 *   real                    -> <expansao decimal exata do double>  (nan/inf/-inf: string)
 *   date                    -> {"$date":<real>}   segundos desde 2001-01-01T00:00:00Z
 *   data                    -> {"$data":"<hex minusculo>"}
 *   string, string UTF-16   -> "<texto escapado>"
 *   uid                     -> {"$uid":<decimal>}
 *   array                   -> [ ... ]
 *   set                     -> {"$set":[ ... ]}
 *   dict                    -> {"chave":valor,...}  na ordem do arquivo
 * Chave de dict que comeca com '$' sai com o escape unicode de U+0024, para nenhuma
 * chave do arquivo se confundir com as chaves reservadas.
 * Real e escrito como a expansao decimal EXATA do double (nunca arredondada): com isso
 * a comparacao de texto entre duas implementacoes nao depende de arredondamento. */
int32_t fg_plist_to_json(const fg_plist* handle, char** out, size_t* out_len);

/* Serializa um protobuf em JSON canonico.
 *   {"fields":[{"n":<campo>,"w":<wire>,"i":<valor>,"z":<zigzag>}, ...]}
 *   w=0: "i" (varint u64) e "z" (mesmo valor em zigzag); w=1: "i" fixed64;
 *   w=5: "i" fixed32; w=2: "len" e "hex" (bytes crus); w=3: "fields" do grupo. */
int32_t fg_proto_to_json(const fg_proto* handle, char** out, size_t* out_len);

/* --------------------------------------------------------------- mensagens */

/* Mensagem fixa (estatica) de um codigo. Nunca liberar. */
const char* fg_strerror(int32_t code);

/* Detalhe textual do ultimo erro desta thread (pode ser vazio). Nunca liberar.
 * O ponteiro vale ate a proxima chamada que falhe nesta thread. */
const char* fg_last_error(void);

/* Versao da biblioteca, ex.: "formatguard 0.1.0". Nunca liberar. */
const char* fg_version(void);

/* Versao da ABI (comparar com FG_ABI_VERSION). */
int32_t fg_abi_version(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* FORMATGUARD_H */
