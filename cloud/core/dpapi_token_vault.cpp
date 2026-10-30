#include "dpapi_token_vault.hpp"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace deep::cloud {
namespace {

constexpr BYTE kEntropy[] = {
    'O', 'l', 'h', 'o', 'D', 'e', 'D', 'e', 'u', 's', '.', 'C', 'l', 'o', 'u', 'd',
    '.', 'T', 'o', 'k', 'e', 'n', '.', 'v', '1'
};
constexpr char kMagic[4] = {'O', 'D', 'T', '1'};
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

std::vector<BYTE> serialize(const OAuthToken& token) {
    std::vector<BYTE> out;
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    put_u32(out, kVersion);
    put_string(out, token.access_token);
    put_string(out, token.refresh_token);
    put_string(out, token.token_type);
    put_u32(out, static_cast<std::uint32_t>(token.expires_in));
    const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
        token.obtained_at.time_since_epoch()).count();
    put_u64(out, static_cast<std::uint64_t>(epoch));
    return out;
}

bool deserialize(const std::vector<BYTE>& in, OAuthToken& token) {
    if (in.size() < 8 || !std::equal(std::begin(kMagic), std::end(kMagic), in.begin()))
        return false;
    std::size_t offset = 4;
    std::uint32_t version = 0;
    if (!get_u32(in, offset, version) || version != kVersion) return false;
    if (!get_string(in, offset, token.access_token) ||
        !get_string(in, offset, token.refresh_token) ||
        !get_string(in, offset, token.token_type)) return false;
    std::uint32_t expires = 0;
    std::uint64_t epoch = 0;
    if (!get_u32(in, offset, expires) || !get_u64(in, offset, epoch)) return false;
    if (offset != in.size() || token.access_token.empty()) return false;
    token.expires_in = static_cast<int>(expires);
    token.obtained_at = Clock::time_point(std::chrono::seconds(epoch));
    return true;
}

std::wstring default_root() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) return L".\\cloud_tokens";
    return std::wstring(buffer) + L"\\OlhoDeDeus\\cloud_tokens";
}

std::wstring hash_key(const std::string& key) {
    std::uint64_t value = 1469598103934665603ull;
    for (unsigned char c : key) {
        value ^= c;
        value *= 1099511628211ull;
    }
    static const wchar_t* hex = L"0123456789abcdef";
    std::wstring result;
    for (int i = 0; i < 16; ++i) {
        result.push_back(hex[(value >> ((15 - i) * 4)) & 0xf]);
    }
    return result;
}

} // namespace

DpapiTokenVault::DpapiTokenVault(std::wstring root)
    : root_(root.empty() ? default_root() : std::move(root)) {}

std::wstring DpapiTokenVault::path_for(const std::string& key) const {
    return root_ + L"\\token_" + hash_key(key) + L".bin";
}

std::optional<OAuthToken> DpapiTokenVault::load(const std::string& key) {
    const std::wstring path = path_for(key);
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    if (!input) return std::nullopt;
    std::vector<BYTE> protected_bytes((std::istreambuf_iterator<char>(input)),
                                      std::istreambuf_iterator<char>());
    if (protected_bytes.empty()) return std::nullopt;

    DATA_BLOB in{static_cast<DWORD>(protected_bytes.size()), protected_bytes.data()};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy)),
                      const_cast<BYTE*>(kEntropy)};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return std::nullopt;
    }
    std::vector<BYTE> plain(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    OAuthToken token;
    if (!deserialize(plain, token)) return std::nullopt;
    return token;
}

bool DpapiTokenVault::save(const std::string& key, const OAuthToken& token) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(root_), error);
    if (error) return false;
    const std::vector<BYTE> plain = serialize(token);
    DATA_BLOB in{static_cast<DWORD>(plain.size()), const_cast<BYTE*>(plain.data())};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy)),
                      const_cast<BYTE*>(kEntropy)};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"Olho de Deus cloud token", &entropy, nullptr,
            nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return false;
    }
    const std::wstring path = path_for(key);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        LocalFree(out.pbData);
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, out.pbData, out.cbData, &written, nullptr) &&
                    written == out.cbData;
    CloseHandle(file);
    LocalFree(out.pbData);
    return ok;
}

bool DpapiTokenVault::erase(const std::string& key) {
    const std::wstring path = path_for(key);
    if (DeleteFileW(path.c_str())) return true;
    return GetLastError() == ERROR_FILE_NOT_FOUND;
}

} // namespace deep::cloud
