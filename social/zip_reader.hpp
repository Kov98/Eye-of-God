// language: C++, file: social/zip_reader.hpp, target: Windows 11 (MSVC / MinGW g++)
// Leitor de ZIP minimo, somente leitura, para o arquivo de exportacao de rede
// social ("Baixar suas informacoes").
//
// ESCOPO DELIBERADO
//   Metodos 0 (stored) e 8 (deflate), CRC-32 conferido, caminho do ZIP validado.
//   ZIP64 suportado nos dois lugares em que ele aparece: no EOCD64 (contagem,
//   tamanho ou deslocamento acima de 32 bits) e no campo extra 0x0001 de cada
//   entrada. Provado contra o zipfile do Python, que e implementacao
//   independente, com um arquivo de 70.000 entradas e outro com force_zip64.
//   O que continua de fora: ZIP multi-disco e entradas cujo tamanho real venha
//   apenas no data descriptor (o export do Meta nao usa nenhum dos dois).
//
// POR QUE NAO ZLIB
//   O projeto nao linka zlib. O unico inflate do repositorio vive em
//   android_backup.cpp (RFC 1951) e e reaproveitado aqui via inflate_deflate().
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::social {

struct zip_entry {
    std::string name;                 // caminho dentro do arquivo, com '/'
    std::uint16_t method = 0;         // 0 = stored, 8 = deflate
    std::uint32_t crc32 = 0;
    std::uint64_t size = 0;           // tamanho descomprimido
    std::uint64_t compressed_size = 0;
    std::uint64_t local_offset = 0;   // deslocamento do cabecalho local
};

class zip_archive {
public:
    // Abre somente o diretorio central; nada e descomprimido aqui.
    bool open(const std::wstring& path, std::string& error);
    void close();

    const std::vector<zip_entry>& entries() const { return entries_; }
    bool opened() const { return handle_ != nullptr; }

    // Extrai uma entrada para a memoria. Confere CRC-32 e tamanho; uma
    // divergencia e erro, nunca resultado parcial aceito.
    bool extract(const zip_entry& entry, std::string& out, std::string& error) const;

    ~zip_archive() { close(); }

private:
    bool read_at(std::uint64_t offset, void* buffer, std::size_t length,
                 std::string& error) const;

    void* handle_ = nullptr;   // HANDLE, sem incluir windows.h no header
    std::uint64_t file_size_ = 0;
    std::vector<zip_entry> entries_;
};

// CRC-32 (polinomio 0xEDB88320), a mesma conta que o ZIP carrega no trailer.
std::uint32_t crc32_of(const void* data, std::size_t length);

// Normaliza um caminho de entrada e recusa o que escaparia da pasta de destino:
// componente "..", barra inicial, letra de unidade e caractere de controle.
bool zip_path_is_safe(const std::string& name, std::string& reason);

}  // namespace olho::social
