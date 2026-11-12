// language: C++, file: .oracle/afc_test.cpp
#include "afc.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace olho::afc;

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}

static void put_u64(bytes& b, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

int main() {
    // 1. cabecalho: ida e volta
    {
        header h;
        h.entire_length = 40 + 12;
        h.this_length = 40 + 12;
        h.packet_num = 3;
        h.operation = static_cast<std::uint64_t>(op::read_dir);
        bytes wire;
        std::string err;
        check("encode header", encode_header(h, wire, err));
        check("cabecalho tem 40 bytes", wire.size() == 40);
        check("magic correto", std::memcmp(wire.data(), "CFA6LPAA", 8) == 0);

        header back;
        check("decode header", decode_header(wire.data(), wire.size(), back, err));
        check("packet_num preservado", back.packet_num == 3);
        check("operacao preservada", back.operation_kind() == op::read_dir);
        check("comprimentos preservados", back.this_length == 52 && back.entire_length == 52);
    }

    // 2. pedido ReadDir: path + NUL depois do cabecalho
    {
        const bytes req = read_dir_request("/", 1);
        check("tamanho do pedido", req.size() == 40 + 2);
        check("path terminado em NUL", req.back() == 0 && req[40] == '/');
        header h;
        std::string err;
        check("pedido tem cabecalho valido", decode_header(req.data(), req.size(), h, err));
        check("op = ReadDir", h.operation_kind() == op::read_dir);
        check("this_length cobre o payload", h.this_length == req.size());
    }

    // 3. status
    {
        bytes ok_payload;
        put_u64(ok_payload, 0);
        status st;
        check("status 0 = sucesso", parse_status(ok_payload, st) && st.ok);

        bytes bad;
        put_u64(bad, 7);
        check("status 7 = recusa", !parse_status(bad, st) && st.code == 7);

        check("status truncado recusado", !parse_status(bytes{1, 2, 3}, st));
    }

    // 4. listagem de diretorio com structs de 40 bytes
    {
        bytes payload;
        put_u64(payload, 0);                       // status
        const char* names[] = {".", "..", "Documents", "tmp"};
        for (const char* n : names) {
            payload.insert(payload.end(), n, n + std::strlen(n));
            payload.push_back(0);
            put_u64(payload, 1700000000000000000ull);  // mtime ns
            put_u64(payload, 1234);                    // size
            put_u64(payload, 0);
            put_u64(payload, 0);
            put_u64(payload, 0);                       // 40 bytes no total
        }
        std::vector<dir_entry> entries;
        status st;
        check("parse ReadDir", parse_dir_listing(payload, entries, st));
        check("quatro entradas", entries.size() == 4);
        if (entries.size() == 4) {
            check("nomes na ordem", entries[2].name == "Documents" && entries[3].name == "tmp");
            check("size lido", entries[2].size == 1234 && entries[2].size_known);
            check("mtime lido", entries[2].mtime_ns == 1700000000000000000ull);
            check("file_info cru guardado", entries[2].file_info.size() == 40);
        }

        // recusa: status diferente de zero
        bytes denied;
        put_u64(denied, 2);
        std::vector<dir_entry> none;
        check("ReadDir recusado no status", !parse_dir_listing(denied, none, st) && st.code == 2);
    }

    // 5. device info: pares chave/valor terminados por string vazia
    {
        bytes payload;
        put_u64(payload, 0);
        auto add = [&payload](const char* s) {
            payload.insert(payload.end(), s, s + std::strlen(s));
            payload.push_back(0);
        };
        add("Model"); add("iPhone12,1");
        add("FSTotalBytes"); add("63997952");
        add("SerialNumber"); add("F17ABCDEFGHI");
        add("");

        std::vector<std::pair<std::string, std::string>> pairs;
        status st;
        check("parse DeviceInfo", parse_device_info(payload, pairs, st));
        check("tres pares", pairs.size() == 3);
        if (pairs.size() == 3) {
            check("par 1", pairs[0].first == "Model" && pairs[0].second == "iPhone12,1");
            check("par 3", pairs[2].first == "SerialNumber" && pairs[2].second == "F17ABCDEFGHI");
        }
    }

    // 6. FileOpen
    {
        bytes good;
        put_u64(good, 0x10);
        std::uint64_t handle = 0;
        status st;
        check("FileOpen devolve handle", parse_file_open(good, handle, st) && handle == 0x10);

        bytes refused;
        put_u64(refused, 0);
        check("FileOpen com 0 recusado", !parse_file_open(refused, handle, st));
    }

    // 7. hostis
    {
        header h;
        std::string err;
        bytes bad_magic(40, 0);
        std::memcpy(bad_magic.data(), "XXXXXXXX", 8);
        check("magic errado recusado", !decode_header(bad_magic.data(), bad_magic.size(), h, err));

        bytes short_header(20, 0);
        check("cabecalho truncado recusado", !decode_header(short_header.data(), short_header.size(), h, err));

        header liar;
        liar.this_length = 10;          // menor que o cabecalho
        liar.entire_length = 10;
        bytes out;
        check("comprimento impossivel recusado", !encode_header(liar, out, err));

        bytes inconsistent(40, 0);
        std::memcpy(inconsistent.data(), "CFA6LPAA", 8);
        // entire_length = 40, this_length = 80  -> incoerente
        for (int i = 0; i < 8; ++i) inconsistent[8 + i] = (i == 0) ? 40 : 0;
        for (int i = 0; i < 8; ++i) inconsistent[16 + i] = (i == 0) ? 80 : 0;
        check("entire < this recusado", !decode_header(inconsistent.data(), inconsistent.size(), h, err));
    }

    std::printf(failures == 0 ? "AFC OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}