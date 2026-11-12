// language: C++, file: custody_chain.hpp, target: Windows 11 (MSVC / MinGW g++)
// Hash-chained custody manifest.
//
// WHY A CHAIN AND NOT A LIST
//   A per-item SHA-256 proves each file did not change. It does NOT prevent the
//   list itself from losing a line: delete an entry, recompute nothing, and the
//   report still looks coherent because every remaining hash is valid. Chaining
//   makes position part of the evidence — entry N commits to entry N-1, so a
//   removed or reordered entry breaks every link after it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::custody {

using bytes = std::vector<std::uint8_t>;

struct entry {
    std::uint64_t index = 0;
    std::string relative_path;
    std::uint64_t size = 0;
    std::string sha256;      // digest of the file itself
    std::string kind;        // detected container family
    std::string prev_link;   // digest of the previous entry as serialised
    std::string link;        // digest of (index|path|size|sha256|kind|prev_link)
};

struct chain {
    bool ok = false;
    std::string genesis;     // digest that starts the chain: the case identifier
    std::vector<entry> entries;
    std::string head;        // link of the last entry: the seal
    std::string error;
};

// Serialisation of one entry, exactly as it is hashed. Kept in one place so the
// writer and the verifier cannot drift apart.
std::string entry_payload(const entry& e);

// One row of input, for callers that already walked the case and would otherwise
// pay for a second full read of every file.
struct input_entry {
    std::string relative_path;
    std::uint64_t size = 0;
    std::string sha256;
    std::string kind;
};

// Builds the chain from rows that are already hashed.
bool build_from(const std::string& genesis, const std::vector<input_entry>& inputs,
                chain& out, std::string& error);

// Builds the chain. `genesis` should be the case id plus the acquisition time,
// so two cases never share a starting link.
bool build(const std::string& genesis, const std::string& root, chain& out, std::string& error);

// Recomputes every link from the manifest and reports the first entry that does
// not match. An empty `bad_path` with ok == true means the chain is intact.
bool verify(const chain& c, std::size_t& bad_index, std::string& bad_path, std::string& error);

// JSON form, one entry per line, so a report can embed it and a script can diff it.
std::string to_json(const chain& c);

// Reads back what to_json wrote.
bool from_json(const std::string& text, chain& out, std::string& error);

}  // namespace olho::custody