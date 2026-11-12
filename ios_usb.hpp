// language: C++, file: ios_usb.hpp, target: Windows 11 (MSVC / MinGW g++)
// USB transport for Apple mobile devices (VID 0x05AC), over WinUSB.
//
// WHAT THIS LAYER MUST DO
//   Find the usbmuxd interface on the bus, claim it, and move bytes: the protocol
//   framing is already proven in usbmux.cpp, so this file only has to be a pipe.
//
// NOT PROVEN
//   No Apple device has been attached to this machine, so nothing here has ever
//   moved a byte over a real cable. Enumeration is exercised for real (it sees the
//   host controllers on this box); the transfer path is not.
//
// The usbmuxd interface is USB class 0xFF, subclass 0xFE, protocol 0x02, usually
// interface 0, with bulk endpoints 0x04 OUT / 0x85 IN on stock devices. Those are
// read from the descriptors rather than assumed: a device that reports different
// endpoints is described, not guessed at.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::ios {

using bytes = std::vector<std::uint8_t>;

struct usb_device {
    std::string path;            // WinUSB device interface path
    std::string serial;          // from the USB descriptor, when readable
    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
    std::uint8_t interface_number = 0;
    std::uint8_t endpoint_out = 0;
    std::uint8_t endpoint_in = 0;
    std::uint32_t bulk_out_max = 0;
    std::uint32_t bulk_in_max = 0;
};

// Enumerates Apple devices exposing a usbmuxd interface. No handle is opened, so
// it is safe to call with nothing plugged in.
struct enumerate_result {
    bool ok = false;
    std::vector<usb_device> devices;
    std::vector<std::string> notes;   // devices seen but rejected, with the reason
    std::string error;
};

enumerate_result enumerate_apple_devices();

// An open pipe. All calls report failure instead of throwing.
class connection {
public:
    connection() = default;
    ~connection();
    connection(const connection&) = delete;
    connection& operator=(const connection&) = delete;

    bool open(const usb_device& dev, std::string& error);
    void close();
    bool is_open() const;

    // A timeout is reported through timed_out with ok == false and an empty error:
    // a quiet device is not a broken one.
    bool read(bytes& out, std::uint32_t timeout_ms, bool& timed_out, std::string& error);
    bool write(const bytes& data, std::uint32_t timeout_ms, std::string& error);

private:
    void* handle_ = nullptr;
    std::uint8_t endpoint_in_ = 0;
    std::uint8_t endpoint_out_ = 0;
};

}  // namespace olho::ios
