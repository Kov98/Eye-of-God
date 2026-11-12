// language: C++, file: xml_plist.hpp, target: Windows 11 (MSVC / MinGW g++)
// The XML plist dialect Apple's device services speak.
//
// WHY THIS IS ITS OWN MODULE
//   usbmux, lockdown and every service behind it exchange XML plists. Keeping one
//   reader means a bug fixed here is fixed everywhere, and the device-facing layers
//   stay free of a property-list dependency.
//
// SCOPE, STATED
//   dict, array, key, string, integer, real, true, false, data. That is what the
//   lockdownd handshake actually uses. Anything else is a reported error, never a
//   silent default — a parser that guesses is worse than one that refuses.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace olho::plistxml {

struct value {
    enum class kind { dict, array, string, integer, real, boolean, data } k = kind::string;

    std::string text;                                    // string, or base64 for data
    std::int64_t number = 0;                             // integer
    double real = 0.0;                                   // real
    bool flag = false;                                   // boolean
    std::vector<std::pair<std::string, value>> items;    // dict
    std::vector<value> elements;                         // array

    const value* find(const std::string& key) const;
    std::string as_string() const;      // string, or the decimal form of an integer
    bool as_int(std::int64_t& out) const;
};

// Parses a complete plist document. Tolerates the XML prolog, DOCTYPE and
// comments; refuses unknown element types.
bool parse(const std::string& text, value& out, std::string& error);

// ---- writing ---------------------------------------------------------------
// The requests below are small and fixed in shape, so they are built directly
// rather than through a tree API. The helpers exist so escaping is never forgotten.
std::string escape(const std::string& raw);
std::string doc_open();          // prolog + <plist><dict>
std::string doc_close();         // </dict></plist>
std::string entry_string(const std::string& key, const std::string& val);
std::string entry_integer(const std::string& key, std::int64_t val);
std::string entry_true(const std::string& key);

}  // namespace olho::plistxml
