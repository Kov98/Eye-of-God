// language: C++, file: xml_plist.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "xml_plist.hpp"

#include <cstdlib>
#include <cstring>

namespace olho::plistxml {
namespace {

std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s.compare(i, 5, "&amp;") == 0) { out.push_back('&'); i += 5; }
        else if (s.compare(i, 4, "&lt;") == 0) { out.push_back('<'); i += 4; }
        else if (s.compare(i, 4, "&gt;") == 0) { out.push_back('>'); i += 4; }
        else if (s.compare(i, 6, "&quot;") == 0) { out.push_back('"'); i += 6; }
        else if (s.compare(i, 6, "&apos;") == 0) { out.push_back('\''); i += 6; }
        else if (s.compare(i, 5, "&#38;") == 0) { out.push_back('&'); i += 5; }
        else { out.push_back(s[i]); ++i; }
    }
    return out;
}

class reader {
public:
    explicit reader(std::string text) : s_(std::move(text)) {}

    bool parse_document(value& out, std::string& error) {
        skip_prolog();
        std::string tag;
        if (!open_tag(tag)) { error = "sem <plist>"; return false; }
        if (tag != "plist") { error = "documento nao comeca com <plist>"; return false; }
        skip_prolog();
        if (!parse_value(out, error)) return false;
        skip_prolog();
        if (!close_tag("plist")) { error = "</plist> ausente"; return false; }
        return true;
    }

private:
    void skip_prolog() {
        for (;;) {
            const std::size_t lt = s_.find('<', pos_);
            if (lt == std::string::npos) { pos_ = s_.size(); return; }
            if (s_.compare(lt, 2, "<?") == 0) { pos_ = after(lt, "?>"); continue; }
            if (s_.compare(lt, 4, "<!--") == 0) { pos_ = after(lt, "-->"); continue; }
            if (s_.compare(lt, 9, "<!DOCTYPE") == 0 || s_.compare(lt, 2, "<!") == 0) {
                pos_ = after(lt, ">");
                continue;
            }
            pos_ = lt;
            return;
        }
    }

    std::size_t after(std::size_t from, const char* term) {
        const std::size_t at = s_.find(term, from);
        return at == std::string::npos ? s_.size() : at + std::strlen(term);
    }

    bool open_tag(std::string& name) {
        while (pos_ < s_.size() && s_[pos_] != '<') ++pos_;
        if (pos_ >= s_.size()) return false;
        const std::size_t end = s_.find('>', pos_);
        if (end == std::string::npos) return false;
        std::string inner = s_.substr(pos_ + 1, end - pos_ - 1);
        pos_ = end + 1;
        if (!inner.empty() && inner[0] == '/') return false;
        self_closing_ = false;
        if (!inner.empty() && inner.back() == '/') { inner.pop_back(); self_closing_ = true; }
        const std::size_t sp = inner.find_first_of(" \t\r\n");
        name = sp == std::string::npos ? inner : inner.substr(0, sp);
        return !name.empty();
    }

    bool close_tag(const char* name) {
        const std::string term = std::string("</") + name;
        const std::size_t at = s_.find(term, pos_);
        if (at == std::string::npos) return false;
        const std::size_t end = s_.find('>', at);
        if (end == std::string::npos) return false;
        pos_ = end + 1;
        return true;
    }

    bool text_until(const char* close, std::string& out) {
        const std::string term = std::string("</") + close;
        const std::size_t at = s_.find(term, pos_);
        if (at == std::string::npos) return false;
        out = unescape(s_.substr(pos_, at - pos_));
        const std::size_t end = s_.find('>', at);
        if (end == std::string::npos) return false;
        pos_ = end + 1;
        return true;
    }

