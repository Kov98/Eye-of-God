#define NOMINMAX

#include "ios_backup.hpp"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <memory>

#include "sqlite3.h"

namespace olho::ios {
namespace {

constexpr std::size_t kMaxPlistDepth = 64;
// Node budget for one parse. A real Manifest.plist is far below this; the point
// is that the cost stops being exponential, not that the limit is tight.
constexpr std::uint64_t kMaxPlistNodes = 1u << 20;

// ------------------------------------------------------------------ texto/UTF-8
std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0],
                        needed);
    return out;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0],
                        needed, nullptr, nullptr);
    return out;
}

std::string utf16be_to_utf8(const std::uint8_t* data, std::size_t units) {
    std::wstring wide;
    wide.reserve(units);
    for (std::size_t i = 0; i < units; ++i) {
        const std::uint16_t unit = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(data[i * 2]) << 8) | data[i * 2 + 1]);
        wide.push_back(static_cast<wchar_t>(unit));
    }
    return wide_to_utf8(wide);
}

std::string last_error_text(DWORD code) {
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string text;
    if (length != 0 && buffer != nullptr) {
        std::wstring wide(buffer, length);
        while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n')) wide.pop_back();
        text = wide_to_utf8(wide);
    }
    if (buffer) LocalFree(buffer);
    if (text.empty()) text = "codigo " + std::to_string(code);
    return text;
}

// ------------------------------------------------------------------ arquivos
bool win_read_file(const std::wstring& path, Bytes& out, std::string& error) {
    out.clear();
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel abrir " + wide_to_utf8(path) + ": " +
                last_error_text(GetLastError());
        return false;
    }
    LARGE_INTEGER size;
    size.QuadPart = 0;
    if (GetFileSizeEx(handle, &size) && size.QuadPart > 0) {
        out.reserve(static_cast<std::size_t>(size.QuadPart));
    }
    std::uint8_t buffer[64 * 1024];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(handle, buffer, sizeof(buffer), &read, nullptr)) {
            error = "falha de leitura em " + wide_to_utf8(path) + ": " +
                    last_error_text(GetLastError());
            CloseHandle(handle);
            return false;
        }
        if (read == 0) break;
        out.insert(out.end(), buffer, buffer + read);
    }
    CloseHandle(handle);
    return true;
}

bool win_write_file(const std::wstring& path, const Bytes& data, std::string& error) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel criar " + wide_to_utf8(path) + ": " +
                last_error_text(GetLastError());
        return false;
    }
    std::size_t written = 0;
    while (written < data.size()) {
        const std::size_t chunk =
            (data.size() - written) > (1u << 20) ? (1u << 20) : (data.size() - written);
        DWORD part = 0;
        if (!WriteFile(handle, data.data() + written, static_cast<DWORD>(chunk), &part,
                       nullptr) ||
            part != chunk) {
            error = "falha de escrita em " + wide_to_utf8(path) + ": " +
                    last_error_text(GetLastError());
            CloseHandle(handle);
            return false;
        }
        written += part;
    }
    CloseHandle(handle);
    return true;
}

bool win_create_dirs(const std::wstring& path, std::string& error) {
    if (path.empty()) return true;
    std::wstring partial;
    partial.reserve(path.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        const wchar_t c = path[i];
        partial.push_back(c);
        const bool separator = (c == L'\\' || c == L'/');
        if (!separator) continue;
        if (partial.size() <= 1) continue;
        if (partial.size() == 3 && partial[1] == L':') continue;   // "C:\"
        if (!CreateDirectoryW(partial.c_str(), nullptr)) {
            const DWORD code = GetLastError();
            if (code != ERROR_ALREADY_EXISTS) {
                error = "nao foi possivel criar " + wide_to_utf8(partial) + ": " +
                        last_error_text(code);
                return false;
            }
        }
    }
    if (!CreateDirectoryW(path.c_str(), nullptr)) {
        const DWORD code = GetLastError();
        if (code != ERROR_ALREADY_EXISTS) {
            error = "nao foi possivel criar " + wide_to_utf8(path) + ": " +
                    last_error_text(code);
            return false;
        }
    }
    return true;
}

