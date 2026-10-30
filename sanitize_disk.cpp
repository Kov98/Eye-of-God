// language: C++, file: sanitize_disk.cpp, target: Windows 10/11 / WinPE
// Overwrite-based physical disk sanitizer. Refuses the system disk and any
// disk with a mounted volume. No claim of SSD firmware/overprovisioning
// erasure; use vendor ATA/NVMe sanitize or physical destruction for that.

#include "sanitize_disk.hpp"

#include "artifacts.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <ntddstor.h>
#include <ntddscsi.h>
#include <nvme.h>
#include <winioctl.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <string>
#include <vector>

namespace deep {
namespace {

struct DiskInfo {
    int number = -1;
    std::uint64_t size = 0;
    std::string model;
    std::string serial;
    bool system = false;
    bool mounted = false;
    bool present = false;
    bool ssd = false;
    DWORD bus_type = 0;
};

std::string trim_ascii(const char* text) {
    if (text == nullptr) return {};
    std::string value(text);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\0' ||
                              value.back() == '\r' || value.back() == '\n'))
        value.pop_back();
    std::size_t begin = 0;
    while (begin < value.size() && value[begin] == ' ') ++begin;
    return value.substr(begin);
}

bool query_disk(int number, DiskInfo& info) {
    const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(number);
    HANDLE disk = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (disk == INVALID_HANDLE_VALUE) return false;
    info.number = number;
    info.present = true;
    GET_LENGTH_INFORMATION length{};
    DWORD returned = 0;
    if (DeviceIoControl(disk, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &length,
                        sizeof(length), &returned, nullptr))
        info.size = static_cast<std::uint64_t>(length.Length.QuadPart);
    std::vector<std::uint8_t> buffer(4096);
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    if (DeviceIoControl(disk, IOCTL_STORAGE_QUERY_PROPERTY, &query,
                        sizeof(query), buffer.data(),
                        static_cast<DWORD>(buffer.size()), &returned, nullptr)) {
        auto* descriptor = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
        info.bus_type = static_cast<DWORD>(descriptor->BusType);
        if (descriptor->ProductIdOffset != 0 &&
            descriptor->ProductIdOffset < returned)
            info.model = trim_ascii(reinterpret_cast<const char*>(
                buffer.data() + descriptor->ProductIdOffset));
        if (descriptor->SerialNumberOffset != 0 &&
            descriptor->SerialNumberOffset < returned)
            info.serial = trim_ascii(reinterpret_cast<const char*>(
                buffer.data() + descriptor->SerialNumberOffset));
    }
    STORAGE_PROPERTY_QUERY seek_query{};
    seek_query.PropertyId = StorageDeviceSeekPenaltyProperty;
    seek_query.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR seek_descriptor{};
    if (DeviceIoControl(disk, IOCTL_STORAGE_QUERY_PROPERTY, &seek_query,
                        sizeof(seek_query), &seek_descriptor,
                        sizeof(seek_descriptor), &returned, nullptr)) {
        info.ssd = !seek_descriptor.IncursSeekPenalty;
    }
    CloseHandle(disk);
    return true;
}

int volume_disk_number(const std::wstring& mount_point) {
    wchar_t volume_name[MAX_PATH] = {};
    if (!GetVolumeNameForVolumeMountPointW(mount_point.c_str(), volume_name,
                                            MAX_PATH))
        return -1;
    HANDLE volume = CreateFileW(volume_name, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (volume == INVALID_HANDLE_VALUE) return -1;
    std::vector<std::uint8_t> buffer(4096);
    DWORD returned = 0;
    int number = -1;
    if (DeviceIoControl(volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                        buffer.data(), static_cast<DWORD>(buffer.size()),
                        &returned, nullptr)) {
        auto* extents = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buffer.data());
        if (extents->NumberOfDiskExtents > 0)
            number = static_cast<int>(extents->Extents[0].DiskNumber);
    }
    CloseHandle(volume);
    return number;
}

int system_disk_number() {
    wchar_t windows[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windows, MAX_PATH) == 0) return -1;
    wchar_t root[4] = {windows[0], L':', L'\\', 0};
    return volume_disk_number(root);
}

