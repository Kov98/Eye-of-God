// language: C++, file: artifact_browser_secrets.cpp, target: Windows 10/11
// Opt-in Chrome/Edge cookie and password collector. The AES-256 key lives in
// Local State, DPAPI-protected for the current user; values are v10/v11
// AES-256-GCM. v20 app-bound values are reported but not decrypted.

#include "artifact_browser_secrets.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include "sqlite3.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

namespace deep {
namespace {

std::uint64_t read_u64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) value = (value << 8) | p[i];
    return value;
}

bool read_all(const std::wstring& path, std::vector<std::uint8_t>& data) {
    HANDLE file = CreateFileW(art::extend_path(path).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        size.QuadPart > 64ll * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    data.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = data.empty() || (ReadFile(file, data.data(),
        static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size());
    CloseHandle(file);
    return ok;
}

bool copy_shared(const std::wstring& from, const std::wstring& to) {
    HANDLE in = CreateFileW(art::extend_path(from).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (in == INVALID_HANDLE_VALUE) return false;
    HANDLE out = CreateFileW(art::extend_path(to).c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        CloseHandle(in);
        return false;
    }
    std::vector<std::uint8_t> buffer(1024 * 1024);
    bool ok = true;
    DWORD read = 0;
    while (ReadFile(in, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                    nullptr) && read > 0) {
        DWORD written = 0;
        if (!WriteFile(out, buffer.data(), read, &written, nullptr) ||
            written != read) {
            ok = false;
            break;
        }
    }
    FlushFileBuffers(out);
    CloseHandle(in);
    CloseHandle(out);
    return ok;
}

bool dpapi_unprotect(const std::vector<std::uint8_t>& input,
                     std::vector<std::uint8_t>& output) {
    if (input.empty()) return false;
    DATA_BLOB in{};
    in.pbData = const_cast<BYTE*>(input.data());
    in.cbData = static_cast<DWORD>(input.size());
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        return false;
    output.assign(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return true;
}

bool decrypt_gcm(const std::vector<std::uint8_t>& key,
                 const std::uint8_t* nonce, std::size_t nonce_size,
                 const std::uint8_t* ciphertext, std::size_t ciphertext_size,
                 const std::uint8_t* tag, std::size_t tag_size,
                 std::vector<std::uint8_t>& plaintext) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key_handle = nullptr;
    bool ok = false;
    do {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm,
            BCRYPT_AES_ALGORITHM, nullptr, 0))) break;
        if (!BCRYPT_SUCCESS(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
            sizeof(BCRYPT_CHAIN_MODE_GCM), 0))) break;
        if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(algorithm, &key_handle,
            nullptr, 0, const_cast<PUCHAR>(key.data()),
            static_cast<ULONG>(key.size()), 0))) break;
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = const_cast<PUCHAR>(nonce);
        info.cbNonce = static_cast<ULONG>(nonce_size);
        info.pbTag = const_cast<PUCHAR>(tag);
        info.cbTag = static_cast<ULONG>(tag_size);
        plaintext.resize(ciphertext_size);
        ULONG produced = 0;
        if (!BCRYPT_SUCCESS(BCryptDecrypt(key_handle,
            const_cast<PUCHAR>(ciphertext), static_cast<ULONG>(ciphertext_size),
            &info, nullptr, 0, plaintext.empty() ? nullptr : plaintext.data(),
            static_cast<ULONG>(plaintext.size()), &produced, 0))) break;
        plaintext.resize(produced);
        ok = true;
    } while (false);
    if (key_handle) BCryptDestroyKey(key_handle);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

bool base64_decode(const std::string& text, std::vector<std::uint8_t>& output) {
    DWORD size = 0;
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
        CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr))
        return false;
    output.resize(size);
    return CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
        CRYPT_STRING_BASE64, output.data(), &size, nullptr, nullptr) != FALSE;
}

std::string extract_json_string(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t position = text.find(needle);
    if (position == std::string::npos) return {};
    position = text.find(':', position + needle.size());
    if (position == std::string::npos) return {};
    position = text.find('"', position + 1);
    if (position == std::string::npos) return {};
    const std::size_t end = text.find('"', position + 1);
    if (end == std::string::npos) return {};
    return text.substr(position + 1, end - position - 1);
}

bool load_master_key(const std::wstring& local_state,
                     std::vector<std::uint8_t>& key) {
    std::vector<std::uint8_t> raw;
    if (!read_all(local_state, raw)) return false;
    const std::string text(raw.begin(), raw.end());
    std::string encoded = extract_json_string(text, "encrypted_key");
    if (encoded.empty()) return false;
    std::string clean;
    for (char c : encoded) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '/' || c == '=')
            clean.push_back(c);
    }
    std::vector<std::uint8_t> protected_key;
    if (!base64_decode(clean, protected_key)) return false;
    if (protected_key.size() <= 5 ||
        std::memcmp(protected_key.data(), "DPAPI", 5) != 0)
        return false;
    std::vector<std::uint8_t> dpapi(protected_key.begin() + 5,
                                    protected_key.end());
    return dpapi_unprotect(dpapi, key) && key.size() == 32;
}

