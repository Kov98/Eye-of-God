// language: C++, file: wa_carve.hpp, target: Windows 11 (MSVC / MinGW g++)
// Deleted-record recovery from a decrypted SQLite image.
//
// SQLite does not zero a deleted row: with the default secure_delete=OFF the
// cell bytes stay in place until something else reuses the space. Three places
// hold them, and this module reads all three:
//
//   1. freelist pages - whole pages SQLite retired but did not wipe
//   2. freeblocks inside live pages - fragmented space between cells
//   3. orphan WAL frames - pages written but never checkpointed
//
// A recovered row is real evidence only if it is not also present in the live
// tree, so every candidate is checked against the rowids SQLite still serves.
// Anything unverified is reported as a candidate, never as a fact.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::wa {

struct carved_row {
    std::uint32_t page = 0;
    std::int64_t rowid = 0;
    bool rowid_plausible = false;
    std::string origin;                  // freelist / freeblock / orphan
    std::string table_guess;             // matched by shape, may be empty
    std::vector<std::string> texts;      // decoded TEXT columns
    std::vector<std::int64_t> ints;      // decoded integer columns
    bool confirmed_deleted = false;      // rowid absent from the live tree
};

struct live_table {
    std::string name;
    std::vector<std::uint32_t> root_pages;
    std::uint64_t live_rows = 0;
    std::vector<std::string> columns;
};

struct carve_result {
    bool ok = false;
    std::vector<live_table> tables;
    std::vector<carved_row> rows;
    std::uint64_t pages_scanned = 0;
    std::uint64_t freelist_pages = 0;
    std::uint64_t candidates = 0;
    std::uint64_t confirmed = 0;
    std::string detail;
};

carve_result carve_sqlite(const std::wstring& plain_db);

}  // namespace olho::wa
