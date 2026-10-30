// language: C++, file: kernel_client.cpp, target: Windows 10/11
// User-mode client for the OlhoDeDeusKernel driver. The driver must be loaded
// separately (sc create / test signing); without it these commands fail.

#include "kernel_client.hpp"

#include "artifacts.hpp"

#include <windows.h>
#include <winioctl.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace deep {
namespace {

constexpr const wchar_t* kDevice = L"\\\\.\\OlhoDeDeusKernel";
constexpr DWORD kReadIoctl = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801,
    METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA);
constexpr DWORD kVersionIoctl = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802,
    METHOD_BUFFERED, FILE_READ_DATA);

struct ReadRequest {
    std::uint32_t disk = 0;
    std::uint32_t reserved0 = 0;
    std::uint64_t offset = 0;
    std::uint32_t length = 0;
    std::uint32_t reserved1 = 0;
};

HANDLE open_driver(std::wstring& error) {
    HANDLE device = CreateFileW(kDevice, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        error = L"driver not loaded or access denied (error " +
            std::to_wstring(GetLastError()) + L"); load OlhoDeDeus.sys first";
    }
    return device;
}

} // namespace

int kernel_version(std::wstring& error) {
    HANDLE device = open_driver(error);
    if (device == INVALID_HANDLE_VALUE) return 1;
    std::uint32_t version = 0;
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(device, kVersionIoctl, nullptr, 0,
        &version, sizeof(version), &returned, nullptr);
    CloseHandle(device);
    if (!ok) {
        error = L"IOCTL_EOG_VERSION failed, error " +
            std::to_wstring(GetLastError());
        return 1;
    }
    std::cout << "kernel: version=" << version << " bytes=" << returned << "\n";
    return 0;
}

int kernel_read_disk(int disk_number, std::uint64_t offset,
                     std::uint32_t length, const std::wstring& out_file,
                     std::wstring& error) {
    if (disk_number < 0 || disk_number > 31 || length == 0 ||
        length > 16u * 1024u * 1024u) {
        error = L"invalid disk/offset/length";
        return 2;
    }
    HANDLE device = open_driver(error);
    if (device == INVALID_HANDLE_VALUE) return 1;
    ReadRequest request;
    request.disk = static_cast<std::uint32_t>(disk_number);
    request.offset = offset;
    request.length = length;
    std::vector<std::uint8_t> buffer(length);
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(device, kReadIoctl, &request,
        sizeof(request), buffer.data(), static_cast<DWORD>(buffer.size()),
        &returned, nullptr);
    const DWORD last_error = GetLastError();
    CloseHandle(device);
    if (!ok || returned == 0) {
        error = L"IOCTL_EOG_READ_DISK failed, error " +
            std::to_wstring(last_error);
        return 1;
    }
    HANDLE file = CreateFileW(art::extend_path(out_file).c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"cannot create output, error " +
            std::to_wstring(GetLastError());
        return 1;
    }
    DWORD written = 0;
    const bool write_ok = WriteFile(file, buffer.data(), returned, &written,
        nullptr) && written == returned;
    CloseHandle(file);
    if (!write_ok) {
        error = L"output write failed";
        return 1;
    }
    std::cout << "kernel: disk=" << disk_number << " offset=" << offset
              << " bytes=" << returned << " out=" << art::to_utf8(out_file)
              << "\n";
    return 0;
}

} // namespace deep
