// language: C++, file: forensic_report.cpp, target: Windows 11 (MSVC / MinGW g++)
// Hand-written PDF 1.4 report. See forensic_report.hpp for the reasoning.
#include "forensic_report.hpp"

#include "pkcs7_builder.hpp"
#include "tsa_client.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

namespace olho::report {
namespace {

constexpr double kPageW = 595.0;
constexpr double kPageH = 842.0;
constexpr double kMargin = 50.0;
constexpr double kBody = 10.0;
constexpr double kLine = 14.0;
constexpr double kSmall = 8.0;
constexpr double kMono = 7.0;

enum class face { regular = 0, bold = 1, mono = 2 };

// Advance width of one byte in 1/1000 em, for the base-14 fonts. Without these
// the wrap points are guesses and long paths run off the page edge.
double glyph_width(unsigned char c, face f) {
    if (f == face::mono) return 600.0;
    if (c < 32 || c > 126) return 556.0;
    static const short helvetica[95] = {
        278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,
        556,556,556,556,556,556,556,556,556,556,
        278,278,584,584,584,556,1015,
        667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,667,
        778,722,667,611,722,667,944,667,667,611,
        278,278,278,469,556,333,
        556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,556,
        556,333,500,278,556,500,722,500,500,500,
        334,260,334,584};
    if (f == face::regular) return helvetica[c - 32];
    static const short bold[95] = {
        278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,
        556,556,556,556,556,556,556,556,556,556,
        333,333,584,584,584,611,975,
        722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,667,
        778,722,667,611,722,667,944,667,667,611,
        333,278,333,584,556,333,
        556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,611,
        611,389,556,333,611,556,778,556,556,500,
        389,280,389,584};
    return bold[c - 32];
}

double text_width(const std::string& s, face f, double size) {
    double w = 0;
    for (unsigned char c : s) w += glyph_width(c, f);
    return w * size / 1000.0;
}

// UTF-8 -> WinAnsi. The base-14 fonts address bytes, not code points, and a raw
// UTF-8 accent would render as two wrong glyphs. Unmappable input becomes '?'
// rather than corrupting the rest of the line.
std::string to_winansi(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) { out.push_back(static_cast<char>(c)); ++i; continue; }
        // Decode one UTF-8 sequence.
        std::size_t len = 0;
        unsigned cp = 0;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else { out.push_back('?'); ++i; continue; }
        if (i + len > utf8.size()) { out.push_back('?'); ++i; continue; }
        bool valid = true;
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(utf8[i + k]);
            if ((cc & 0xC0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!valid) { out.push_back('?'); ++i; continue; }
        i += len;
        // WinAnsi is Latin-1 for our purposes, plus a handful of typographic marks.
        if (cp == 0x2014) out.push_back(static_cast<char>(0x97));        // em dash
        else if (cp == 0x2013) out.push_back(static_cast<char>(0x96));   // en dash
        else if (cp == 0x201C) out.push_back(static_cast<char>(0x93));
        else if (cp == 0x201D) out.push_back(static_cast<char>(0x94));
        else if (cp == 0x2018) out.push_back(static_cast<char>(0x91));
        else if (cp == 0x2019) out.push_back(static_cast<char>(0x92));
        else if (cp <= 0xFF) out.push_back(static_cast<char>(cp));
        else out.push_back('?');
    }
    return out;
}

// Literal string escaping: backslash, both parens, and anything non-printable as
// octal so a stray byte cannot break the tokenizer.
std::string pdf_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if (c == '\\' || c == '(' || c == ')') { out.push_back('\\'); out.push_back(static_cast<char>(c)); }
        else if (c < 32 || c > 126) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\%03o", c);
            out += buf;
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

std::string fmt_double(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    // Trim trailing zeros so the content stream stays readable.
    std::string s(buf);
    while (s.size() > 1 && s.back() == '0' && s.find('.') != std::string::npos) s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string human_size(std::uint64_t bytes) {
    char buf[64];
    if (bytes < 1024) std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    else if (bytes < 1024ull * 1024) std::snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else if (bytes < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    return std::string(buf);
}

// Document builder: pages are op lists, serialized once at the end so the xref
// offsets are computed from real byte positions and never guessed.
class pdf_doc {
public:
    pdf_doc() { pages_.emplace_back(); }

    void text(double x, double y, const std::string& utf8, face f, double size) {
        const std::string s = to_winansi(utf8);
        if (s.empty()) return;
        // Built with std::string, not a fixed buffer: a full-width line is longer
        // than any stack buffer worth choosing, and truncation here silently eats
        // the closing "Tj" so the text swallows the next operator.
        std::string op;
        op.reserve(s.size() + 64);
        op += "BT /";
        op += font_name(f);
        op += " ";
        op += fmt_double(size);
        op += " Tf ";
        op += fmt_double(x);
        op += " ";
        op += fmt_double(y);
        op += " Td (";
        op += pdf_escape(s);
        op += ") Tj ET\n";
        pages_.back() += op;
    }

    void rule(double x1, double y1, double x2, double y2, double gray, double width) {
        char op[160];
        std::snprintf(op, sizeof(op), "q %s G %s w %s %s m %s %s l S Q\n",
                      fmt_double(gray).c_str(), fmt_double(width).c_str(),
                      fmt_double(x1).c_str(), fmt_double(y1).c_str(),
                      fmt_double(x2).c_str(), fmt_double(y2).c_str());
        pages_.back() += op;
    }

    void fill(double x, double y, double w, double h, double gray) {
        char op[160];
        std::snprintf(op, sizeof(op), "q %s g %s %s %s %s re f Q\n",
                      fmt_double(gray).c_str(), fmt_double(x).c_str(), fmt_double(y).c_str(),
                      fmt_double(w).c_str(), fmt_double(h).c_str());
        pages_.back() += op;
    }

    void new_page() { pages_.emplace_back(); }
    std::size_t page_count() const { return pages_.size(); }

    // Footers are only known once every page exists, so they are appended after
    // the fact rather than guessed while laying out.
    void append(std::size_t index, const std::string& ops) {
        if (index < pages_.size()) pages_[index] += ops;
    }

    std::string& current() { return pages_.back(); }
    const std::vector<std::string>& pages() const { return pages_; }

private:
    static const char* font_name(face f) {
        switch (f) { case face::bold: return "F2"; case face::mono: return "F3"; default: return "F1"; }
    }
    std::vector<std::string> pages_;
};

// Cursor with automatic page breaks and a repeating header.
class layout {
public:
    explicit layout(pdf_doc& d) : doc_(d) { y_ = kPageH - kMargin; }

    double y() const { return y_; }
    void y(double v) { y_ = v; }

    bool ensure(double needed) {
        if (y_ - needed >= kMargin + 30) return true;
        doc_.new_page();
        y_ = kPageH - kMargin;
        return false;
    }

    void gap(double h) { y_ -= h; }

    void heading(const std::string& s) {
        ensure(26);
        doc_.text(kMargin, y_, s, face::bold, 13);
        y_ -= 6;
        doc_.rule(kMargin, y_, kPageW - kMargin, y_, 0.65, 0.8);
        y_ -= 13;
    }

    void subheading(const std::string& s) {
        ensure(20);
        doc_.text(kMargin, y_, s, face::bold, 10.5);
        y_ -= 14;
    }

    // Wraps by real glyph advance, not by character count.
    //
    // Two rules this has to respect, both learned from a checker that compares the
    // extracted text against the input:
    //   1. Never re-convert. Lines are built from the ORIGINAL UTF-8 and the
    //      conversion happens once, inside text(). Converting here and then again
    //      there turns every accented byte into '?'.
    //   2. A token with no spaces must still break: a 180-char hash otherwise runs
    //      from x=50 to x=1010 on a 595 pt page.
    void paragraph(const std::string& text, face f = face::regular, double size = kBody) {
        const double max_w = kPageW - 2 * kMargin;
        auto width_of = [&](const std::string& s) { return text_width(to_winansi(s), f, size); };

        std::string line;
        std::size_t i = 0;
        while (i <= text.size()) {
            const std::size_t space = text.find(' ', i);
            std::string word = text.substr(i, (space == std::string::npos ? text.size() : space) - i);

            while (width_of(word) > max_w && !word.empty()) {
                if (!line.empty()) {
                    ensure(kLine);
                    doc_.text(kMargin, y_, line, f, size);
                    y_ -= kLine;
                    line.clear();
                }
                std::size_t cut = 0, taken = 0;
                while (cut < word.size()) {
                    const unsigned char c = static_cast<unsigned char>(word[cut]);
                    std::size_t len = 1;
                    if ((c & 0xE0) == 0xC0) len = 2;
                    else if ((c & 0xF0) == 0xE0) len = 3;
                    else if ((c & 0xF8) == 0xF0) len = 4;
                    if (cut + len > word.size()) break;
                    if (width_of(word.substr(0, cut + len)) > max_w && taken > 0) break;
                    cut += len;
                    ++taken;
                }
                if (cut == 0) break;
                ensure(kLine);
                doc_.text(kMargin, y_, word.substr(0, cut), f, size);
                y_ -= kLine;
                word = word.substr(cut);
            }

            const std::string candidate = line.empty() ? word : line + " " + word;
            if (width_of(candidate) > max_w && !line.empty()) {
                ensure(kLine);
                doc_.text(kMargin, y_, line, f, size);
                y_ -= kLine;
                line = word;
            } else {
                line = candidate;
            }
            if (space == std::string::npos) break;
            i = space + 1;
        }
        if (!line.empty()) {
            ensure(kLine);
            doc_.text(kMargin, y_, line, f, size);
            y_ -= kLine;
        }
    }

    void kv(const std::string& key, const std::string& value) {
        ensure(kLine);
        doc_.text(kMargin, y_, key, face::regular, kBody);
        // Value column is derived from the label, not fixed: a hard-coded 130pt
        // gutter collides as soon as a label is longer than "Base legal" and the
        // number lands on top of the word.
        const double label_w = text_width(to_winansi(key), face::regular, kBody);
        const double column = kMargin + std::max(130.0, label_w + 14.0);
        doc_.text(column, y_, value, face::bold, kBody);
        y_ -= kLine;
    }

private:
    pdf_doc& doc_;
    double y_ = 0;
};

// SHA-256 of a byte range, for the record in the report and for the audit trail.
std::string sha256_of(const std::vector<std::uint8_t>& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string out;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return out;
    ULONG obj = 0, written = 0, digest_len = 0;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj), sizeof(obj), &written, 0);
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digest_len), sizeof(digest_len), &written, 0);
    std::vector<std::uint8_t> object(obj);
    std::vector<std::uint8_t> digest(digest_len);
    if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, object.data(), obj, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(data.data()),
                                      static_cast<ULONG>(data.size()), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), digest_len, 0))) {
        static const char* hex = "0123456789abcdef";
        for (auto b : digest) { out.push_back(hex[b >> 4]); out.push_back(hex[b & 15]); }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

std::string now_pdf_date() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tm);
    return std::string("D:") + buf;
}