bool win_is_directory(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::string join_path(const std::string& base, const std::string& child) {
    if (base.empty()) return child;
    if (child.empty()) return base;
    if (base.back() == '/' || base.back() == '\\') return base + child;
    return base + "/" + child;
}

// Um relativePath hostil pode escapar do diretorio de saida; nada e confiado.
bool sanitize_relative(const std::string& raw, std::string& out, std::string& error) {
    out.clear();
    std::string normalized;
    normalized.reserve(raw.size());
    for (char c : raw) normalized.push_back(c == '\\' ? '/' : c);

    std::size_t start = 0;
    while (start <= normalized.size()) {
        const std::size_t slash = normalized.find('/', start);
        const std::string part = normalized.substr(
            start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                error = "caminho relativo com '..' recusado: " + raw;
                return false;
            }
            for (char c : part) {
                if (c == ':' || static_cast<unsigned char>(c) < 0x20) {
                    error = "caminho relativo com caractere invalido recusado: " + raw;
                    return false;
                }
            }
            if (!out.empty()) out.push_back('/');
            out += part;
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

// ------------------------------------------------------------------ binary plist
class PlistParser {
public:
    explicit PlistParser(const Bytes& blob) : data_(blob) {}

    bool parse(PlistValue& out, std::string& error) {
        if (!read_trailer(error)) return false;
        return parse_object(top_object_, out, 0, error);
    }

private:
    std::uint64_t nodes_visited_ = 0;

    bool read_uint(std::size_t pos, std::size_t size, std::uint64_t& out) const {
        if (size == 0 || size > 8 || pos + size > data_.size()) return false;
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < size; ++i) {
            value = (value << 8) | static_cast<std::uint64_t>(data_[pos + i]);
        }
        out = value;
        return true;
    }

    bool read_trailer(std::string& error) {
        if (data_.size() < 40) {
            error = "blob pequeno demais para ser um binary plist";
            return false;
        }
        if (std::memcmp(data_.data(), "bplist00", 8) != 0) {
            if (data_.size() >= 5 && std::memcmp(data_.data(), "<?xml", 5) == 0) {
                error = "plist em XML: este parser cobre apenas bplist00";
            } else if (data_.size() >= 6 && std::memcmp(data_.data(), "bplist", 6) == 0) {
                error = "versao de binary plist nao suportada (esperado bplist00)";
            } else {
                error = "assinatura bplist00 ausente";
            }
            return false;
        }
        const std::size_t trailer = data_.size() - 32;
        offset_size_ = data_[trailer + 6];
        ref_size_ = data_[trailer + 7];
        if (offset_size_ < 1 || offset_size_ > 8 || ref_size_ < 1 || ref_size_ > 8) {
            error = "trailer com tamanhos de inteiro invalidos";
            return false;
        }
        if (!read_uint(trailer + 8, 8, object_count_) ||
            !read_uint(trailer + 16, 8, top_object_) ||
            !read_uint(trailer + 24, 8, offset_table_)) {
            error = "trailer truncado";
            return false;
        }
        if (object_count_ == 0 || object_count_ > data_.size()) {
            error = "trailer declara contagem de objetos absurda";
            return false;
        }
        if (top_object_ >= object_count_) {
            error = "trailer aponta objeto raiz inexistente";
            return false;
        }
        if (offset_table_ >= trailer ||
            offset_table_ + object_count_ * offset_size_ > data_.size()) {
            error = "offset table fora do blob";
            return false;
        }
        offsets_.resize(static_cast<std::size_t>(object_count_));
        for (std::uint64_t i = 0; i < object_count_; ++i) {
            if (!read_uint(offset_table_ + static_cast<std::size_t>(i) * offset_size_,
                           offset_size_, offsets_[static_cast<std::size_t>(i)])) {
                error = "offset table truncada";
                return false;
            }
        }
        return true;
    }

    bool read_length(std::size_t& pos, std::uint8_t info, std::uint64_t& count,
                     std::string& error) const {
        if (info != 0x0F) {
            count = info;
            return true;
        }
        if (pos >= data_.size()) {
            error = "comprimento estendido truncado";
            return false;
        }
        const std::uint8_t marker = data_[pos];
        if ((marker >> 4) != 0x1) {
            error = "comprimento estendido sem marcador de inteiro";
            return false;
        }
        const std::size_t width = static_cast<std::size_t>(1) << (marker & 0x0F);
        ++pos;
        if (!read_uint(pos, width, count)) {
            error = "comprimento estendido fora do blob";
            return false;
        }
        pos += width;
        return true;
    }

    bool parse_object(std::uint64_t index, PlistValue& out, int depth, std::string& error) {
        if (depth > static_cast<int>(kMaxPlistDepth)) {
            error = "binary plist com aninhamento excessivo";
            return false;
        }
        // Depth alone does not bound the work: a plist of 153 bytes whose objects
        // reference each other without forming a cycle expands to 2^N nodes inside
        // the depth limit (measured: 2.097.151 nodes in 341 ms). The budget is what
        // makes the cost linear in the input.
        if (++nodes_visited_ > kMaxPlistNodes) {
            error = "binary plist com objetos demais (orcamento de nos)";
            return false;
        }
        if (index >= offsets_.size()) {
            error = "referencia de objeto fora da tabela";
            return false;
        }
        std::size_t pos = static_cast<std::size_t>(offsets_[static_cast<std::size_t>(index)]);
        if (pos >= data_.size()) {
            error = "offset de objeto fora do blob";
            return false;
        }
        const std::uint8_t marker = data_[pos++];
        const std::uint8_t type = static_cast<std::uint8_t>(marker >> 4);
        const std::uint8_t info = static_cast<std::uint8_t>(marker & 0x0F);

        switch (type) {
            case 0x0:
                switch (info) {
                    case 0x0:
                    case 0xF:
                        out.kind = PlistKind::Null;
                        return true;
                    case 0x8:
                        out.kind = PlistKind::Bool;
                        out.boolean = false;
                        return true;
                    case 0x9:
                        out.kind = PlistKind::Bool;
                        out.boolean = true;
                        return true;
                    default:
                        error = "marcador nulo invalido no binary plist";
                        return false;
                }
            case 0x1: {
                const std::size_t width = static_cast<std::size_t>(1) << info;
                std::uint64_t value = 0;
                if (!read_uint(pos, width, value)) {
                    error = "inteiro truncado";
                    return false;
                }
                out.kind = PlistKind::Integer;
                out.integer = static_cast<std::int64_t>(value);
                return true;
            }
            case 0x2: {
                const std::size_t width = static_cast<std::size_t>(1) << info;
                std::uint64_t raw = 0;
                if (!read_uint(pos, width, raw)) {
                    error = "real truncado";
                    return false;
                }
                out.kind = PlistKind::Real;
                if (width == 4) {
                    const std::uint32_t bits = static_cast<std::uint32_t>(raw);
                    float value = 0.0f;
                    std::memcpy(&value, &bits, sizeof(value));
                    out.real = static_cast<double>(value);
                } else if (width == 8) {
                    std::memcpy(&out.real, &raw, sizeof(out.real));
                } else {
                    error = "largura de real nao suportada";
                    return false;
                }
                return true;
            }
            case 0x3: {
                if (info != 0x3) {
                    error = "marcador de data invalido";
                    return false;
                }
                std::uint64_t raw = 0;
                if (!read_uint(pos, 8, raw)) {
                    error = "data truncada";
                    return false;
                }
                out.kind = PlistKind::Date;
                std::memcpy(&out.real, &raw, sizeof(out.real));
                return true;
            }
            case 0x4: {
                std::uint64_t count = 0;
                if (!read_length(pos, info, count, error)) return false;
                if (count > data_.size() - pos) {
                    error = "bloco de dados maior que o blob";
                    return false;
                }
                out.kind = PlistKind::Data;
                out.bytes.assign(data_.begin() + static_cast<std::ptrdiff_t>(pos),
                                 data_.begin() + static_cast<std::ptrdiff_t>(pos + count));
                return true;
            }
            case 0x5: {
                std::uint64_t count = 0;
                if (!read_length(pos, info, count, error)) return false;
                if (count > data_.size() - pos) {
                    error = "string ASCII maior que o blob";
                    return false;
                }
                out.kind = PlistKind::String;
                out.text.assign(reinterpret_cast<const char*>(data_.data() + pos),
                                static_cast<std::size_t>(count));
                return true;
            }
            case 0x6: {
                std::uint64_t units = 0;
                if (!read_length(pos, info, units, error)) return false;
                // units * 2 overflows for units > 2^63, and the guard below then
                // passes on a lie. Divide instead of multiplying: the check has
                // to be the one that cannot wrap.
                if (units > (data_.size() - pos) / 2) {
                    error = "string UTF-16 maior que o blob";
                    return false;
                }
                out.kind = PlistKind::String;
                out.text = utf16be_to_utf8(data_.data() + pos, static_cast<std::size_t>(units));
                return true;
            }
            case 0x8: {
                const std::size_t width = static_cast<std::size_t>(info) + 1;
                std::uint64_t value = 0;
                if (!read_uint(pos, width, value)) {
                    error = "UID truncado";
                    return false;
                }
                out.kind = PlistKind::Uid;
                out.uid = value;
                return true;
            }
            case 0xA:
            case 0xB:
            case 0xC: {
                std::uint64_t count = 0;
                if (!read_length(pos, info, count, error)) return false;
                if (count > (data_.size() - pos) / ref_size_) {
                    error = "array maior que o blob";
                    return false;
                }
                out.kind = (type == 0xC) ? PlistKind::Set : PlistKind::Array;
                out.items.resize(static_cast<std::size_t>(count));
                for (std::uint64_t i = 0; i < count; ++i) {
                    std::uint64_t ref = 0;
                    if (!read_uint(pos, ref_size_, ref)) {
                        error = "referencia de array truncada";
                        return false;
                    }
                    pos += ref_size_;
                    if (!parse_object(ref, out.items[static_cast<std::size_t>(i)], depth + 1,
                                      error)) {
                        return false;
                    }
                }
                return true;
            }
            case 0xD: {
                std::uint64_t count = 0;
                if (!read_length(pos, info, count, error)) return false;
                if (count > (data_.size() - pos) / (ref_size_ * 2)) {
                    error = "dicionario maior que o blob";
                    return false;
                }
                std::vector<std::uint64_t> keys(static_cast<std::size_t>(count));
                for (std::uint64_t i = 0; i < count; ++i) {
                    if (!read_uint(pos, ref_size_, keys[static_cast<std::size_t>(i)])) {
                        error = "chave de dicionario truncada";
                        return false;
                    }
                    pos += ref_size_;
                }
                out.kind = PlistKind::Dict;
                out.entries.reserve(static_cast<std::size_t>(count));
                for (std::uint64_t i = 0; i < count; ++i) {
                    PlistValue key;
                    if (!parse_object(keys[static_cast<std::size_t>(i)], key, depth + 1,
                                      error)) {
                        return false;
                    }
                    std::string name;
                    if (key.kind == PlistKind::String) {
                        name = key.text;
                    } else if (key.kind == PlistKind::Data) {
                        name = bytes_to_hex(key.bytes);
                    } else {
                        name = "<chave " + plist_kind_name(key.kind) + ">";
                    }
                    std::uint64_t ref = 0;
                    if (!read_uint(pos, ref_size_, ref)) {
                        error = "valor de dicionario truncado";
                        return false;
                    }
                    pos += ref_size_;
                    PlistValue value;
                    if (!parse_object(ref, value, depth + 1, error)) return false;
                    out.entries.emplace_back(std::move(name), std::move(value));
                }
                return true;
            }
            default:
                error = "tipo de objeto binary plist nao suportado (0x" +
                        bytes_to_hex(Bytes{type}) + ")";
                return false;
        }
    }

    const Bytes& data_;
    std::size_t offset_size_ = 0;
    std::size_t ref_size_ = 0;
    std::uint64_t object_count_ = 0;
    std::uint64_t top_object_ = 0;
    std::uint64_t offset_table_ = 0;
    std::vector<std::uint64_t> offsets_;
};

// ------------------------------------------------------------------ arquivamento
const PlistValue* object_at(const PlistValue& root, std::uint64_t uid) {
    const PlistValue* objects = root.find("$objects");
    if (objects == nullptr || objects->kind != PlistKind::Array) return nullptr;
    if (uid >= objects->items.size()) return nullptr;
    return &objects->items[static_cast<std::size_t>(uid)];
}

bool unbox_bytes(const PlistValue& root, const PlistValue& field, Bytes& out, int depth) {
    if (depth > 16) return false;
    switch (field.kind) {
        case PlistKind::Data:
            out = field.bytes;
            return true;
        case PlistKind::Uid: {
            const PlistValue* resolved = object_at(root, field.uid);
            return resolved != nullptr && unbox_bytes(root, *resolved, out, depth + 1);
        }
        case PlistKind::Dict: {
            const PlistValue* inner = field.find("NS.data");
            if (inner == nullptr) inner = field.find("NS.bytes");
            return inner != nullptr && unbox_bytes(root, *inner, out, depth + 1);
        }
        default:
            return false;
    }
}

bool unbox_string(const PlistValue& root, const PlistValue& field, std::string& out,
                  int depth) {
    if (depth > 16) return false;
    switch (field.kind) {
        case PlistKind::String:
            out = field.text;
            return true;
        case PlistKind::Uid: {
            const PlistValue* resolved = object_at(root, field.uid);
            return resolved != nullptr && unbox_string(root, *resolved, out, depth + 1);
        }
        case PlistKind::Dict: {
            const PlistValue* inner = field.find("NS.string");
            return inner != nullptr && unbox_string(root, *inner, out, depth + 1);
        }
        default:
            return false;
    }
}

bool unbox_integer(const PlistValue& root, const PlistValue& field, std::int64_t& out,
                   int depth) {
    if (depth > 16) return false;
    switch (field.kind) {
        case PlistKind::Integer:
            out = field.integer;
            return true;
        case PlistKind::Bool:
            out = field.boolean ? 1 : 0;
            return true;
        case PlistKind::Uid: {
            const PlistValue* resolved = object_at(root, field.uid);
            return resolved != nullptr && unbox_integer(root, *resolved, out, depth + 1);
        }
        case PlistKind::Dict: {
            const PlistValue* inner = field.find("NS.number");
            if (inner == nullptr) inner = field.find("NS.intval");
            return inner != nullptr && unbox_integer(root, *inner, out, depth + 1);
        }
        default:
            return false;
    }
}

// ------------------------------------------------------------------ JSON
std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

std::string json_string(const std::string& text) { return "\"" + json_escape(text) + "\""; }

} // namespace

// ------------------------------------------------------------------ plist API
const PlistValue* PlistValue::find(const char* key) const {
    if (kind != PlistKind::Dict) return nullptr;
    for (const auto& entry : entries) {
        if (entry.first == key) return &entry.second;
    }
    return nullptr;
}

std::string plist_kind_name(PlistKind kind) {
    switch (kind) {
        case PlistKind::Null:
            return "null";
        case PlistKind::Bool:
            return "bool";
        case PlistKind::Integer:
            return "integer";
        case PlistKind::Real:
            return "real";
        case PlistKind::Date:
            return "date";
        case PlistKind::Data:
            return "data";
        case PlistKind::String:
            return "string";
        case PlistKind::Uid:
            return "uid";
        case PlistKind::Array:
            return "array";
        case PlistKind::Dict:
            return "dict";
        case PlistKind::Set:
            return "set";
    }
    return "desconhecido";
}

bool plist_parse(const Bytes& blob, PlistValue& out, std::string& error) {
    out = PlistValue{};
    PlistParser parser(blob);
    return parser.parse(out, error);
}

bool plist_load(const std::string& path, PlistValue& out, std::string& error) {
    Bytes blob;
    if (!read_whole_file(path, blob, error)) return false;
    if (!plist_parse(blob, out, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

const PlistValue* nskeyed_object(const PlistValue& root, std::uint64_t uid) {
    return object_at(root, uid);
}

const PlistValue* nskeyed_top(const PlistValue& root, const char* key) {
    const PlistValue* top = root.find("$top");
    if (top == nullptr) return nullptr;
    const PlistValue* ref = top->find(key);
    if (ref == nullptr || ref->kind != PlistKind::Uid) return nullptr;
    return object_at(root, ref->uid);
}

bool archive_bytes(const PlistValue& root, const PlistValue& field, Bytes& out) {
    return unbox_bytes(root, field, out, 0);
}

bool archive_string(const PlistValue& root, const PlistValue& field, std::string& out) {
    return unbox_string(root, field, out, 0);
}

bool archive_integer(const PlistValue& root, const PlistValue& field, std::int64_t& out) {
    return unbox_integer(root, field, out, 0);
}

// ------------------------------------------------------------------ utilidades
std::string sha256_hex(const Bytes& data) {
    Bytes digest;
    std::string error;
    if (!sha256(data, digest, error)) return std::string();
    return bytes_to_hex(digest);
}

bool read_whole_file(const std::string& path, Bytes& out, std::string& error) {
    return win_read_file(utf8_to_wide(path), out, error);
}

bool write_whole_file(const std::string& path, const Bytes& data, std::string& error) {
    return win_write_file(utf8_to_wide(path), data, error);
}

bool ensure_directory(const std::string& path, std::string& error) {
    return win_create_dirs(utf8_to_wide(path), error);
}

bool sha256_file(const std::string& path, std::string& hex, std::uint64_t& size,
                 std::string& error) {
    hex.clear();
    size = 0;
    const std::wstring wide = utf8_to_wide(path);
    HANDLE handle = CreateFileW(wide.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = "nao foi possivel abrir " + path + ": " + last_error_text(GetLastError());
        return false;
    }
    Sha256Stream stream;
    if (!stream.init(error)) {
        CloseHandle(handle);
        return false;
    }
    std::uint8_t buffer[64 * 1024];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(handle, buffer, sizeof(buffer), &read, nullptr)) {
            error = "falha de leitura em " + path + ": " + last_error_text(GetLastError());
            CloseHandle(handle);
            return false;
        }
        if (read == 0) break;
        if (!stream.update(buffer, read, error)) {
            CloseHandle(handle);
            return false;
        }
        size += read;
    }
    CloseHandle(handle);
    Bytes digest;
    if (!stream.finish(digest, error)) return false;
    hex = bytes_to_hex(digest);
    return true;
}

namespace {

std::uint32_t read_le32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

struct SqliteDb {
    sqlite3* handle = nullptr;
    ~SqliteDb() {
        if (handle) sqlite3_close(handle);
    }
};

bool open_sqlite(const std::string& path, SqliteDb& out, std::string& error) {
    const int rc = sqlite3_open_v2(path.c_str(), &out.handle, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        error = "nao foi possivel abrir " + path + ": " +
                (out.handle ? sqlite3_errmsg(out.handle) : sqlite3_errstr(rc));
        return false;
    }
    return true;
}

bool table_exists(sqlite3* db, const char* name) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name=?",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

bool column_exists(sqlite3* db, const char* table, const char* column) {
    const std::string sql = std::string("PRAGMA table_info(") + table + ")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        if (name != nullptr && column != nullptr &&
            std::strcmp(reinterpret_cast<const char*>(name), column) == 0) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

void classify_entry(BackupEntry& entry, std::int64_t mode) {
    const std::uint32_t format = static_cast<std::uint32_t>(mode) & 0170000u;
    if (format == 0100000u) {
        entry.is_file = true;
    } else if (format == 0040000u) {
        entry.is_directory = true;
    } else if (format == 0120000u) {
        entry.is_symlink = true;
    }
    if (entry.is_file || entry.is_directory || entry.is_symlink) return;
    if (entry.flags == 4) {
        entry.is_symlink = true;
    } else if (entry.flags == 2) {
        entry.is_directory = true;
    } else if (entry.flags != 0) {
        entry.is_file = true;
    }
}

bool decode_mbfile(const Bytes& blob, BackupEntry& entry, std::string& error) {
    PlistValue root;
    if (!plist_parse(blob, root, error)) return false;
    const PlistValue* record = nskeyed_top(root, "root");
    if (record == nullptr || record->kind != PlistKind::Dict) {
        error = "NSKeyedArchiver sem $top.root utilizavel";
        return false;
    }
    entry.metadata_ok = true;

    std::int64_t mode = 0;
    if (const PlistValue* field = record->find("Mode")) {
        if (!archive_integer(root, *field, mode)) mode = 0;
    }
    std::int64_t size = 0;
    if (const PlistValue* field = record->find("Size")) {
        if (archive_integer(root, *field, size) && size > 0) {
            entry.size = static_cast<std::uint64_t>(size);
        }
    }
    std::int64_t klass = 0;
    if (const PlistValue* field = record->find("ProtectionClass")) {
        if (archive_integer(root, *field, klass) && klass > 0) {
            entry.protection_class = static_cast<std::uint32_t>(klass);
        }
    }
    std::string relative;
    if (const PlistValue* field = record->find("RelativePath")) {
        std::string decoded;
        if (archive_string(root, *field, decoded) && !decoded.empty()) relative = decoded;
    }
    if (!relative.empty()) entry.relative_path = relative;

    if (const PlistValue* field = record->find("Target")) {
        std::string decoded;
        if (archive_string(root, *field, decoded)) entry.target = decoded;
    }
    if (const PlistValue* field = record->find("EncryptionKey")) {
        Bytes decoded;
        if (archive_bytes(root, *field, decoded)) entry.encryption_key = std::move(decoded);
    }
    classify_entry(entry, mode);
    return true;
}

bool load_files(SqliteDb& db, Backup& backup, std::string& error) {
    if (!table_exists(db.handle, "Files")) {
        error = "Manifest.db sem tabela Files";
        return false;
    }
    if (!column_exists(db.handle, "Files", "file")) {
        error = "Manifest.db com layout pre-iOS 10 (sem coluna Files.file): nao suportado";
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT fileID, domain, relativePath, flags, file FROM Files";
    if (sqlite3_prepare_v2(db.handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = std::string("consulta a Files falhou: ") + sqlite3_errmsg(db.handle);
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        BackupEntry entry;
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        entry.file_id = text ? reinterpret_cast<const char*>(text) : "";
        text = sqlite3_column_text(stmt, 1);
        entry.domain = text ? reinterpret_cast<const char*>(text) : "";
        text = sqlite3_column_text(stmt, 2);
        entry.relative_path = text ? reinterpret_cast<const char*>(text) : "";
        entry.flags = static_cast<std::uint32_t>(sqlite3_column_int(stmt, 3));

        const void* blob = sqlite3_column_blob(stmt, 4);
        const int blob_size = sqlite3_column_bytes(stmt, 4);
        if (blob != nullptr && blob_size > 0) {
            const std::uint8_t* begin = static_cast<const std::uint8_t*>(blob);
            Bytes raw(begin, begin + blob_size);
            std::string ignored;
            if (!decode_mbfile(raw, entry, ignored)) {
                backup.notes.push_back("metadados NSKeyedArchiver ilegiveis para " +
                                       entry.file_id + ": " + ignored);
            }
        }
        if (entry.relative_path.empty() && entry.domain.empty()) {
            backup.notes.push_back("entrada sem domain/relativePath ignorada: " +
                                   entry.file_id);
            continue;
        }
        if (!entry.is_file && !entry.is_directory && !entry.is_symlink) {
            if (!entry.relative_path.empty()) {
                entry.is_file = true;
            } else {
                entry.is_directory = true;
            }
        }
        backup.entries.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
    backup.entry_count = backup.entries.size();

    if (table_exists(db.handle, "Properties")) {
        sqlite3_stmt* props = nullptr;
        if (sqlite3_prepare_v2(db.handle, "SELECT key, value FROM Properties", -1, &props,
                               nullptr) == SQLITE_OK) {
            while (sqlite3_step(props) == SQLITE_ROW) {
                const unsigned char* key = sqlite3_column_text(props, 0);
                if (key == nullptr) continue;
                const std::string name(reinterpret_cast<const char*>(key));
                if (sqlite3_column_type(props, 1) == SQLITE_BLOB) {
                    backup.notes.push_back("Manifest.db Properties." + name + " = blob de " +
                                           std::to_string(sqlite3_column_bytes(props, 1)) +
                                           " bytes (nao usado na derivacao)");
                } else {
                    const unsigned char* value = sqlite3_column_text(props, 1);
                    backup.notes.push_back(
                        "Manifest.db Properties." + name + " = " +
                        (value ? reinterpret_cast<const char*>(value) : "") +
                        " (nao usado na derivacao)");
                }
            }
            sqlite3_finalize(props);
        }
    }
    return true;
}

std::string ciphertext_path(const Backup& backup, const std::string& file_id) {
    if (file_id.size() >= 2) {
        const std::string sharded = join_path(join_path(backup.root, file_id.substr(0, 2)),
                                              file_id);
        if (GetFileAttributesW(utf8_to_wide(sharded).c_str()) != INVALID_FILE_ATTRIBUTES) {
            return sharded;
        }
    }
    return join_path(backup.root, file_id);
}

struct FileKeyCandidate {
    std::uint32_t class_id = 0;
    Bytes wrapped;
    const char* layout = "";
};

bool resolve_file_key(const Backup& backup, const BackupEntry& entry, Bytes& key,
                      std::string& error, const char** layout_out = nullptr) {
    key.clear();
    if (entry.encryption_key.empty()) {
        error = "entrada sem EncryptionKey no manifesto";
        return false;
    }
    const Bytes& blob = entry.encryption_key;
    std::vector<FileKeyCandidate> candidates;
    if (blob.size() >= 4 && blob.size() - 4 >= 16 && (blob.size() - 4) % 8 == 0) {
        Bytes rest(blob.begin() + 4, blob.end());
        candidates.push_back({read_be32(blob.data()), rest, "class-prefix-be"});
        const std::uint32_t little = read_le32(blob.data());
        if (little != candidates.front().class_id) {
            candidates.push_back({little, rest, "class-prefix-le"});
        }
    }
    if (blob.size() >= 24 && blob.size() % 8 == 0) {
        candidates.push_back({entry.protection_class, blob, "no-prefix"});
    }

    for (const FileKeyCandidate& candidate : candidates) {
        const KeybagClass* klass = backup.keybag.protection_class(candidate.class_id);
        if (klass == nullptr || klass->class_key.empty()) continue;
        Bytes unwrapped;
        std::string ignored;
        if (aes_key_unwrap(klass->class_key, candidate.wrapped, unwrapped, ignored) &&
            unwrapped.size() >= 16) {
            key = std::move(unwrapped);
            if (layout_out != nullptr) *layout_out = candidate.layout;
            return true;
        }
    }
    error = "nao foi possivel desembrulhar a file key (classe " +
            std::to_string(entry.protection_class) + ", blob de " +
            std::to_string(blob.size()) + " bytes)";
    return false;
}

} // namespace

// ------------------------------------------------------------------ backup API
bool backup_open(const std::string& directory, Backup& out, std::string& error) {
    out = Backup{};
    out.root = directory;
    if (!win_is_directory(utf8_to_wide(directory))) {
        error = "diretorio de backup inexistente: " + directory;
        return false;
    }

    const std::string manifest_plist = join_path(directory, "Manifest.plist");
    if (GetFileAttributesW(utf8_to_wide(manifest_plist).c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = "Manifest.plist ausente em " + directory;
        return false;
    }
    PlistValue root;
    if (!plist_load(manifest_plist, root, error)) return false;
    if (root.kind != PlistKind::Dict) {
        error = "Manifest.plist nao e um dicionario";
        return false;
    }

    if (const PlistValue* value = root.find("Version")) {
        if (value->kind == PlistKind::Integer && value->integer > 0) {
            out.plist_version = static_cast<std::uint32_t>(value->integer);
        }
    }
    if (const PlistValue* value = root.find("IsEncrypted")) {
        if (value->kind == PlistKind::Bool) out.is_encrypted = value->boolean;
    }
    if (const PlistValue* lockdown = root.find("Lockdown")) {
        if (lockdown->kind == PlistKind::Dict) {
            if (const PlistValue* name = lockdown->find("DeviceName")) {
                if (name->kind == PlistKind::String) out.device_name = name->text;
            }
            if (const PlistValue* version = lockdown->find("ProductVersion")) {
                if (version->kind == PlistKind::String) out.product_version = version->text;
            }
        }
    }

    const PlistValue* keybag_value = root.find("BackupKeyBag");
    if (keybag_value == nullptr || keybag_value->kind != PlistKind::Data) {
        if (out.is_encrypted) {
            error = "Manifest.plist marcado como cifrado mas sem BackupKeyBag";
            return false;
        }
        out.notes.push_back("Manifest.plist sem BackupKeyBag (backup nao cifrado)");
    } else {
        std::string keybag_error;
        if (!keybag_parse(keybag_value->bytes, out.keybag, keybag_error)) {
            if (out.is_encrypted) {
                error = "BackupKeyBag invalido: " + keybag_error;
                return false;
            }
            out.notes.push_back("BackupKeyBag presente mas invalido: " + keybag_error);
        }
    }

    const std::string manifest_db = join_path(directory, "Manifest.db");
    if (GetFileAttributesW(utf8_to_wide(manifest_db).c_str()) == INVALID_FILE_ATTRIBUTES) {
        const std::string legacy = join_path(directory, "Manifest.mbdb");
        if (GetFileAttributesW(utf8_to_wide(legacy).c_str()) != INVALID_FILE_ATTRIBUTES) {
            error = "backup com Manifest.mbdb (pre-iOS 10): layout nao suportado";
            return false;
        }
        error = "Manifest.db ausente em " + directory;
        return false;
    }

    SqliteDb db;
    if (!open_sqlite(manifest_db, db, error)) return false;
    return load_files(db, out, error);
}

bool backup_unlock(Backup& backup, const std::string& password, std::string& error) {
    if (!backup.is_encrypted) {
        backup.unlocked = true;
        return true;
    }
    if (backup.keybag.classes.empty()) {
        error = "backup cifrado sem keybag utilizavel";
        return false;
    }
    if (!keybag_unlock(backup.keybag, password, backup.passcode_key, error)) return false;
    backup.unlocked = true;
    return true;
}

bool backup_read_entry(Backup& backup, const BackupEntry& entry, Bytes& out,
                       std::string& error) {
    out.clear();
    if (entry.file_id.empty()) {
        error = "entrada sem fileID";
        return false;
    }
    const std::string path = ciphertext_path(backup, entry.file_id);
    Bytes raw;
    if (!read_whole_file(path, raw, error)) return false;

    if (!backup.is_encrypted) {
        out = std::move(raw);
        return true;
    }
    if (!backup.unlocked) {
        error = "backup cifrado ainda nao foi desbloqueado";
        return false;
    }
    if (entry.encryption_key.empty()) {
        if (raw.empty()) return true;   // arquivo vazio nao carrega chave
        error = "entrada cifrada sem EncryptionKey: " + entry.file_id;
        return false;
    }

    Bytes file_key;
    if (!resolve_file_key(backup, entry, file_key, error)) {
        error = entry.domain + "/" + entry.relative_path + ": " + error;
        return false;
    }

    std::uint8_t iv[16] = {0};
    Bytes plain;
    if (!aes_cbc_decrypt(file_key, iv, raw, plain, error)) {
        error = entry.domain + "/" + entry.relative_path + ": " + error;
        return false;
    }
    if (entry.size > 0 && plain.size() >= entry.size) {
        plain.resize(static_cast<std::size_t>(entry.size));
    } else {
        Bytes trimmed = plain;
        if (pkcs7_unpad(trimmed, 16)) plain = std::move(trimmed);
    }
    out = std::move(plain);
    return true;
}

bool backup_extract(Backup& backup, const std::string& out_dir, ExtractResult& result,
                    std::string& error) {
    result = ExtractResult{};
    if (backup.is_encrypted && !backup.unlocked) {
        error = "backup cifrado precisa ser desbloqueado antes da extracao";
        return false;
    }
    if (!ensure_directory(out_dir, error)) return false;

    for (const BackupEntry& entry : backup.entries) {
        ++result.entries_seen;

        std::string safe_domain;
        std::string problem;
        if (!sanitize_relative(entry.domain, safe_domain, problem)) {
            ++result.files_skipped;
            result.failures.push_back(entry.file_id + ": " + problem);
            continue;
        }
        std::string safe_relative;
        if (!sanitize_relative(entry.relative_path, safe_relative, problem)) {
            ++result.files_skipped;
            result.failures.push_back(entry.file_id + ": " + problem);
            continue;
        }

        std::string relative_out = safe_domain;
        if (!safe_relative.empty()) relative_out = join_path(relative_out, safe_relative);

        ManifestRecord record;
        record.file_id = entry.file_id;
        record.domain = entry.domain;
        record.relative_path = entry.relative_path;
        record.output_path = relative_out;
        record.size = entry.size;
        record.protection_class = entry.protection_class;

        if (relative_out.empty()) {
            record.status = "skipped";
            ++result.files_skipped;
            result.records.push_back(record);
            continue;
        }

        const std::string target = join_path(out_dir, relative_out);
        if (entry.is_directory) {
            if (!ensure_directory(target, problem)) {
                record.status = "failed";
                ++result.files_failed;
                result.failures.push_back(relative_out + ": " + problem);
            } else {
                record.status = "directory";
                ++result.directories_created;
            }
            result.records.push_back(record);
            continue;
        }
        if (entry.is_symlink) {
            record.status = "symlink";
            ++result.symlinks_skipped;
            result.records.push_back(record);
            continue;
        }

        const std::size_t slash = target.find_last_of('/');
        if (slash != std::string::npos) {
            if (!ensure_directory(target.substr(0, slash), problem)) {
                record.status = "failed";
                ++result.files_failed;
                result.failures.push_back(relative_out + ": " + problem);
                result.records.push_back(record);
                continue;
            }
        }

        Bytes plain;
        if (!backup_read_entry(backup, entry, plain, problem)) {
            record.status = "failed";
            ++result.files_failed;
            result.failures.push_back(relative_out + ": " + problem);
            result.records.push_back(record);
            continue;
        }
        if (!write_whole_file(target, plain, problem)) {
            record.status = "failed";
            ++result.files_failed;
            result.failures.push_back(relative_out + ": " + problem);
            result.records.push_back(record);
            continue;
        }

        record.status = "extracted";
        record.size = plain.size();
        record.sha256 = sha256_hex(plain);
        if (backup.is_encrypted) {
            Bytes raw;
            std::string ignored;
            if (read_whole_file(ciphertext_path(backup, entry.file_id), raw, ignored)) {
                record.sha256_ciphertext = sha256_hex(raw);
            }
        }
        ++result.files_written;
        result.bytes_written += plain.size();
        result.records.push_back(std::move(record));
    }
    return true;
}

bool backup_write_manifest_json(const Backup& backup, const ExtractResult& result,
                                const std::string& path, std::string& error) {
    std::string json;
    json.reserve(1024 + result.records.size() * 320);
    json += "{\n";
    json += "  \"module\": \"olho::ios\",\n";
    json += "  \"backup_root\": " + json_string(backup.root) + ",\n";
    json += "  \"encrypted\": " + std::string(backup.is_encrypted ? "true" : "false") + ",\n";
    json += "  \"plist_version\": " + std::to_string(backup.plist_version) + ",\n";
    json += "  \"device_name\": " + json_string(backup.device_name) + ",\n";
    json += "  \"product_version\": " + json_string(backup.product_version) + ",\n";
    json += "  \"summary\": {\n";
    json += "    \"entries_seen\": " + std::to_string(result.entries_seen) + ",\n";
    json += "    \"files_written\": " + std::to_string(result.files_written) + ",\n";
    json += "    \"bytes_written\": " + std::to_string(result.bytes_written) + ",\n";
    json += "    \"directories_created\": " + std::to_string(result.directories_created) + ",\n";
    json += "    \"symlinks_skipped\": " + std::to_string(result.symlinks_skipped) + ",\n";
    json += "    \"files_skipped\": " + std::to_string(result.files_skipped) + ",\n";
    json += "    \"files_failed\": " + std::to_string(result.files_failed) + "\n";
    json += "  },\n";

    json += "  \"notes\": [";
    for (std::size_t i = 0; i < backup.notes.size(); ++i) {
        if (i != 0) json += ", ";
        json += json_string(backup.notes[i]);
    }
    json += "],\n";

    json += "  \"files\": [\n";
    for (std::size_t i = 0; i < result.records.size(); ++i) {
        const ManifestRecord& record = result.records[i];
        json += "    {\"file_id\": " + json_string(record.file_id) +
                ", \"domain\": " + json_string(record.domain) +
                ", \"relative_path\": " + json_string(record.relative_path) +
                ", \"output_path\": " + json_string(record.output_path) +
                ", \"size\": " + std::to_string(record.size) +
                ", \"protection_class\": " + std::to_string(record.protection_class) +
                ", \"sha256\": " + json_string(record.sha256) +
                ", \"sha256_ciphertext\": " + json_string(record.sha256_ciphertext) +
                ", \"status\": " + json_string(record.status) + "}";
        if (i + 1 != result.records.size()) json += ",";
        json += "\n";
    }
    json += "  ]\n}\n";

    const Bytes bytes(json.begin(), json.end());
    return write_whole_file(path, bytes, error);
}

} // namespace olho::ios
