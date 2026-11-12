// language: C++, file: sqlite_enc_vfs.hpp, target: Windows 11 (MSVC / MinGW g++)
// A SQLite VFS that encrypts every file it opens.
//
// WHY A VFS AND NOT "ENCRYPT THE FILE AND DECRYPT TO TEMP"
//   SQLite spills pages into a rollback journal and into the WAL. Encrypting only
//   the main database would leave every recent write in plaintext next to it, so
//   the protection would be cosmetic. Intercepting at the VFS means the database,
//   the journal and the WAL all go through the same cipher without SQLite knowing
//   anyone is there.
//
// WHY CTR MAKES THIS SMALL
//   The VFS is handed arbitrary offsets. A chained mode would need to know block
//   boundaries and page layout; a seekable stream needs neither, so there is no
//   page-size assumption anywhere in this file.
//
// LIMITS, STATED
//   Confidentiality only: a modified byte reads back modified and is not detected
//   here. The report hashes and SQLite's own page checks are what catch damage.
//   One cipher per process (set before opening); concurrent cases with different
//   keys are not supported by this registration.
#pragma once

#include <string>

struct sqlite3_vfs;

namespace deep {

// The cipher is installed once, before any database is opened through this VFS.
// Pass nullptr to detach (opening through the VFS then fails, it does not
// silently write plaintext).
void install_encryption(const class case_cipher* cipher);

// Registers the VFS and returns its name, or empty on failure.
const char* encryption_vfs_name();

// True when a cipher is installed and the VFS is registered.
bool encryption_vfs_ready();

}  // namespace deep
