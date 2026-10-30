// language: C++, file: artifact_srum.cpp, target: Windows 10/11
// SRUM (System Resource Usage Monitor) reader. Copies SRUDB.dat and its logs
// to a private temp directory, opens the ESE database through Jet/ESENT and
// emits one "srum" row per record with AppId/UserId resolved from
// SruDbIdMapTable.

#include "artifacts.hpp"

#include <windows.h>
#include <esent.h>
#include <sddl.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace deep {
namespace {

constexpr std::size_t kMaxRows = 200000;
constexpr std::size_t kMaxString = 512;

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t read_u64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) value = (value << 8) | p[i];
    return value;
}

std::wstring widen_ansi(const char* text) {
    if (text == nullptr || *text == '\0') return {};
    const int needed = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (needed <= 1) return {};
    std::wstring out(static_cast<std::size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, &out[0], needed);
    return out;
}

std::wstring temp_directory() {
    wchar_t path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, path);
    wchar_t unique[64];
    std::swprintf(unique, 64, L"eog_srum_%lu_%lu", GetCurrentProcessId(),
                  GetTickCount());
    return art::join_path(path, unique);
}

std::wstring directory_of(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring base_name(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool copy_file(const std::wstring& from, const std::wstring& to) {
    return CopyFileW(from.c_str(), to.c_str(), FALSE) != FALSE;
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

std::string hex_prefix(const std::vector<std::uint8_t>& data, std::size_t cap) {
    static const char* digits = "0123456789abcdef";
    const std::size_t count = (std::min)(data.size(), cap);
    std::string out;
    out.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

class SrumDatabase {
public:
    ~SrumDatabase() { close(); }

    bool open(const std::wstring& source, std::wstring& error) {
        source_ = source;
        temp_ = temp_directory();
        if (!art::ensure_directory(temp_)) {
            error = L"cannot create SRUM temp directory";
            return false;
        }
        const std::wstring source_dir = directory_of(source);
        const std::wstring db_copy = art::join_path(temp_, L"SRUDB.dat");
        if (!copy_file(source, db_copy)) {
            error = L"cannot copy SRUDB.dat";
            cleanup_files();
            return false;
        }
        WIN32_FIND_DATAW entry{};
        HANDLE search = FindFirstFileW(art::join_path(source_dir, L"SRU*").c_str(),
                                       &entry);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                std::wstring name = entry.cFileName;
                std::wstring lower = name;
                for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
                if (lower == L"srudb.dat") continue;
                if (lower.find(L".log") == std::wstring::npos &&
                    lower.find(L".chk") == std::wstring::npos) continue;
                copy_file(art::join_path(source_dir, name), art::join_path(temp_, name));
            } while (FindNextFileW(search, &entry));
            FindClose(search);
        }
        std::wstring dir = temp_;
        if (!dir.empty() && dir.back() != L'\\') dir.push_back(L'\\');
        JET_ERR result = JetCreateInstance2W(&instance_, L"eog_srum", 0, 0);
        if (result != JET_errSuccess) {
            error = L"JetCreateInstance failed: " + std::to_wstring(result);
            cleanup_files();
            return false;
        }
        JetSetSystemParameterW(&instance_, JET_sesidNil, JET_paramSystemPath, 0,
                               dir.c_str());
        JetSetSystemParameterW(&instance_, JET_sesidNil, JET_paramTempPath, 0,
                               dir.c_str());
        JetSetSystemParameterW(&instance_, JET_sesidNil, JET_paramLogFilePath, 0,
                               dir.c_str());
        JetSetSystemParameterW(&instance_, JET_sesidNil, JET_paramBaseName, 0,
                               L"SRU");
        JetSetSystemParameterW(&instance_, JET_sesidNil, JET_paramRecovery, 0,
                               L"Off");
        result = JetInit(&instance_);
        if (result != JET_errSuccess) {
            error = L"JetInit failed: " + std::to_wstring(result);
            cleanup_files();
            return false;
        }
        initialized_ = true;
        result = JetBeginSessionW(instance_, &session_, nullptr, nullptr);
        if (result != JET_errSuccess) {
            error = L"JetBeginSession failed: " + std::to_wstring(result);
            return false;
        }
        result = JetAttachDatabase2W(session_, db_copy.c_str(), 0, 0);
        if (result != JET_errSuccess) {
            error = L"JetAttachDatabase failed: " + std::to_wstring(result);
            return false;
        }
        attached_ = true;
        result = JetOpenDatabaseW(session_, db_copy.c_str(), nullptr, &dbid_, 0);
        if (result != JET_errSuccess) {
            error = L"JetOpenDatabase failed: " + std::to_wstring(result);
            return false;
        }
        opened_ = true;
        return true;
    }

    void close() {
        if (opened_) { JetCloseDatabase(session_, dbid_, 0); opened_ = false; }
        if (attached_) {
            // JetDetachDatabaseW is declared with an ANSI path in some MinGW
            // headers; closing the database and terminating is enough for our
            // private copy.
            attached_ = false;
        }
        if (session_ != JET_sesidNil) {
            JetEndSession(session_, 0);
            session_ = JET_sesidNil;
        }
        if (initialized_) {
            JetTerm(instance_);
            initialized_ = false;
        }
        cleanup_files();
    }

    bool tables(std::vector<std::wstring>& names, std::wstring& error) {
        JET_OBJECTLIST list{};
        list.cbStruct = sizeof(list);
        JET_ERR result = JetGetObjectInfoW(session_, dbid_, JET_objtypTable,
            nullptr, nullptr, &list, sizeof(list), 1);
        if (result != JET_errSuccess) {
            error = L"JetGetObjectInfo failed: " + std::to_wstring(result);
            return false;
        }
        for (ULONG i = 0; i < list.cRecord; ++i) {
            wchar_t name[256] = {};
            ULONG actual = 0;
            if (JetRetrieveColumn(session_, list.tableid, list.columnidobjectname,
                name, sizeof(name) - 2, &actual, 0, nullptr) == JET_errSuccess) {
                names.emplace_back(name);
            }
            if (JetMove(session_, list.tableid, JET_MoveNext, 0) != JET_errSuccess)
                break;
        }
        JetCloseTable(session_, list.tableid);
        return true;
    }

    struct Column {
        std::string name;
        JET_COLUMNID id = 0;
        JET_COLTYP type = JET_coltypNil;
        unsigned cp = 0;
    };

    bool columns(JET_TABLEID table, std::vector<Column>& result) {
        JET_COLUMNLIST list{};
        list.cbStruct = sizeof(list);
        if (JetGetTableColumnInfoW(session_, table, nullptr, &list, sizeof(list),
                                   1) != JET_errSuccess) {
            return false;
        }
        for (ULONG i = 0; i < list.cRecord; ++i) {
            Column column;
            char name[256] = {};
            ULONG actual = 0;
            if (JetRetrieveColumn(session_, list.tableid, list.columnidcolumnname,
                name, sizeof(name) - 1, &actual, 0, nullptr) != JET_errSuccess) {
                break;
            }
            column.name = name;
            LONG value = 0;
            actual = 0;
            JetRetrieveColumn(session_, list.tableid, list.columnidcolumnid,
                &value, sizeof(value), &actual, 0, nullptr);
            column.id = static_cast<JET_COLUMNID>(value);
            SHORT type = 0;
            JetRetrieveColumn(session_, list.tableid, list.columnidcoltyp,
                &type, sizeof(type), &actual, 0, nullptr);
            column.type = static_cast<JET_COLTYP>(type);
            SHORT cp = 0;
            JetRetrieveColumn(session_, list.tableid, list.columnidCp,
                &cp, sizeof(cp), &actual, 0, nullptr);
            column.cp = static_cast<unsigned>(static_cast<unsigned short>(cp));
            result.push_back(column);
            if (JetMove(session_, list.tableid, JET_MoveNext, 0) != JET_errSuccess)
                break;
        }
        JetCloseTable(session_, list.tableid);
        return true;
    }

    bool retrieve(JET_TABLEID table, JET_COLUMNID id,
                  std::vector<std::uint8_t>& data) {
        ULONG actual = 0;
        JET_ERR result = JetRetrieveColumn(session_, table, id, nullptr, 0,
                                           &actual, 0, nullptr);
        if (result == JET_errSuccess || actual == 0) {
            data.clear();
            return true;
        }
        if (result != JET_errBufferTooSmall && result != JET_wrnBufferTruncated)
            return false;
        data.resize(actual);
        result = JetRetrieveColumn(session_, table, id, data.data(), actual,
                                   &actual, 0, nullptr);
        if (result != JET_errSuccess) return false;
        data.resize(actual);
        return true;
    }

    bool move_first(JET_TABLEID table) {
        return JetMove(session_, table, JET_MoveFirst, 0) == JET_errSuccess;
    }
    bool move_next(JET_TABLEID table) {
        return JetMove(session_, table, JET_MoveNext, 0) == JET_errSuccess;
    }
    bool open_table(const std::wstring& name, JET_TABLEID& table) {
        return JetOpenTableW(session_, dbid_, name.c_str(), nullptr, 0,
                             JET_bitTableReadOnly, &table) == JET_errSuccess;
    }
    void close_table(JET_TABLEID table) {
        if (table != JET_tableidNil) JetCloseTable(session_, table);
    }
    const std::wstring& source() const { return source_; }

private:
    void cleanup_files() {
        if (!temp_.empty()) {
            remove_tree(temp_);
            temp_.clear();
        }
    }

    std::wstring source_;
    std::wstring temp_;
    JET_INSTANCE instance_ = JET_instanceNil;
    JET_SESID session_ = JET_sesidNil;
    JET_DBID dbid_ = JET_dbidNil;
    bool initialized_ = false;
    bool attached_ = false;
    bool opened_ = false;
};

std::string value_json(const SrumDatabase& db, const SrumDatabase::Column& column,
                       std::vector<std::uint8_t>& data) {
    (void)db;
    if (data.empty()) return "null";
    switch (column.type) {
    case JET_coltypBit:
    case JET_coltypUnsignedByte:
        return std::to_string(data[0]);
    case JET_coltypShort:
        if (data.size() >= 2) return std::to_string(static_cast<std::int16_t>(
            read_u16(data.data())));
        break;
    case JET_coltypUnsignedShort:
        if (data.size() >= 2) return std::to_string(read_u16(data.data()));
        break;
    case JET_coltypLong:
        if (data.size() >= 4) return std::to_string(static_cast<std::int32_t>(
            read_u32(data.data())));
        break;
    case JET_coltypUnsignedLong:
        if (data.size() >= 4) return std::to_string(read_u32(data.data()));
        break;
    case JET_coltypLongLong:
        if (data.size() >= 8) return std::to_string(static_cast<std::int64_t>(
            read_u64(data.data())));
        break;
    case JET_coltypIEEESingle:
        if (data.size() >= 4) {
            float value = 0;
            std::memcpy(&value, data.data(), 4);
            return std::to_string(value);
        }
        break;
    case JET_coltypIEEEDouble:
        if (data.size() >= 8) {
            double value = 0;
            std::memcpy(&value, data.data(), 8);
            return std::to_string(value);
        }
        break;
    case JET_coltypDateTime: {
        if (data.size() >= 8) {
            double value = 0;
            std::memcpy(&value, data.data(), 8);
            const std::uint64_t ft = static_cast<std::uint64_t>(
                (value + 109205.0) * 86400.0 * 10000000.0);
            const std::string iso = art::filetime_iso(ft);
            return art::json_escape(iso);
        }
        break;
    }
    case JET_coltypText:
    case JET_coltypLongText: {
        std::wstring text;
        if (column.cp == 1200) {
            const std::size_t chars = data.size() / 2;
            text.assign(reinterpret_cast<const wchar_t*>(data.data()), chars);
        } else {
            text = widen_ansi(reinterpret_cast<const char*>(data.data()));
        }
        while (!text.empty() && text.back() == L'\0') text.pop_back();
        if (text.size() > kMaxString) text.resize(kMaxString);
        return art::json_escape_w(text);
    }
    case JET_coltypGUID:
        return art::json_escape(hex_prefix(data, 16));
    default:
        return art::json_escape(hex_prefix(data, 512));
    }
    return "null";
}

std::string decode_id_blob(const std::vector<std::uint8_t>& blob) {
    if (blob.empty()) return {};
    PSID sid = const_cast<PSID>(static_cast<const void*>(blob.data()));
    LPWSTR sid_text = nullptr;
    if (IsValidSid(sid) && ConvertSidToStringSidW(sid, &sid_text)) {
        std::string out = art::to_utf8(sid_text);
        LocalFree(sid_text);
        return out;
    }
    if (blob.size() >= 2 && (blob.size() % 2) == 0) {
        std::wstring text(reinterpret_cast<const wchar_t*>(blob.data()),
                          blob.size() / 2);
        while (!text.empty() && text.back() == L'\0') text.pop_back();
        bool printable = !text.empty();
        for (wchar_t c : text) {
            if (c < 0x20 || c == 0x7F) { printable = false; break; }
        }
        if (printable) return art::to_utf8(text);
    }
    return hex_prefix(blob, 128);
}

struct IdMap {
    std::unordered_map<std::uint32_t, std::string> by_index;
};

void read_id_map(SrumDatabase& db, IdMap& map) {
    JET_TABLEID table = JET_tableidNil;
    if (!db.open_table(L"SruDbIdMapTable", table)) return;
    std::vector<SrumDatabase::Column> columns;
    db.columns(table, columns);
    JET_COLUMNID id_index = 0, id_blob = 0, id_type = 0;
    for (const auto& column : columns) {
        if (column.name == "IdIndex") id_index = column.id;
        if (column.name == "IdBlob") id_blob = column.id;
        if (column.name == "IdType") id_type = column.id;
    }
    if (id_index == 0 || id_blob == 0) {
        db.close_table(table);
        return;
    }
    if (db.move_first(table)) {
        do {
            std::vector<std::uint8_t> index_data, blob_data, type_data;
            if (!db.retrieve(table, id_index, index_data) || index_data.size() < 4)
                continue;
            if (!db.retrieve(table, id_blob, blob_data)) continue;
            db.retrieve(table, id_type, type_data);
            const std::uint32_t index = read_u32(index_data.data());
            map.by_index[index] = decode_id_blob(blob_data);
        } while (db.move_next(table));
    }
    db.close_table(table);
}

std::string friendly_table(const std::wstring& name) {
    const std::wstring upper = [&]() {
        std::wstring copy = name;
        for (wchar_t& c : copy) c = static_cast<wchar_t>(std::towupper(c));
        return copy;
    }();
    if (upper.find(L"973F5D5C-1D90-4944-BE8E-24B94231A174") != std::wstring::npos)
        return "NetworkUsages";
    if (upper.find(L"DD6636C4-8929-4683-974E-22C046A43763") != std::wstring::npos)
        return "NetworkConnectivity";
    if (upper.find(L"D10CA2FE-6FCF-4F6D-848E-B2E99266FA89") != std::wstring::npos)
        return "AppResourceUsages";
    if (upper.find(L"5C8CF1C7-7257-4F13-B223-970EF5939312") != std::wstring::npos)
        return "AppTimeline";
    if (upper.find(L"FEE4E14F-02A9-4550-B5CE-5FA2DA202E37") != std::wstring::npos)
        return "AppResourceUsage";
    return art::to_utf8(name);
}

} // namespace

int collect_srum(const std::wstring& database, const ArtifactSink& sink,
                 std::wstring& error) {
    SrumDatabase db;
    if (!db.open(database, error)) return -1;
    IdMap ids;
    read_id_map(db, ids);

    std::vector<std::wstring> tables;
    if (!db.tables(tables, error)) {
        db.close();
        return -1;
    }
    int rows = 0;
    for (const auto& table_name : tables) {
        if (rows >= static_cast<int>(kMaxRows)) break;
        std::wstring lower = table_name;
        for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
        if (lower.rfind(L"msys", 0) == 0 || lower.rfind(L"srudb", 0) == 0)
            continue;
        JET_TABLEID table = JET_tableidNil;
        if (!db.open_table(table_name, table)) continue;
        std::vector<SrumDatabase::Column> columns;
        if (!db.columns(table, columns)) {
            db.close_table(table);
            continue;
        }
        JET_COLUMNID auto_inc = 0, timestamp = 0, app_id = 0, user_id = 0;
        const SrumDatabase::Column* timestamp_column = nullptr;
        for (const auto& column : columns) {
            if (column.name == "AutoIncId") auto_inc = column.id;
            if (column.name == "TimeStamp") {
                timestamp = column.id;
                timestamp_column = &column;
            }
            if (column.name == "AppId") app_id = column.id;
            if (column.name == "UserId") user_id = column.id;
        }
        const std::string table_label = friendly_table(table_name);
        int table_rows = 0;
        if (db.move_first(table)) {
            do {
                if (rows >= static_cast<int>(kMaxRows)) break;
                std::vector<std::uint8_t> raw;
                std::string key = table_label;
                if (auto_inc != 0 && db.retrieve(table, auto_inc, raw) &&
                    raw.size() >= 4) {
                    key += ":" + std::to_string(read_u32(raw.data()));
                }
                std::string timestamp_utc;
                if (timestamp != 0 && timestamp_column != nullptr &&
                    db.retrieve(table, timestamp, raw)) {
                    timestamp_utc = value_json(db, *timestamp_column, raw);
                }
                if (timestamp_utc.size() >= 2 && timestamp_utc.front() == '"')
                    timestamp_utc = timestamp_utc.substr(1, timestamp_utc.size() - 2);
                std::string app, user;
                if (app_id != 0 && db.retrieve(table, app_id, raw) && raw.size() >= 4) {
                    const auto it = ids.by_index.find(read_u32(raw.data()));
                    if (it != ids.by_index.end()) app = it->second;
                }
                if (user_id != 0 && db.retrieve(table, user_id, raw) && raw.size() >= 4) {
                    const auto it = ids.by_index.find(read_u32(raw.data()));
                    if (it != ids.by_index.end()) user = it->second;
                }
                std::string data = "{\"table\":" + art::json_escape(table_label) +
                    ",\"table_id\":" + art::json_escape_w(table_name) +
                    ",\"app\":" + art::json_escape(app) +
                    ",\"user\":" + art::json_escape(user) +
                    ",\"columns\":{";
                bool first = true;
                for (const auto& column : columns) {
                    std::vector<std::uint8_t> value;
                    if (!db.retrieve(table, column.id, value)) continue;
                    if (!first) data += ",";
                    first = false;
                    data += art::json_escape(column.name) + ":" +
                        value_json(db, column, value);
                    if (data.size() > 64 * 1024) break;
                }
                data += "}}";
                ArtifactRow row;
                row.type = "srum";
                row.source = art::to_utf8(db.source());
                row.key = key;
                row.timestamp_utc = timestamp_utc;
                row.path = app.empty() ? table_label : app;
                row.name = app.empty() ? table_label : app;
                row.data = std::move(data);
                sink(row);
                ++rows;
                ++table_rows;
            } while (db.move_next(table));
        }
        db.close_table(table);
    }
    db.close();
    if (rows == 0) {
        error = L"no SRUM rows parsed";
        return -1;
    }
    return rows;
}

} // namespace deep
