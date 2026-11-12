// language: C++, file: sqlite_enc_vfs.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "sqlite_enc_vfs.hpp"

#include "case_crypto.hpp"

#include "sqlite3.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace deep {
namespace {

const case_cipher* g_cipher = nullptr;
sqlite3_vfs* g_real = nullptr;
sqlite3_vfs g_vfs;
bool g_registered = false;
bool g_open_called = false;

struct enc_file {
    sqlite3_file base;          // must be first
    sqlite3_file* real = nullptr;
};

int enc_close(sqlite3_file* file) {
    auto* f = reinterpret_cast<enc_file*>(file);
    int rc = SQLITE_OK;
    if (f->real) rc = f->real->pMethods->xClose(f->real);
    return rc;
}

int enc_read(sqlite3_file* file, void* buffer, int amount, sqlite3_int64 offset) {
    auto* f = reinterpret_cast<enc_file*>(file);
    const int rc = f->real->pMethods->xRead(f->real, buffer, amount, offset);
    if (rc == SQLITE_OK && g_cipher) {
        g_cipher->apply(static_cast<std::uint64_t>(offset),
                        static_cast<std::uint8_t*>(buffer), static_cast<std::size_t>(amount));
    }
    return rc;
}

int enc_write(sqlite3_file* file, const void* buffer, int amount, sqlite3_int64 offset) {
    auto* f = reinterpret_cast<enc_file*>(file);
    std::vector<std::uint8_t> copy(static_cast<const std::uint8_t*>(buffer),
                                   static_cast<const std::uint8_t*>(buffer) + amount);
    if (g_cipher) {
        g_cipher->apply(static_cast<std::uint64_t>(offset), copy.data(), copy.size());
    }
    return f->real->pMethods->xWrite(f->real, copy.data(), amount, offset);
}

int enc_truncate(sqlite3_file* file, sqlite3_int64 size) {
    auto* f = reinterpret_cast<enc_file*>(file);
    return f->real->pMethods->xTruncate(f->real, size);
}
int enc_sync(sqlite3_file* file, int flags) {
    auto* f = reinterpret_cast<enc_file*>(file);
    return f->real->pMethods->xSync(f->real, flags);
}
int enc_file_size(sqlite3_file* file, sqlite3_int64* size) {
    auto* f = reinterpret_cast<enc_file*>(file);
    return f->real->pMethods->xFileSize(f->real, size);
}
int enc_lock(sqlite3_file* file, int level) {
    auto* f = reinterpret_cast<enc_file*>(file);
    return f->real->pMethods->xLock(f->real, level);
}
int enc_unlock(sqlite3_file* file, int level) {
    auto* f = reinterpret_cast<enc_file*>(file);
    return f->real->pMethods->xUnlock(f->real, level);
}
int enc_check_reserved_lock(sqlite3_file* file, int* out) {
    auto* f = reinterpret_cast<enc_file*>(file);
    return f->real->pMethods->xCheckReservedLock(f->real, out);
}
int enc_file_control(sqlite3_file* file, int op, void* arg) {
    auto* f = reinterpret_cast<enc_file*>(file);
    return f->real->pMethods->xFileControl(f->real, op, arg);
}
int enc_sector_size(sqlite3_file* file) {
    auto* f = reinterpret_cast<enc_file*>(file);
    return f->real->pMethods->xSectorSize(f->real);
}
int enc_device_characteristics(sqlite3_file* file) {
    auto* f = reinterpret_cast<enc_file*>(file);
    return f->real->pMethods->xDeviceCharacteristics(f->real);
}

const sqlite3_io_methods kMethods = {
    1,
    enc_close,
    enc_read,
    enc_write,
    enc_truncate,
    enc_sync,
    enc_file_size,
    enc_lock,
    enc_unlock,
    enc_check_reserved_lock,
    enc_file_control,
    enc_sector_size,
    enc_device_characteristics,
    nullptr,   // xFetchSharedMemory
    nullptr,   // xUnfetchSharedMemory
};

int enc_open(sqlite3_vfs* vfs, const char* name, sqlite3_file* file, int flags, int* out_flags) {
    g_open_called = true;
    if (!g_cipher || !g_real) return SQLITE_CANTOPEN;

    auto* f = reinterpret_cast<enc_file*>(file);
    // Room for the real VFS's file object lives right after ours.
    f->real = reinterpret_cast<sqlite3_file*>(reinterpret_cast<char*>(file) + sizeof(enc_file));
    sqlite3_file* real = f->real;
    const int rc = g_real->xOpen(g_real, name, real, flags, out_flags);
    if (rc != SQLITE_OK) return rc;
    if (!real->pMethods) return SQLITE_IOERR;
    file->pMethods = &kMethods;
    (void)vfs;
    return SQLITE_OK;
}

int enc_delete(sqlite3_vfs* vfs, const char* name, int sync_dir) {
    return g_real->xDelete(g_real, name, sync_dir);
}
int enc_access(sqlite3_vfs* vfs, const char* name, int flags, int* out) {
    return g_real->xAccess(g_real, name, flags, out);
}
int enc_full_pathname(sqlite3_vfs* vfs, const char* name, int n, char* out) {
    return g_real->xFullPathname(g_real, name, n, out);
}
void* enc_dl_open(sqlite3_vfs* vfs, const char* name) { return g_real->xDlOpen(g_real, name); }
void enc_dl_error(sqlite3_vfs* vfs, int n, char* out) { g_real->xDlError(g_real, n, out); }
void (*enc_dl_sym(sqlite3_vfs* vfs, void* handle, const char* name))(void) {
    return g_real->xDlSym(g_real, handle, name);
}
void enc_dl_close(sqlite3_vfs* vfs, void* handle) { g_real->xDlClose(g_real, handle); }
int enc_randomness(sqlite3_vfs* vfs, int n, char* out) { return g_real->xRandomness(g_real, n, out); }
int enc_sleep(sqlite3_vfs* vfs, int micro) { return g_real->xSleep(g_real, micro); }
int enc_current_time(sqlite3_vfs* vfs, double* out) { return g_real->xCurrentTime(g_real, out); }
int enc_last_error(sqlite3_vfs* vfs, int n, char* out) { return g_real->xGetLastError(g_real, n, out); }

}  // namespace

void install_encryption(const case_cipher* cipher) { g_cipher = cipher; }

const char* encryption_vfs_name() {
    if (g_registered) return g_vfs.zName;
    g_real = sqlite3_vfs_find(nullptr);
    if (!g_real) return "";

    g_vfs = *g_real;
    g_vfs.zName = "olho-enc";
    g_vfs.szOsFile = static_cast<int>(sizeof(enc_file)) + g_real->szOsFile;
    g_vfs.xOpen = enc_open;
    g_vfs.xDelete = enc_delete;
    g_vfs.xAccess = enc_access;
    g_vfs.xFullPathname = enc_full_pathname;
    g_vfs.xDlOpen = enc_dl_open;
    g_vfs.xDlError = enc_dl_error;
    g_vfs.xDlSym = enc_dl_sym;
    g_vfs.xDlClose = enc_dl_close;
    g_vfs.xRandomness = enc_randomness;
    g_vfs.xSleep = enc_sleep;
    g_vfs.xCurrentTime = enc_current_time;
    g_vfs.xGetLastError = enc_last_error;

    if (sqlite3_vfs_register(&g_vfs, 0) != SQLITE_OK) return "";
    g_registered = true;
    return g_vfs.zName;
}

bool encryption_vfs_ready() { return g_registered && g_cipher != nullptr; }

}  // namespace deep
