// language: C++, file: evidence_verify.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "evidence_verify.hpp"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace olho::evidence {
namespace {

struct head {
    std::vector<std::uint8_t> bytes;
    std::uint64_t size = 0;
};

// Reads the first bytes and the last bytes: enough for a magic number and for
// the structural markers a PDF needs at both ends.
bool read_window(const std::wstring& path, head& out, std::vector<std::uint8_t>& tail) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    out.size = static_cast<std::uint64_t>(size.QuadPart);
    const DWORD want = static_cast<DWORD>(std::min<std::uint64_t>(out.size, 4096));
    out.bytes.resize(want);
    DWORD got = 0;
    if (want > 0) ReadFile(h, out.bytes.data(), want, &got, nullptr);
    out.bytes.resize(got);

    const std::uint64_t tail_len = std::min<std::uint64_t>(out.size, 2048);
    if (tail_len > 0) {
        tail.resize(static_cast<std::size_t>(tail_len));
        LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<LONGLONG>(out.size - tail_len);
        SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
        DWORD got_tail = 0;
        ReadFile(h, tail.data(), static_cast<DWORD>(tail_len), &got_tail, nullptr);
        tail.resize(got_tail);
    }
    CloseHandle(h);
    return true;
}

bool starts_with(const std::vector<std::uint8_t>& b, const char* magic, std::size_t len) {
    return b.size() >= len && std::memcmp(b.data(), magic, len) == 0;
}

bool contains(const std::vector<std::uint8_t>& b, const char* needle) {
    const std::size_t n = std::strlen(needle);
    if (b.size() < n) return false;
    return std::search(b.begin(), b.end(), needle, needle + n) != b.end();
}

std::string lower_ext(const fs::path& p) {
    std::string e = p.extension().string();
    if (!e.empty() && e[0] == '.') e.erase(0, 1);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

// Extension -> accepted families. Several extensions legitimately share one
// container (docx/xlsx/pptx are ZIP; heic/mov are ISO-BMFF).
const std::map<std::string, std::set<std::string>>& extension_families() {
    static const std::map<std::string, std::set<std::string>> table = {
        {"pdf", {"pdf"}},
        {"jpg", {"jpeg"}}, {"jpeg", {"jpeg"}}, {"jpe", {"jpeg"}},
        {"png", {"png"}}, {"gif", {"gif"}}, {"webp", {"webp"}}, {"bmp", {"bmp"}}, {"tif", {"tiff"}}, {"tiff", {"tiff"}},
        {"mp4", {"isobmff"}}, {"m4a", {"isobmff"}}, {"3gp", {"isobmff"}}, {"mov", {"isobmff"}},
        {"heic", {"isobmff"}}, {"heif", {"isobmff"}}, {"avif", {"isobmff"}},
        {"mp3", {"mpeg-audio"}}, {"ogg", {"ogg"}}, {"opus", {"ogg"}}, {"amr", {"amr"}}, {"wav", {"wav"}},
        {"zip", {"zip"}}, {"docx", {"zip"}}, {"xlsx", {"zip"}}, {"pptx", {"zip"}}, {"apk", {"zip"}}, {"jar", {"zip"}}, {"aff4", {"zip"}},
        {"rar", {"rar"}}, {"7z", {"7z"}}, {"gz", {"gzip"}},
        {"doc", {"ole2"}}, {"xls", {"ole2"}}, {"ppt", {"ole2"}}, {"msg", {"ole2"}},
        {"exe", {"pe"}}, {"dll", {"pe"}}, {"sys", {"pe"}}, {"mui", {"pe"}},
        {"db", {"sqlite"}}, {"sqlite", {"sqlite"}}, {"sqlite3", {"sqlite"}},
        {"evtx", {"evtx"}}, {"hiv", {"registry"}}, {"dat", {"registry", "sqlite", "unknown"}},
        {"e01", {"ewf"}}, {"ex01", {"ewf"}}, {"l01", {"ewf"}},
        {"txt", {"text"}}, {"json", {"text"}}, {"jsonl", {"text"}}, {"log", {"text"}},
        {"csv", {"text"}}, {"xml", {"text"}}, {"md", {"text"}}, {"ini", {"text"}},
    };
    return table;
}

bool looks_like_text(const std::vector<std::uint8_t>& b) {
    if (b.empty()) return true;
    std::size_t printable = 0;
    for (std::uint8_t c : b) {
        if (c == 0) return false;                       // NUL means binary
        if (c >= 0x20 || c == '\n' || c == '\r' || c == '\t' || c >= 0x80) ++printable;
    }
    return printable * 100 >= b.size() * 95;
}

}  // namespace

