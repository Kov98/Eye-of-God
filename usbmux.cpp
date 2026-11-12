// language: C++, file: usbmux.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "usbmux.hpp"

#include "xml_plist.hpp"

#include <algorithm>
#include <cstring>
#include <map>

namespace olho::usbmux {
namespace {

void put_u32(bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string xml_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s.compare(i, 5, "&amp;") == 0) { out.push_back('&'); i += 5; }
        else if (s.compare(i, 4, "&lt;") == 0) { out.push_back('<'); i += 4; }
        else if (s.compare(i, 4, "&gt;") == 0) { out.push_back('>'); i += 4; }
        else if (s.compare(i, 6, "&quot;") == 0) { out.push_back('"'); i += 6; }
        else if (s.compare(i, 6, "&apos;") == 0) { out.push_back('\''); i += 6; }
        else { out.push_back(s[i]); ++i; }
    }
    return out;
}

// ---------------------------------------------------------------- plist
// The XML reader lives in xml_plist.cpp: one parser for every device service.

bool as_u32(const plistxml::value* n, std::uint32_t& out) {
    if (!n) return false;
    std::int64_t v = 0;
    if (!n->as_int(v)) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}

std::string as_string(const plistxml::value* n) {
    return n ? n->as_string() : std::string();
}



}  // namespace

const char* to_string(message m) {
    switch (m) {
        case message::result: return "Result";
        case message::connect: return "Connect";
        case message::listen: return "Listen";
        case message::device_add: return "Attached";
        case message::device_remove: return "Detached";
        case message::device_paired: return "Paired";
        case message::plist: return "Plist";
    }
    return "Unknown";
}

bool encode(const frame& f, bytes& out, std::string& error) {
    // length(4) + version(4) + message(4) + tag(4) + plist_length(4) + payload
    const std::uint64_t total = 20ull + f.payload.size();
    if (total > 0xFFFFFFFFull) { error = "frame maior que o campo de tamanho"; return false; }
    out.clear();
    out.reserve(static_cast<std::size_t>(total));
    put_u32(out, static_cast<std::uint32_t>(total));
    put_u32(out, f.version);
    put_u32(out, static_cast<std::uint32_t>(f.type));
    put_u32(out, f.tag);
    put_u32(out, static_cast<std::uint32_t>(f.payload.size()));
    out.insert(out.end(), f.payload.begin(), f.payload.end());
    return true;
}

bool decode(const std::uint8_t* data, std::size_t len, frame& out, std::size_t& consumed,
            std::string& error) {
    consumed = 0;
    if (len < 20) { error = "frame menor que o cabecalho"; return false; }
    const std::uint32_t total = read_u32(data);
    if (total < 20 || total > len) { error = "tamanho de frame inconsistente"; return false; }
    out.version = read_u32(data + 4);
    out.type = static_cast<message>(read_u32(data + 8));
    out.tag = read_u32(data + 12);
    const std::uint32_t payload_len = read_u32(data + 16);
    if (static_cast<std::uint64_t>(20) + payload_len > total) {
        error = "plist maior que o frame declarado";
        return false;
    }
    out.payload.assign(reinterpret_cast<const char*>(data + 20), payload_len);
    consumed = total;
    return true;
}

std::string connect_request(std::uint32_t device_id, std::uint16_t port) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<plist version=\"1.0\"><dict>"
           "<key>MessageType</key><string>Connect</string>"
           "<key>ClientVersionString</key><string>OlhoDeDeus/1.0</string>"
           "<key>ProgName</key><string>OlhoDeDeus</string>"
           "<key>DeviceID</key><integer>" + std::to_string(device_id) + "</integer>"
           "<key>PortNumber</key><integer>" + std::to_string(port) + "</integer>"
           "</dict></plist>";
}

std::string list_devices_request() {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<plist version=\"1.0\"><dict>"
           "<key>MessageType</key><string>ListDevices</string>"
           "<key>ClientVersionString</key><string>OlhoDeDeus/1.0</string>"
           "<key>ProgName</key><string>OlhoDeDeus</string>"
           "</dict></plist>";
}

bool parse_device_list(const std::string& plist, std::vector<device_entry>& out, std::string& error) {
    out.clear();
    plistxml::value root;
    
    if (!plistxml::parse(plist, root, error)) return false;
    const plistxml::value* list = root.find("DeviceList");
    if (!list || list->k != plistxml::value::kind::array) { error = "resposta sem DeviceList"; return false; }
    for (const auto& item : list->elements) {
        if (item.k != plistxml::value::kind::dict) continue;
        device_entry e;
        as_u32(item.find("DeviceID"), e.device_id);
        e.serial = as_string(item.find("SerialNumber"));
        e.connection = as_string(item.find("ConnectionType"));
        std::uint32_t pid = 0;
        as_u32(item.find("ProductID"), pid);
        e.product_id = static_cast<std::uint16_t>(pid);
        e.location = as_string(item.find("LocationID"));
        out.push_back(std::move(e));
    }
    return true;
}

bool parse_result(const std::string& plist, std::uint32_t& result, std::uint16_t& port,
                  std::string& error) {
    result = 0xFFFFFFFFu;
    port = 0;
    plistxml::value root;
    
    if (!plistxml::parse(plist, root, error)) return false;
    if (!as_u32(root.find("Result"), result)) { error = "resposta sem Result"; return false; }
    std::uint32_t p = 0;
    if (as_u32(root.find("PortNumber"), p)) port = static_cast<std::uint16_t>(p);
    return true;
}

}  // namespace olho::usbmux