const char* gcm_or_dpapi(const std::vector<std::uint8_t>& key,
                         const void* data, std::size_t size,
                         std::string& plaintext, bool& decrypted) {
    decrypted = false;
    plaintext.clear();
    if (data == nullptr || size == 0) return "empty";
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    if (size >= 3 && bytes[0] == 'v' && bytes[1] == '1' &&
        (bytes[2] == '0' || bytes[2] == '1')) {
        if (size < 3 + 12 + 16) return "truncated";
        std::vector<std::uint8_t> out;
        if (!decrypt_gcm(key, bytes + 3, 12, bytes + 15, size - 15 - 16,
                         bytes + size - 16, 16, out))
            return "gcm_failed";
        plaintext.assign(out.begin(), out.end());
        decrypted = true;
        return "aes_gcm";
    }
    if (size >= 3 && bytes[0] == 'v' && bytes[1] == '2' && bytes[2] == '0')
        return "app_bound_v20";
    std::vector<std::uint8_t> input(bytes, bytes + size);
    std::vector<std::uint8_t> out;
    if (dpapi_unprotect(input, out)) {
        plaintext.assign(out.begin(), out.end());
        decrypted = true;
        return "dpapi";
    }
    return "failed";
}

std::string cleanup(const std::string& value) {
    std::string out;
    out.reserve((std::min)(value.size(), static_cast<std::size_t>(4096)));
    for (char c : value) {
        if (out.size() >= 4096) break;
        if (c == '\0') continue;
        out.push_back(c);
    }
    return out;
}

std::wstring temp_directory() {
    wchar_t path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, path);
    wchar_t name[64];
    std::swprintf(name, 64, L"eog_browser_%lu_%lu", GetCurrentProcessId(),
                  GetTickCount());
    return art::join_path(path, name);
}

void remove_tree(const std::wstring& dir) {
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(art::join_path(dir, L"*").c_str(), &entry);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(entry.cFileName, L".") == 0 ||
                wcscmp(entry.cFileName, L"..") == 0) continue;
            const std::wstring path = art::join_path(dir, entry.cFileName);
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                remove_tree(path);
            else
                DeleteFileW(path.c_str());
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
    RemoveDirectoryW(dir.c_str());
}

std::string timestamp_from_chrome(std::uint64_t value) {
    if (value == 0 || value > 0x7FFFFFFFFFFFFFFFull / 10) return {};
    return art::filetime_iso(value * 10);
}

int collect_cookies(sqlite3* db, const std::vector<std::uint8_t>& key,
                    const std::wstring& source, const ArtifactSink& sink) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT host_key,name,path,expires_utc,is_secure,"
        "is_httponly,value,encrypted_value FROM cookies";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return 0;
    int rows = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const char* host = reinterpret_cast<const char*>(
            sqlite3_column_text(statement, 0));
        const char* name = reinterpret_cast<const char*>(
            sqlite3_column_text(statement, 1));
        const char* path = reinterpret_cast<const char*>(
            sqlite3_column_text(statement, 2));
        const std::uint64_t expires = static_cast<std::uint64_t>(
            sqlite3_column_int64(statement, 3));
        const int secure = sqlite3_column_int(statement, 4);
        const int http_only = sqlite3_column_int(statement, 5);
        const char* plain = reinterpret_cast<const char*>(
            sqlite3_column_text(statement, 6));
        const void* encrypted = sqlite3_column_blob(statement, 7);
        const int encrypted_size = sqlite3_column_bytes(statement, 7);
        std::string value = plain != nullptr ? plain : "";
        std::string method = value.empty() ? "none" : "plaintext";
        bool decrypted = false;
        if (value.empty()) {
            method = gcm_or_dpapi(key, encrypted,
                static_cast<std::size_t>(encrypted_size), value, decrypted);
        }
        if (value.empty() && method != "app_bound_v20") continue;
        ArtifactRow row;
        row.type = "browser_cookie";
        row.source = art::to_utf8(source);
        row.key = std::string(host ? host : "") + "|" +
            std::string(name ? name : "") + "|" + std::string(path ? path : "");
        row.timestamp_utc = timestamp_from_chrome(expires);
        row.path = art::to_utf8(source);
        row.name = name ? name : "";
        row.data = "{\"host\":" + art::json_escape(host ? host : "") +
            ",\"name\":" + art::json_escape(name ? name : "") +
            ",\"path\":" + art::json_escape(path ? path : "") +
            ",\"expires_utc\":" + art::json_escape(
                timestamp_from_chrome(expires)) +
            ",\"secure\":" + (secure ? "true" : "false") +
            ",\"http_only\":" + (http_only ? "true" : "false") +
            ",\"value\":" + art::json_escape(cleanup(value)) +
            ",\"encryption\":" + art::json_escape(method) + "}";
        sink(row);
        ++rows;
    }
    sqlite3_finalize(statement);
    return rows;
}

