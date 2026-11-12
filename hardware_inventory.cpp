#include "hardware_inventory.hpp"

#include "third_party/nlohmann/json.hpp"

#include <windows.h>
#include <winioctl.h>
#include <tbs.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace deep::forensic {
namespace {

std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    }
    return result;
}

std::string utc_now() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &now);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::optional<std::string> registry_string(HKEY root, const wchar_t* subkey,
                                           const wchar_t* name) {
    DWORD bytes = 0;
    DWORD type = 0;
    if (RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, &type, nullptr, &bytes) !=
            ERROR_SUCCESS || bytes == 0) {
        return std::nullopt;
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, &type, value.data(),
                     &bytes) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return to_utf8(value);
}

std::optional<DWORD> registry_dword(HKEY root, const wchar_t* subkey,
                                    const wchar_t* name) {
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    DWORD type = 0;
    if (RegGetValueW(root, subkey, name, RRF_RT_REG_DWORD, &type, &value,
                     &bytes) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return value;
}

std::string bus_name(STORAGE_BUS_TYPE type) {
    switch (type) {
    case BusTypeScsi: return "SCSI";
    case BusTypeAtapi: return "ATAPI";
    case BusTypeAta: return "ATA";
    case BusType1394: return "1394";
    case BusTypeSsa: return "SSA";
    case BusTypeFibre: return "Fibre";
    case BusTypeUsb: return "USB";
    case BusTypeRAID: return "RAID";
    case BusTypeiScsi: return "iSCSI";
    case BusTypeSas: return "SAS";
    case BusTypeSata: return "SATA";
    case BusTypeSd: return "SD";
    case BusTypeMmc: return "MMC";
    case BusTypeVirtual: return "Virtual";
    case BusTypeFileBackedVirtual: return "FileBackedVirtual";
    case BusTypeSpaces: return "Spaces";
    case BusTypeNvme: return "NVMe";
    case BusTypeSCM: return "SCM";
    case BusTypeUfs: return "UFS";
    default: return "Desconhecido";
    }
}

void collect_disks(HardwareInventory& inventory) {
    for (int index = 0; index < 32; ++index) {
        const std::wstring device = L"\\\\.\\PhysicalDrive" +
            std::to_wstring(index);
        HANDLE handle = CreateFileW(device.c_str(), 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) continue;

        HardwareDisk disk;
        disk.device = "PhysicalDrive" + std::to_string(index);

        GET_LENGTH_INFORMATION length{};
        DWORD returned = 0;
        if (DeviceIoControl(handle, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
                            &length, sizeof(length), &returned, nullptr)) {
            disk.bytes = static_cast<std::uint64_t>(length.Length.QuadPart);
        }
        if (disk.bytes == 0) {
            DISK_GEOMETRY_EX geometry{};
            if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                                nullptr, 0, &geometry, sizeof(geometry),
                                &returned, nullptr)) {
                disk.bytes = static_cast<std::uint64_t>(
                    geometry.DiskSize.QuadPart);
            }
        }

        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        std::vector<unsigned char> buffer(4096);
        if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query,
                            sizeof(query), buffer.data(),
                            static_cast<DWORD>(buffer.size()), &returned,
                            nullptr) &&
            returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            const auto* descriptor =
                reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
            auto string_at = [&](DWORD offset) -> std::string {
                if (offset == 0 || offset >= returned) return {};
                const char* text =
                    reinterpret_cast<const char*>(buffer.data() + offset);
                return std::string(text);
            };
            disk.model = string_at(descriptor->ProductIdOffset);
            disk.serial = string_at(descriptor->SerialNumberOffset);
            disk.bus = bus_name(descriptor->BusType);
            disk.media = descriptor->RemovableMedia ? "Removível" : "Fixo";
        }

        STORAGE_PROPERTY_QUERY seek_query{};
        seek_query.PropertyId = StorageDeviceSeekPenaltyProperty;
        seek_query.QueryType = PropertyStandardQuery;
        DEVICE_SEEK_PENALTY_DESCRIPTOR seek{};
        if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &seek_query,
                            sizeof(seek_query), &seek, sizeof(seek), &returned,
                            nullptr)) {
            disk.ssd = !seek.IncursSeekPenalty;
        }

        STORAGE_PREDICT_FAILURE predict{};
        if (DeviceIoControl(handle, IOCTL_STORAGE_PREDICT_FAILURE, nullptr, 0,
                            &predict, sizeof(predict), &returned, nullptr)) {
            disk.smart = predict.PredictFailure ? "falha prevista" : "ok";
        } else {
            inventory.warnings.push_back(
                disk.device + ": SMART não consultável sem privilégio");
        }

        CloseHandle(handle);
        inventory.disks.push_back(std::move(disk));
    }
}