std::string detect_kind(const std::wstring& path) {
    head h;
    std::vector<std::uint8_t> tail;
    if (!read_window(path, h, tail)) return "unreadable";
    const auto& b = h.bytes;
    if (b.empty()) return "empty";

    if (starts_with(b, "%PDF-", 5)) {
        // A PDF is not a magic number: it needs objects and a cross-reference.
        // A header plus noise is exactly how a fake exhibit is built.
        const bool has_obj = contains(b, " obj") || contains(tail, " obj");
        const bool has_xref = contains(tail, "xref") || contains(tail, "startxref") || contains(tail, "/XRef");
        const bool has_eof = contains(tail, "%%EOF");
        if (has_obj && has_xref && has_eof) return "pdf";
        return "pdf-broken";
    }
    if (starts_with(b, "\xFF\xD8\xFF", 3)) return "jpeg";
    if (starts_with(b, "\x89PNG\r\n\x1A\n", 8)) return "png";
    if (starts_with(b, "GIF87a", 6) || starts_with(b, "GIF89a", 6)) return "gif";
    if (starts_with(b, "RIFF", 4) && b.size() >= 12 && std::memcmp(b.data() + 8, "WEBP", 4) == 0) return "webp";
    if (starts_with(b, "BM", 2) && b.size() > 26) return "bmp";
    if (starts_with(b, "II*\0", 4) || starts_with(b, "MM\0*", 4)) return "tiff";
    if (b.size() >= 12 && std::memcmp(b.data() + 4, "ftyp", 4) == 0) return "isobmff";
    if (starts_with(b, "ID3", 3) || (b.size() > 1 && b[0] == 0xFF && (b[1] & 0xE0) == 0xE0)) return "mpeg-audio";
    if (starts_with(b, "OggS", 4)) return "ogg";
    if (starts_with(b, "#!AMR", 5)) return "amr";
    if (starts_with(b, "RIFF", 4) && b.size() >= 12 && std::memcmp(b.data() + 8, "WAVE", 4) == 0) return "wav";
    if (starts_with(b, "PK\x03\x04", 4) || starts_with(b, "PK\x05\x06", 4)) return "zip";
    if (starts_with(b, "Rar!\x1A\x07", 6)) return "rar";
    if (starts_with(b, "7z\xBC\xAF\x27\x1C", 6)) return "7z";
    if (starts_with(b, "\x1F\x8B", 2)) return "gzip";
    if (starts_with(b, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8)) return "ole2";
    if (starts_with(b, "MZ", 2)) return "pe";
    if (starts_with(b, "SQLite format 3\0", 16)) return "sqlite";
    if (starts_with(b, "ElfFile\0", 8)) return "evtx";
    if (starts_with(b, "regf", 4)) return "registry";
    if (starts_with(b, "EVF\x09\x0D\x0A\xFF", 8)) return "ewf";
    if (looks_like_text(b)) return "text";
    return "unknown";
}

file_check classify(const std::string& relative_path, const std::string& detected) {
    file_check check;
    check.relative_path = relative_path;
    check.detected = detected;
    const std::string ext = lower_ext(fs::path(relative_path));
    check.extension = ext;

    const auto it = extension_families().find(ext);
    if (it == extension_families().end()) {
        check.result = verdict::unknown;
        check.detail = "extensao fora da tabela";
        return check;
    }
    if (detected == "pdf-broken") {
        check.result = verdict::mismatch;
        check.detail = "declara PDF mas nao tem objetos e xref (arquivo plantado ou truncado)";
        return check;
    }
    if (it->second.count(detected) > 0) {
        check.result = verdict::ok;
        return check;
    }
    if (detected == "unknown" || detected == "unreadable") {
        check.result = verdict::unknown;
        check.detail = "conteudo nao reconhecido; sem acusacao";
        return check;
    }
    check.result = verdict::mismatch;
    check.detail = "extensao ." + ext + " mas o conteudo e " + detected;
    return check;
}

verify_result verify_tree(const std::wstring& root) {
    verify_result result;
    std::error_code ec;
    const fs::path base(root);
    if (!fs::is_directory(base, ec)) { result.detail = "diretorio invalido"; return result; }

    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(base, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file()) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    for (const auto& p : files) {
        const std::string relative = fs::relative(p, base, ec).string();
        const file_check check = classify(relative, detect_kind(p.wstring()));
        ++result.checked;
        if (check.result == verdict::ok) {
            ++result.matched;
        } else if (check.result == verdict::mismatch) {
            ++result.mismatched;
            result.findings.push_back(check);
        } else {
            ++result.unknown;
        }
    }

    result.ok = true;
    result.detail = std::to_string(result.matched) + " conferem, " +
                    std::to_string(result.mismatched) + " divergem, " +
                    std::to_string(result.unknown) + " sem verificacao";
    return result;
}

std::vector<std::string> findings_as_notes(const verify_result& result) {
    std::vector<std::string> notes;
    for (const auto& f : result.findings) {
        notes.push_back("DIVERGENCIA DE TIPO: " + f.relative_path + " - " + f.detail);
    }
    return notes;
}

}  // namespace olho::evidence
