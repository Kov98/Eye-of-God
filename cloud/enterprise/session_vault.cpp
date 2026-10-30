#include "session_vault.hpp"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace deep::cloud {
namespace {

constexpr BYTE kEntropy[] = {
    'O', 'l', 'h', 'o', 'D', 'e', 'D', 'e', 'u', 's', '.', 'S', 'e', 's', 's', 'i',
    'o', 'n', '.', 'v', '1'
};
constexpr char kMagic[4] = {'O', 'D', 'S', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaxString = 64 * 1024;

void put_u32(std::vector<BYTE>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<BYTE>((value >> (i * 8)) & 0xff));
}
void put_u64(std::vector<BYTE>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<BYTE>((value >> (i * 8)) & 0xff));
}
void put_string(std::vector<BYTE>& out, const std::string& value) {
    put_u32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}
bool get_u32(const std::vector<BYTE>& in, std::size_t& offset, std::uint32_t& value) {
    if (offset + 4 > in.size()) return false;
    value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(in[offset + i]) << (i * 8);
    offset += 4;
    return true;
}
bool get_u64(const std::vector<BYTE>& in, std::size_t& offset, std::uint64_t& value) {
    if (offset + 8 > in.size()) return false;
    value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(in[offset + i]) << (i * 8);
    offset += 8;
    return true;
}
bool get_string(const std::vector<BYTE>& in, std::size_t& offset, std::string& value) {
    std::uint32_t length = 0;
    if (!get_u32(in, offset, length) || length > kMaxString || offset + length > in.size())
        return false;
    value.assign(reinterpret_cast<const char*>(in.data() + offset), length);
    offset += length;
    return true;
}

std::vector<BYTE> serialize(const PersistedSession& session) {
    std::vector<BYTE> out;
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    put_u32(out, kVersion);
    put_string(out, session.token);
    put_string(out, session.principal.id);
    put_string(out, session.principal.display_name);
    put_u32(out, static_cast<std::uint32_t>(session.principal.role));
    out.push_back(session.principal.active ? 1 : 0);
    put_u64(out, static_cast<std::uint64_t>(session.expires_at_epoch));
    return out;
}

bool deserialize(const std::vector<BYTE>& in, PersistedSession& session) {
    if (in.size() < 8 || !std::equal(std::begin(kMagic), std::end(kMagic), in.begin()))
        return false;
    std::size_t offset = 4;
    std::uint32_t version = 0;
    if (!get_u32(in, offset, version) || version != kVersion) return false;
    if (!get_string(in, offset, session.token) ||
        !get_string(in, offset, session.principal.id) ||
        !get_string(in, offset, session.principal.display_name)) return false;
    std::uint32_t role = 0;
    if (!get_u32(in, offset, role) || role > 3 || offset >= in.size()) return false;
    session.principal.role = static_cast<Role>(role);
    session.principal.active = in[offset++] != 0;
    std::uint64_t expires = 0;
    if (!get_u64(in, offset, expires) || offset != in.size()) return false;
    session.expires_at_epoch = static_cast<std::int64_t>(expires);
    return !session.token.empty() && !session.principal.id.empty() &&
           session.expires_at_epoch > 0;
}

} // namespace

SessionVault::SessionVault(std::string path) : path_(std::move(path)) {}

bool SessionVault::save(const PersistedSession& session, std::string& error) {
    error.clear();
    const std::vector<BYTE> plain = serialize(session);
    DATA_BLOB input{static_cast<DWORD>(plain.size()), const_cast<BYTE*>(plain.data())};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy)), const_cast<BYTE*>(kEntropy)};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Olho de Deus session", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        error = "DPAPI could not protect session";
        return false;
    }
    try {
        const std::filesystem::path path(path_);
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path_, std::ios::binary | std::ios::trunc);
        if (!file) {
            LocalFree(output.pbData);
            error = "cannot open session vault";
            return false;
        }
        file.write(reinterpret_cast<const char*>(output.pbData), output.cbData);
        file.close();
        LocalFree(output.pbData);
        if (!file) {
            error = "cannot write session vault";
            return false;
        }
        return true;
    } catch (const std::exception&) {
        LocalFree(output.pbData);
        error = "cannot write session vault";
        return false;
    }
}

std::optional<PersistedSession> SessionVault::load(bool& found, std::string& error) {
    found = false;
    error.clear();
    std::ifstream file(path_, std::ios::binary);
    if (!file) return std::nullopt;
    found = true;
    std::vector<BYTE> protected_bytes((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());
    if (protected_bytes.empty()) {
        error = "empty session vault";
        return std::nullopt;
    }
    DATA_BLOB input{static_cast<DWORD>(protected_bytes.size()), protected_bytes.data()};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy)), const_cast<BYTE*>(kEntropy)};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        error = "DPAPI could not read session";
        return std::nullopt;
    }
    std::vector<BYTE> plain(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    PersistedSession session;
    if (!deserialize(plain, session)) {
        error = "invalid session vault";
        return std::nullopt;
    }
    return session;
}

bool SessionVault::erase(std::string& error) {
    error.clear();
    const std::filesystem::path path(path_);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return true;
    std::filesystem::remove(path, ec);
    if (ec) {
        error = "cannot erase session vault";
        return false;
    }
    return true;
}

} // namespace deep::cloud
