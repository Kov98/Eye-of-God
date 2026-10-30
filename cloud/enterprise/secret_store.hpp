#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deep::cloud {

// DPAPI-backed store for short secrets (currently the token-vault key).
// A queue job carries only an opaque ref; the worker resolves it locally.
class SecretStore {
public:
    explicit SecretStore(std::string root = {});

    bool create(const std::string& secret, std::string& ref, std::string& error);
    bool resolve(const std::string& ref, std::string& secret, std::string& error) const;
    bool remove(const std::string& ref, std::string& error) const;
    std::vector<std::string> list(std::string& error) const;
    bool prune(const std::vector<std::string>& keep, std::int64_t min_age_seconds,
               int& removed, std::string& error) const;

    static std::string default_root();
    static bool valid_ref(const std::string& ref);

private:
    std::string path_for(const std::string& ref) const;
    std::string root_;
};

} // namespace deep::cloud
