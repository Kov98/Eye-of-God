#include "tsa_client.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <string>

static olho::tsa::bytes sha256(const olho::tsa::bytes& d) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    olho::tsa::bytes out(32);
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    ULONG obj = 0, w = 0, hl = 0;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj, sizeof(obj), &w, 0);
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hl, sizeof(hl), &w, 0);
    olho::tsa::bytes object(obj);
    out.assign(hl, 0);
    BCryptCreateHash(alg, &h, object.data(), obj, nullptr, 0, 0);
    BCryptHashData(h, (PUCHAR)d.data(), (ULONG)d.size(), 0);
    BCryptFinishHash(h, out.data(), hl, 0);
    BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

int main() {
    const std::string sig_text = "assinatura do laudo";
    olho::tsa::bytes signature(sig_text.begin(), sig_text.end());
    const olho::tsa::bytes imprint = sha256(signature);

    const std::uint64_t nonce = 0x1122334455667788ull;
    const olho::tsa::bytes request = olho::tsa::build_request(imprint, nonce, true);
    std::printf("requisicao: %zu bytes\n", request.size());

    olho::tsa::bytes response;
    std::string issuer;
    auto issued = olho::tsa::stamp_local_selftest(request, response, issuer);
    std::printf("emissao: ok=%d %s (emissor %s)\n", (int)issued.ok, issued.text.c_str(), issuer.c_str());
    if (!issued.ok) return 1;
    std::printf("resposta: %zu bytes\n", response.size());

    olho::tsa::bytes token;
    auto parsed = olho::tsa::parse_response(response, token, nonce);
    std::printf("parse: ok=%d %s (token %zu bytes)\n", (int)parsed.ok, parsed.text.c_str(), token.size());
    if (!parsed.ok) return 1;

    std::string gen, subject;
    auto verified = olho::tsa::verify_token(token, imprint, gen, subject);
    std::printf("verificacao: ok=%d %s\n  genTime=%s\n  signatario=%s\n",
                (int)verified.ok, verified.text.c_str(), gen.c_str(), subject.c_str());

    olho::tsa::bytes wrong = imprint;
    wrong[0] ^= 0xFF;
    std::string g2, s2;
    auto bad = olho::tsa::verify_token(token, wrong, g2, s2);
    std::printf("imprint errado: ok=%d %s\n", (int)bad.ok, bad.text.c_str());
    return verified.ok && !bad.ok ? 0 : 1;
}