int collect_passwords(sqlite3* db, const std::vector<std::uint8_t>& key,
                      const std::wstring& source, const ArtifactSink& sink) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT origin_url,username_value,password_value,"
        "date_created,date_last_used,blacklisted_by_user FROM logins";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return 0;
    int rows = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (sqlite3_column_int(statement, 5) != 0) continue;
        const char* origin = reinterpret_cast<const char*>(
            sqlite3_column_text(statement, 0));
        const char* username = reinterpret_cast<const char*>(
            sqlite3_column_text(statement, 1));
        const void* encrypted = sqlite3_column_blob(statement, 2);
        const int encrypted_size = sqlite3_column_bytes(statement, 2);
        const std::uint64_t created = static_cast<std::uint64_t>(
            sqlite3_column_int64(statement, 3));
        const std::uint64_t used = static_cast<std::uint64_t>(
            sqlite3_column_int64(statement, 4));
        std::string password;
        bool decrypted = false;
        const char* method = gcm_or_dpapi(key, encrypted,
            static_cast<std::size_t>(encrypted_size), password, decrypted);
        if (password.empty() && std::string(method) != "app_bound_v20") continue;
        ArtifactRow row;
        row.type = "browser_password";
        row.source = art::to_utf8(source);
        row.key = std::string(origin ? origin : "") + "|" +
            std::string(username ? username : "");
        row.timestamp_utc = timestamp_from_chrome(used);
        row.path = art::to_utf8(source);
        row.name = origin ? origin : "";
        row.data = "{\"origin\":" + art::json_escape(origin ? origin : "") +
            ",\"username\":" + art::json_escape(username ? username : "") +
            ",\"password\":" + art::json_escape(cleanup(password)) +
            ",\"date_created\":" + art::json_escape(
                timestamp_from_chrome(created)) +
            ",\"date_last_used\":" + art::json_escape(
                timestamp_from_chrome(used)) +
            ",\"encryption\":" + art::json_escape(method) + "}";
        sink(row);
        ++rows;
    }
    sqlite3_finalize(statement);
    return rows;
}

void collect_one_profile(const std::wstring& profile,
                         const std::vector<std::uint8_t>& key,
                         const ArtifactSink& sink, int& rows) {
    const std::wstring temp = temp_directory();
    art::ensure_directory(temp);
    const std::wstring cookies = art::join_path(profile, L"Network\\Cookies");
    const std::wstring logins = art::join_path(profile, L"Login Data");
    std::wstring cookies_copy = art::join_path(temp, L"Cookies");
    std::wstring logins_copy = art::join_path(temp, L"Login Data");
    if (GetFileAttributesW(cookies.c_str()) != INVALID_FILE_ATTRIBUTES &&
        copy_shared(cookies, cookies_copy)) {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(art::to_utf8(cookies_copy).c_str(), &db,
                            SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
            rows += collect_cookies(db, key, cookies, sink);
            sqlite3_close(db);
        }
    }
    if (GetFileAttributesW(logins.c_str()) != INVALID_FILE_ATTRIBUTES &&
        copy_shared(logins, logins_copy)) {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(art::to_utf8(logins_copy).c_str(), &db,
                            SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
            rows += collect_passwords(db, key, logins, sink);
            sqlite3_close(db);
        }
    }
    remove_tree(temp);
}

} // namespace

int collect_browser_secrets(const std::wstring& user_data_dir,
                            const ArtifactSink& sink, std::wstring& error) {
    std::vector<std::wstring> roots;
    if (!user_data_dir.empty()) {
        roots.push_back(user_data_dir);
    } else {
        wchar_t local[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) != 0) {
            roots.push_back(art::join_path(local, L"Google\\Chrome\\User Data"));
            roots.push_back(art::join_path(local, L"Microsoft\\Edge\\User Data"));
        }
    }
    int rows = 0;
    for (const auto& root : roots) {
        const std::wstring local_state = art::join_path(root, L"Local State");
        if (GetFileAttributesW(local_state.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;
        std::vector<std::uint8_t> key;
        if (!load_master_key(local_state, key)) {
            error = L"Local State key unavailable or not DPAPI";
            continue;
        }
        std::vector<std::wstring> profiles;
        WIN32_FIND_DATAW entry{};
        HANDLE search = FindFirstFileW(art::join_path(root, L"*").c_str(), &entry);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(entry.cFileName, L".") == 0 ||
                    wcscmp(entry.cFileName, L"..") == 0 ||
                    (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                    continue;
                const std::wstring name = entry.cFileName;
                if (_wcsicmp(name.c_str(), L"Default") == 0 ||
                    _wcsnicmp(name.c_str(), L"Profile ", 8) == 0) {
                    profiles.push_back(art::join_path(root, name));
                }
            } while (FindNextFileW(search, &entry));
            FindClose(search);
        }
        for (const auto& profile : profiles)
            collect_one_profile(profile, key, sink, rows);
    }
    return rows;
}

} // namespace deep
