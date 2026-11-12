// language: C++, file: ios_usb.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "ios_usb.hpp"

#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <initguid.h>
#include <usbiodef.h>

#include <cstring>

namespace olho::ios {
namespace {

constexpr std::uint16_t kAppleVendor = 0x05AC;
constexpr std::uint8_t kUsbmuxClass = 0xFF;
constexpr std::uint8_t kUsbmuxSubclass = 0xFE;
constexpr std::uint8_t kUsbmuxProtocol = 0x02;

std::string narrow(const wchar_t* w) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::string registry_property(HDEVINFO set, SP_DEVINFO_DATA* dev, DWORD property) {
    DWORD type = 0, size = 0;
    SetupDiGetDeviceRegistryPropertyW(set, dev, property, &type, nullptr, 0, &size);
    if (size == 0) return {};
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, dev, property, &type,
                                           reinterpret_cast<PBYTE>(buffer.data()), size, &size)) {
        return {};
    }
    return narrow(buffer.data());
}

// iOS devices publish their serial inside the friendly name as "SER=...".
std::string serial_from_friendly(const std::string& friendly) {
    const std::size_t at = friendly.find("SER=");
    if (at == std::string::npos) return {};
    std::string serial = friendly.substr(at + 4);
    while (!serial.empty() && (serial.back() == '\0' || serial.back() == '\n' || serial.back() == ' ')) {
        serial.pop_back();
    }
    return serial;
}

bool parse_vid_pid(const std::string& hardware_id, std::uint16_t& vid, std::uint16_t& pid) {
    const std::size_t v = hardware_id.find("VID_");
    const std::size_t p = hardware_id.find("PID_");
    if (v == std::string::npos || p == std::string::npos) return false;
    vid = static_cast<std::uint16_t>(std::strtoul(hardware_id.substr(v + 4, 4).c_str(), nullptr, 16));
    pid = static_cast<std::uint16_t>(std::strtoul(hardware_id.substr(p + 4, 4).c_str(), nullptr, 16));
    return true;
}

// Opens the device interface just far enough to read its descriptors, then closes.
// Enumeration must never leave a device claimed: a caller that only wants to list
// must not disturb an acquisition that is about to start.
bool probe_interface(const std::string& path, usb_device& out, std::string& note) {
    const std::wstring wide(path.begin(), path.end());
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        note = "CreateFile falhou (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    WINUSB_INTERFACE_HANDLE winusb = nullptr;
    if (!WinUsb_Initialize(file, &winusb)) {
        note = "WinUsb_Initialize falhou (" + std::to_string(GetLastError()) +
               "); o driver pode nao ser WinUSB";
        CloseHandle(file);
        return false;
    }

    bool found = false;
    USB_INTERFACE_DESCRIPTOR descriptor{};
    if (WinUsb_QueryInterfaceSettings(winusb, 0, &descriptor)) {
        out.interface_number = descriptor.bInterfaceNumber;
        if (descriptor.bInterfaceClass == kUsbmuxClass &&
            descriptor.bInterfaceSubClass == kUsbmuxSubclass &&
            descriptor.bInterfaceProtocol == kUsbmuxProtocol) {
            found = true;
            for (int i = 0; i < descriptor.bNumEndpoints; ++i) {
                WINUSB_PIPE_INFORMATION pipe{};
                if (!WinUsb_QueryPipe(winusb, 0, static_cast<UCHAR>(i), &pipe)) continue;
                if (pipe.PipeType != UsbdPipeTypeBulk) continue;
                if (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId)) {
                    out.endpoint_in = pipe.PipeId;
                    out.bulk_in_max = pipe.MaximumPacketSize;
                } else {
                    out.endpoint_out = pipe.PipeId;
                    out.bulk_out_max = pipe.MaximumPacketSize;
                }
            }
        } else {
            note = "interface nao e usbmuxd (classe 0x" + std::to_string(descriptor.bInterfaceClass) +
                   " sub 0x" + std::to_string(descriptor.bInterfaceSubClass) + ")";
        }
    } else {
        note = "QueryInterfaceSettings falhou";
    }

    WinUsb_Free(winusb);
    CloseHandle(file);
    return found;
}

}  // namespace

