#include "identity.hpp"

#include "cloud/core/cloud_core.hpp"
#include "third_party/nlohmann/json.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <ctime>

namespace deep::cloud {
namespace {

constexpr int kIterations = 120000;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string to_hex(const unsigned char* data, std::size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        output.push_back(digits[data[i] >> 4]);
        output.push_back(digits[data[i] & 0x0f]);
    }
    return output;
}

bool from_hex(const std::string& text, std::vector<unsigned char>& output) {
    if (text.empty() || text.size() % 2 != 0) return false;
    output.clear();
    output.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int high = nibble(text[i]);
        const int low = nibble(text[i + 1]);
        if (high < 0 || low < 0) return false;
        output.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return true;
}

bool constant_time_equal(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i)
        difference |= static_cast<unsigned char>(left[i] ^ right[i]);
    return difference == 0;
}

bool derive_password(const std::string& password, const std::vector<unsigned char>& salt,
                     int iterations, std::string& hash_hex, std::string& error) {
    if (password.empty()) {
        error = "password must not be empty";
        return false;
    }
    if (iterations < 10000) {
        error = "invalid PBKDF2 iterations";
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) {
        error = "could not open PBKDF2 provider";
        return false;
    }
    unsigned char derived[32] = {};
    const NTSTATUS status = BCryptDeriveKeyPBKDF2(algorithm,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()),
        static_cast<ULONGLONG>(iterations), derived, sizeof(derived), 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status != 0) {
        error = "password derivation failed";
        return false;
    }
    hash_hex = to_hex(derived, sizeof(derived));
    return true;
}

std::string utc_now_iso() {
    const std::time_t now = std::time(nullptr);
    std::tm value{};
    gmtime_s(&value, &now);
    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &value);
    return text;
}

bool valid_user_id(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (unsigned char c : id) {
        if (!std::isalnum(c) && c != '.' && c != '_' && c != '@' && c != '-')
            return false;
    }
    return true;
}

} // namespace

LocalIdentityStore::LocalIdentityStore(std::string path) : path_(std::move(path)) {}

bool LocalIdentityStore::load(std::string& error) {
    users_.clear();
    error.clear();
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        if (std::filesystem::exists(path_)) {
            error = "cannot read identity store";
            return false;
        }
        return true;
    }
    const std::string text((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    try {
        const nlohmann::json document = nlohmann::json::parse(text);
        if (!document.is_object() || !document.contains("users") ||
            !document["users"].is_array()) {
            error = "invalid identity store schema";
            return false;
        }
        for (const auto& entry : document["users"]) {
            if (!entry.is_object()) {
                error = "invalid identity entry";
                return false;
            }
            LocalUser user;
            user.id = entry.value("id", "");
            user.display_name = entry.value("display_name", "");
            user.active = entry.value("active", false);
            user.salt_hex = entry.value("salt", "");
            user.hash_hex = entry.value("hash", "");
            user.iterations = entry.value("iterations", kIterations);
            user.created_at_utc = entry.value("created_at_utc", "");
            Role role = Role::Auditor;
            if (!valid_user_id(user.id) || !parse_role(entry.value("role", ""), role) ||
                user.salt_hex.empty() || user.hash_hex.empty()) {
                error = "invalid identity entry";
                return false;
            }
            user.role = role;
            users_.push_back(std::move(user));
        }
    } catch (const std::exception&) {
        error = "identity store is not valid JSON";
        return false;
    }
    return true;
}

bool LocalIdentityStore::save(std::string& error) const {
    error.clear();
    try {
        const std::filesystem::path target(path_);
        if (target.has_parent_path())
            std::filesystem::create_directories(target.parent_path());
        nlohmann::json document;
        document["version"] = 1;
        document["users"] = nlohmann::json::array();
        for (const auto& user : users_) {
            document["users"].push_back({
                {"id", user.id},
                {"display_name", user.display_name},
                {"role", role_name(user.role)},
                {"active", user.active},
                {"salt", user.salt_hex},
                {"hash", user.hash_hex},
                {"iterations", user.iterations},
                {"created_at_utc", user.created_at_utc}
            });
        }
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "cannot write identity store";
            return false;
        }
        output << document.dump(2);
        output.close();
        if (!output) {
            error = "cannot write identity store";
            return false;
        }
        return true;
    } catch (const std::exception&) {
        error = "cannot write identity store";
        return false;
    }
}

const LocalUser* LocalIdentityStore::find(const std::string& id) const {
    for (const auto& user : users_)
        if (user.id == id) return &user;
    return nullptr;
}

LocalUser* LocalIdentityStore::find(const std::string& id) {
    for (auto& user : users_)
        if (user.id == id) return &user;
    return nullptr;
}

bool LocalIdentityStore::create_user(const std::string& id,
                                     const std::string& display_name, Role role,
                                     const std::string& password, std::string& error,
                                     bool overwrite) {
    if (!valid_user_id(id)) {
        error = "invalid user id";
        return false;
    }
    if (password.size() < 10) {
        error = "password must have at least 10 characters";
        return false;
    }
    if (!overwrite && find(id) != nullptr) {
        error = "user already exists";
        return false;
    }
    std::vector<unsigned char> salt(16);
    if (BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        error = "could not generate password salt";
        return false;
    }
    std::string hash_hex;
    if (!derive_password(password, salt, kIterations, hash_hex, error))
        return false;
    LocalUser user;
    user.id = id;
    user.display_name = display_name.empty() ? id : display_name;
    user.role = role;
    user.active = true;
    user.salt_hex = to_hex(salt.data(), salt.size());
    user.hash_hex = hash_hex;
    user.iterations = kIterations;
    user.created_at_utc = utc_now_iso();
    LocalUser* existing = find(id);
    const LocalUser previous = existing ? *existing : LocalUser{};
    if (existing) {
        *existing = std::move(user);
    } else {
        users_.push_back(std::move(user));
    }
    if (!save(error)) {
        if (existing) *existing = previous;
        else users_.pop_back();
        return false;
    }
    return true;
}

std::optional<Principal> LocalIdentityStore::authenticate(
    const std::string& id, const std::string& password) const {
    const LocalUser* user = find(id);
    if (user == nullptr || !user->active) return std::nullopt;
    std::vector<unsigned char> salt;
    if (!from_hex(user->salt_hex, salt) || salt.empty()) return std::nullopt;
    std::string hash_hex;
    std::string error;
    if (!derive_password(password, salt, user->iterations, hash_hex, error))
        return std::nullopt;
    if (!constant_time_equal(hash_hex, lower(user->hash_hex))) return std::nullopt;
    Principal principal;
    principal.id = user->id;
    principal.display_name = user->display_name;
    principal.role = user->role;
    principal.active = user->active;
    return principal;
}

std::vector<LocalUser> LocalIdentityStore::users() const {
    return users_;
}

bool LocalIdentityStore::set_active(const std::string& id, bool active,
                                    std::string& error) {
    LocalUser* user = find(id);
    if (user == nullptr) {
        error = "user not found";
        return false;
    }
    user->active = active;
    return save(error);
}

bool LocalIdentityStore::set_role(const std::string& id, Role role,
                                  std::string& error) {
    LocalUser* user = find(id);
    if (user == nullptr) {
        error = "user not found";
        return false;
    }
    user->role = role;
    return save(error);
}

} // namespace deep::cloud
