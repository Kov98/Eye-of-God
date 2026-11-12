// language: C++, file: forensic_report.hpp, target: Windows 11 (MSVC / MinGW g++)
// Forensic report generator: a signed-able PDF with the case, the acquisition
// provenance, every evidence hash and what was recovered.
//
// WHY A HAND-WRITTEN PDF
//   A report attaches no external toolchain at runtime. PDF 1.4 with the base-14
//   fonts needs no embedded font program and no compression, so the binary stays
//   a single exe. What it costs is that every offset in the xref is ours to get
//   right — which is why the conformance checker lives next door and is written
//   by someone else.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::report {

struct evidence_item {
    std::string path;        // as it appears in the case directory
    std::string sha256;      // lowercase hex, 64 chars
    std::uint64_t size = 0;
    std::string source;      // whatsapp / ios / android / disk
};

struct recovered_item {
    std::string kind;        // "mensagem apagada", "midia", "arquivo"
    std::string detail;      // human-readable, may be long
    std::string sha256;      // optional; empty when the item is a row, not a file
    std::string origin;      // freelist / freeblock / orphan / vivo
};

struct case_info {
    std::string case_id;
    std::string operator_name;
    std::string legal_basis;      // warrant number, consent term, ...
    std::string device;
    std::string generated_at;     // ISO-8601, caller supplies (no clock guessing here)
    std::string tool_version;
    std::string tool_build;       // build stamp / commit
};

struct document {
    case_info info;
    std::vector<evidence_item> evidence;
    std::vector<recovered_item> recovered;
    std::vector<std::string> notes;

    // Counts carried on the summary page. Kept as numbers, not prose, so the
    // report cannot claim a figure the caller did not measure.
    std::uint64_t deleted_messages = 0;
    std::uint64_t media_files = 0;
    std::uint64_t wal_frames = 0;
    std::uint64_t pages_decrypted = 0;
};

struct signer_options {
    bool enabled = false;
    // Substring matched against the certificate subject in the current user's
    // MY store. Empty picks the first certificate that actually has a private
    // key — a public-only certificate cannot sign and must not be offered.
    std::string subject_filter;

    // RFC 3161 endpoint. Without a timestamp a signature proves the bytes did not
    // change, not WHEN they were signed.
    std::string tsa_url;

    // Issues the token locally with the same certificate. Proves the request /
    // response / token path on a machine with no TSA reachable. The token it
    // produces is NOT trusted by anyone and is labelled as such.
    bool tsa_selftest = false;
};

// Result of the signing pass, for the report itself and for the operator.
struct signature_info {
    bool signed_pdf = false;
    std::string subject;        // certificate subject actually used
    std::string issuer;
    std::string thumbprint;     // SHA-1 hex of the certificate
    std::string digest;         // SHA-256 hex of the signed byte ranges
    std::string signed_at;
    bool timestamped = false;
    std::string timestamp_time;   // genTime from the token
    std::string tsa_subject;
    std::string error;
};

// Writes an A4 PDF. Returns false and fills error on any failure; no partial
// file is left behind on failure.
bool write_pdf(const document& doc, const std::string& path, std::string& error);

// Same, optionally embedding a PKCS#7 detached signature over the byte ranges.
// When signing is requested and fails, the PDF is still written unsigned and
// *info records why: a laudo that silently loses its signature is worse than
// one that says it has none.
bool write_pdf_signed(const document& doc, const std::string& path,
                      const signer_options& opts, signature_info* info,
                      std::string& error);

}  // namespace olho::report
