// language: C++, file: wa_carve.cpp, target: Windows 11 (MSVC / MinGW g++)
// Deleted-record recovery from a decrypted SQLite image. See wa_carve.hpp.
#include "wa_carve.hpp"

#include <windows.h>
#include <sqlite3.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace olho::wa {
namespace {

constexpr std::uint32_t kMaxPages = 40u * 1024u * 1024u;   // guard against a corrupt header

std::vector<std::uint8_t> read_all(const std::wstring& path) {
    std::vector<std::uint8_t> out;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return out;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (size.QuadPart <= 0) { CloseHandle(h); return out; }
    out.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD got = 0;
    const BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr);
    CloseHandle(h);
    if (!ok || got != out.size()) out.clear();
    return out;
}

// SQLite's variable-length integer: 7 bits per byte, up to 9 bytes.
bool read_varint(const std::uint8_t* p, std::size_t avail, std::uint64_t& value, std::size_t& used) {
    value = 0;
    used = 0;
    for (int i = 0; i < 9 && i < static_cast<int>(avail); ++i) {
        const std::uint8_t b = p[i];
        if (i == 8) { value = (value << 8) | b; used = 9; return true; }
        value = (value << 7) | (b & 0x7f);
        if ((b & 0x80) == 0) { used = static_cast<std::size_t>(i) + 1; return true; }
    }
    return false;
}

std::uint32_t be16(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 8) | p[1];
}
std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

struct column_value {
    enum class kind { null, integer, real, text, blob } k = kind::null;
    std::int64_t i = 0;
    std::string s;
};

std::int64_t sign_extend(std::uint64_t v, int bytes) {
    const int bits = bytes * 8;
    if (bits >= 64) return static_cast<std::int64_t>(v);
    const std::uint64_t sign = 1ull << (bits - 1);
    if (v & sign) v |= ~((1ull << bits) - 1);
    return static_cast<std::int64_t>(v);
}

// A record is: header-size varint, serial types, then the body values.
bool decode_record(const std::uint8_t* p, std::size_t len, std::vector<column_value>& out,
                   std::size_t* consumed = nullptr) {
    const std::size_t start_len = len;
    std::uint64_t header_len = 0;
    std::size_t hdr_used = 0;
    if (!read_varint(p, len, header_len, hdr_used)) return false;
    if (header_len > len || header_len < hdr_used) return false;

    std::vector<std::uint64_t> serials;
    std::size_t pos = hdr_used;
    while (pos < header_len) {
        std::uint64_t serial = 0;
        std::size_t used = 0;
        if (!read_varint(p + pos, len - pos, serial, used)) return false;
        serials.push_back(serial);
        pos += used;
    }

    std::size_t body = static_cast<std::size_t>(header_len);
    for (std::uint64_t serial : serials) {
        column_value v;
        std::size_t width = 0;
        if (serial == 0) {
            v.k = column_value::kind::null;
        } else if (serial >= 1 && serial <= 6) {
            static const int widths[] = {1, 2, 3, 4, 6, 8};
            width = static_cast<std::size_t>(widths[serial - 1]);
            if (body + width > len) return false;
            std::uint64_t raw = 0;
            for (std::size_t i = 0; i < width; ++i) raw = (raw << 8) | p[body + i];
            v.k = column_value::kind::integer;
            v.i = sign_extend(raw, static_cast<int>(width));
        } else if (serial == 7) {
            width = 8;
            if (body + width > len) return false;
            double d = 0;
            std::uint64_t raw = 0;
            for (std::size_t i = 0; i < 8; ++i) raw = (raw << 8) | p[body + i];
            std::memcpy(&d, &raw, sizeof(d));
            v.k = column_value::kind::real;
        } else if (serial == 8) {
            v.k = column_value::kind::integer; v.i = 0;
        } else if (serial == 9) {
            v.k = column_value::kind::integer; v.i = 1;
        } else if (serial >= 12) {
            width = static_cast<std::size_t>((serial - 12) / 2);
            if (body + width > len) return false;
            if (serial & 1) {
                v.k = column_value::kind::text;
                v.s.assign(reinterpret_cast<const char*>(p + body), width);
            } else {
                v.k = column_value::kind::blob;
                v.s.assign(reinterpret_cast<const char*>(p + body), width);
            }
        } else {
            return false;   // 10 and 11 are reserved
        }
        body += width;
        out.push_back(std::move(v));
    }
    if (consumed) *consumed = body;
    (void)start_len;
    return true;
}

