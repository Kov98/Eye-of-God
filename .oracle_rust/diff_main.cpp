// diff_main.cpp — teste diferencial: parser C++ de referencia (ios_backup.cpp) contra o
// parser Rust (libformatguard.a), nos dois sentidos que importam:
//
//   1. os dois concordam em aceitar ou recusar cada entrada;
//   2. quando os dois aceitam, o JSON canonico sai byte a byte identico.
//
// O JSON do lado C++ e produzido aqui, seguindo exatamente o contrato documentado em
// rust/formatguard/src/json.rs (real = expansao decimal exata, data = segundos Apple,
// data binario = hex, uid/set com chave reservada, chave de dict com '$' escapado).
//
// Uso: diff_main --list <arquivo com um caminho por linha> [--start N] [--verbose]
// Saida: uma linha por arquivo, mais um resumo no fim.
// Codigo de saida: 0 = tudo concordou, 1 = alguma divergencia, 2 = uso/IO.
#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>

#include "formatguard.h"
#include "ios_backup.hpp"

namespace {

using Bytes = std::vector<std::uint8_t>;

// --------------------------------------------------------------- texto/escape
bool is_valid_utf8(const Bytes& bytes) {
    size_t index = 0;
    while (index < bytes.size()) {
        const std::uint8_t lead = bytes[index];
        size_t extra = 0;
        if (lead < 0x80) {
            index += 1;
            continue;
        }
        if (lead >= 0xC2 && lead <= 0xDF) {
            extra = 1;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            extra = 2;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            extra = 3;
        } else {
            return false;
        }
        if (index + extra >= bytes.size()) return false;
        const std::uint8_t second = bytes[index + 1];
        if (lead == 0xE0) {
            if (second < 0xA0 || second > 0xBF) return false;
        } else if (lead == 0xED) {
            if (second < 0x80 || second > 0x9F) return false;
        } else if (lead == 0xF0) {
            if (second < 0x90 || second > 0xBF) return false;
        } else if (lead == 0xF4) {
            if (second < 0x80 || second > 0x8F) return false;
        } else if (second < 0x80 || second > 0xBF) {
            return false;
        }
        for (size_t k = 2; k <= extra; ++k) {
            const std::uint8_t continuation = bytes[index + k];
            if (continuation < 0x80 || continuation > 0xBF) return false;
        }
        index += extra + 1;
    }
    return true;
}

void append_unicode_escape(std::string& out, std::uint16_t code) {
    static const char* digits = "0123456789abcdef";
    out += "\\u";
    out.push_back(digits[(code >> 12) & 0x0F]);
    out.push_back(digits[(code >> 8) & 0x0F]);
    out.push_back(digits[(code >> 4) & 0x0F]);
    out.push_back(digits[code & 0x0F]);
}

void append_control_escape(std::string& out, std::uint8_t code) {
    switch (code) {
        case 0x08: out += "\\b"; break;
        case 0x0C: out += "\\f"; break;
        case 0x0A: out += "\\n"; break;
        case 0x0D: out += "\\r"; break;
        case 0x09: out += "\\t"; break;
        default: append_unicode_escape(out, code); break;
    }
}

void append_escaped(std::string& out, const Bytes& bytes) {
    if (is_valid_utf8(bytes)) {
        size_t index = 0;
        while (index < bytes.size()) {
            const std::uint8_t lead = bytes[index];
            size_t size = 1;
            if (lead >= 0xF0) {
                size = 4;
            } else if (lead >= 0xE0) {
                size = 3;
            } else if (lead >= 0xC0) {
                size = 2;
            }
            if (size == 1) {
                if (lead == '"') {
                    out += "\\\"";
                } else if (lead == '\\') {
                    out += "\\\\";
                } else if (lead < 0x20) {
                    append_control_escape(out, lead);
                } else {
                    out.push_back(static_cast<char>(lead));
                }
            } else {
                out.append(reinterpret_cast<const char*>(bytes.data() + index), size);
            }
            index += size;
        }
        return;
    }
    for (const std::uint8_t byte : bytes) {
        if (byte == '"') {
            out += "\\\"";
        } else if (byte == '\\') {
            out += "\\\\";
        } else if (byte >= 0x20 && byte <= 0x7E) {
            out.push_back(static_cast<char>(byte));
        } else if (byte < 0x20) {
            append_control_escape(out, byte);
        } else {
            append_unicode_escape(out, byte);
        }
    }
}

// ------------------------------------------------------------- real exato
void mul_small(std::vector<std::uint8_t>& dec, std::uint32_t factor) {
    std::uint32_t carry = 0;
    for (size_t index = 0; index < dec.size(); ++index) {
        const std::uint32_t current = static_cast<std::uint32_t>(dec[index]) * factor + carry;
        dec[index] = static_cast<std::uint8_t>(current % 10);
        carry = current / 10;
    }
    while (carry > 0) {
        dec.push_back(static_cast<std::uint8_t>(carry % 10));
        carry /= 10;
    }
}

std::string dec_text(const std::vector<std::uint8_t>& dec) {
    std::string text;
    text.reserve(dec.size());
    for (size_t index = dec.size(); index > 0; --index) text.push_back(static_cast<char>('0' + dec[index - 1]));
    return text;
}

std::string exact_real(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value < 0.0 ? "-inf" : "inf";
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, 8);
    const int exp_field = static_cast<int>((bits >> 52) & 0x7FF);
    const std::uint64_t frac = bits & ((1ULL << 52) - 1);
    const bool negative = (bits >> 63) != 0;
    if (exp_field == 0 && frac == 0) return negative ? "-0" : "0";
    const int exp2 = (exp_field == 0) ? -1074 : exp_field - 1075;
    const std::uint64_t mantissa = (exp_field == 0) ? frac : (frac | (1ULL << 52));
    std::vector<std::uint8_t> dec;
    {
        std::uint64_t rest = mantissa;
        if (rest == 0) dec.push_back(0);
        while (rest > 0) {
            dec.push_back(static_cast<std::uint8_t>(rest % 10));
            rest /= 10;
        }
    }
    std::string out;
    if (exp2 >= 0) {
        for (int step = 0; step < exp2; ++step) mul_small(dec, 2);
        out = dec_text(dec);
    } else {
        const int scale = -exp2;
        for (int step = 0; step < scale; ++step) mul_small(dec, 5);
        const std::string digits = dec_text(dec);
        if (static_cast<int>(digits.size()) <= scale) {
            out = "0.";
            out.append(static_cast<size_t>(scale - static_cast<int>(digits.size())), '0');
            out.append(digits);
        } else {
            const size_t whole = digits.size() - static_cast<size_t>(scale);
            out.append(digits, 0, whole);
            out.push_back('.');
            out.append(digits, whole, std::string::npos);
        }
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (negative && !out.empty()) out.insert(out.begin(), '-');
    return out;
}

std::string hex_of(const Bytes& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0F]);
    }
    return out;
}

