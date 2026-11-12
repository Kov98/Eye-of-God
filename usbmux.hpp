// language: C++, file: usbmux.hpp, target: Windows 11 (MSVC / MinGW g++)
// usbmuxd client: the multiplexer an iOS device exposes over USB.
//
// WHY THIS LAYER FIRST
//   Everything that talks to an iPhone — lockdown, AFC, the file system — rides on
//   this framing. It is pure bytes, so it is fully testable without a handset,
//   which is exactly the part worth building before hardware is on the desk.
//
// WIRE FORMAT (little-endian throughout)
//   request   = u32 length | u32 version(1) | u32 message(8) | u32 tag |
//               u32 plist_length | plist(UTF-8 XML)
//   response  = u32 length | u32 version(1) | u32 message(8) | u32 tag |
//               u32 plist_length | plist
//   length counts the whole frame including itself.
//
// Nothing here spawns a process or opens a socket: parsing and building are pure,
// connection lives behind the caller.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::usbmux {

using bytes = std::vector<std::uint8_t>;

// Values carried in the message field of the header.
enum class message : std::uint32_t {
    result = 1,
    connect = 2,
    listen = 3,
    device_add = 4,
    device_remove = 5,
    device_paired = 6,
    plist = 8,
};

const char* to_string(message m);

struct frame {
    std::uint32_t version = 1;
    message type = message::plist;
    std::uint32_t tag = 0;
    std::string payload;      // plist XML, or the empty string
};

// Serialises a frame. False when the payload would overflow the u32 length field.
bool encode(const frame& f, bytes& out, std::string& error);

// Parses one frame from the front of a buffer. consumed receives how many bytes
// the frame occupied, so a caller can walk a buffer holding several.
bool decode(const std::uint8_t* data, std::size_t len, frame& out, std::size_t& consumed,
            std::string& error);

// ---- plists this protocol speaks -------------------------------------------
// They are small and fixed in shape; building them by hand keeps the module free
// of a plist dependency on the outbound side.

// MessageType Connect, DeviceID, PortNumber, ClientVersionString.
std::string connect_request(std::uint32_t device_id, std::uint16_t port);
// MessageType ListDevices, ClientVersionString.
std::string list_devices_request();

struct device_entry {
    std::uint32_t device_id = 0;
    std::string serial;        // SerialNumber
    std::string connection;    // USB / Network
    std::uint16_t product_id = 0;
    std::string location;      // LocationID, when present
};

// Reads the DeviceList of a ListDevices reply. Tolerant of key order.
bool parse_device_list(const std::string& plist, std::vector<device_entry>& out, std::string& error);

// Reads Result (0 = success) and, on connect replies, PortNumber.
bool parse_result(const std::string& plist, std::uint32_t& result, std::uint16_t& port,
                  std::string& error);

}  // namespace olho::usbmux
