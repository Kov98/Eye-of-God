// language: C++, file: tsa_client.hpp, target: Windows 11 (MSVC / MinGW g++)
// RFC 3161 timestamping for the laudo signature.
//
// A signature says "these bytes have not changed". It does NOT say when it was
// made: whoever holds the key can backdate or forward-date it. The timestamp
// token is what pins the signature to a moment, and it is what a court asks for
// when the date is contested.
//
// The network endpoint is isolated in stamp_remote(); everything else is pure
// and testable, which is why a local issuer (local_tsa) can prove the whole
// chain without internet.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::tsa {

using bytes = std::vector<std::uint8_t>;

struct status {
    bool ok = false;
    int code = 0;          // PKIStatus: 0 granted, 1 grantedWithMods, 2 rejection, ...
    std::string text;
};

// SHA-256 helper, so callers do not re-implement the digest the imprint needs.
bytes sha256(const bytes& data);

// TimeStampReq over a digest. RFC 3161 asks for a nonce so the reply cannot be
// replayed; it is returned so the caller can check it.
bytes build_request(const bytes& imprint, std::uint64_t nonce, bool request_certificate);

// Pulls the token out of a TimeStampResp. A rejection is reported as such, with
// the PKIStatus, instead of leaving the caller with an empty token.
status parse_response(const bytes& response, bytes& token, std::uint64_t expected_nonce);

// Verifies the token: CMS signature valid, TSTInfo messageImprint equals the
// digest we asked about, and the timestamp is inside the token.
status verify_token(const bytes& token, const bytes& imprint, std::string& gen_time,
                    std::string& signer_subject);

// POSTs the request. The only function that touches the network.
status stamp_remote(const std::string& url, const bytes& request, bytes& response);

// Issues the response locally with the certificate in the MY store. This exists
// so the request/response/token path can be proven on a machine with no TSA
// reachable; it is NOT a timestamp authority anyone should trust, and the token
// it produces says nothing about the time being honest. Callers must label it.
status stamp_local_selftest(const bytes& request, bytes& response, std::string& issuer);

}  // namespace olho::tsa