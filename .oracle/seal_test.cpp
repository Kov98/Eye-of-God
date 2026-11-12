// language: C++, file: .oracle/seal_test.cpp
#include "case_db_seal.hpp"
#include "case_crypto.hpp"

#include <windows.h>   // travar o selo aberto e o que prova a escrita atomica

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++failures;
}
static void write_pattern(const fs::path& p, std::size_t size, unsigned char seed) {
    std::vector<unsigned char> data(size);
    for (std::size_t i = 0; i < size; ++i) data[i] = static_cast<unsigned char>((i * 31 + seed) & 0xFF);
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}
static void flip_byte(const fs::path& p, std::size_t offset) {
    std::ifstream in(p, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    in.close();
    data[offset] ^= 0x01;
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

int main() {
    fs::remove_all(".oracle/seal_case");
    fs::create_directories(".oracle/seal_case");
    const fs::path db = ".oracle/seal_case/case.db";
    const fs::path key = ".oracle/seal_case/case.key";

    deep::case_cipher cipher;
    std::string error;
    check("cria chave do caso", cipher.open_or_create(key.wstring(), error));
    if (!cipher.ready()) { std::printf("    erro: %s\n", error.c_str()); return 1; }

    write_pattern(db, 10000, 7);   // duas paginas e meia
    deep::seal_report seal;
    check("sela o banco", deep::seal_database(db.wstring(), cipher, seal));
    if (!seal.ok) std::printf("    erro: %s\n", seal.error.c_str());
    check("tres blocos", seal.blocks == 3);
    check("selo gravado", fs::exists(".oracle/seal_case/case.db.seal"));

    deep::seal_report verify;
    check("verifica intacto", deep::verify_database(db.wstring(), cipher, verify));
    check("sem bloco ruim", !verify.has_bad_block);

    // Adultera um byte no bloco 1 e espera a denuncia do bloco certo.
    flip_byte(db, 4096 + 10);
    deep::seal_report tampered;
    check("adulteracao detectada", !deep::verify_database(db.wstring(), cipher, tampered));
    check("aponta o bloco 1", tampered.has_bad_block && tampered.bad_block == 1);
    std::printf("    motivo: %s\n", tampered.error.c_str());

    // Restaura e volta a passar.
    flip_byte(db, 4096 + 10);
    deep::seal_report restored;
    check("restaurado volta a passar", deep::verify_database(db.wstring(), cipher, restored));


    // 1b. Selo do banco tambem e streaming e avisa progresso por bloco.
    {
        std::uint64_t calls = 0;
        std::uint64_t last_done = 0, last_units = 0, last_bytes = 0, last_bytes_total = 0;
        bool ordered = true;
        const deep::seal_progress_fn progress =
            [&](std::uint64_t done, std::uint64_t total, std::uint64_t bytes_done,
                std::uint64_t bytes_total) {
                ++calls;
                if (done > total || bytes_done > bytes_total) ordered = false;
                if (done < last_done || bytes_done < last_bytes) ordered = false;
                last_done = done;
                last_units = total;
                last_bytes = bytes_done;
                last_bytes_total = bytes_total;
            };
        deep::seal_report streamed;
        check("sela o banco avisando progresso",
              deep::seal_database(db.wstring(), cipher, streamed, progress));
        check("progresso cobre todos os blocos",
              calls >= streamed.blocks && last_done == streamed.blocks && last_units == streamed.blocks);
        check("progresso do banco e ordenado", ordered);
        check("progresso do banco termina no tamanho do arquivo",
              last_bytes == last_bytes_total && last_bytes_total == 10000);
        check("selo do banco nao deixa temporario",
              !fs::exists(".oracle/seal_case/case.db.seal.tmp"));
    }

    // Truncar o banco tambem tem de falhar (tamanho difere do selado).
    {
        fs::resize_file(db, 5000);
        deep::seal_report shrunk;
        check("truncamento detectado", !deep::verify_database(db.wstring(), cipher, shrunk));
        write_pattern(db, 10000, 7);
    }

    // Selo ausente e erro explicito.
    {
        fs::remove(".oracle/seal_case/case.db.seal");
        deep::seal_report missing;
        check("selo ausente recusado", !deep::verify_database(db.wstring(), cipher, missing));
        check("motivo fala do selo", missing.error.find("selo") != std::string::npos);
    }


    // 2. Conjunto do caso: sela db + journal + wal, e denuncia adulteracao no wal.
    {
        const fs::path dir = ".oracle/seal_set";
        fs::remove_all(dir);
        fs::create_directories(dir);
        write_pattern(dir / "case.db", 8192, 3);
        write_pattern(dir / "case.db-wal", 4200, 9);

        deep::case_cipher set_cipher;
        std::string set_error;
        check("chave do conjunto", set_cipher.open_or_create((dir / "case.key").wstring(), set_error));

        const auto sealed = deep::seal_case_files(dir.wstring(), set_cipher);
        check("sela db e wal", sealed.ok && sealed.files == 2);
        if (!sealed.ok) std::printf("    erro: %s\n", sealed.error.c_str());

        const auto intact = deep::verify_case_files(dir.wstring(), set_cipher);
        check("conjunto intacto", intact.ok && intact.files == 2);

        flip_byte(dir / "case.db-wal", 100);
        const auto broken = deep::verify_case_files(dir.wstring(), set_cipher);
        check("adulteracao no wal detectada", !broken.ok);
        check("motivo cita o wal", broken.error.find("wal") != std::string::npos);
        std::printf("    motivo: %s\n", broken.error.c_str());
    }

    // 3. Selo da midia extraida: lista de digests + HMAC da lista.
    {
        const fs::path dir = ".oracle/seal_media";
        fs::remove_all(dir);
        fs::create_directories(dir / "media" / "sub");
        std::printf("  [--] selo de midia\n");

        deep::case_cipher media_cipher;
        std::string media_error;
        // A chave do caso fica FORA da pasta selada: dentro dela entraria na
        // contagem e seria selada pela chave que deriva de si mesma.
        check("chave do caso de midia",
              media_cipher.open_or_create(std::wstring(L".oracle/seal_media.key"), media_error));

        write_pattern(dir / "report.json", 900, 1);
        write_pattern(dir / "media" / "1_foto.jpg", 3000, 2);
        // 3 MiB para exercitar o hash em fluxo, nao de uma vez so.
        write_pattern(dir / "media" / "2_video.mp4", 3 * 1024 * 1024 + 7, 3);
        write_pattern(dir / "media" / "sub" / "3_audio.opus", 1500, 4);
        // Nome com acento: o caminho entra no digest como UTF-8 e tem que voltar
        // igual na verificacao, senao toda midia acentuada apareceria alterada.
        const fs::path acentuado = dir / "media" / std::wstring(L"4_foto_ac\u00E7\u00E3o.jpg");
        write_pattern(acentuado, 2048, 5);

        const auto sealed = deep::seal_media_dir(dir.wstring(), media_cipher);
        check("sela a midia", sealed.ok);
        if (!sealed.ok) std::printf("    erro: %s\n", sealed.error.c_str());
        check("cinco arquivos selados", sealed.files == 5);
        check("selo escrito ao lado", fs::exists(dir / "media.seal"));

        // O selo tem que ser reproduzivel: mesma pasta, mesmo conteudo, mesmos
        // bytes. Ordem de FindFirstFile nao pode vazar para o selo.
        std::string first_bytes;
        {
            std::ifstream in(dir / "media.seal", std::ios::binary);
            first_bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const auto resealed = deep::seal_media_dir(dir.wstring(), media_cipher);
        std::string second_bytes;
        {
            std::ifstream in(dir / "media.seal", std::ios::binary);
            second_bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        check("selar duas vezes da o mesmo selo",
              resealed.ok && first_bytes == second_bytes);

        const auto intact = deep::verify_media_dir(dir.wstring(), media_cipher);
        check("midia intacta passa", intact.ok && intact.files == 5);
        if (!intact.ok) std::printf("    erro: %s\n", intact.error.c_str());

        // Conteudo trocado.
        flip_byte(dir / "media" / "1_foto.jpg", 100);
        const auto changed = deep::verify_media_dir(dir.wstring(), media_cipher);
        check("conteudo trocado detectado", !changed.ok);
        check("aponta o arquivo certo",
              !changed.problems.empty() &&
                  changed.problems.front().find("media/1_foto.jpg") != std::string::npos);
        std::printf("    motivo: %s\n", changed.error.c_str());
        flip_byte(dir / "media" / "1_foto.jpg", 100);

        // Tamanho diferente no mesmo caminho.
        fs::resize_file(dir / "media" / "sub" / "3_audio.opus", 100);
        const auto resized = deep::verify_media_dir(dir.wstring(), media_cipher);
        check("truncamento detectado", !resized.ok);
        check("motivo fala de tamanho",
              !resized.problems.empty() &&
                  resized.problems.front().find("tamanho") != std::string::npos);
        write_pattern(dir / "media" / "sub" / "3_audio.opus", 1500, 4);

        // Arquivo removido: o selo cobre o que sumiu.
        const fs::path removido = dir / "media" / "sub" / "3_audio.opus";
        fs::remove(removido);
        const auto missing = deep::verify_media_dir(dir.wstring(), media_cipher);
        check("arquivo removido detectado", !missing.ok && missing.missing.size() == 1);
        check("aponta o removido",
              missing.missing.size() == 1 &&
                  missing.missing.front().find("3_audio.opus") != std::string::npos);
        write_pattern(removido, 1500, 4);

        // Arquivo novo na pasta: entrar sem selo tambem e adulteracao.
        write_pattern(dir / "media" / "5_intruso.jpg", 400, 6);
        const auto added = deep::verify_media_dir(dir.wstring(), media_cipher);
        check("arquivo novo detectado", !added.ok && added.added.size() == 1);
        check("aponta o intruso",
              added.added.size() == 1 && added.added.front().find("5_intruso.jpg") != std::string::npos);
        fs::remove(dir / "media" / "5_intruso.jpg");

        // Renomear: some um caminho e aparece outro.
        fs::rename(dir / "media" / "1_foto.jpg", dir / "media" / "1_foto_renomeada.jpg");
        const auto renamed = deep::verify_media_dir(dir.wstring(), media_cipher);
        check("renomeacao detectada", !renamed.ok && renamed.missing.size() == 1 &&
                                          renamed.added.size() == 1);
        fs::rename(dir / "media" / "1_foto_renomeada.jpg", dir / "media" / "1_foto.jpg");

        check("volta a passar depois de desfazer",
              deep::verify_media_dir(dir.wstring(), media_cipher).ok);

        // Adulterar o proprio selo tem que falhar no HMAC, nao passar batido.
        flip_byte(dir / "media.seal", 20);
        const auto seal_hit = deep::verify_media_dir(dir.wstring(), media_cipher);
        check("selo adulterado detectado", !seal_hit.ok);
        check("motivo fala de HMAC", seal_hit.error.find("HMAC") != std::string::npos ||
                                         seal_hit.error.find("autentica") != std::string::npos);
    }

    // 4. Pasta de midia vazia: selo valido, zero arquivos.
    {
        const fs::path dir = ".oracle/seal_media_vazia";
        fs::remove_all(dir);
        fs::create_directories(dir);
        deep::case_cipher empty_cipher;
        std::string empty_error;
        check("chave da pasta vazia",
              empty_cipher.open_or_create(std::wstring(L".oracle/seal_media_vazia.key"), empty_error));
        const auto sealed = deep::seal_media_dir(dir.wstring(), empty_cipher);
        check("sela pasta vazia", sealed.ok && sealed.files == 0);
        check("pasta vazia verifica", deep::verify_media_dir(dir.wstring(), empty_cipher).ok);
    }


    // 5. Indice do caso: fecha o buraco de "apagar a pasta leva o selo junto".
    {
        const fs::path dir = ".oracle/seal_index";
        fs::remove_all(dir);
        fs::create_directories(dir / "recovered" / "media");
        fs::create_directories(dir / "cloud" / "dropbox" / "conta");
        std::printf("  [--] indice do caso\n");

        deep::case_cipher idx_cipher;
        std::string idx_error;
        check("chave do caso indexado",
              idx_cipher.open_or_create(std::wstring(L".oracle/seal_index.key"), idx_error));

        write_pattern(dir / "case.db", 8192, 11);
        write_pattern(dir / "recovered" / "media" / "1.jpg", 2000, 12);
        write_pattern(dir / "cloud" / "dropbox" / "conta" / "m.json", 300, 13);

        check("sela o banco do caso", deep::seal_case_files(dir.wstring(), idx_cipher).ok);
        check("sela recovered", deep::seal_media_dir((dir / "recovered").wstring(), idx_cipher).ok);
        check("sela cloud", deep::seal_media_dir((dir / "cloud").wstring(), idx_cipher).ok);

        // Indice sem nenhum selo para indexar e recusado de proposito: um indice
        // vazio daria a impressao de caso coberto.
        check("indice sem selos e recusado",
              !deep::seal_case_index(std::wstring(L".oracle/seal_index_vazio"), idx_cipher).ok);

        const auto sealed_index = deep::seal_case_index(dir.wstring(), idx_cipher);
        check("sela o indice", sealed_index.ok && sealed_index.entries == 3);
        if (!sealed_index.ok) std::printf("    erro: %s\n", sealed_index.error.c_str());
        check("indice intacto passa", deep::verify_case_index(dir.wstring(), idx_cipher).ok);

        // Apagar a pasta INTEIRA leva o selo junto: era indistinguivel de "nunca
        // selado" antes do indice.
        const fs::path cloud_backup = ".oracle/seal_index_cloud";
        fs::remove_all(cloud_backup);
        fs::rename(dir / "cloud", cloud_backup);
        const auto gone = deep::verify_case_index(dir.wstring(), idx_cipher);
        check("pasta removida com o selo dentro e detectada", !gone.ok && gone.missing.size() == 1);
        check("aponta o selo que sumiu",
              gone.missing.size() == 1 &&
                  gone.missing.front().find("cloud/media.seal") != std::string::npos);
        std::printf("    motivo: %s\n", gone.error.c_str());
        fs::rename(cloud_backup, dir / "cloud");

        // Selo trocado no lugar (nao sumiu, mudou).
        flip_byte(dir / "recovered" / "media.seal", 10);
        const auto swapped = deep::verify_case_index(dir.wstring(), idx_cipher);
        check("selo trocado detectado", !swapped.ok && swapped.problems.size() == 1);
        flip_byte(dir / "recovered" / "media.seal", 10);

        // Selo novo que apareceu depois do indice.
        check("sela pasta nova", deep::seal_media_dir((dir / "recovered" / "media").wstring(), idx_cipher).ok);
        const auto extra = deep::verify_case_index(dir.wstring(), idx_cipher);
        check("selo fora do indice detectado", !extra.ok && extra.added.size() == 1);
        check("aponta o selo extra",
              extra.added.size() == 1 &&
                  extra.added.front().find("media/media.seal") != std::string::npos);
        fs::remove(dir / "recovered" / "media" / "media.seal");
        check("indice volta a fechar", deep::verify_case_index(dir.wstring(), idx_cipher).ok);

        // Indice adulterado tem que falhar no HMAC.
        flip_byte(dir / "case.seal", 12);
        const auto index_hit = deep::verify_case_index(dir.wstring(), idx_cipher);
        check("indice adulterado detectado", !index_hit.ok);
        check("motivo fala de autenticidade",
              index_hit.error.find("autentica") != std::string::npos ||
                  index_hit.error.find("HMAC") != std::string::npos);
    }


    // 6. Progresso por arquivo e escrita atomica do selo.
    {
        const fs::path dir = ".oracle/seal_progresso";
        fs::remove_all(dir);
        fs::create_directories(dir / "media");
        std::printf("  [--] progresso e escrita atomica\n");

        deep::case_cipher prog_cipher;
        std::string prog_error;
        check("chave do progresso",
              prog_cipher.open_or_create(std::wstring(L".oracle/seal_progresso.key"), prog_error));
        for (int i = 1; i <= 7; ++i) {
            write_pattern(dir / "media" / ("f" + std::to_string(i) + ".jpg"), 500 + i,
                          static_cast<unsigned char>(i));
        }

        std::uint64_t calls = 0;
        std::uint64_t max_files = 0, files_total_seen = 0;
        std::uint64_t last_bytes = 0, max_bytes = 0, bytes_total_seen = 0;
        bool ordered = true;
        const deep::seal_progress_fn progress = [&](std::uint64_t files_done,
                                                    std::uint64_t files_total,
                                                    std::uint64_t bytes_done,
                                                    std::uint64_t bytes_total) {
            ++calls;
            if (files_done > files_total) ordered = false;
            if (files_done < max_files) ordered = false;
            if (bytes_done > bytes_total) ordered = false;
            if (bytes_done < last_bytes) ordered = false;
            max_files = files_done > max_files ? files_done : max_files;
            files_total_seen = files_total;
            max_bytes = bytes_done > max_bytes ? bytes_done : max_bytes;
            last_bytes = bytes_done;
            bytes_total_seen = bytes_total;
        };

        const auto sealed = deep::seal_media_dir(dir.wstring(), prog_cipher, progress);
        check("sela avisando progresso", sealed.ok && sealed.files == 7);
        check("progresso avanca nos dois eixos e nao passa do total", ordered);
        check("progresso termina no total de arquivos",
              max_files == 7 && files_total_seen == 7);
        // Avisa mais de uma vez por arquivo (por bloco), nao so ao fechar cada um.
        check("progresso avisa por bloco, nao so por arquivo", calls > 7);
        check("progresso termina nos bytes lidos",
              max_bytes == bytes_total_seen && max_bytes == sealed.bytes);
        check("nenhum temporario sobrando", !fs::exists(dir / "media.seal.tmp"));

        // Temporario de uma selagem interrompida nao pode virar "arquivo extra".
        write_pattern(dir / "media.seal.tmp", 64, 9);
        check("temporario plantado e ignorado",
              deep::verify_media_dir(dir.wstring(), prog_cipher).ok);
        fs::remove(dir / "media.seal.tmp");

        // Selo antigo sobrevive a uma selagem que nao consegue publicar: segurar o
        // arquivo aberto faz o rename falhar, e o .tmp tem que sumir.
        std::printf("  [--] escrita atomica\n");
        HANDLE held = CreateFileW((dir / "media.seal").c_str(), GENERIC_READ, 0, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held != INVALID_HANDLE_VALUE) {
            const auto blocked = deep::seal_media_dir(dir.wstring(), prog_cipher);
            check("re-selagem com o selo travado falha", !blocked.ok);
            check("falha explica o rename", blocked.error.find("publicar") != std::string::npos);
            CloseHandle(held);
            check("selo anterior continua valido",
                  deep::verify_media_dir(dir.wstring(), prog_cipher).ok);
            check("travamento nao deixou temporario", !fs::exists(dir / "media.seal.tmp"));
        } else {
            check("abrir o selo para travar a re-selagem", false);
        }
    }

    std::printf(failures == 0 ? "SEAL OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
