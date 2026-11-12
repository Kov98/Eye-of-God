// language: C++, file: mem_acq.hpp, target: Windows 11 (MSVC / MinGW g++)
// Process memory acquisition, for media the app never wrote to disk.
//
// WHAT THIS IS FOR
//   "View once" media and ephemeral keys live in a running process heap for as
//   long as the screen shows them. Nothing on disk, nothing in the cache. The
//   only place they exist is memory, so that is where an acquisition has to look.
//
// HOW IT WORKS
//   Walk the target committed, readable regions with VirtualQueryEx and read them
//   with ReadProcessMemory, then carve containers by signature. No injection, no
//   hooks: it reads memory the way Task Manager does, which means it needs a
//   handle with PROCESS_VM_READ and whatever privilege the target DACL demands.
//
// LIMITS, BEFORE ANYONE EXPECTS MORE
//   This does not defeat PPL, kernel-protected processes, or a target that has
//   already freed the buffer. It reads what is readable at the moment of capture.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::mem {

using bytes = std::vector<std::uint8_t>;

struct process_entry {
    std::uint32_t pid = 0;
    std::string name;
    std::uint64_t working_set = 0;
};

// Every process the caller can see, by name. No handle is opened.
std::vector<process_entry> list_processes();

struct region_stats {
    std::uint64_t regions_seen = 0;
    std::uint64_t regions_read = 0;
    std::uint64_t bytes_read = 0;
    std::uint64_t regions_denied = 0;
};

struct carve_result {
    bool ok = false;
    std::string kind;          // jpeg / png / mp4
    bytes data;
    std::uint64_t address = 0; // where it was found, for the report
    std::string error;
};

// Reads every readable region of the process and carves media containers out of
// it. max_bytes caps the total read; 0 means no cap.
bool acquire(std::uint32_t pid, std::uint64_t max_bytes, region_stats& stats,
             std::vector<carve_result>& found, std::string& error);

// Finds a byte pattern inside the process and returns the surrounding window.
// Used for key material: the caller supplies what to look for.
bool find_bytes(std::uint32_t pid, const bytes& needle, std::size_t window,
                std::uint64_t& address, bytes& hit, std::string& error);

}  // namespace olho::mem
