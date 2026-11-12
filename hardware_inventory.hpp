#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deep::forensic {

struct HardwareDisk {
    std::string device;
    std::string model;
    std::string serial;
    std::string bus;
    std::string media;
    std::uint64_t bytes = 0;
    bool ssd = false;
    std::optional<std::string> smart;
};

struct HardwareVolume {
    std::string root;
    std::string label;
    std::string filesystem;
    std::uint64_t total_bytes = 0;
    std::uint64_t free_bytes = 0;
};

struct HardwareUsb {
    std::string id;
    std::string name;
};

struct HardwareInventory {
    std::string collected_at_utc;
    std::string hostname;
    std::string os_name;
    std::string os_version;
    std::string os_build;
    std::string cpu;
    std::uint64_t ram_bytes = 0;
    std::string system_manufacturer;
    std::string system_model;
    std::string system_serial;
    std::string bios_vendor;
    std::string bios_version;
    std::string bios_date;
    std::string board_manufacturer;
    std::string board_product;
    std::string board_serial;
    std::optional<bool> secure_boot;
    std::optional<std::string> tpm;
    std::vector<HardwareDisk> disks;
    std::vector<HardwareVolume> volumes;
    std::vector<HardwareUsb> usb_storage;
    std::vector<std::string> warnings;
};

HardwareInventory collect_hardware_inventory();

bool write_hardware_inventory_json(const HardwareInventory& inventory,
                                   const std::string& path, std::string& error);

} // namespace deep::forensic
