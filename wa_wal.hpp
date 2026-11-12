// language: C++, file: wa_wal.hpp, target: Windows 11 (MSVC / MinGW g++)
// Reconstrucao do estado atual do SQLite a partir do par db + wal.
//
// POR QUE ISSO IMPORTA
//   O arquivo .db pode estar atrasado: as ultimas transacoes podem existir apenas
//   no -wal. Periciar so o .db mostra um passado, nao o presente. Aplicar os
//   frames commitados reconstroi o estado real e, a partir dele, o carver
//   encontra as linhas apagadas.
//
// O QUE E VERIFICADO
//   Cabecalho do WAL (magic, tamanho de pagina, salts), salts de cada frame e o
//   checksum cumulativo. Frame com checksum errado e descartado, e o descarte e
//   contado: nada entra no banco reconstruido sem conferir.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::wa {

// Um frame valido do WAL. "after_last_commit" marca os frames que vieram depois
// do ultimo commit: eles sao aplicados de proposito (evidencia mais nova num
// aparelho apreendido no meio da escrita), mas o laudo precisa saber que o
// SQLite nao os confirmou.
struct wal_frame_info {
    std::uint32_t pgno = 0;
    std::uint32_t db_size = 0;
    std::uint64_t offset = 0;      // deslocamento do cabecalho do frame no arquivo
    bool after_last_commit = false;
};

struct wal_stats {
    bool ok = false;
    std::uint32_t page_size = 0;
    std::uint64_t frames = 0;          // frames lidos
    std::uint64_t committed = 0;       // frames que fazem parte de um commit valido
    std::uint64_t discarded = 0;       // checksum/salt invalido, ou resto de transacao
    std::uint64_t db_pages_after = 0;  // tamanho final declarado pelo ultimo commit
    std::string error;
};

// Percorre o WAL e devolve apenas os frames confiaveis (magic, salt e checksum).
// Funcao pura: nao escreve nada, e a mesma decisao que a aplicacao usa.
bool wal_scan(const std::vector<unsigned char>& wal, std::vector<wal_frame_info>& frames,
              wal_stats& stats);

// Escreve em out_path o banco reconstruido (db + frames commitados do wal).
// O arquivo de saida nunca e o mesmo do banco de origem.
bool wal_apply(const std::wstring& db_path, const std::wstring& wal_path,
               const std::wstring& out_path, wal_stats& stats);

}  // namespace olho::wa