// ------------------------------------------------------- JSON do parser C++
std::string quoted(const std::string& text) {
    const Bytes bytes(text.begin(), text.end());
    std::string out = "\"";
    append_escaped(out, bytes);
    out.push_back('"');
    return out;
}

void write_key(std::string& out, const std::string& name) {
    const Bytes bytes(name.begin(), name.end());
    out.push_back('"');
    if (!bytes.empty() && bytes.front() == '$') {
        out += "\\u0024";
        const Bytes rest(bytes.begin() + 1, bytes.end());
        append_escaped(out, rest);
    } else {
        append_escaped(out, bytes);
    }
    out.push_back('"');
}

std::string real_json(double value) {
    const std::string text = exact_real(value);
    if (std::isfinite(value)) return text;
    return "\"" + text + "\"";
}

std::string ref_json(const olho::ios::PlistValue& value, int depth = 0) {
    if (depth > 70) return "\"<profundo demais>\"";
    using olho::ios::PlistKind;
    switch (value.kind) {
        case PlistKind::Null:
            return "null";
        case PlistKind::Bool:
            return value.boolean ? "true" : "false";
        case PlistKind::Integer:
            return std::to_string(value.integer);
        case PlistKind::Real:
            return real_json(value.real);
        case PlistKind::Date:
            return "{\"$date\":" + real_json(value.real) + "}";
        case PlistKind::Data:
            return "{\"$data\":\"" + hex_of(value.bytes) + "\"}";
        case PlistKind::String:
            return quoted(value.text);
        case PlistKind::Uid:
            return "{\"$uid\":" + std::to_string(value.uid) + "}";
        case PlistKind::Array:
        case PlistKind::Set: {
            std::string out;
            if (value.kind == PlistKind::Set) out += "{\"$set\":";
            out.push_back('[');
            for (size_t index = 0; index < value.items.size(); ++index) {
                if (index > 0) out.push_back(',');
                out += ref_json(value.items[index], depth + 1);
            }
            out.push_back(']');
            if (value.kind == PlistKind::Set) out.push_back('}');
            return out;
        }
        case PlistKind::Dict: {
            std::string out = "{";
            for (size_t index = 0; index < value.entries.size(); ++index) {
                if (index > 0) out.push_back(',');
                write_key(out, value.entries[index].first);
                out.push_back(':');
                out += ref_json(value.entries[index].second, depth + 1);
            }
            out.push_back('}');
            return out;
        }
    }
    return "\"<tipo desconhecido>\"";
}