void collect_volumes(HardwareInventory& inventory) {
    wchar_t buffer[512]{};
    const DWORD length = GetLogicalDriveStringsW(
        static_cast<DWORD>(std::size(buffer)), buffer);
    if (length == 0 || length >= std::size(buffer)) return;
    for (const wchar_t* root = buffer; *root; root += std::wcslen(root) + 1) {
        HardwareVolume volume;
        volume.root = to_utf8(root);
        wchar_t label[MAX_PATH + 1]{};
        wchar_t filesystem[MAX_PATH + 1]{};
        if (GetVolumeInformationW(root, label, MAX_PATH, nullptr, nullptr,
                                  nullptr, filesystem, MAX_PATH)) {
            volume.label = to_utf8(label);
            volume.filesystem = to_utf8(filesystem);
        }
        ULARGE_INTEGER free_bytes{};
        ULARGE_INTEGER total_bytes{};
        if (GetDiskFreeSpaceExW(root, &free_bytes, &total_bytes, nullptr)) {
            volume.free_bytes = free_bytes.QuadPart;
            volume.total_bytes = total_bytes.QuadPart;
        }
        inventory.volumes.push_back(std::move(volume));
    }
}

void collect_usb(HardwareInventory& inventory) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Enum\\USBSTOR", 0, KEY_READ,
            &key) != ERROR_SUCCESS) {
        return;
    }
    for (DWORD index = 0;; ++index) {
        wchar_t name[512]{};
        DWORD name_length = static_cast<DWORD>(std::size(name));
        const LSTATUS status = RegEnumKeyExW(key, index, name, &name_length,
            nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) continue;
        HKEY device = nullptr;
        if (RegOpenKeyExW(key, name, 0, KEY_READ, &device) == ERROR_SUCCESS) {
            wchar_t friendly[512]{};
            DWORD friendly_length = sizeof(friendly);
            DWORD type = 0;
            if (RegQueryValueExW(device, L"FriendlyName", nullptr, &type,
                    reinterpret_cast<LPBYTE>(friendly), &friendly_length) ==
                ERROR_SUCCESS) {
                inventory.usb_storage.push_back({to_utf8(name),
                    to_utf8(friendly)});
            }
            RegCloseKey(device);
        }
    }
    RegCloseKey(key);
}

void collect_tpm(HardwareInventory& inventory) {
    TPM_DEVICE_INFO info{};
    info.structVersion = 1;
    const TBS_RESULT result = Tbsi_GetDeviceInfo(sizeof(info), &info);
    if (result != TBS_SUCCESS) {
        inventory.warnings.push_back("TPM não consultável");
        return;
    }
    if (info.tpmVersion == TPM_VERSION_12) inventory.tpm = "1.2";
    else if (info.tpmVersion == TPM_VERSION_20) inventory.tpm = "2.0";
    else inventory.tpm = "desconhecido";
}

} // namespace

