// language: C++, file: .oracle/ctr_test.cpp
#include "case_crypto.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::wstring key_path = (argc > 1) ? std::wstring(argv[1], argv[1] + std::strlen(argv[1]))
                                             : L".oracle/case.key";
    deep::case_cipher cipher;
    std::string error;
    if (!cipher.open_or_create(key_path, error)) {
        std::printf("abrir chave falhou: %s\n", error.c_str());
        return 1;
    }
    std::printf("keystream[0..16]: %s\n", cipher.keystream_probe().c_str());

    int failures = 0;

    // 1. ida e volta
    {
        std::vector<std::uint8_t> data(200);
        for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i);
        const auto original = data;
        cipher.apply(0, data.data(), data.size());
        if (data == original) { std::printf("  [FAIL] apply nao alterou nada\n"); ++failures; }
        else std::printf("  [ok] apply cifra\n");
        cipher.apply(0, data.data(), data.size());
        if (data != original) { std::printf("  [FAIL] ida e volta divergiu\n"); ++failures; }
        else std::printf("  [ok] ida e volta identica\n");
    }

    // 2. buscabilidade: um bloco de uma vez == tres pedacos em offsets diferentes,
    // inclusive desalinhados. E o que permite cifrar journal e WAL sem saber nada
    // sobre a estrutura deles.
    {
        std::vector<std::uint8_t> whole(1000, 0x5A);
        std::vector<std::uint8_t> parts(1000, 0x5A);
        cipher.apply(0, whole.data(), whole.size());
        cipher.apply(0, parts.data(), 37);
        cipher.apply(37, parts.data() + 37, 400);
        cipher.apply(437, parts.data() + 437, parts.size() - 437);
        if (whole != parts) { std::printf("  [FAIL] cifra nao e buscavel\n"); ++failures; }
        else std::printf("  [ok] buscavel em offsets arbitrarios\n");
    }

    // 3. offset diferente produz keystream diferente
    {
        std::vector<std::uint8_t> a(16, 0), b(16, 0);
        cipher.apply(0, a.data(), a.size());
        cipher.apply(16, b.data(), b.size());
        if (a == b) { std::printf("  [FAIL] keystream repetiu entre blocos\n"); ++failures; }
        else std::printf("  [ok] keystream muda por bloco\n");
    }

    std::printf(failures == 0 ? "CTR OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}