    bool parse_value(value& out, std::string& error) {
        skip_prolog();
        std::string tag;
        const std::size_t save = pos_;
        if (!open_tag(tag)) { pos_ = save; error = "tag inesperada"; return false; }

        // <true/> and <false/> are self-closing AND carry meaning. Testing
        // self_closing_ before the tag name turned every boolean into an empty
        // string, which silently disabled PasswordProtected and EnableSessionSSL.
        if (tag == "true" || tag == "false") {
            out.k = value::kind::boolean;
            out.flag = (tag == "true");
            return true;
        }
        if (self_closing_) { out.k = value::kind::string; return true; }

        if (tag == "dict") {
            out.k = value::kind::dict;
            for (;;) {
                skip_prolog();
                std::size_t probe = pos_;
                while (probe < s_.size() && s_[probe] != '<') ++probe;
                if (s_.compare(probe, 2, "</") == 0) break;
                std::string key_tag;
                const std::size_t mark = pos_;
                if (!open_tag(key_tag) || key_tag != "key") { pos_ = mark; break; }
                std::string key;
                if (!text_until("key", key)) { error = "key sem fechamento"; return false; }
                skip_prolog();
                value child;
                if (!parse_value(child, error)) return false;
                out.items.emplace_back(std::move(key), std::move(child));
            }
            return close_tag("dict");
        }
        if (tag == "array") {
            out.k = value::kind::array;
            for (;;) {
                skip_prolog();
                std::size_t probe = pos_;
                while (probe < s_.size() && s_[probe] != '<') ++probe;
                if (s_.compare(probe, 2, "</") == 0) break;
                value child;
                const std::size_t mark = pos_;
                if (!parse_value(child, error)) { pos_ = mark; break; }
                out.elements.push_back(std::move(child));
            }
            return close_tag("array");
        }
        if (tag == "string") { out.k = value::kind::string; return text_until("string", out.text); }
        if (tag == "data")   { out.k = value::kind::data;   return text_until("data", out.text); }
        if (tag == "integer") {
            std::string body;
            if (!text_until("integer", body)) { error = "integer sem fechamento"; return false; }
            out.k = value::kind::integer;
            out.number = std::strtoll(body.c_str(), nullptr, 10);
            return true;
        }
        if (tag == "real") {
            std::string body;
            if (!text_until("real", body)) { error = "real sem fechamento"; return false; }
            out.k = value::kind::real;
            out.real = std::strtod(body.c_str(), nullptr);
            return true;
        }
        error = "tipo de plist nao suportado: " + tag;
        return false;
    }

    std::string s_;
    std::size_t pos_ = 0;
    bool self_closing_ = false;
};

}  // namespace

const value* value::find(const std::string& key) const {
    for (const auto& kv : items) if (kv.first == key) return &kv.second;
    return nullptr;
}

std::string value::as_string() const {
    if (k == kind::string || k == kind::data) return text;
    if (k == kind::integer) return std::to_string(number);
    if (k == kind::real) return std::to_string(real);
    if (k == kind::boolean) return flag ? "true" : "false";
    return {};
}

bool value::as_int(std::int64_t& out) const {
    if (k == kind::integer) { out = number; return true; }
    if (k == kind::string && !text.empty()) {
        char* end = nullptr;
        const long long v = std::strtoll(text.c_str(), &end, 10);
        if (end && *end == '\0') { out = v; return true; }
    }
    return false;
}

bool parse(const std::string& text, value& out, std::string& error) {
    reader r(text);
    return r.parse_document(out, error);
}

std::string escape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 8);
    for (char c : raw) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string doc_open() {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"
           "<plist version=\"1.0\"><dict>";
}

std::string doc_close() { return "</dict></plist>"; }

std::string entry_string(const std::string& key, const std::string& val) {
    return "<key>" + escape(key) + "</key><string>" + escape(val) + "</string>";
}

std::string entry_integer(const std::string& key, std::int64_t val) {
    return "<key>" + escape(key) + "</key><integer>" + std::to_string(val) + "</integer>";
}

std::string entry_true(const std::string& key) {
    return "<key>" + escape(key) + "</key><true/>";
}

}  // namespace olho::plistxml
