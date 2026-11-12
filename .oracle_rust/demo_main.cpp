// demo_main.cpp — prova minima de que o g++ inclui o header e linka a staticlib.
//
//   g++ -std=c++17 -O2 demo_main.cpp -I rust/formatguard/include
//       rust/formatguard/target/release/libformatguard.a <libs do sistema> -o demo.exe
//
// Le um arquivo, chama fg_parse_plist e imprime o JSON devolvido por fg_plist_to_json.
#include <cstdio>
#include <string>
#include <vector>

#include "formatguard.h"

static bool read_file(const char* path, std::vector<unsigned char>& out) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr) return false;
    unsigned char buffer[64 * 1024];
    for (;;) {
        const size_t part = std::fread(buffer, 1, sizeof(buffer), file);
        if (part == 0) break;
        out.insert(out.end(), buffer, buffer + part);
    }
    std::fclose(file);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s <arquivo bplist00>\n", argv[0]);
        return 2;
    }
    std::vector<unsigned char> data;
    if (!read_file(argv[1], data)) {
        std::fprintf(stderr, "nao foi possivel ler %s\n", argv[1]);
        return 2;
    }
    std::printf("abi=%d versao=%s\n", fg_abi_version(), fg_version());

    fg_plist* handle = nullptr;
    const int32_t status =
        fg_parse_plist(data.empty() ? nullptr : data.data(), data.size(), &handle);
    if (status != FG_OK) {
        std::printf("parse recusado: status=%d (%s) detalhe=%s\n", status, fg_strerror(status),
                    fg_last_error());
        return 1;
    }
    char* json = nullptr;
    size_t json_len = 0;
    const int32_t json_status = fg_plist_to_json(handle, &json, &json_len);
    if (json_status != FG_OK) {
        std::printf("json recusado: status=%d (%s)\n", json_status, fg_strerror(json_status));
        fg_free_plist(handle);
        return 1;
    }
    std::printf("bytes=%zu json:\n%.*s\n", json_len, static_cast<int>(json_len), json);
    fg_free_string(json);
    fg_free_plist(handle);
    return 0;
}