struct image {
    std::vector<std::uint8_t> bytes;
    std::uint32_t page_size = 0;
    std::uint32_t reserved = 0;
    std::uint32_t pages = 0;
    std::uint32_t freelist_head = 0;
    std::uint32_t freelist_count = 0;

    std::uint32_t usable() const { return page_size - reserved; }
    const std::uint8_t* page(std::uint32_t pgno) const {
        if (pgno == 0 || pgno > pages) return nullptr;
        return bytes.data() + (static_cast<std::size_t>(pgno) - 1) * page_size;
    }
};

bool load_image(const std::vector<std::uint8_t>& bytes, image& img) {
    if (bytes.size() < 512) return false;
    if (std::memcmp(bytes.data(), "SQLite format 3\0", 16) != 0) return false;
    img.bytes = bytes;
    img.page_size = be16(bytes.data() + 16);
    if (img.page_size == 1) img.page_size = 65536;
    img.reserved = bytes[20];
    if (img.page_size <= img.reserved || img.page_size > 65536) return false;
    img.pages = img.page_size ? static_cast<std::uint32_t>(bytes.size() / img.page_size) : 0;
    img.freelist_head = be32(bytes.data() + 32);
    img.freelist_count = be32(bytes.data() + 36);
    if (img.pages == 0 || img.pages > kMaxPages) return false;
    return true;
}

struct btree_stats {
    std::set<std::uint32_t> owned_pages;
    std::set<std::int64_t> live_rowids;
    std::uint64_t rows = 0;
};

// Walks a table b-tree, recording which pages belong to it and which rowids it
// still serves. Page ownership is what makes a "deleted" verdict defensible.
void walk_tree(const image& img, std::uint32_t root, btree_stats& stats, int depth = 0) {
    if (depth > 64 || root == 0) return;
    const std::uint8_t* p = img.page(root);
    if (!p) return;
    if (!stats.owned_pages.insert(root).second) return;

    const std::uint32_t hdr = (root == 1) ? 100 : 0;
    const std::uint8_t type = p[hdr];
    const std::uint32_t cell_count = be16(p + hdr + 3);

    if (type == 0x05) {   // interior table
        const std::uint32_t ptr_base = hdr + 12;
        for (std::uint32_t i = 0; i < cell_count; ++i) {
            const std::uint32_t off = be16(p + ptr_base + i * 2);
            if (off + 4 > img.page_size) continue;
            walk_tree(img, be32(p + off), stats, depth + 1);
        }
        walk_tree(img, be32(p + hdr + 8), stats, depth + 1);   // right-most child
    } else if (type == 0x0D) {   // leaf table
        const std::uint32_t ptr_base = hdr + 8;
        for (std::uint32_t i = 0; i < cell_count; ++i) {
            const std::uint32_t off = be16(p + ptr_base + i * 2);
            if (off >= img.page_size) continue;
            std::uint64_t payload = 0, rowid = 0;
            std::size_t used = 0;
            if (!read_varint(p + off, img.page_size - off, payload, used)) continue;
            const std::size_t rowid_at = off + used;
            if (!read_varint(p + rowid_at, img.page_size - rowid_at, rowid, used)) continue;
            stats.live_rowids.insert(static_cast<std::int64_t>(rowid));
            ++stats.rows;
        }
    }
}

// Tries to read a table-leaf cell at an arbitrary offset. Used on retired space,
// where nothing guarantees the bytes still form a cell.
bool try_cell(const image& img, std::uint32_t pgno, std::uint32_t offset,
              std::int64_t& rowid, std::vector<column_value>& values) {
    const std::uint8_t* p = img.page(pgno);
    if (!p || offset >= img.usable()) return false;
    std::uint64_t payload = 0, rid = 0;
    std::size_t used = 0;
    if (!read_varint(p + offset, img.usable() - offset, payload, used)) return false;
    const std::size_t rowid_at = offset + used;
    if (!read_varint(p + rowid_at, img.usable() - rowid_at, rid, used)) return false;
    if (rid == 0 || rid > (1ll << 40)) return false;
    const std::size_t body = rowid_at + used;
    if (payload == 0 || body >= img.usable()) return false;
    const std::size_t available = std::min<std::size_t>(payload, img.usable() - body);
    if (available < 2) return false;
    std::size_t consumed = 0;
    values.clear();
    if (!decode_record(p + body, available, values, &consumed)) return false;

    // Structural proof that the offset really is a cell boundary: the decoded
    // columns must account for the whole payload, not merely its first bytes.
    if (payload <= available && consumed != payload) return false;
    if (payload > available && available < 16) return false;

    rowid = static_cast<std::int64_t>(rid);
    return true;
}