bool save_pdf(const std::vector<std::string>& pages, const std::string& path,
              const std::string& title, bool sign_slot, const signer_options& opts,
              signature_info* info, std::string& error) {
    std::vector<std::string> objects;
    const int first_page = 6;
    const int page_count = static_cast<int>(pages.size());
    // Object count is known before building the catalog, so the catalog can point
    // at the signature field it has not appended yet.
    const std::size_t info_number = 5 + static_cast<std::size_t>(page_count) * 2 + 1;
    const std::size_t sig_number = info_number + 1;

    std::string kids;
    for (int i = 0; i < page_count; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d 0 R", first_page + i * 2);
        if (i) kids += " ";
        kids += buf;
    }

    std::string catalog = "<< /Type /Catalog /Pages 2 0 R";
    if (sign_slot) {
        catalog += " /AcroForm << /SigFlags 3 /Fields [" + std::to_string(sig_number) + " 0 R] >>";
    }
    catalog += " >>";
    objects.push_back(catalog);
    objects.push_back("<< /Type /Pages /Count " + std::to_string(page_count) + " /Kids [" + kids + "] >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>");

    for (int i = 0; i < page_count; ++i) {
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + fmt_double(kPageW) + " " +
                          fmt_double(kPageH) + "] /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R >> >> "
                          "/Contents " + std::to_string(first_page + i * 2 + 1) + " 0 R >>");
        const std::string& content = pages[static_cast<std::size_t>(i)];
        objects.push_back("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" +
                          content + "endstream");
    }

    objects.push_back("<< /Title (" + pdf_escape(to_winansi(title)) +
                      ") /Producer (Olho de Deus) /Creator (Olho de Deus) >>");

    // Fixed-width placeholders. Every field that will be patched after the fact
    // has a constant width, so patching never shifts a byte offset — which is the
    // one thing a PDF signature cannot survive.
    const std::size_t kHexLen = 8192;     // 4096 bytes of DER
    const std::string kRangeField(20, '0');
    std::string sig_body;
    const std::size_t sig_body_index = objects.size();
    if (sign_slot) {
        sig_body = "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached"
                   " /ByteRange [0 " + kRangeField + " " + kRangeField + " " + kRangeField + "]"
                   " /Contents <" + std::string(kHexLen, '0') + ">"
                   " /Name (" + pdf_escape(to_winansi(title)) + ")"
                   " /M (" + now_pdf_date() + ")"
                   " /Reason (Laudo forense) >>";
        objects.push_back(sig_body);
    }

    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    offsets.reserve(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }

    if (sign_slot) {
        // offsets[] points at "N 0 obj", not at the dictionary: the body starts
        // after the object header, and patching from offsets[] writes into the
        // header and corrupts every following byte.
        const std::size_t body_at = offsets[sig_body_index] +
                                    std::to_string(sig_body_index + 1).size() + std::strlen(" 0 obj\n");
        const std::size_t contents_at = sig_body.find("/Contents <");
        if (contents_at == std::string::npos) { error = "slot de assinatura malformado"; return false; }
        const std::size_t lt_pos = body_at + contents_at + std::strlen("/Contents ");
        const std::size_t excluded = 1 + kHexLen + 1;          // '<' + hex + '>'

        auto patch_range = [&](std::size_t field_off, std::size_t value) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%020llu", (unsigned long long)value);
            // body_at, not offsets[]: offsets[] points at "N 0 obj", and the
            // dictionary starts after that header.
            const std::size_t at = body_at + field_off;
            std::memcpy(&out[at], buf, 20);
        };
        // Offsets of the three fields inside the body.
        const std::size_t r1 = sig_body.find(kRangeField);
        const std::size_t r2 = sig_body.find(kRangeField, r1 + 20);
        // PDF /ByteRange is [start1 length1 start2 length2]: the third value is
        // the OFFSET where signing resumes, not the length of the gap.
        patch_range(r1, lt_pos);
        patch_range(r2, lt_pos + excluded);

        // The third value depends on the final file length, which is not known
        // until the xref is appended — so it is patched after that, below.
        if (info) *info = signature_info{};
    }

    const std::size_t xref_at = out.size();
    out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (std::size_t off : offsets) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010llu 00000 n \n", (unsigned long long)off);
        out += buf;
    }
    out += "trailer\n<< /Size " + std::to_string(objects.size() + 1) +
           " /Root 1 0 R /Info " + std::to_string(info_number) + " 0 R >>\n";
    out += "startxref\n" + std::to_string(xref_at) + "\n%%EOF\n";

    // Now the length is final: patch the third range field and sign.
    if (sign_slot) {
        const std::size_t body_at = offsets[sig_body_index] +
                                    std::to_string(sig_body_index + 1).size() + std::strlen(" 0 obj\n");
        const std::size_t contents_at = sig_body.find("/Contents <");
        const std::size_t lt_pos = body_at + contents_at + std::strlen("/Contents ");
        const std::size_t excluded = 1 + kHexLen + 1;
        const std::size_t range2 = lt_pos + excluded;
        const std::size_t range3 = out.size() - range2;

        const std::size_t r1 = sig_body.find(kRangeField);
        const std::size_t r3 = sig_body.find(kRangeField, sig_body.find(kRangeField, r1 + 20) + 20);
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%020llu", (unsigned long long)range3);
            std::memcpy(&out[body_at + r3], buf, 20);
        }

        // Signed bytes: everything except the /Contents payload.
        std::vector<std::uint8_t> signed_bytes;
        signed_bytes.reserve(out.size());
        signed_bytes.insert(signed_bytes.end(), out.begin(), out.begin() + static_cast<std::ptrdiff_t>(lt_pos));
        signed_bytes.insert(signed_bytes.end(), out.begin() + static_cast<std::ptrdiff_t>(range2), out.end());

        // The signature is built by pkcs7_builder over an NCrypt key: the legacy
        // CryptSignMessage path cannot drive a CNG key and produced a blob the
        // platform refused to verify (CRYPT_E_NOT_FOUND, measured).
        void* cert = nullptr;
        std::string cert_subject, cert_thumb;
        if (!olho::pkcs7::pick_signing_certificate(opts.subject_filter, &cert, cert_subject, cert_thumb)) {
            if (info) info->error = "nenhum certificado com chave privada disponivel no repositorio MY";
            error = info ? info->error : "sem certificado para assinar";
        } else {
            std::string sign_error;
            olho::pkcs7::partial part;
            std::vector<std::uint8_t> signature;
            std::vector<std::uint8_t> token;
            bool ok = olho::pkcs7::prepare(cert, signed_bytes, part, sign_error) &&
                      olho::pkcs7::sign_digest(cert, part.attrs_digest, signature, sign_error);

            // Timestamp the SIGNATURE, not the document: that is what RFC 3161
            // pins, and it is what survives the container being rebuilt.
            if (ok && (opts.tsa_selftest || !opts.tsa_url.empty())) {
                const std::vector<std::uint8_t> imprint = olho::tsa::sha256(signature);
                const std::uint64_t nonce = 0x9E3779B97F4A7C15ull;
                const std::vector<std::uint8_t> request = olho::tsa::build_request(imprint, nonce, true);
                std::vector<std::uint8_t> response;
                olho::tsa::status issued;
                std::string issuer_subject;
                if (opts.tsa_selftest) {
                    issued = olho::tsa::stamp_local_selftest(request, response, issuer_subject);
                } else {
                    issued = olho::tsa::stamp_remote(opts.tsa_url, request, response);
                }
                if (issued.ok) {
                    std::string token_error;
                    auto parsed = olho::tsa::parse_response(response, token, nonce);
                    if (parsed.ok) {
                        std::string gen_time, signer_subject;
                        auto verified = olho::tsa::verify_token(token, imprint, gen_time, signer_subject);
                        if (verified.ok) {
                            if (info) {
                                info->timestamped = true;
                                info->timestamp_time = gen_time;
                                info->tsa_subject = opts.tsa_selftest ? "SELFTEST (nao confiavel)" : signer_subject;
                            }
                        } else {
                            token.clear();
                            if (info) info->error = "token recusado: " + verified.text;
                        }
                    } else {
                        if (info) info->error = parsed.text;
                    }
                } else if (info) {
                    info->error = "TSA: " + issued.text;
                }
            }

            std::vector<std::uint8_t> der;
            if (ok) ok = olho::pkcs7::assemble(cert, part, signature, token, der, sign_error);

            if (!ok) {
                if (info) info->error = sign_error;
                error = sign_error;
            } else if (der.size() * 2 > kHexLen) {
                if (info) info->error = "assinatura maior que o slot reservado";
                error = info->error;
            } else {
                static const char* hex = "0123456789abcdef";
                std::string hexsig;
                hexsig.reserve(der.size() * 2);
                for (std::uint8_t b : der) {
                    hexsig.push_back(hex[b >> 4]);
                    hexsig.push_back(hex[b & 15]);
                }
                hexsig.append(kHexLen - hexsig.size(), (char)48);
                std::memcpy(&out[lt_pos + 1], hexsig.data(), kHexLen);
                if (info) {
                    info->signed_pdf = true;
                    info->subject = cert_subject;
                    info->thumbprint = cert_thumb;
                    info->digest = sha256_of(signed_bytes);
                    info->signed_at = now_pdf_date();
                }
            }
            olho::pkcs7::free_certificate(cert);
        }
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { error = "nao foi possivel criar " + path; return false; }
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
    if (!f) { error = "falha ao escrever " + path; return false; }
    return true;
}

}  // namespace