// ------------------------------------------------------------------ util
// Caminho UTF-8 -> wide: o fopen do mingw nao aceita UTF-8, e o corpus tem nomes
// com acento (fixture do oraculo iOS).
FILE* open_utf8(const std::string& path) {
    const int needed = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()),
                                           nullptr, 0);
    if (needed <= 0) return nullptr;
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), wide.data(), needed);
    return _wfopen(wide.c_str(), L"rb");
}

bool read_file(const std::string& path, Bytes& out) {
    FILE* file = open_utf8(path);
    if (file == nullptr) return false;
    std::uint8_t buffer[64 * 1024];
    for (;;) {
        const size_t part = std::fread(buffer, 1, sizeof(buffer), file);
        if (part == 0) break;
        out.insert(out.end(), buffer, buffer + part);
    }
    std::fclose(file);
    return true;
}

std::vector<std::string> read_list(const std::string& path) {
    std::vector<std::string> files;
    FILE* file = open_utf8(path);
    if (file == nullptr) return files;
    std::string line;
    for (;;) {
        const int ch = std::fgetc(file);
        if (ch == EOF) {
            if (!line.empty()) files.push_back(line);
            break;
        }
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) files.push_back(line);
            line.clear();
            continue;
        }
        line.push_back(static_cast<char>(ch));
    }
    std::fclose(file);
    // BOM UTF-8 na primeira linha (PowerShell grava com BOM por padrao) nao faz parte
    // do caminho.
    if (!files.empty() && files.front().size() >= 3 &&
        static_cast<unsigned char>(files.front()[0]) == 0xEF &&
        static_cast<unsigned char>(files.front()[1]) == 0xBB &&
        static_cast<unsigned char>(files.front()[2]) == 0xBF) {
        files.front().erase(0, 3);
    }
    return files;
}

const char* first_difference(const std::string& left, const std::string& right, size_t& position) {
    const size_t limit = std::min(left.size(), right.size());
    for (size_t index = 0; index < limit; ++index) {
        if (left[index] != right[index]) {
            position = index;
            return "conteudo";
        }
    }
    position = limit;
    return "tamanho";
}

struct Stats {
    std::uint64_t both_accepted_same_json = 0;
    std::uint64_t both_rejected = 0;
    std::uint64_t json_differs = 0;
    std::uint64_t cpp_ok_rust_rejected = 0;
    std::uint64_t rust_ok_cpp_rejected = 0;
    std::uint64_t io_errors = 0;
    std::uint64_t divergence() const {
        return json_differs + cpp_ok_rust_rejected + rust_ok_cpp_rejected;
    }
};

