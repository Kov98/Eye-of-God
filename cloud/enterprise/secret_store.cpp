#include "secret_store.hpp"

#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

namespace deep::cloud {
namespace {

constexpr const char* kEntropy = "OlhoDeDeus.SecretStore.v1";
constexpr std::size_t kMaxSecret = 4096;
constexpr std::size_t kRefBytes = 16;

std::string hex_ref(const BYTE* bytes, std::size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(digits[(bytes[i] >> 4) & 0xf]);
        out.push_back(digits[bytes[i] & 0xf]);
    }
    return out;
}

bool protect_secret(const std::string& plain, std::vector<BYTE>& sealed,
                    std::string& error) {
    DATA_BLOB input{static_cast<DWORD>(plain.size()),
                    const_cast<BYTE*>(reinterpret_cast<const BYTE*>(plain.data()))};
    DATA_BLOB entropy{static_cast<DWORD>(std::char_traits<char>::length(kEntropy)),
                      const_cast<BYTE*>(reinterpret_cast<const BYTE*>(kEntropy))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Olho de Deus secret", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        error = "DPAPI could not protect secret";
        return false;
    }
    sealed.assign(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return true;
}

bool unprotect_secret(const std::vector<BYTE>& sealed, std::string& plain,
                      std::string& error) {
    if (sealed.empty()) {
        error = "secret is empty";
        return false;
    }
    DATA_BLOB input{static_cast<DWORD>(sealed.size()),
                    const_cast<BYTE*>(sealed.data())};
    DATA_BLOB entropy{static_cast<DWORD>(std::char_traits<char>::length(kEntropy)),
                      const_cast<BYTE*>(reinterpret_cast<const BYTE*>(kEntropy))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        error = "DPAPI could not unprotect secret";
        return false;
    }
    plain.assign(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return true;
}

bool write_all(const std::filesystem::path& path, const std::vector<BYTE>& data,
               std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot create secret file";
        return false;
    }
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    output.close();
    if (!output) {
        error = "cannot write secret file";
        return false;
    }
    return true;
}

} // namespace

SecretStore::SecretStore(std::string root) : root_(std::move(root)) {
    if (root_.empty()) root_ = default_root();
}

std::string SecretStore::default_root() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
        return (std::filesystem::path(buffer) / "OlhoDeDeus" / "secrets").string();
    return (std::filesystem::temp_directory_path() / "OlhoDeDeus" / "secrets").string();
}

bool SecretStore::valid_ref(const std::string& ref) {
    if (ref.size() != kRefBytes * 2) return false;
    for (unsigned char c : ref) {
        if (!std::isxdigit(c)) return false;
    }
    return true;
}

std::string SecretStore::path_for(const std::string& ref) const {
    return (std::filesystem::path(root_) / (ref + ".bin")).string();
}

bool SecretStore::create(const std::string& secret, std::string& ref,
                         std::string& error) {
    error.clear();
    ref.clear();
    if (secret.empty() || secret.size() > kMaxSecret) {
        error = "secret is empty or too large";
        return false;
    }
    std::error_code directory_error;
    std::filesystem::create_directories(root_, directory_error);
    if (directory_error) {
        error = "cannot create secret store";
        return false;
    }
    std::vector<BYTE> sealed;
    if (!protect_secret(secret, sealed, error)) return false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        BYTE random[kRefBytes] = {};
        if (BCryptGenRandom(nullptr, random, sizeof(random),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            error = "cannot generate secret reference";
            return false;
        }
        const std::string candidate = hex_ref(random, sizeof(random));
        const auto path = std::filesystem::path(path_for(candidate));
        if (std::filesystem::exists(path)) continue;
        const auto temporary = path.string() + ".tmp";
        if (!write_all(temporary, sealed, error)) return false;
        if (!MoveFileExW(std::filesystem::path(temporary).wstring().c_str(),
                         path.wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            error = "cannot commit secret";
            return false;
        }
        ref = candidate;
        return true;
    }
    error = "could not allocate a unique secret reference";
    return false;
}

bool SecretStore::resolve(const std::string& ref, std::string& secret,
                          std::string& error) const {
    error.clear();
    secret.clear();
    if (!valid_ref(ref)) {
        error = "invalid secret reference";
        return false;
    }
    std::ifstream input(path_for(ref), std::ios::binary);
    if (!input) {
        error = "secret reference not found";
        return false;
    }
    std::vector<BYTE> sealed((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    if (sealed.empty() || sealed.size() > kMaxSecret * 2) {
        error = "invalid secret file";
        return false;
    }
    return unprotect_secret(sealed, secret, error);
}

std::vector<std::string> SecretStore::list(std::string& error) const {
    error.clear();
    std::vector<std::string> refs;
    std::error_code directory_error;
    if (!std::filesystem::exists(root_, directory_error)) return refs;
    for (const auto& entry : std::filesystem::directory_iterator(root_, directory_error)) {
        if (directory_error || !entry.is_regular_file()) continue;
        if (entry.path().extension() != ".bin") continue;
        const std::string ref = entry.path().stem().string();
        if (valid_ref(ref)) refs.push_back(ref);
    }
    std::sort(refs.begin(), refs.end());
    return refs;
}

bool SecretStore::prune(const std::vector<std::string>& keep,
                        std::int64_t min_age_seconds, int& removed,
                        std::string& error) const {
    error.clear();
    removed = 0;
    const std::set<std::string> keep_set(keep.begin(), keep.end());
    const auto now = std::filesystem::file_time_type::clock::now();
    for (const std::string& ref : list(error)) {
        if (error.empty() == false) return false;
        if (keep_set.count(ref) != 0) continue;
        std::error_code file_error;
        const auto modified = std::filesystem::last_write_time(path_for(ref), file_error);
        if (file_error) continue;
        const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - modified).count();
        if (age < min_age_seconds) continue;
        if (!std::filesystem::remove(path_for(ref), file_error) || file_error) continue;
        ++removed;
    }
    return true;
}
bool SecretStore::remove(const std::string& ref, std::string& error) const {
    error.clear();
    if (!valid_ref(ref)) {
        error = "invalid secret reference";
        return false;
    }
    std::error_code remove_error;
    if (!std::filesystem::remove(path_for(ref), remove_error) || remove_error) {
        error = "could not remove secret";
        return false;
    }
    return true;
}

} // namespace deep::cloud