bool looks_like_text(const std::string& s) {
    if (s.empty() || s.size() > 4096) return false;
    std::size_t good = 0, control = 0, continuation = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20) {
            if (c == '\n' || c == '\r' || c == '\t') ++good;
            else ++control;
        } else if (c < 0x80) {
            ++good;
        } else if ((c & 0xC0) == 0x80) {
            // A continuation byte only counts if a lead byte precedes it.
            if (continuation > 0) { ++good; --continuation; }
            else ++control;
        } else if ((c & 0xE0) == 0xC0) { continuation = 1; ++good; }
        else if ((c & 0xF0) == 0xE0) { continuation = 2; ++good; }
        else if ((c & 0xF8) == 0xF0) { continuation = 3; ++good; }
        else ++control;
    }
    // A page-span misread scores high on raw printable bytes; real text does not
    // carry a stream of stray control bytes.
    if (control > s.size() / 100) return false;
    if (continuation != 0) return false;
    return good * 100 >= s.size() * 92;
}

void collect_texts(const std::vector<column_value>& values, carved_row& row) {
    for (const auto& v : values) {
        if (v.k == column_value::kind::text && looks_like_text(v.s)) row.texts.push_back(v.s);
    }
}

}  // namespace

carve_result carve_sqlite(const std::wstring& plain_db) {
    carve_result result;

    auto bytes = read_all(plain_db);
    image img;
    if (!load_image(bytes, img)) { result.detail = "imagem nao e um SQLite valido"; return result; }

    // Live schema and rowids. sqlite3 answers this authoritatively; the page walk
    // below only ever adds knowledge about space SQLite has already abandoned.
    {
        const std::string path(plain_db.begin(), plain_db.end());
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT name, rootpage FROM sqlite_master WHERE type='table'",
                                   -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const unsigned char* name = sqlite3_column_text(stmt, 0);
                    if (!name) continue;
                    live_table t;
                    t.name = reinterpret_cast<const char*>(name);
                    t.root_pages.push_back(static_cast<std::uint32_t>(sqlite3_column_int(stmt, 1)));

                    std::string cols;
                    sqlite3_stmt* pc = nullptr;
                    const std::string pragma = "PRAGMA table_info(\"" + t.name + "\")";
                    if (sqlite3_prepare_v2(db, pragma.c_str(), -1, &pc, nullptr) == SQLITE_OK) {
                        while (sqlite3_step(pc) == SQLITE_ROW) {
                            const unsigned char* cn = sqlite3_column_text(pc, 1);
                            if (cn) t.columns.emplace_back(reinterpret_cast<const char*>(cn));
                        }
                        sqlite3_finalize(pc);
                    }
                    result.tables.push_back(std::move(t));
                }
                sqlite3_finalize(stmt);
            }
            sqlite3_close(db);
        }
    }

    // Which pages the live trees own, and which rowids they still serve.
    std::map<std::uint32_t, std::string> page_owner;
    std::set<std::int64_t> live_rowids;
    for (auto& t : result.tables) {
        btree_stats stats;
        for (std::uint32_t root : t.root_pages) walk_tree(img, root, stats);
        t.live_rows = stats.rows;
        for (std::uint32_t pg : stats.owned_pages) page_owner[pg] = t.name;
        live_rowids.insert(stats.live_rowids.begin(), stats.live_rowids.end());
    }

    // Freelist: trunk pages chain to more trunks, and list retired pages.
    std::set<std::uint32_t> freelist;
    {
        std::uint32_t trunk = img.freelist_head;
        int guard = 0;
        while (trunk != 0 && trunk <= img.pages && guard++ < 100000) {
            const std::uint8_t* p = img.page(trunk);
            if (!p) break;
            freelist.insert(trunk);
            const std::uint32_t next = be32(p);
            const std::uint32_t count = be32(p + 4);
            for (std::uint32_t i = 0; i < count && (8 + (i + 1) * 4) <= img.usable(); ++i) {
                const std::uint32_t leaf = be32(p + 8 + i * 4);
                if (leaf > 0 && leaf <= img.pages) freelist.insert(leaf);
            }
            trunk = next;
        }
    }
    result.freelist_pages = freelist.size();

    // Carve. Retired pages are read whole; live pages are read at their
    // freeblocks, which is where a deleted cell waits before reuse.
    for (std::uint32_t pg = 1; pg <= img.pages; ++pg) {
        ++result.pages_scanned;
        const std::uint8_t* p = img.page(pg);
        if (!p) continue;

        const auto owner = page_owner.find(pg);
        const bool retired = freelist.count(pg) > 0 || owner == page_owner.end();

        std::vector<std::uint32_t> offsets;
        if (retired) {
            // Whole page is suspect. Walk it as a leaf cell array when it still
            // parses as one, otherwise sweep offsets that begin a plausible cell.
            const std::uint32_t hdr = (pg == 1) ? 100 : 0;
            const std::uint8_t type = p[hdr];
            if (type == 0x0D) {
                const std::uint32_t cells = std::min<std::uint32_t>(be16(p + hdr + 3), 2000);
                for (std::uint32_t i = 0; i < cells; ++i) {
                    const std::uint32_t off = be16(p + hdr + 8 + i * 2);
                    if (off > 0 && off < img.usable()) offsets.push_back(off);
                }
            } else {
                for (std::uint32_t off = hdr; off + 8 < img.usable(); off += 1) offsets.push_back(off);
            }
        } else {
            const std::uint32_t hdr = (pg == 1) ? 100 : 0;
            if (p[hdr] != 0x0D) continue;
            std::uint32_t fb = be16(p + hdr + 1);
            int guard = 0;
            while (fb != 0 && fb < img.usable() && guard++ < 1000) {
                offsets.push_back(fb + 4);   // skip the freeblock header
                const std::uint32_t next = be16(p + fb);
                if (next == 0 || next <= fb) break;
                fb = next;
            }
        }

        for (std::uint32_t off : offsets) {
            std::int64_t rowid = 0;
            std::vector<column_value> values;
            if (!try_cell(img, pg, off, rowid, values)) continue;
            if (values.size() < 2) continue;

            carved_row row;
            row.page = pg;
            row.rowid = rowid;
            row.rowid_plausible = true;
            collect_texts(values, row);
            if (row.texts.empty()) continue;

            row.origin = retired ? (freelist.count(pg) ? "freelist" : "orphan") : "freeblock";
            if (owner != page_owner.end()) row.table_guess = owner->second;

            // Match by shape to give the row a table when the page is orphaned.
            if (row.table_guess.empty()) {
                for (const auto& t : result.tables) {
                    if (!t.columns.empty() && t.columns.size() == values.size()) {
                        row.table_guess = t.name;
                        break;
                    }
                }
            }

            // Shape must match a real table. A record decoded from a page span
            // collapses into one enormous column; no table in the schema has
            // that width, so it is rejected rather than reported as a message.
            bool shape_known = false;
            for (const auto& t : result.tables) {
                if (!t.columns.empty() && t.columns.size() == values.size()) { shape_known = true; break; }
            }
            if (!shape_known) continue;

            row.confirmed_deleted = live_rowids.find(rowid) == live_rowids.end();

            // One row per (page, rowid, payload). A retired page can be reachable
            // from more than one offset after fragmentation; the same record must
            // not enter the evidence set twice.
            bool duplicate = false;
            for (const auto& existing : result.rows) {
                if (existing.page == row.page && existing.rowid == row.rowid &&
                    existing.texts.size() == row.texts.size() &&
                    (row.texts.empty() || existing.texts.front() == row.texts.front())) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            if (row.confirmed_deleted) ++result.confirmed;
            ++result.candidates;
            result.rows.push_back(std::move(row));
        }
    }

    result.ok = true;
    result.detail = std::to_string(result.confirmed) + " linhas confirmadas apagadas de " +
                    std::to_string(result.candidates) + " candidatas; " +
                    std::to_string(result.tables.size()) + " tabelas";
    return result;
}

}  // namespace olho::wa