namespace {

void render_into(const document& doc, pdf_doc& pdf) {
    layout lay(pdf);

    // ---- cover
    lay.y(kPageH - 120);
    pdf.text(kMargin, lay.y(), "OLHO DE DEUS", face::bold, 22);
    lay.gap(24);
    pdf.text(kMargin, lay.y(), "Laudo de aquisicao e recuperacao forense", face::regular, 13);
    lay.gap(30);
    pdf.rule(kMargin, lay.y(), kPageW - kMargin, lay.y(), 0.4, 1.2);
    lay.gap(24);

    lay.kv("Caso", doc.info.case_id);
    lay.kv("Perito", doc.info.operator_name);
    lay.kv("Base legal", doc.info.legal_basis);
    lay.kv("Dispositivo", doc.info.device);
    lay.kv("Emitido em", doc.info.generated_at);
    lay.kv("Ferramenta", doc.info.tool_version);
    lay.kv("Build", doc.info.tool_build);

    lay.gap(18);
    lay.subheading("Resumo");
    lay.kv("Paginas SQLCipher decifradas", std::to_string(doc.pages_decrypted));
    lay.kv("Frames de WAL recuperados", std::to_string(doc.wal_frames));
    lay.kv("Mensagens apagadas recuperadas", std::to_string(doc.deleted_messages));
    lay.kv("Arquivos de midia recuperados", std::to_string(doc.media_files));
    lay.kv("Itens de evidencia", std::to_string(doc.evidence.size()));

    // ---- evidence
    pdf.new_page();
    lay.y(kPageH - kMargin);
    lay.heading("Evidencia adquirida");
    lay.paragraph("Cada item abaixo foi lido em modo somente-leitura. O SHA-256 e do "
                  "conteudo no momento da aquisicao; qualquer alteracao posterior muda o "
                  "hash e invalida o item.", face::regular, kSmall);
    lay.gap(6);

    for (const auto& item : doc.evidence) {
        lay.ensure(30);
        std::string head = item.path;
        if (!item.source.empty()) head = "[" + item.source + "]  " + head;
        pdf.text(kMargin, lay.y(), head, face::regular, kBody);
        const std::string size = human_size(item.size);
        pdf.text(kPageW - kMargin - text_width(size, face::regular, kBody), lay.y(), size,
                 face::regular, kBody);
        lay.gap(kLine - 2);
        pdf.text(kMargin + 10, lay.y(), item.sha256, face::mono, kMono);
        lay.gap(kLine - 3);
    }

    // ---- recovered
    if (!doc.recovered.empty()) {
        lay.heading("Itens recuperados");
        lay.paragraph("Registros que o aplicativo havia removido e que foram reconstruidos "
                      "a partir de espaco liberado do banco. A origem indica onde o registro "
                      "foi encontrado.", face::regular, kSmall);
        lay.gap(6);
        for (const auto& item : doc.recovered) {
            lay.ensure(34);
            std::string head = item.kind;
            if (!item.origin.empty()) head += "  (" + item.origin + ")";
            pdf.text(kMargin, lay.y(), head, face::bold, kBody);
            lay.gap(kLine - 2);
            lay.paragraph(item.detail, face::regular, kSmall);
            if (!item.sha256.empty()) {
                lay.ensure(kLine);
                pdf.text(kMargin + 10, lay.y(), item.sha256, face::mono, kMono);
                lay.gap(kLine - 4);
            }
            lay.gap(4);
        }
    }

    // ---- notes
    if (!doc.notes.empty()) {
        lay.heading("Observacoes e limites");
        for (const auto& note : doc.notes) lay.paragraph("- " + note, face::regular, kSmall);
    }

    // ---- footer on every page, now that the total page count is known
    const std::size_t total = pdf.page_count();
    const std::string case_label = pdf_escape(to_winansi("Caso " + doc.info.case_id));
    for (std::size_t i = 0; i < total; ++i) {
        std::string footer;
        footer += "q 0.6 G 0.5 w 50 36 m 545 36 l S Q\n";
        footer += "BT /F1 8 Tf 50 26 Td (" + case_label + ") Tj ET\n";
        char page_label[64];
        std::snprintf(page_label, sizeof(page_label), "pagina %zu de %zu", i + 1, total);
        const std::string pl = pdf_escape(std::string(page_label));
        const double w = text_width(page_label, face::regular, 8);
        footer += "BT /F1 8 Tf " + fmt_double(kPageW - kMargin - w) + " 26 Td (" + pl + ") Tj ET\n";
        pdf.append(i, footer);
    }

    (void)lay;
}

}  // namespace

bool write_pdf(const document& doc, const std::string& path, std::string& error) {
    pdf_doc pdf;
    render_into(doc, pdf);
    return save_pdf(pdf.pages(), path, doc.info.case_id, false, signer_options{}, nullptr, error);
}

bool write_pdf_signed(const document& doc, const std::string& path,
                      const signer_options& opts, signature_info* info,
                      std::string& error) {
    pdf_doc pdf;
    render_into(doc, pdf);
    return save_pdf(pdf.pages(), path, doc.info.case_id, opts.enabled, opts, info, error);
}

}  // namespace olho::report
