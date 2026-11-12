// language: C++, file: case_db_seal.hpp, target: Windows 11 (MSVC / MinGW g++)
// Selo de integridade do banco do caso.
//
// POR QUE UM SELO E NAO CIFRA AUTENTICADA DENTRO DO ARQUIVO
//   O banco e um SQLite vivo: o VFS grava em qualquer deslocamento, e o mesmo
//   vale para o journal e o WAL. Trocar o CTR por um modo autenticado exigiria
//   reescrever o mapeamento de paginas em tres arquivos ao mesmo tempo - risco
//   alto para o dado que mais importa. O selo resolve o problema sem tocar no
//   formato: um arquivo ao lado guarda um HMAC-SHA256 por bloco, chaveado pela
//   chave do caso.
//
// O QUE ELE DETECTA
//   Qualquer alteracao, insercao ou truncamento no banco, journal ou WAL entre o
//   selo e a verificacao. Um bloco alterado e apontado pelo numero, para o laudo
//   dizer onde olhar.
//
// O QUE ELE NAO DETECTA
//   Alteracao feita ANTES do selo, e troca do selo junto com o arquivo por quem
//   tenha a chave do caso. Por isso o selo e registrado no laudo assinado - o
//   digest do selo entra na cadeia de custodia.
#pragma once

#include "case_crypto.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace deep {

// Aviso de progresso da selagem/verificacao de pastas. A biblioteca nao imprime
// nada: informa quantos arquivos ja foram hasheados e quem chama decide o que
// mostrar. Sem isso, um caso de 10 GB fica minutos em silencio no console e o
// operador nao sabe se esta andando ou travado.
// Duas dimensoes porque uma so mente: contar arquivos deixa um video de 8 GB
// parado em "1/1" durante minutos, e contar bytes nao diz quantos arquivos faltam.
using seal_progress_fn = std::function<void(std::uint64_t files_done, std::uint64_t files_total,
                                            std::uint64_t bytes_done,
                                            std::uint64_t bytes_total)>;

struct seal_report {
    bool ok = false;
    std::uint32_t block_size = 0;
    std::uint64_t blocks = 0;
    std::uint64_t bad_block = 0;      // primeira divergencia (verify)
    bool has_bad_block = false;
    std::string path;
    std::string error;
};

// Escreve <db>.seal ao lado do banco, com um HMAC-SHA256 por bloco.
bool seal_database(const std::wstring& db_path, const case_cipher& cipher,
                   seal_report& report, const seal_progress_fn& progress = {});

// Recalcula e compara com o selo. Um arquivo ausente e erro, nao "vazio".
bool verify_database(const std::wstring& db_path, const case_cipher& cipher,
                     seal_report& report, const seal_progress_fn& progress = {});

// O banco vivo tem dois companheiros que tambem carregam dado: o journal e o
// WAL. Selar so o .db deixaria o caminho mais provavel de adulteracao aberto.
struct seal_set_report {
    bool ok = false;
    int files = 0;
    int failures = 0;
    std::vector<std::string> sealed_files;
    std::vector<std::string> errors;
    std::string error;
};

seal_set_report seal_case_files(const std::wstring& case_dir, const case_cipher& cipher,
                                const seal_progress_fn& progress = {});
seal_set_report verify_case_files(const std::wstring& case_dir, const case_cipher& cipher,
                                  const seal_progress_fn& progress = {});

// ---------------------------------------------------------------------------
// Selo da midia extraida
// ---------------------------------------------------------------------------
// POR QUE LISTA DE DIGESTS, E NAO UM HMAC POR BLOCO
//   O selo do banco aponta o bloco alterado porque o SQLite grava em qualquer
//   deslocamento e o perito precisa saber onde olhar. Midia nao e assim: um JPEG
//   ou um video e um arquivo inteiro, e a pergunta e "este arquivo e o que saiu
//   da recuperacao?". Um HMAC por bloco de 4 KiB num video de 4 GB custaria 32 MB
//   de selo para responder o mesmo; um SHA-256 por arquivo custa 32 bytes.
//
// O QUE ELE DETECTA
//   Conteudo trocado, arquivo removido, arquivo novo na pasta e arquivo renomeado
//   (o caminho relativo entra no digest). A lista inteira e coberta por um HMAC
//   chaveado pela chave do caso, entao reescrever o selo tambem e detectado.
//
// O QUE ELE NAO DETECTA
//   Troca feita ANTES do selo, e troca do selo por quem tenha a chave do caso -
//   mesma fronteira do selo do banco, e o mesmo remédio: o digest do selo entra
//   na cadeia de custodia do laudo assinado.
struct media_seal_report {
    bool ok = false;
    bool verified = false;               // true = verificacao, false = selagem
    std::uint64_t files = 0;             // arquivos selados (ou conferidos)
    std::uint64_t bytes = 0;             // soma dos tamanhos conferidos
    std::string seal_path;
    std::vector<std::string> problems;   // conteudo ou tamanho diferente do selo
    std::vector<std::string> missing;    // constava do selo e nao esta no disco
    std::vector<std::string> added;      // esta no disco e nao constava do selo
    std::string error;
};

// Sela <dir>/media.seal com um digest por arquivo, recursivo e em ordem estavel.
media_seal_report seal_media_dir(const std::wstring& dir, const case_cipher& cipher,
                                 const seal_progress_fn& progress = {});

// Recalcula tudo e compara. Arquivo a mais, a menos ou alterado reprova.
media_seal_report verify_media_dir(const std::wstring& dir, const case_cipher& cipher,
                                   const seal_progress_fn& progress = {});

// ---------------------------------------------------------------------------
// Indice do caso (case.seal)
// ---------------------------------------------------------------------------
// POR QUE UM INDICE, SE CADA PASTA JA TEM O SEU SELO
//   O `media.seal` mora DENTRO da pasta que ele sela. Apagar a pasta leva o selo
//   junto, e o que sobra e indistinguivel de "nunca foi selada" - quem tem escrita
//   no disco apaga a prova inteira e o caso parece limpo. O indice fica na raiz do
//   caso e guarda o nome e o digest de CADA selo: com ele, sumico deixa de ser
//   ambiguo.
//
// O QUE ELE DETECTA
//   Selo apagado, pasta selada removida por inteiro, selo trocado, e selo novo que
//   apareceu depois (pasta selada fora do indice). Tudo com o nome do caminho.
//
// O QUE ELE NAO DETECTA
//   O mesmo caso limite dos outros selos: troca feita antes de selar, e reescrita
//   do indice inteiro por quem tenha a chave do caso. O digest do `case.seal`
//   entra na cadeia de custodia do laudo assinado, que e o que ancora o conjunto.
struct case_index_report {
    bool ok = false;
    bool verified = false;                // true = verificacao, false = selagem
    std::uint64_t entries = 0;
    std::string index_path;
    std::vector<std::string> problems;    // selo no lugar errado (digest difere)
    std::vector<std::string> missing;     // constava do indice e nao esta no disco
    std::vector<std::string> added;       // selo no disco fora do indice
    std::string error;
};

// Escreve <caso>/case.seal com o digest de cada selo encontrado no caso.
case_index_report seal_case_index(const std::wstring& case_dir, const case_cipher& cipher,
                                 const seal_progress_fn& progress = {});

// Confere o indice contra o disco: selo que falta, que sobra ou que mudou.
case_index_report verify_case_index(const std::wstring& case_dir, const case_cipher& cipher,
                                    const seal_progress_fn& progress = {});

}  // namespace deep
