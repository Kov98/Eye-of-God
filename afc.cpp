// language: C++, file: afc.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "afc.hpp"

#include <cstring>

namespace olho::afc {
namespace {

constexpr char kMagic[8] = {'C', 'F', 'A', '6', 'L', 'P', 'A', 'A'};

void put_u64(bytes& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

std::uint64_t read_u64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (i * 8);
    return v;
}

bytes with_path(op operation, const std::string& path, std::uint64_t packet_num) {
    header h;
    h.packet_num = packet_num;
    h.operation = static_cast<std::uint64_t>(operation);
    h.this_length = kHeaderSize + path.size() + 1;
    h.entire_length = h.this_length;

    bytes out;
    std::string error;
    encode_header(h, out, error);
    out.insert(out.end(), path.begin(), path.end());
    out.push_back(0);
    return out;
}

bytes without_payload(op operation, std::uint64_t packet_num) {
    header h;
    h.packet_num = packet_num;
    h.operation = static_cast<std::uint64_t>(operation);
    h.this_length = kHeaderSize;
    h.entire_length = kHeaderSize;
    bytes out;
    std::string error;
    encode_header(h, out, error);
    return out;
}

bytes with_u64(op operation, std::uint64_t value, std::uint64_t packet_num) {
    header h;
    h.packet_num = packet_num;
    h.operation = static_cast<std::uint64_t>(operation);
    h.this_length = kHeaderSize + 8;
    h.entire_length = h.this_length;
    bytes out;
    std::string error;
    encode_header(h, out, error);
    put_u64(out, value);
    return out;
}

// Reads a NUL-terminated string starting at pos; advances past the terminator.
bool read_cstr(const bytes& data, std::size_t& pos, std::string& out) {
    const std::size_t start = pos;
    while (pos < data.size() && data[pos] != 0) ++pos;
    if (pos >= data.size()) return false;
    out.assign(data.begin() + static_cast<std::ptrdiff_t>(start),
               data.begin() + static_cast<std::ptrdiff_t>(pos));
    ++pos;   // skip the terminator
    return true;
}

}  // namespace

const char* to_string(op o) {
    switch (o) {
        case op::status: return "Status";
        case op::data: return "Data";
        case op::read_dir: return "ReadDir";
        case op::get_file_info: return "GetFileInfo";
        case op::get_device_info: return "GetDeviceInfo";
        case op::read_file: return "ReadFile";
        case op::write_file: return "WriteFile";
        case op::seek: return "Seek";
        case op::truncate: return "Truncate";
        case op::remove_path: return "RemovePath";
        case op::make_dir: return "MakeDir";
        case op::get_file_info_list: return "GetFileInfoList";
        case op::file_open: return "FileOpen";
        case op::file_open_result: return "FileOpenResult";
        case op::file_read: return "FileRead";
        case op::file_write: return "FileWrite";
        case op::file_seek: return "FileSeek";
        case op::file_close: return "FileClose";
        case op::set_file_len: return "SetFileLen";
    }
    return "Unknown";
}

bool encode_header(const header& h, bytes& out, std::string& error) {
    if (h.this_length < kHeaderSize || h.entire_length < h.this_length) {
        error = "comprimentos inconsistentes no cabecalho AFC";
        return false;
    }
    out.clear();
    out.reserve(kHeaderSize);
    out.insert(out.end(), kMagic, kMagic + 8);
    put_u64(out, h.entire_length);
    put_u64(out, h.this_length);
    put_u64(out, h.packet_num);
    put_u64(out, h.operation);
    return true;
}

bool decode_header(const std::uint8_t* data, std::size_t len, header& out, std::string& error) {
    if (len < kHeaderSize) { error = "cabecalho AFC truncado"; return false; }
    if (std::memcmp(data, kMagic, 8) != 0) { error = "magic AFC invalido (esperado CFA6LPAA)"; return false; }
    out.entire_length = read_u64(data + 8);
    out.this_length = read_u64(data + 16);
    out.packet_num = read_u64(data + 24);
    out.operation = read_u64(data + 32);
    if (out.this_length < kHeaderSize) { error = "this_length menor que o cabecalho"; return false; }
    if (out.entire_length < out.this_length) { error = "entire_length menor que this_length"; return false; }
    return true;
}

bytes read_dir_request(const std::string& path, std::uint64_t packet_num) {
    return with_path(op::read_dir, path, packet_num);
}
bytes device_info_request(std::uint64_t packet_num) { return without_payload(op::get_device_info, packet_num); }
bytes file_open_request(const std::string& path, std::uint64_t packet_num) {
    return with_path(op::file_open, path, packet_num);
}
bytes file_read_request(std::uint64_t handle, std::uint64_t length, std::uint64_t packet_num) {
    header h;
    h.packet_num = packet_num;
    h.operation = static_cast<std::uint64_t>(op::file_read);
    h.this_length = kHeaderSize + 16;
    h.entire_length = h.this_length;
    bytes out;
    std::string error;
    encode_header(h, out, error);
    put_u64(out, handle);
    put_u64(out, length);
    return out;
}
bytes file_close_request(std::uint64_t handle, std::uint64_t packet_num) {
    return with_u64(op::file_close, handle, packet_num);
}
bytes remove_path_request(const std::string& path, std::uint64_t packet_num) {
    return with_path(op::remove_path, path, packet_num);
}

bool parse_status(const bytes& payload, status& out) {
    out = status{};
    if (payload.size() < 8) { out.text = "resposta AFC sem status"; return false; }
    out.code = read_u64(payload.data());
    out.ok = (out.code == 0);
    if (!out.ok) out.text = "AFC recusou (codigo " + std::to_string(out.code) + ")";
    return out.ok;
}

namespace {

// One pass assuming a fixed struct width. Returns true only when it consumes the
// payload exactly — a partial parse means the width was wrong.
bool scan_listing(const bytes& payload, std::size_t struct_len, std::vector<dir_entry>& out) {
    out.clear();
    std::size_t pos = 8;
    while (pos < payload.size()) {
        std::string name;
        if (!read_cstr(payload, pos, name)) return false;
        if (name.empty()) return false;
        if (payload.size() - pos < struct_len) return false;

        dir_entry e;
        e.name = name;
        e.file_info.assign(payload.begin() + static_cast<std::ptrdiff_t>(pos),
                           payload.begin() + static_cast<std::ptrdiff_t>(pos + struct_len));
        pos += struct_len;
        e.mtime_ns = read_u64(e.file_info.data());
        e.mtime_known = true;
        e.size = read_u64(e.file_info.data() + 8);
        e.size_known = true;
        out.push_back(std::move(e));
    }
    return pos == payload.size();
}

}  // namespace

bool parse_dir_listing(const bytes& payload, std::vector<dir_entry>& out, status& st) {
    out.clear();
    if (!parse_status(payload, st)) return false;

    // The file-info struct is 40 bytes on older iOS and 52 on newer, and it does
    // not describe itself. Guessing by "is there room for 52" misaligns every
    // entry after the first, so both widths are tried and only the one that lands
    // exactly on the end of the payload is accepted.
    std::vector<dir_entry> attempt;
    if (scan_listing(payload, 40, attempt)) { out = std::move(attempt); return true; }
    if (scan_listing(payload, 52, attempt)) { out = std::move(attempt); return true; }

    st.ok = false;
    st.text = "listagem AFC com alinhamento nao reconhecido (40 e 52 bytes falharam)";
    return false;
}

bool parse_device_info(const bytes& payload, std::vector<std::pair<std::string, std::string>>& out,
                       status& st) {
    out.clear();
    if (!parse_status(payload, st)) return false;

    std::size_t pos = 8;
    while (pos < payload.size()) {
        std::string key, value;
        if (!read_cstr(payload, pos, key)) break;
        if (key.empty()) break;
        if (!read_cstr(payload, pos, value)) break;
        out.emplace_back(std::move(key), std::move(value));
    }
    return true;
}

bool parse_file_open(const bytes& payload, std::uint64_t& handle, status& st) {
    handle = 0;
    if (payload.size() < 8) { st.text = "FileOpen sem handle"; return false; }
    handle = read_u64(payload.data());
    st.ok = (handle != 0);
    st.code = handle;
    if (!st.ok) st.text = "FileOpen recusado";
    return st.ok;
}

}  // namespace olho::afc