void process(const std::string& path, Stats& stats, bool verbose) {
    Bytes data;
    if (!read_file(path, data)) {
        stats.io_errors += 1;
        std::printf("IO\t%s\n", path.c_str());
        std::fflush(stdout);
        return;
    }
    olho::ios::PlistValue tree;
    std::string cpp_error;
    const bool cpp_ok = olho::ios::plist_parse(data, tree, cpp_error);

    fg_plist* handle = nullptr;
    const std::int32_t rust_status =
        fg_parse_plist(data.empty() ? nullptr : data.data(), data.size(), &handle);
    const bool rust_ok = rust_status == FG_OK && handle != nullptr;

    if (cpp_ok && rust_ok) {
        std::string rust_json;
        char* json = nullptr;
        size_t json_len = 0;
        if (fg_plist_to_json(handle, &json, &json_len) == FG_OK && json != nullptr) {
            rust_json.assign(json, json_len);
            fg_free_string(json);
        }
        const std::string cpp_json = ref_json(tree);
        if (cpp_json == rust_json) {
            stats.both_accepted_same_json += 1;
            std::printf("OK\t%s\n", path.c_str());
        } else {
            stats.json_differs += 1;
            size_t position = 0;
            const char* why = first_difference(cpp_json, rust_json, position);
            std::printf("DIFF-JSON\t%s\tdiferenca_de_%s_em_%zu\n", path.c_str(), why, position);
            if (verbose) {
                std::printf("  cpp : %s\n", cpp_json.c_str());
                std::printf("  rust: %s\n", rust_json.c_str());
            }
        }
    } else if (!cpp_ok && !rust_ok) {
        stats.both_rejected += 1;
        std::printf("OK-REJ\t%s\n", path.c_str());
        if (verbose) {
            std::printf("  cpp : %s\n", cpp_error.c_str());
            std::printf("  rust: %s\n", fg_last_error());
        }
    } else if (cpp_ok) {
        stats.cpp_ok_rust_rejected += 1;
        std::printf("DIVERGE\t%s\tcpp_aceitou_rust_recusou\t%s\n", path.c_str(), fg_last_error());
    } else {
        stats.rust_ok_cpp_rejected += 1;
        std::printf("DIVERGE\t%s\trust_aceitou_cpp_recusou\t%s\n", path.c_str(), cpp_error.c_str());
    }
    fg_free_plist(handle);
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    std::string list_path;
    int start = 0;
    bool verbose = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--list" && index + 1 < argc) {
            list_path = argv[++index];
        } else if (argument == "--start" && index + 1 < argc) {
            start = std::atoi(argv[++index]);
        } else if (argument == "--verbose") {
            verbose = true;
        } else {
            std::fprintf(stderr, "argumento desconhecido: %s\n", argument.c_str());
            return 2;
        }
    }
    if (list_path.empty()) {
        std::fprintf(stderr, "uso: diff_main --list <arquivo> [--start N] [--verbose]\n");
        return 2;
    }
    const std::vector<std::string> files = read_list(list_path);
    if (files.empty()) {
        std::fprintf(stderr, "lista vazia: %s\n", list_path.c_str());
        return 2;
    }
    Stats stats;
    for (size_t index = static_cast<size_t>(start); index < files.size(); ++index) {
        process(files[index], stats, verbose);
    }
    std::printf("RESUMO\tarquivos=%zu\tok_mesmo_json=%" PRIu64 "\tok_recusados=%" PRIu64
                "\tjson_divergente=%" PRIu64 "\tcpp_ok_rust_recusou=%" PRIu64
                "\trust_ok_cpp_recusou=%" PRIu64 "\tio=%" PRIu64 "\tdivergencias=%" PRIu64 "\n",
                files.size(), stats.both_accepted_same_json, stats.both_rejected,
                stats.json_differs, stats.cpp_ok_rust_rejected, stats.rust_ok_cpp_rejected,
                stats.io_errors, stats.divergence());
    std::fflush(stdout);
    return stats.divergence() == 0 ? 0 : 1;
}