HardwareInventory collect_hardware_inventory() {
    HardwareInventory inventory;
    inventory.collected_at_utc = utc_now();

    wchar_t hostname[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD hostname_length = static_cast<DWORD>(std::size(hostname));
    if (GetComputerNameW(hostname, &hostname_length)) {
        inventory.hostname = to_utf8(hostname);
    }

    const wchar_t* os_key =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    inventory.os_name = registry_string(HKEY_LOCAL_MACHINE, os_key, L"ProductName")
        .value_or("");
    inventory.os_version = registry_string(HKEY_LOCAL_MACHINE, os_key,
        L"DisplayVersion").value_or("");
    inventory.os_build = registry_string(HKEY_LOCAL_MACHINE, os_key,
        L"CurrentBuild").value_or("");
    const auto ubr = registry_dword(HKEY_LOCAL_MACHINE, os_key, L"UBR");
    if (ubr) inventory.os_build += "." + std::to_string(*ubr);

    inventory.cpu = registry_string(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString").value_or("");

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        inventory.ram_bytes = memory.ullTotalPhys;
    }

    const wchar_t* bios_key = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    inventory.system_manufacturer = registry_string(HKEY_LOCAL_MACHINE,
        bios_key, L"SystemManufacturer").value_or("");
    inventory.system_model = registry_string(HKEY_LOCAL_MACHINE, bios_key,
        L"SystemProductName").value_or("");
    inventory.system_serial = registry_string(HKEY_LOCAL_MACHINE, bios_key,
        L"SystemSerialNumber").value_or("");
    inventory.bios_vendor = registry_string(HKEY_LOCAL_MACHINE, bios_key,
        L"BIOSVendor").value_or("");
    inventory.bios_version = registry_string(HKEY_LOCAL_MACHINE, bios_key,
        L"BIOSVersion").value_or("");
    inventory.bios_date = registry_string(HKEY_LOCAL_MACHINE, bios_key,
        L"BIOSReleaseDate").value_or("");
    inventory.board_manufacturer = registry_string(HKEY_LOCAL_MACHINE, bios_key,
        L"BaseBoardManufacturer").value_or("");
    inventory.board_product = registry_string(HKEY_LOCAL_MACHINE, bios_key,
        L"BaseBoardProduct").value_or("");
    inventory.board_serial = registry_string(HKEY_LOCAL_MACHINE, bios_key,
        L"BaseBoardSerialNumber").value_or("");

    const auto secure_boot = registry_dword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
        L"UEFISecureBootEnabled");
    if (secure_boot) inventory.secure_boot = (*secure_boot != 0);

    collect_disks(inventory);
    collect_volumes(inventory);
    collect_usb(inventory);
    collect_tpm(inventory);
    return inventory;
}

bool write_hardware_inventory_json(const HardwareInventory& inventory,
                                   const std::string& path, std::string& error) {
    error.clear();
    try {
        nlohmann::json document;
        document["schema"] = "olho-de-deus/hardware-inventory/v1";
        document["collected_at_utc"] = inventory.collected_at_utc;
        document["hostname"] = inventory.hostname;
        document["os"] = {
            {"name", inventory.os_name},
            {"version", inventory.os_version},
            {"build", inventory.os_build}
        };
        document["cpu"] = inventory.cpu;
        document["ram_bytes"] = inventory.ram_bytes;
        document["system"] = {
            {"manufacturer", inventory.system_manufacturer},
            {"model", inventory.system_model},
            {"serial", inventory.system_serial}
        };
        document["bios"] = {
            {"vendor", inventory.bios_vendor},
            {"version", inventory.bios_version},
            {"date", inventory.bios_date}
        };
        document["board"] = {
            {"manufacturer", inventory.board_manufacturer},
            {"product", inventory.board_product},
            {"serial", inventory.board_serial}
        };
        if (inventory.secure_boot) {
            document["secure_boot"] = *inventory.secure_boot;
        } else {
            document["secure_boot"] = nullptr;
        }
        if (inventory.tpm) {
            document["tpm"] = *inventory.tpm;
        } else {
            document["tpm"] = nullptr;
        }
        document["disks"] = nlohmann::json::array();
        for (const auto& disk : inventory.disks) {
            nlohmann::json entry = {
                {"device", disk.device},
                {"model", disk.model},
                {"serial", disk.serial},
                {"bus", disk.bus},
                {"media", disk.media},
                {"bytes", disk.bytes},
                {"ssd", disk.ssd}
            };
            if (disk.smart) entry["smart"] = *disk.smart;
            else entry["smart"] = nullptr;
            document["disks"].push_back(std::move(entry));
        }
        document["volumes"] = nlohmann::json::array();
        for (const auto& volume : inventory.volumes) {
            document["volumes"].push_back({
                {"root", volume.root},
                {"label", volume.label},
                {"filesystem", volume.filesystem},
                {"total_bytes", volume.total_bytes},
                {"free_bytes", volume.free_bytes}
            });
        }
        document["usb_storage"] = nlohmann::json::array();
        for (const auto& usb : inventory.usb_storage) {
            document["usb_storage"].push_back({
                {"id", usb.id},
                {"name", usb.name}
            });
        }
        document["warnings"] = inventory.warnings;

        std::error_code fs_error;
        const std::filesystem::path target(path);
        if (target.has_parent_path()) {
            std::filesystem::create_directories(target.parent_path(), fs_error);
            if (fs_error) {
                error = "could not create inventory directory: " +
                    fs_error.message();
                return false;
            }
        }
        std::ofstream stream(target, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = "could not open inventory file";
            return false;
        }
        stream << document.dump(2) << '\n';
        if (!stream) {
            error = "could not write inventory file";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace deep::forensic