bool disk_has_mounted_volume(int number) {
    wchar_t drives[4096] = {};
    const DWORD length = GetLogicalDriveStringsW(4096, drives);
    if (length == 0 || length >= 4096) return false;
    for (const wchar_t* drive = drives; *drive != L'\0';
         drive += wcslen(drive) + 1) {
        if (volume_disk_number(drive) == number) return true;
    }
    return false;
}

void fill_random(std::vector<std::uint8_t>& buffer) {
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, buffer.data(),
        static_cast<ULONG>(buffer.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        for (std::size_t i = 0; i < buffer.size(); ++i)
            buffer[i] = static_cast<std::uint8_t>((i * 131u + 17u) & 0xFF);
    }
}

bool write_all(HANDLE disk, const std::vector<std::uint8_t>& buffer,
               std::uint64_t size, std::uint64_t& written_total) {
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(disk, zero, nullptr, FILE_BEGIN)) return false;
    std::uint64_t remaining = size;
    while (remaining > 0) {
        DWORD chunk = static_cast<DWORD>(
            (std::min)(remaining, static_cast<std::uint64_t>(buffer.size())));
        if (chunk == 0) break;
        DWORD written = 0;
        if (!WriteFile(disk, buffer.data(), chunk, &written, nullptr) ||
            written != chunk)
            return false;
        remaining -= chunk;
        written_total += chunk;
    }
    return true;
}

bool verify_all(HANDLE disk, const std::vector<std::uint8_t>& buffer,
                std::uint64_t size) {
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(disk, zero, nullptr, FILE_BEGIN)) return false;
    std::vector<std::uint8_t> read(buffer.size());
    std::uint64_t remaining = size;
    while (remaining > 0) {
        DWORD chunk = static_cast<DWORD>(
            (std::min)(remaining, static_cast<std::uint64_t>(buffer.size())));
        DWORD got = 0;
        if (!ReadFile(disk, read.data(), chunk, &got, nullptr) || got != chunk)
            return false;
        if (std::memcmp(read.data(), buffer.data(), chunk) != 0) return false;
        remaining -= chunk;
    }
    return true;
}

} // namespace

