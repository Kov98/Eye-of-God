// language: C++, file: afc.hpp, target: Windows 11 (MSVC / MinGW g++)
// AFC (Apple File Conduit): the file-system service behind a lockdown session.
//
// WHERE IT SITS
//   lockdown StartService("com.apple.afc") hands back a port, usbmux connects, and
//   the session may already be TLS. AFC is a packet protocol on top of that pipe:
//   a fixed 40-byte header followed by a payload, every packet numbered.
//
// WHAT IS PROVEN HERE
//   Header framing, request building, and response parsing are pure and tested
//   against synthetic replies. The file-info struct varies between iOS versions
//   (40 and 52 bytes), so it is carried as raw bytes with best-effort fields and
//   flagged: no real device has confirmed the layout.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::afc {

using bytes = std::vector<std::uint8_t>;

// Sent in the operation field.
enum class op : std::uint64_t {
    status = 0x01,
    data = 0x02,
    read_dir = 0x03,
    get_file_info = 0x04,
    get_device_info = 0x05,
    read_file = 0x06,
    write_file = 0x07,
    seek = 0x08,
    truncate = 0x09,
    remove_path = 0x0A,
    make_dir = 0x0B,
    get_file_info_list = 0x0D,
    file_open = 0x0E,
    file_open_result = 0x0F,
    file_read = 0x10,
    file_write = 0x11,
    file_seek = 0x12,
    file_close = 0x13,
    set_file_len = 0x14,
};

const char* to_string(op o);

constexpr std::uint64_t kHeaderSize = 40;

struct header {
    std::uint64_t entire_length = 0;   // header + payload of the whole logical reply
    std::uint64_t this_length = 0;     // header + payload of this packet
    std::uint64_t packet_num = 0;
    std::uint64_t operation = 0;

    op operation_kind() const { return static_cast<op>(operation); }
};

// Magic is CFA6LPAA. Encoding and decoding are symmetric and covered by tests.
bool encode_header(const header& h, bytes& out, std::string& error);
bool decode_header(const std::uint8_t* data, std::size_t len, header& out, std::string& error);

// ---- requests --------------------------------------------------------------
// Payload for path-carrying operations is the path plus a NUL terminator.
bytes read_dir_request(const std::string& path, std::uint64_t packet_num);
bytes device_info_request(std::uint64_t packet_num);
bytes file_open_request(const std::string& path, std::uint64_t packet_num);
bytes file_read_request(std::uint64_t handle, std::uint64_t length, std::uint64_t packet_num);
bytes file_close_request(std::uint64_t handle, std::uint64_t packet_num);
bytes remove_path_request(const std::string& path, std::uint64_t packet_num);

// ---- responses -------------------------------------------------------------
// Status is the first uint64 of a reply payload. 0 is success; anything else is a
// documented AFC error and is reported with its number.
struct status {
    bool ok = false;
    std::uint64_t code = 0;
    std::string text;
};

bool parse_status(const bytes& payload, status& out);

struct dir_entry {
    std::string name;
    bytes file_info;      // raw struct: layout differs between iOS versions
    bool size_known = false;
    std::uint64_t size = 0;
    bool mtime_known = false;
    std::uint64_t mtime_ns = 0;   // nanoseconds since the Unix epoch
};

// A ReadDir reply is: status u64, then repeated (name NUL, file_info[40 or 52]).
bool parse_dir_listing(const bytes& payload, std::vector<dir_entry>& out, status& st);

// A GetDeviceInfo reply is a sequence of NUL-terminated strings: key, value,
// key, value, ... terminated by an empty string.
bool parse_device_info(const bytes& payload, std::vector<std::pair<std::string, std::string>>& out,
                       status& st);

// A FileOpen reply carries the handle in the status field position.
bool parse_file_open(const bytes& payload, std::uint64_t& handle, status& st);

}  // namespace olho::afc