enumerate_result enumerate_apple_devices() {
    enumerate_result result;

    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        result.error = "SetupDiGetClassDevs falhou (" + std::to_string(GetLastError()) + ")";
        return result;
    }

    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data{};
        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_USB_DEVICE, index,
                                         &interface_data)) {
            break;
        }

        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &interface_data, nullptr, 0, &needed, nullptr);
        if (needed == 0) continue;
        std::vector<std::uint8_t> buffer(needed);
        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA devinfo{};
        devinfo.cbSize = sizeof(devinfo);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &interface_data, detail, needed, &needed, &devinfo)) {
            continue;
        }

        const std::string hardware = registry_property(set, &devinfo, SPDRP_HARDWAREID);
        std::uint16_t vid = 0, pid = 0;
        if (!parse_vid_pid(hardware, vid, pid)) continue;
        if (vid != kAppleVendor) continue;

        usb_device device;
        device.path = narrow(detail->DevicePath);
        device.vid = vid;
        device.pid = pid;
        device.serial = serial_from_friendly(registry_property(set, &devinfo, SPDRP_FRIENDLYNAME));

        std::string note;
        if (probe_interface(device.path, device, note)) {
            result.devices.push_back(std::move(device));
        } else {
            result.notes.push_back(device.path + ": " + (note.empty() ? "sem usbmuxd" : note));
        }
    }

    SetupDiDestroyDeviceInfoList(set);
    result.ok = true;
    return result;
}

connection::~connection() { close(); }

void connection::close() {
    if (handle_) {
        // The WinUSB handle shares the file handle's lifetime; closing the file is
        // enough, and WinUsb_Free was already called in open().
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

bool connection::is_open() const { return handle_ != nullptr; }

bool connection::open(const usb_device& dev, std::string& error) {
    close();
    const std::wstring wide(dev.path.begin(), dev.path.end());
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "nao consegui abrir o dispositivo (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    WINUSB_INTERFACE_HANDLE winusb = nullptr;
    if (!WinUsb_Initialize(file, &winusb)) {
        error = "WinUsb_Initialize falhou (" + std::to_string(GetLastError()) + ")";
        CloseHandle(file);
        return false;
    }
    WinUsb_Free(winusb);   // the pipe operations below are per-transfer on the file

    handle_ = file;
    endpoint_in_ = dev.endpoint_in;
    endpoint_out_ = dev.endpoint_out;
    return true;
}

bool connection::read(bytes& out, std::uint32_t timeout_ms, bool& timed_out, std::string& error) {
    timed_out = false;
    out.clear();
    if (!handle_) { error = "conexao fechada"; return false; }

    WINUSB_INTERFACE_HANDLE winusb = nullptr;
    if (!WinUsb_Initialize(static_cast<HANDLE>(handle_), &winusb)) {
        error = "WinUsb_Initialize falhou na leitura (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    std::vector<std::uint8_t> buffer(64 * 1024);
    DWORD transferred = 0;
    const BOOL ok = WinUsb_ReadPipe(winusb, endpoint_in_, buffer.data(),
                                    static_cast<ULONG>(buffer.size()), &transferred, nullptr);
    const DWORD last = GetLastError();
    WinUsb_Free(winusb);

    if (ok) {
        out.assign(buffer.begin(), buffer.begin() + transferred);
        return true;
    }
    if (last == ERROR_SEM_TIMEOUT || last == ERROR_IO_PENDING) {
        timed_out = true;
        return false;
    }
    error = "WinUsb_ReadPipe falhou (" + std::to_string(last) + ")";
    (void)timeout_ms;
    return false;
}

bool connection::write(const bytes& data, std::uint32_t timeout_ms, std::string& error) {
    if (!handle_) { error = "conexao fechada"; return false; }
    WINUSB_INTERFACE_HANDLE winusb = nullptr;
    if (!WinUsb_Initialize(static_cast<HANDLE>(handle_), &winusb)) {
        error = "WinUsb_Initialize falhou na escrita (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    DWORD transferred = 0;
    const BOOL ok = WinUsb_WritePipe(winusb, endpoint_out_, const_cast<PUCHAR>(data.data()),
                                     static_cast<ULONG>(data.size()), &transferred, nullptr);
    const DWORD last = GetLastError();
    WinUsb_Free(winusb);
    (void)timeout_ms;
    if (!ok) {
        error = "WinUsb_WritePipe falhou (" + std::to_string(last) + ")";
        return false;
    }
    if (transferred != data.size()) {
        error = "escrita parcial: " + std::to_string(transferred) + " de " + std::to_string(data.size());
        return false;
    }
    return true;
}

}  // namespace olho::ios
