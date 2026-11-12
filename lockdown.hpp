// language: C++, file: lockdown.hpp, target: Windows 11 (MSVC / MinGW g++)
// lockdownd: the service that tells us what the device is and hands out sessions.
//
// WHERE THIS SITS
//   usbmux connects to port 62078 and hands back a byte pipe. lockdownd speaks XML
//   plists over that pipe: ask a question, get a Value or an Error. Once a session
//   starts, the device may demand TLS on the same pipe (EnableSessionSSL), and
//   every service behind lockdown (AFC, installation, syslog) requires that
//   session first.
//
// WHAT IS PROVEN HERE
//   Request building, response parsing and the session state decisions are pure
//   and covered by tests. The TLS step is a separate module and, like the USB
//   transfer, has never run against a real handset: there is no iPhone on this
//   machine.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::lockdown {

struct device_info {
    std::string unique_device_id;    // UniqueDeviceID
    std::string device_name;         // DeviceName
    std::string product_type;        // ProductType, e.g. iPhone12,1
    std::string product_version;     // ProductVersion
    std::string build_version;       // BuildVersion
    std::string hardware_model;      // HardwareModel
    std::string cpu_architecture;    // CPUArchitecture
    std::string model_number;        // ModelNumber
    std::string serial_number;       // SerialNumber
    std::string wifi_mac;            // WiFiAddress
    std::string bluetooth_mac;       // BluetoothAddress
    bool password_protected = false;// PasswordProtected
    bool paired = true;              // cleared when the device answers PairingDialogResponsePending
};

struct response {
    bool ok = false;
    int error_code = 0;              // lockdownd Error codes, when present
    std::string error_text;          // Error or a transport message
    std::string raw;                 // the plist, for the evidence log
};

// The label every request carries; it must be identical across a session.
const char* default_label();

// ---- request builders -------------------------------------------------------
std::string query_type_request(const std::string& label);
std::string start_session_request(const std::string& label, const std::string& host_id);
std::string stop_session_request(const std::string& label, const std::string& session_id);
// Requests a value from the device ("com.apple.mobile.activation" and friends).
std::string query_value_request(const std::string& label);

// ---- response parsing -------------------------------------------------------
// A reply either carries a Value dictionary or an Error string; both are handled
// so a refusal is reported as a refusal instead of an empty success.
bool parse_reply(const std::string& plist, response& out);

// Fills device_info from a QueryType reply.
bool parse_device_info(const std::string& plist, device_info& out, response& status);

struct session {
    std::string session_id;          // SessionID from StartSession
    bool enable_ssl = false;         // EnableSessionSSL
};

typedef enum {
    ssl_required,
    ssl_not_required,
} ssl_decision;

bool parse_session(const std::string& plist, session& out, response& status);

}  // namespace olho::lockdown
