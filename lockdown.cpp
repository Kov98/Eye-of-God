// language: C++, file: lockdown.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "lockdown.hpp"

#include "xml_plist.hpp"

#include <cstdlib>

namespace olho::lockdown {
namespace {

std::string bool_text(bool v) { return v ? "true" : "false"; }

const plistxml::value* find_ci(const plistxml::value* dict, const char* key) {
    return dict ? dict->find(key) : nullptr;
}

}  // namespace

const char* default_label() { return "OlhoDeDeus"; }

std::string query_type_request(const std::string& label) {
    return plistxml::doc_open() +
           plistxml::entry_string("Label", label) +
           plistxml::entry_string("Request", "QueryType") +
           plistxml::doc_close();
}

std::string query_value_request(const std::string& label) {
    return plistxml::doc_open() +
           plistxml::entry_string("Label", label) +
           plistxml::entry_string("Request", "GetValue") +
           plistxml::doc_close();
}

std::string start_session_request(const std::string& label, const std::string& host_id) {
    std::string body = plistxml::doc_open();
    body += plistxml::entry_string("Label", label);
    body += plistxml::entry_string("Request", "StartSession");
    if (!host_id.empty()) body += plistxml::entry_string("HostID", host_id);
    body += plistxml::doc_close();
    return body;
}

std::string stop_session_request(const std::string& label, const std::string& session_id) {
    std::string body = plistxml::doc_open();
    body += plistxml::entry_string("Label", label);
    body += plistxml::entry_string("Request", "StopSession");
    if (!session_id.empty()) body += plistxml::entry_string("SessionID", session_id);
    body += plistxml::doc_close();
    return body;
}

bool parse_reply(const std::string& plist, response& out) {
    out = response{};
    out.raw = plist;

    plistxml::value root;
    std::string error;
    if (!plistxml::parse(plist, root, error)) {
        out.error_text = "resposta nao e plist: " + error;
        return false;
    }
    if (const plistxml::value* err = root.find("Error")) {
        out.error_text = err->as_string();
        const plistxml::value* code = root.find("ErrorCode");
        std::int64_t value = 0;
        if (code && code->as_int(value)) out.error_code = static_cast<int>(value);
        return false;
    }
    out.ok = true;
    return true;
}

bool parse_device_info(const std::string& plist, device_info& out, response& status) {
    out = device_info{};
    if (!parse_reply(plist, status)) return false;

    plistxml::value root;
    std::string error;
    if (!plistxml::parse(plist, root, error)) {
        status.error_text = "plist invalido: " + error;
        return false;
    }
    const plistxml::value* value = root.find("Value");
    if (!value || value->k != plistxml::value::kind::dict) {
        status.ok = false;
        status.error_text = "resposta sem Value";
        return false;
    }

    auto grab = [value](const char* key) {
        const plistxml::value* f = value->find(key);
        return f ? f->as_string() : std::string();
    };
    out.unique_device_id = grab("UniqueDeviceID");
    out.device_name = grab("DeviceName");
    out.product_type = grab("ProductType");
    out.product_version = grab("ProductVersion");
    out.build_version = grab("BuildVersion");
    out.hardware_model = grab("HardwareModel");
    out.cpu_architecture = grab("CPUArchitecture");
    out.model_number = grab("ModelNumber");
    out.serial_number = grab("SerialNumber");
    out.wifi_mac = grab("WiFiAddress");
    out.bluetooth_mac = grab("BluetoothAddress");

    if (const plistxml::value* pp = find_ci(value, "PasswordProtected")) {
        out.password_protected = (pp->k == plistxml::value::kind::boolean) ? pp->flag
                                                                          : (pp->as_string() == "true");
    }
    // A device that still wants a pairing dialog cannot be treated as paired.
    if (const plistxml::value* pending = root.find("PairingDialogResponsePending")) {
        if (pending->k == plistxml::value::kind::boolean && pending->flag) out.paired = false;
    }
    return true;
}

bool parse_session(const std::string& plist, session& out, response& status) {
    out = session{};
    if (!parse_reply(plist, status)) return false;

    plistxml::value root;
    std::string error;
    if (!plistxml::parse(plist, root, error)) {
        status.error_text = "plist invalido: " + error;
        return false;
    }
    const plistxml::value* value = root.find("Value");
    if (!value || value->k != plistxml::value::kind::dict) {
        status.ok = false;
        status.error_text = "resposta sem Value";
        return false;
    }
    if (const plistxml::value* id = value->find("SessionID")) out.session_id = id->as_string();
    if (const plistxml::value* ssl = value->find("EnableSessionSSL")) {
        out.enable_ssl = (ssl->k == plistxml::value::kind::boolean) ? ssl->flag
                                                                   : (ssl->as_string() == "true");
    }
    if (out.session_id.empty()) {
        status.ok = false;
        status.error_text = "StartSession sem SessionID";
        return false;
    }
    return true;
}

}  // namespace olho::lockdown
