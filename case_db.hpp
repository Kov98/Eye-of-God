#pragma once

#include <string>

#include "artifacts.hpp"

struct sqlite3;

namespace deep {

class CaseDatabase {
public:
    CaseDatabase() = default;
    ~CaseDatabase();

    CaseDatabase(const CaseDatabase&) = delete;
    CaseDatabase& operator=(const CaseDatabase&) = delete;

    bool open(const std::wstring& path, std::wstring& error);
    void close();
    bool is_open() const { return db_ != nullptr; }

    bool begin(std::wstring& error);
    bool commit(std::wstring& error);
    bool rollback(std::wstring& error);

    bool insert(const ArtifactRow& row, std::wstring& error);
    bool set_meta(const std::string& key, const std::string& value,
                  std::wstring& error);
    int count(const std::string& table, std::wstring& error);
    bool query_json(const std::string& sql, std::string& output,
                    std::wstring& error);

private:
    bool exec(const std::string& sql, std::wstring& error);
    sqlite3* db_ = nullptr;
};

} // namespace deep
