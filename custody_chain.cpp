// language: C++, file: custody_chain.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "custody_chain.hpp"

#include "evidence_verify.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace olho::custody {
namespace {

std::string to_hex(const bytes& v) {
    static const char* h = "0123456789abcdef";
    std::string s;
    s.reserve(v.size() * 2);
    for (auto b : v) { s.push_back(h[b >> 4]); s.push_back(h[b & 15]); }
    return s;
}

std::string sha256_hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    std::string out;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return out;
    ULONG obj = 0, hl = 0, written = 0;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj), sizeof(obj), &written, 0);
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hl), sizeof(hl), &written, 0);
    bytes object(obj);
    bytes digest(hl);
    if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &h, object.data(), obj, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                                      static_cast<ULONG>(data.size()), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(h, digest.data(), hl, 0))) {
        out = to_hex(digest);
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

std::string sha256_file_hex(const fs::path& p, std::uint64_t& size) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    std::string out;
    size = 0;
    HANDLE f = CreateFileW(p.wstring().c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        CloseHandle(f);
        return out;
    }
    ULONG obj = 0, hl = 0, written = 0;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj), sizeof(obj), &written, 0);
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hl), sizeof(hl), &written, 0);
    bytes object(obj);
    bytes digest(hl);
    if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &h, object.data(), obj, nullptr, 0, 0))) {
        bytes buffer(64 * 1024);
        DWORD got = 0;
        bool failed = false;
        while (ReadFile(f, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) && got > 0) {
            size += got;
            if (!BCRYPT_SUCCESS(BCryptHashData(h, buffer.data(), got, 0))) { failed = true; break; }
        }
        if (!failed && BCRYPT_SUCCESS(BCryptFinishHash(h, digest.data(), hl, 0))) out = to_hex(digest);
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return out;
}

// Length-prefixed fields: a path containing the separator cannot be re-split into
// different fields, which is how hash-chain canonicalisation usually gets broken.
void append_field(std::string& out, const std::string& v) {
    out += std::to_string(v.size());
    out += ":";
    out += v;
    out += "|";
}

}  // namespace

std::string entry_payload(const entry& e) {
    std::string s;
    s.reserve(256);
    append_field(s, std::to_string(e.index));
    append_field(s, e.relative_path);
    append_field(s, std::to_string(e.size));
    append_field(s, e.sha256);
    append_field(s, e.kind);
    append_field(s, e.prev_link);
    return s;
}

bool build_from(const std::string& genesis, const std::vector<input_entry>& inputs,
                chain& out, std::string& error) {
    out = chain{};
    out.genesis = genesis;
    std::string prev = sha256_hex("genesis|" + genesis);
    std::uint64_t index = 0;
    for (const auto& in : inputs) {
        entry e;
        e.index = index++;
        e.relative_path = in.relative_path;
        e.size = in.size;
        e.sha256 = in.sha256;
        e.kind = in.kind;
        e.prev_link = prev;
        e.link = sha256_hex(entry_payload(e));
        prev = e.link;
        out.entries.push_back(std::move(e));
    }
    out.head = prev;
    out.ok = true;
    if (out.entries.empty()) error = "nenhuma entrada";
    return true;
}

bool build(const std::string& genesis, const std::string& root, chain& out, std::string& error) {
    out = chain{};
    out.genesis = genesis;

    std::error_code ec;
    const fs::path base(root);
    if (!fs::is_directory(base, ec)) { error = "diretorio invalido: " + root; return false; }

    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(base, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file()) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    std::string prev = sha256_hex("genesis|" + genesis);
    std::uint64_t index = 0;
    for (const auto& p : files) {
        entry e;
        e.index = index++;
        e.relative_path = fs::relative(p, base, ec).string();
        e.sha256 = sha256_file_hex(p, e.size);
        if (e.sha256.empty()) { error = "nao consegui hashear " + e.relative_path; return false; }
        e.kind = olho::evidence::detect_kind(p.wstring());
        e.prev_link = prev;
        e.link = sha256_hex(entry_payload(e));
        prev = e.link;
        out.entries.push_back(std::move(e));
    }

    out.head = prev;
    out.ok = true;
    return true;
}

bool verify(const chain& c, std::size_t& bad_index, std::string& bad_path, std::string& error) {
    bad_index = 0;
    bad_path.clear();
    std::string prev = sha256_hex("genesis|" + c.genesis);
    for (std::size_t i = 0; i < c.entries.size(); ++i) {
        const entry& e = c.entries[i];
        if (e.prev_link != prev) {
            bad_index = i;
            bad_path = e.relative_path;
            error = "elo anterior nao bate no indice " + std::to_string(i) +
                    " (entrada removida, inserida ou reordenada)";
            return false;
        }
        if (e.index != i) {
            bad_index = i;
            bad_path = e.relative_path;
            error = "indice fora de sequencia no indice " + std::to_string(i);
            return false;
        }
        const std::string expect = sha256_hex(entry_payload(e));
        if (expect != e.link) {
            bad_index = i;
            bad_path = e.relative_path;
            error = "link nao bate no indice " + std::to_string(i) + " (" + e.relative_path + ")";
            return false;
        }
        prev = e.link;
    }
    if (prev != c.head) {
        bad_index = c.entries.size();
        error = "selo final nao corresponde ao ultimo elo";
        return false;
    }
    return true;
}

std::string to_json(const chain& c) {
    nlohmann::json j;
    j["genesis"] = c.genesis;
    j["head"] = c.head;
    j["entries"] = nlohmann::json::array();
    for (const auto& e : c.entries) {
        j["entries"].push_back({{"index", e.index},
                                {"path", e.relative_path},
                                {"size", e.size},
                                {"sha256", e.sha256},
                                {"kind", e.kind},
                                {"prev", e.prev_link},
                                {"link", e.link}});
    }
    // Compact: a 10k-entry manifest pretty-printed costs seconds and megabytes for
    // no gain — it is meant to be diffed by a script, not read by a person.
    return j.dump();
}

bool from_json(const std::string& text, chain& out, std::string& error) {
    out = chain{};
    try {
        const nlohmann::json j = nlohmann::json::parse(text);
        out.genesis = j.value("genesis", "");
        out.head = j.value("head", "");
        for (const auto& item : j.value("entries", nlohmann::json::array())) {
            entry e;
            e.index = item.value("index", 0ull);
            e.relative_path = item.value("path", "");
            e.size = item.value("size", 0ull);
            e.sha256 = item.value("sha256", "");
            e.kind = item.value("kind", "");
            e.prev_link = item.value("prev", "");
            e.link = item.value("link", "");
            out.entries.push_back(std::move(e));
        }
        out.ok = true;
        return true;
    } catch (const std::exception& ex) {
        error = std::string("JSON invalido: ") + ex.what();
        return false;
    }
}

}  // namespace olho::custody