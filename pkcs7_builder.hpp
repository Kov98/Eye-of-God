// language: C++, file: pkcs7_builder.hpp, target: Windows 11 (MSVC / MinGW g++)
// Builds a detached PKCS#7 / CMS SignedData by hand.
//
// WHY BY HAND
//   CryptSignMessage is the legacy wrapper and cannot drive a CNG (NCrypt) key:
//   it produces a blob the platform then refuses to verify (CRYPT_E_NOT_FOUND),
//   measured on a 25-byte message with SHA-1, SHA-256 and SHA-1-RSA alike. NCrypt
//   itself signs correctly with explicit PKCS#1 padding. This module writes the
//   container, RFC 5652 shape, over a signature produced by NCrypt.
//
// WHY IT IS SPLIT IN THREE STEPS
//   A timestamp authority signs the SIGNATURE, and the token then goes inside the
//   SignerInfo as an unsigned attribute. That means the signature must exist
//   before the container is final, so prepare/sign/assemble are separate.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::pkcs7 {

using bytes = std::vector<std::uint8_t>;

struct partial {
    bytes attrs_der;       // DER of the signed attributes (SET, tag 0x31)
    bytes attrs_digest;    // SHA-256 of attrs_der: what actually gets signed
    bytes content_digest;  // SHA-256 of the content being signed
};

bool pick_signing_certificate(const std::string& subject_filter, void** cert_out,
                              std::string& subject, std::string& thumbprint);
void free_certificate(void* cert);

// Step 1: build the signed attributes (contentType, messageDigest, signingTime).
bool prepare(void* cert, const bytes& content, partial& out, std::string& error);

// Step 2: sign a digest with the certificate key (CNG, PKCS#1 padding).
bool sign_digest(void* cert, const bytes& digest, bytes& signature, std::string& error);

// Step 3: assemble the CMS. `timestamp_token` may be empty; when present it rides
// in the SignerInfo unsignedAttrs as id-aa-signatureTimeStampToken.
bool assemble(void* cert, const partial& p, const bytes& signature,
              const bytes& timestamp_token, bytes& der, std::string& error);

// One-shot convenience (no timestamp).
bool sign_detached(void* cert, const std::vector<std::uint8_t>& content,
                   std::vector<std::uint8_t>& der, std::string& error);

// TSA side: wraps a TSTInfo into a CMS token signed by `cert`, for the local
// issuer used to prove the client without a public endpoint.
bool build_timestamp_token(void* cert, const bytes& tstinfo_der, bytes& token,
                           std::string& error);

}  // namespace olho::pkcs7