int nvme_sanitize(int disk_number, const std::wstring& expected_serial,
                  int action, bool confirm, const std::wstring& out_dir) {
    if (!confirm) {
        std::cerr << "nvme-sanitize: confirmation missing; pass --confirm NVME-SANITIZE\n";
        return 2;
    }
    if (disk_number < 0 || disk_number > 31) return 2;
    if (action < 1 || action > 3) return 2;
    DiskInfo info;
    if (!query_disk(disk_number, info)) {
        std::cerr << "nvme-sanitize: physical disk not found\n";
        return 1;
    }
    if (info.system) {
        std::cerr << "nvme-sanitize: refusing the running system disk\n";
        return 1;
    }
    if (disk_has_mounted_volume(disk_number)) {
        std::cerr << "nvme-sanitize: disk has a mounted volume; offline first\n";
        return 1;
    }
    if (info.bus_type != BusTypeNvme) {
        std::cerr << "nvme-sanitize: target is not an NVMe device\n";
        return 1;
    }
    if (!expected_serial.empty() &&
        _stricmp(art::to_utf8(expected_serial).c_str(), info.serial.c_str()) != 0) {
        std::cerr << "nvme-sanitize: serial mismatch (disk=" << info.serial
                  << ")\n";
        return 1;
    }
    const std::wstring path = L"\\\\.\\PhysicalDrive" +
        std::to_wstring(disk_number);
    HANDLE disk = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (disk == INVALID_HANDLE_VALUE) {
        std::cerr << "nvme-sanitize: open failed, error " << GetLastError()
                  << " (admin required)\n";
        return 1;
    }
    std::vector<std::uint8_t> buffer(sizeof(STORAGE_PROTOCOL_COMMAND) + 64, 0);
    auto* protocol = reinterpret_cast<STORAGE_PROTOCOL_COMMAND*>(buffer.data());
    protocol->Version = STORAGE_PROTOCOL_STRUCTURE_VERSION;
    protocol->Length = sizeof(STORAGE_PROTOCOL_COMMAND);
    protocol->ProtocolType = ProtocolTypeNvme;
    protocol->Flags = STORAGE_PROTOCOL_COMMAND_FLAG_ADAPTER_REQUEST;
    protocol->CommandLength = STORAGE_PROTOCOL_COMMAND_LENGTH_NVME;
    protocol->TimeOutValue = 60;
    protocol->CommandSpecific = STORAGE_PROTOCOL_SPECIFIC_NVME_ADMIN_COMMAND;
    auto* command = reinterpret_cast<NVME_COMMAND*>(protocol->Command);
    command->CDW0.OPC = NVME_ADMIN_COMMAND_SANITIZE;
    command->NSID = NVME_NAMESPACE_ALL;
    command->u.GENERAL.CDW10 = static_cast<ULONG>(action);
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(disk, IOCTL_STORAGE_PROTOCOL_COMMAND,
        buffer.data(), static_cast<DWORD>(buffer.size()), buffer.data(),
        static_cast<DWORD>(buffer.size()), &returned, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(disk);
    const bool success = ok && protocol->ReturnStatus ==
        STORAGE_PROTOCOL_STATUS_SUCCESS;
    const std::wstring audit_dir = out_dir.empty()
        ? art::join_path(L"cases", L"nvme_sanitize_" +
            std::to_wstring(GetTickCount64()))
        : out_dir;
    art::ensure_directory(audit_dir);
    HANDLE audit = CreateFileW(art::join_path(audit_dir,
        L"nvme_sanitize.jsonl").c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (audit != INVALID_HANDLE_VALUE) {
        const std::string line = "{\"action\":\"nvme_sanitize\",\"disk\":" +
            std::to_string(disk_number) + ",\"serial\":" +
            art::json_escape(info.serial) + ",\"sanitize_action\":" +
            std::to_string(action) + ",\"success\":" +
            (success ? "true" : "false") + ",\"win32_error\":" +
            std::to_string(error) + ",\"return_status\":" +
            std::to_string(protocol->ReturnStatus) + ",\"completed_utc\":" +
            art::json_escape(art::now_iso()) + "}";
        const std::string text = line + "\r\n";
        DWORD written = 0;
        WriteFile(audit, text.data(), static_cast<DWORD>(text.size()), &written,
                  nullptr);
        CloseHandle(audit);
    }
    if (!success) {
        std::cerr << "nvme-sanitize: command failed, win32=" << error
                  << " status=" << protocol->ReturnStatus << "\n";
        return 1;
    }
    std::cout << "nvme-sanitize: comando aceito; action=" << action
              << " audit=" << art::to_utf8(audit_dir) << "\n";
    return 0;
}
int list_disks() {
    const int system = system_disk_number();
    for (int number = 0; number < 32; ++number) {
        DiskInfo info;
        if (!query_disk(number, info)) continue;
        info.system = number == system;
        info.mounted = disk_has_mounted_volume(number);
        std::cout << "disk " << number << " size=" << info.size
                  << " model=" << info.model << " serial=" << info.serial
                  << (info.system ? " SYSTEM" : "")
                  << (info.mounted ? " MOUNTED" : "") << "\n";
    }
    return 0;
}

bool ata_passthrough(HANDLE disk, unsigned char command, void* data,
                     unsigned long length, bool data_out, std::string& error) {
    ATA_PASS_THROUGH_DIRECT request{};
    request.Length = sizeof(request);
    request.AtaFlags = ATA_FLAGS_DRDY_REQUIRED |
        (data_out ? ATA_FLAGS_DATA_OUT : 0);
    request.DataTransferLength = length;
    request.TimeOutValue = 120;
    request.DataBuffer = data;
    request.CurrentTaskFile[6] = command;
    DWORD returned = 0;
    if (!DeviceIoControl(disk, IOCTL_ATA_PASS_THROUGH_DIRECT, &request,
                         sizeof(request), &request, sizeof(request), &returned,
                         nullptr)) {
        error = "IOCTL_ATA_PASS_THROUGH_DIRECT failed, error " +
            std::to_string(GetLastError());
        return false;
    }
    const unsigned char status = request.CurrentTaskFile[0];
    const unsigned char device_error = request.CurrentTaskFile[1];
    if ((status & 0x01) != 0 || device_error != 0) {
        error = "ATA command 0x" + std::to_string(command) + " status=0x" +
            std::to_string(status) + " error=0x" + std::to_string(device_error);
        return false;
    }
    return true;
}

int ata_erase(int disk_number, const std::wstring& expected_serial,
              bool enhanced, const std::wstring& password, bool confirm,
              const std::wstring& out_dir) {
    if (!confirm) {
        std::cerr << "ata-erase: confirmation missing; pass --confirm ATA-ERASE\n";
        return 2;
    }
    if (disk_number < 0 || disk_number > 31) return 2;
    DiskInfo info;
    if (!query_disk(disk_number, info)) {
        std::cerr << "ata-erase: physical disk not found\n";
        return 1;
    }
    if (info.system) {
        std::cerr << "ata-erase: refusing the running system disk\n";
        return 1;
    }
    if (disk_has_mounted_volume(disk_number)) {
        std::cerr << "ata-erase: disk has a mounted volume; offline first\n";
        return 1;
    }
    if (info.bus_type != BusTypeAta && info.bus_type != BusTypeSata) {
        std::cerr << "ata-erase: target is not ATA/SATA\n";
        return 1;
    }
    if (!expected_serial.empty() &&
        _stricmp(art::to_utf8(expected_serial).c_str(), info.serial.c_str()) != 0) {
        std::cerr << "ata-erase: serial mismatch (disk=" << info.serial << ")\n";
        return 1;
    }
    const std::wstring path = L"\\\\.\\PhysicalDrive" +
        std::to_wstring(disk_number);
    HANDLE disk = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (disk == INVALID_HANDLE_VALUE) {
        std::cerr << "ata-erase: open failed, error " << GetLastError()
                  << " (admin required)\n";
        return 1;
    }
    std::vector<unsigned char> block(512, 0);
    const std::string ascii = art::to_utf8(password);
    if (ascii.size() > 32) {
        std::cerr << "ata-erase: password must be at most 32 bytes\n";
        CloseHandle(disk);
        return 2;
    }
    block[0] = enhanced ? 1 : 0;
    std::memcpy(block.data() + 2, ascii.data(), ascii.size());
    std::string error;
    bool ok = ata_passthrough(disk, 0xF3, nullptr, 0, false, error);
    if (ok) ok = ata_passthrough(disk, 0xF4, block.data(),
                                 static_cast<unsigned long>(block.size()), true,
                                 error);
    CloseHandle(disk);
    const std::wstring audit_dir = out_dir.empty()
        ? art::join_path(L"cases", L"ata_erase_" +
            std::to_wstring(GetTickCount64()))
        : out_dir;
    art::ensure_directory(audit_dir);
    HANDLE audit = CreateFileW(art::join_path(audit_dir,
        L"ata_erase.jsonl").c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (audit != INVALID_HANDLE_VALUE) {
        const std::string line = "{\"action\":\"ata_erase\",\"disk\":" +
            std::to_string(disk_number) + ",\"serial\":" +
            art::json_escape(info.serial) + ",\"model\":" +
            art::json_escape(info.model) + ",\"enhanced\":" +
            (enhanced ? "true" : "false") + ",\"media\":\"" +
            (info.ssd ? "ssd" : "hdd") + "\",\"success\":" +
            (ok ? "true" : "false") + ",\"error\":" +
            art::json_escape(error) + ",\"completed_utc\":" +
            art::json_escape(art::now_iso()) + "}";
        const std::string text = line + "\r\n";
        DWORD written = 0;
        WriteFile(audit, text.data(), static_cast<DWORD>(text.size()), &written,
                  nullptr);
        CloseHandle(audit);
    }
    if (!ok) {
        std::cerr << "ata-erase: " << error << "\n";
        return 1;
    }
    std::cout << "ata-erase: accepted; enhanced=" << (enhanced ? 1 : 0)
              << " media=" << (info.ssd ? "ssd" : "hdd")
              << " audit=" << art::to_utf8(audit_dir) << "\n";
    return 0;
}
int sanitize_disk(int disk_number, const std::wstring& expected_serial,
                  unsigned passes, bool verify, bool confirm,
                  const std::wstring& out_dir) {
    if (!confirm) {
        std::cerr << "sanitize-disk: confirmation missing; pass --confirm WIPE-DISK\n";
        return 2;
    }
    if (disk_number < 0 || disk_number > 31) return 2;
    if (passes == 0) passes = 1;
    if (passes > 7) passes = 7;
    DiskInfo info;
    if (!query_disk(disk_number, info)) {
        std::cerr << "sanitize-disk: physical disk not found\n";
        return 1;
    }
    if (info.system) {
        std::cerr << "sanitize-disk: refusing the running system disk\n";
        return 1;
    }
    if (disk_has_mounted_volume(disk_number)) {
        std::cerr << "sanitize-disk: disk has a mounted volume; dismount/offline first\n";
        return 1;
    }
    if (!expected_serial.empty()) {
        std::string expected = art::to_utf8(expected_serial);
        if (_stricmp(expected.c_str(), info.serial.c_str()) != 0) {
            std::cerr << "sanitize-disk: serial mismatch (disk=" << info.serial
                      << ")\n";
            return 1;
        }
    }
    if (info.size == 0) {
        std::cerr << "sanitize-disk: disk size unavailable\n";
        return 1;
    }
    if (info.ssd) {
        std::cerr << "sanitize-disk: WARNING: SSD/NVMe media detected; overwrite "
                     "does not guarantee firmware/overprovisioning erasure; use "
                     "nvme-sanitize, ata-erase or physical destruction\n";
    }
    const std::wstring path = L"\\\\.\\PhysicalDrive" +
        std::to_wstring(disk_number);
    HANDLE disk = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (disk == INVALID_HANDLE_VALUE) {
        std::cerr << "sanitize-disk: write open failed, error "
                  << GetLastError() << " (admin required)\n";
        return 1;
    }
    std::vector<std::uint8_t> buffer(1024 * 1024);
    std::uint64_t total_written = 0;
    bool ok = true;
    for (unsigned pass = 0; pass < passes; ++pass) {
        fill_random(buffer);
        std::uint64_t written = 0;
        if (!write_all(disk, buffer, info.size, written)) {
            ok = false;
            break;
        }
        total_written += written;
        FlushFileBuffers(disk);
        std::cout << "sanitize-disk: pass " << (pass + 1) << "/" << passes
                  << " written=" << written << "\n";
        if (verify && !verify_all(disk, buffer, info.size)) {
            std::cerr << "sanitize-disk: verification failed\n";
            ok = false;
            break;
        }
    }
    CloseHandle(disk);
    const std::wstring audit_dir = out_dir.empty()
        ? art::join_path(L"cases", L"sanitize_disk_" + std::to_wstring(
            GetTickCount64()))
        : out_dir;
    art::ensure_directory(audit_dir);
    HANDLE audit = CreateFileW(art::join_path(audit_dir,
        L"disk_sanitize.jsonl").c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (audit != INVALID_HANDLE_VALUE) {
        const std::string line = "{\"action\":\"sanitize_disk\",\"disk\":" +
            std::to_string(disk_number) + ",\"serial\":" +
            art::json_escape(info.serial) + ",\"model\":" +
            art::json_escape(info.model) + ",\"bytes\":" +
            std::to_string(info.size) + ",\"passes\":" +
            std::to_string(passes) + ",\"media\":\"" +
            (info.ssd ? "ssd" : "hdd") + "\",\"verification_scope\":\"" +
            (verify ? "full-readback-last-pass" : "none") + "\",\"verified\":" +
            (verify ? "true" : "false") + ",\"success\":" +
            (ok ? "true" : "false") + ",\"residual_risk\":" +
            "\"backups,cloud-sync,router-cache,remote-logs,snapshots\"" +
            ",\"completed_utc\":" +
            art::json_escape(art::now_iso()) + "}";
        const std::string text = line + "\r\n";
        DWORD written = 0;
        WriteFile(audit, text.data(), static_cast<DWORD>(text.size()), &written,
                  nullptr);
        CloseHandle(audit);
    }
    std::cout << "sanitize-disk: bytes=" << total_written
              << " audit=" << art::to_utf8(audit_dir) << "\n";
    return ok ? 0 : 1;
}

} // namespace deep
