// Harness de verificacao do modulo iOS. Nao faz parte do produto: vive em
// .oracle_ios/ so para provar o comportamento contra a fixture externa.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "ios_backup.hpp"
#include "ios_keybag.hpp"

using namespace olho::ios;

namespace {

struct Report {
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    void check(bool ok, const std::string& name, const std::string& detail = std::string()) {
        if (ok) {
            ++passed;
            std::printf("  [ok]   %s\n", name.c_str());
        } else {
            ++failed;
            std::string line = name;
            if (!detail.empty()) line += "  (" + detail + ")";
            failures.push_back(line);
            std::printf("  [FAIL] %s\n", line.c_str());
        }
    }
};

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "/" + b;
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

struct ExpectClass {
    std::uint32_t id = 0;
    std::string wpky_hex;
    std::string key_hex;
};

struct ExpectFile {
    std::string id;
    std::uint32_t protection_class = 0;
    std::string layout;
    std::uint64_t size = 0;
    std::string key_hex;
    std::string sha_plain;
    std::string sha_cipher;
    std::string domain;
    std::string relative;
};

struct Expect {
    std::map<std::string, std::string> values;
    std::vector<ExpectClass> classes;
    std::vector<ExpectFile> files;

    std::string get(const std::string& key) const {
        auto it = values.find(key);
        return it == values.end() ? std::string() : it->second;
    }
};

bool load_expect(const std::string& path, Expect& out, std::string& error) {
    Bytes raw;
    if (!read_whole_file(path, raw, error)) return false;
    const std::string text(raw.begin(), raw.end());
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string line =
            text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = (end == std::string::npos) ? text.size() + 1 : end + 1;
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> parts = split(line, '\t');
        if (parts.empty()) continue;
        if (parts[0] == "K" && parts.size() >= 3) {
            out.values[parts[1]] = parts[2];
        } else if (parts[0] == "C" && parts.size() >= 4) {
            ExpectClass item;
            item.id = static_cast<std::uint32_t>(std::stoul(parts[1]));
            item.wpky_hex = parts[2];
            item.key_hex = parts[3];
            out.classes.push_back(item);
        } else if (parts[0] == "F" && parts.size() >= 10) {
            ExpectFile item;
            item.id = parts[1];
            item.protection_class = static_cast<std::uint32_t>(std::stoul(parts[2]));
            item.layout = parts[3];
            item.size = std::stoull(parts[4]);
            item.key_hex = parts[5];
            item.sha_plain = parts[6];
            item.sha_cipher = parts[7];
            item.domain = parts[8];
            item.relative = parts[9];
            out.files.push_back(item);
        }
    }
    return true;
}

bool same_file(const std::string& left, const std::string& right, std::string& error) {
    Bytes a;
    Bytes b;
    std::string ignored;
    if (!read_whole_file(left, a, ignored)) {
        error = "nao consegui ler " + left;
        return false;
    }
    if (!read_whole_file(right, b, ignored)) {
        error = "nao consegui ler " + right;
        return false;
    }
    if (a.size() != b.size()) {
        error = "tamanho difere: " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
        return false;
    }
    if (!a.empty() && std::memcmp(a.data(), b.data(), a.size()) != 0) {
        error = "conteudo difere byte a byte";
        return false;
    }
    return true;
}

std::string hex_of(const Bytes& value) { return bytes_to_hex(value); }

std::string want_first_sha(const Expect& expect) {
    for (const ExpectFile& item : expect.files) {
        if (item.size > 0) return item.sha_plain;
    }
    return std::string();
}

std::size_t count_occurrences(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return 0;
    std::size_t count = 0;
    std::size_t position = haystack.find(needle);
    while (position != std::string::npos) {
        ++count;
        position = haystack.find(needle, position + needle.size());
    }
    return count;
}

// ---------------------------------------------------------------- fixtures
void test_digests(Report& report) {
    std::printf("\n== vetores externos: SHA-256, HMAC, PBKDF2, AES ==\n");

    Bytes digest;
    std::string error;
    report.check(sha256(Bytes{}, digest, error) && hex_of(digest) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256 de string vazia bate com o vetor publicado", hex_of(digest));

    const std::string abc = "abc";
    report.check(sha256(Bytes(abc.begin(), abc.end()), digest, error) && hex_of(digest) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256 de \"abc\" bate com o vetor publicado", hex_of(digest));

    // RFC 4231 caso 2
    const std::string key = "Jefe";
    const std::string data = "what do ya want for nothing?";
    Bytes mac;
    report.check(hmac_sha256(Bytes(key.begin(), key.end()), Bytes(data.begin(), data.end()),
                             mac, error) &&
        hex_of(mac) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
        "hmac_sha256 (RFC 4231 caso 2)", hex_of(mac));

    // PBKDF2-HMAC-SHA256, vetores publicados
    struct KdfVector {
        const char* password;
        const char* salt;
        std::uint32_t iterations;
        const char* want;
    };
    const KdfVector vectors[] = {
        {"password", "salt", 1,
         "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"},
        {"password", "salt", 2,
         "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"},
        {"password", "salt", 4096,
         "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"},
    };
    for (const KdfVector& vector : vectors) {
        const std::string password(vector.password);
        const std::string salt(vector.salt);
        Bytes derived;
        const bool ok = pbkdf2_hmac_sha256(Bytes(password.begin(), password.end()),
                                           Bytes(salt.begin(), salt.end()),
                                           vector.iterations, 32, derived, error) &&
                        hex_of(derived) == vector.want;
        report.check(ok, std::string("pbkdf2_hmac_sha256 it=") +
                             std::to_string(vector.iterations) + " (vetor publicado)",
                     hex_of(derived));
    }

    // RFC 3394 secao 4.1
    Bytes kek;
    Bytes plain;
    Bytes wrapped;
    Bytes recovered;
    bytes_from_hex("000102030405060708090A0B0C0D0E0F", kek);
    bytes_from_hex("00112233445566778899AABBCCDDEEFF", plain);
    const bool wrapped_ok = aes_key_wrap(kek, plain, wrapped, error) &&
                            hex_of(wrapped) ==
                                "1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5";
    report.check(wrapped_ok, "aes_key_wrap (RFC 3394 secao 4.1)", hex_of(wrapped));
    const bool unwrapped_ok = aes_key_unwrap(kek, wrapped, recovered, error) &&
                              hex_of(recovered) == hex_of(plain);
    report.check(unwrapped_ok, "aes_key_unwrap (RFC 3394 secao 4.1)", hex_of(recovered));

    // RFC 3394 secao 4.6 (KEK e chave de 256 bits)
    bytes_from_hex("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F", kek);
    bytes_from_hex("00112233445566778899AABBCCDDEEFF000102030405060708090A0B0C0D0E0F", plain);
    const bool wrapped256_ok =
        aes_key_wrap(kek, plain, wrapped, error) &&
        hex_of(wrapped) ==
            "28c9f404c4b810f4cbccb35cfb87f8263f5786e2d80ed326cbc7f0e71a99f43bfb988b9b7a02dd21";
    report.check(wrapped256_ok, "aes_key_wrap (RFC 3394 secao 4.6)", hex_of(wrapped));

    // chave embrulhada corrompida tem de falhar no integrity check
    Bytes corrupted = wrapped;
    corrupted[9] ^= 0x01;
    std::string ignored;
    report.check(!aes_key_unwrap(kek, corrupted, recovered, ignored),
                 "aes_key_unwrap rejeita dado corrompido", ignored);

    // NIST SP 800-38A F.2.5
    Bytes nist_key;
    Bytes nist_iv;
    Bytes nist_plain;
    Bytes nist_cipher;
    bytes_from_hex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", nist_key);
    bytes_from_hex("000102030405060708090A0B0C0D0E0F", nist_iv);
    bytes_from_hex("6bc1bee22e409f96e93d7e117393172a", nist_plain);
    Bytes encrypted;
    std::string enc_error;
    const bool enc_ok = aes_cbc_encrypt(nist_key, nist_iv.data(), nist_plain, encrypted, enc_error);
    report.check(enc_ok && hex_of(encrypted) == "f58c4c04d6e5f1ba779eabfb5f7bfbd6",
                 "aes_cbc_encrypt (NIST SP 800-38A F.2.5)",
                 "ok=" + std::to_string(enc_ok ? 1 : 0) + " out=" + hex_of(encrypted) +
                     " err=" + enc_error);
    Bytes decrypted;
    std::string cbc_error;
    const bool cbc_ok = aes_cbc_decrypt(nist_key, nist_iv.data(), encrypted, decrypted, cbc_error);
    report.check(cbc_ok && hex_of(decrypted) == hex_of(nist_plain),
                 "aes_cbc_decrypt (NIST SP 800-38A F.2.5)",
                 "ok=" + std::to_string(cbc_ok ? 1 : 0) + " out=" + hex_of(decrypted) +
                     " err=" + cbc_error);

    // round trip do key wrap em varios tamanhos
    bool round_trip = true;
    Bytes key32(32, 0x5A);
    for (std::size_t size : {16u, 24u, 32u, 40u, 64u}) {
        Bytes source(size);
        for (std::size_t i = 0; i < size; ++i) source[i] = static_cast<std::uint8_t>(i * 13 + 5);
        Bytes temporary;
        Bytes back;
        if (!aes_key_wrap(key32, source, temporary, error) ||
            !aes_key_unwrap(key32, temporary, back, error) || hex_of(back) != hex_of(source)) {
            round_trip = false;
        }
    }
    report.check(round_trip, "aes_kw ida e volta em 5 tamanhos");
}

void test_plist(Report& report, const std::string& fixture) {
    std::printf("\n== parser de binary plist (fixture escrita por plistlib/CPython) ==\n");
    Expect expect;
    std::string error;
    load_expect(join(fixture, "expect.tab"), expect, error);

    PlistValue root;
    const bool loaded = plist_load(join(join(fixture, "enc"), "Manifest.plist"), root, error);
    report.check(loaded, "Manifest.plist binario carregado", error);
    if (!loaded) return;

    report.check(root.kind == PlistKind::Dict, "raiz do Manifest.plist e dicionario");

    const PlistValue* version = root.find("Version");
    report.check(version != nullptr && version->kind == PlistKind::Integer &&
                     version->integer == std::stoll(expect.get("plist_version")),
                 "Version como inteiro",
                 version ? std::to_string(version->integer) : "ausente");

    const PlistValue* encrypted = root.find("IsEncrypted");
    report.check(encrypted != nullptr && encrypted->kind == PlistKind::Bool && encrypted->boolean,
                 "IsEncrypted como booleano true");

    const PlistValue* text = root.find("UnicodeName");
    report.check(text != nullptr && text->kind == PlistKind::String &&
                     text->text == expect.get("plist_unicode"),
                 "string UTF-16 com acento, CJK e par substituto",
                 text ? text->text : "ausente");

    const PlistValue* negative = root.find("NegativeInt");
    report.check(negative != nullptr && negative->integer == std::stoll(expect.get("plist_negative")),
                 "inteiro negativo", negative ? std::to_string(negative->integer) : "ausente");

    const PlistValue* big = root.find("BigInt");
    report.check(big != nullptr && big->integer == std::stoll(expect.get("plist_bigint")),
                 "inteiro de 8 bytes", big ? std::to_string(big->integer) : "ausente");

    const PlistValue* real = root.find("SystemDomainsVersion");
    const double wanted_real = std::stod(expect.get("plist_real"));
    report.check(real != nullptr && real->kind == PlistKind::Real &&
                     real->real == wanted_real,
                 "real (double)", real ? std::to_string(real->real) : "ausente");

    const double wanted_epoch = std::stod(expect.get("plist_date_epoch"));
    const PlistValue* date = root.find("Date");
    report.check(date != nullptr && date->kind == PlistKind::Date &&
                     std::abs(date->real - wanted_epoch) < 0.5,
                 "data (epoch Apple 2001)", date ? std::to_string(date->real) : "ausente");

    const PlistValue* raw = root.find("RawData");
    report.check(raw != nullptr && raw->kind == PlistKind::Data &&
                     hex_of(raw->bytes) == "000102feff",
                 "bloco data", raw ? hex_of(raw->bytes) : "ausente");

    const PlistValue* empty_text = root.find("EmptyText");
    report.check(empty_text != nullptr && empty_text->kind == PlistKind::String &&
                     empty_text->text.empty(),
                 "string vazia");

    const PlistValue* empty_data = root.find("EmptyData");
    report.check(empty_data != nullptr && empty_data->kind == PlistKind::Data &&
                     empty_data->bytes.empty(),
                 "data vazio");

    const PlistValue* floats = root.find("Floats");
    report.check(floats != nullptr && floats->kind == PlistKind::Array &&
                     floats->items.size() == 3 && floats->items[0].real == 1.5 &&
                     floats->items[1].real == -2.25 && floats->items[2].real == 0.0,
                 "array de reais");

    const PlistValue* nested = root.find("Nested");
    const PlistValue* inner = nested ? nested->find("a") : nullptr;
    const PlistValue* list = inner ? inner->find("b") : nullptr;
    report.check(list != nullptr && list->kind == PlistKind::Array && list->items.size() == 3 &&
                     list->items[0].integer == 1 && list->items[1].boolean &&
                     !list->items[2].boolean,
                 "dicionario aninhado com array de mistos");

    const PlistValue* lockdown = root.find("Lockdown");
    const PlistValue* device = lockdown ? lockdown->find("DeviceName") : nullptr;
    report.check(device != nullptr && device->text == expect.get("device_name"),
                 "Lockdown.DeviceName", device ? device->text : "ausente");

    const PlistValue* apps = root.find("Applications");
    const PlistValue* app = apps ? apps->find("com.example.app") : nullptr;
    const PlistValue* container = app ? app->find("ContainerContentClass") : nullptr;
    report.check(container != nullptr && container->integer == 4,
                 "dicionario dentro de dicionario dentro de dicionario");

    const PlistValue* keybag_value = root.find("BackupKeyBag");
    Bytes keybag_blob = keybag_value != nullptr ? keybag_value->bytes : Bytes();
    report.check(keybag_value != nullptr && keybag_value->kind == PlistKind::Data &&
                     static_cast<std::uint64_t>(keybag_value->bytes.size()) ==
                         std::stoull(expect.get("keybag_len")) &&
                     sha256_hex(keybag_value->bytes) == expect.get("keybag_sha256"),
                 "BackupKeyBag com tamanho e sha256 esperados");

    PlistValue xml_root;
    const bool xml_ok = plist_load(join(join(fixture, "enc"), "Info.plist"), xml_root, error);
    report.check(!xml_ok && error.find("XML") != std::string::npos,
                 "plist em XML recusado com mensagem explicita", error);

    PlistValue truncated;
    std::string truncated_error;
    const Bytes cut(keybag_blob.begin(), keybag_blob.begin() + 20);
    (void)truncated;
    report.check(!plist_parse(cut, truncated, truncated_error),
                 "blob arbitrario recusado como binary plist", truncated_error);
}

void test_keybag(Report& report, const std::string& fixture) {
    std::printf("\n== keybag: parser, derivacao e unwrap ==\n");
    Expect expect;
    std::string error;
    load_expect(join(fixture, "expect.tab"), expect, error);

    PlistValue root;
    if (!plist_load(join(join(fixture, "enc"), "Manifest.plist"), root, error)) {
        report.check(false, "Manifest.plist para o teste do keybag", error);
        return;
    }
    const PlistValue* value = root.find("BackupKeyBag");
    if (value == nullptr) {
        report.check(false, "BackupKeyBag presente");
        return;
    }

    Keybag keybag;
    const bool parsed = keybag_parse(value->bytes, keybag, error);
    report.check(parsed, "keybag_parse aceita o blob da fixture", error);
    if (!parsed) return;

    report.check(keybag.version == 4, "VERS lido", std::to_string(keybag.version));
    report.check(keybag.type == 1, "TYPE lido", std::to_string(keybag.type));
    report.check(hex_of(keybag.uuid) == "00112233445566778899aabbccddeeff", "UUID lido",
                 hex_of(keybag.uuid));
    report.check(hex_of(keybag.salt) == expect.get("salt_hex"), "SALT lido",
                 hex_of(keybag.salt));
    report.check(keybag.iterations == static_cast<std::uint32_t>(std::stoul(expect.get("iterations"))),
                 "ITER lido", std::to_string(keybag.iterations));
    report.check(hex_of(keybag.hmac) == expect.get("keybag_hmac_hex"), "HMCK lido",
                 hex_of(keybag.hmac));
    report.check(keybag.classes.size() == expect.classes.size(),
                 "uma secao CLAS por classe de protecao",
                 std::to_string(keybag.classes.size()) + " de " +
                     std::to_string(expect.classes.size()));

    bool wpky_ok = true;
    for (const ExpectClass& want : expect.classes) {
        const KeybagClass* item = keybag.protection_class(want.id);
        if (item == nullptr || hex_of(item->wrapped_key) != want.wpky_hex) wpky_ok = false;
    }
    report.check(wpky_ok, "WPKY de cada classe bate com a fixture");

    // senha errada tem de falhar
    Keybag wrong = keybag;
    Bytes wrong_key;
    std::string wrong_error;
    report.check(!keybag_unlock(wrong, "senha-errada", wrong_key, wrong_error),
                 "senha errada e recusada pelo integrity check do AES-KW", wrong_error);

    const bool unlocked = keybag_unlock(keybag, expect.get("passcode"), wrong_key, wrong_error);
    report.check(unlocked, "keybag_unlock com a senha da fixture", wrong_error);
    if (!unlocked) return;

    report.check(hex_of(wrong_key) == expect.get("passcode_key_hex"),
                 "passcode key bate com PBKDF2 do hashlib", hex_of(wrong_key));

    int convention = -1;
    std::string hmac_error;
    const bool hmac_ok = keybag_verify_hmac(keybag, wrong_key, convention, hmac_error);
    report.check(hmac_ok && convention == 0,
                 "HMCK confere na convencao 0 (HMAC-SHA1 sobre os bytes antes do bloco)",
                 "convencao=" + std::to_string(convention) + " " + hmac_error);

    bool class_ok = true;
    for (const ExpectClass& want : expect.classes) {
        const KeybagClass* item = keybag.protection_class(want.id);
        if (item == nullptr || hex_of(item->class_key) != want.key_hex) class_ok = false;
    }
    report.check(class_ok, "class keys derivadas batem com a fixture",
                 std::to_string(expect.classes.size()) + " classes");

    Keybag broken;
    std::string broken_error;
    const Bytes truncated(value->bytes.begin(), value->bytes.begin() + 40);
    report.check(!keybag_parse(truncated, broken, broken_error),
                 "keybag truncado recusado", broken_error);
}

struct ExtractionCheck {
    int identical = 0;
    int total = 0;
    std::vector<std::string> mismatches;
};

ExtractionCheck compare_tree(const std::string& produced, const std::string& expected,
                             const Expect& expect) {
    ExtractionCheck result;
    for (const ExpectFile& item : expect.files) {
        ++result.total;
        std::string problem;
        const std::string left = join(join(produced, item.domain), item.relative);
        const std::string right = join(join(expected, item.domain), item.relative);
        if (same_file(left, right, problem)) {
            ++result.identical;
        } else {
            result.mismatches.push_back(item.domain + "/" + item.relative + ": " + problem);
        }
    }
    return result;
}

void test_backup(Report& report, const std::string& fixture, const std::string& work) {
    std::printf("\n== leitor de backup: Manifest.db, extracao e decriptacao ==\n");
    Expect expect;
    std::string error;
    load_expect(join(fixture, "expect.tab"), expect, error);

    Backup plain;
    const bool plain_open = backup_open(join(fixture, "plain"), plain, error);
    report.check(plain_open, "backup NAO cifrado abre", error);
    if (plain_open) {
        report.check(!plain.is_encrypted, "IsEncrypted=false lido do Manifest.plist");
        report.check(plain.entry_count == expect.files.size() +
                         std::stoull(expect.get("dir_count")) +
                         std::stoull(expect.get("symlink_count")),
                     "contagem de entradas do Manifest.db",
                     std::to_string(plain.entry_count));
        ExtractResult plain_result;
        const bool extracted = backup_extract(plain, join(work, "out_plain"), plain_result, error);
        report.check(extracted, "backup NAO cifrado extraido", error);
        report.check(plain_result.files_failed == 0,
                     "nenhuma falha na extracao em claro (" +
                         std::to_string(plain_result.files_written) + " arquivos)");
        const ExtractionCheck comparison =
            compare_tree(join(work, "out_plain"), join(fixture, "expected"), expect);
        report.check(comparison.identical == comparison.total,
                     "backup em claro: " + std::to_string(comparison.identical) + " de " +
                         std::to_string(comparison.total) + " arquivos byte a byte identicos",
                     comparison.mismatches.empty() ? "" : comparison.mismatches.front());
        report.check(plain_result.symlinks_skipped ==
                         std::stoull(expect.get("symlink_count")),
                     "symlink apenas registrado, nao criado",
                     std::to_string(plain_result.symlinks_skipped));
    }

    Backup enc;
    const bool enc_open = backup_open(join(fixture, "enc"), enc, error);
    report.check(enc_open, "backup cifrado abre", error);
    if (!enc_open) return;
    report.check(enc.is_encrypted, "IsEncrypted=true lido");
    report.check(enc.plist_version == 200, "Version=200 lido",
                 std::to_string(enc.plist_version));
    report.check(enc.device_name == expect.get("device_name"), "DeviceName do Lockdown",
                 enc.device_name);
    report.check(enc.product_version == expect.get("product_version"), "ProductVersion",
                 enc.product_version);

    std::string wrong_error;
    Backup wrong = enc;
    report.check(!backup_unlock(wrong, "nao-e-a-senha", wrong_error),
                 "backup cifrado recusa senha errada", wrong_error);

    const bool unlocked = backup_unlock(enc, expect.get("passcode"), error);
    report.check(unlocked, "backup cifrado desbloqueado", error);
    if (!unlocked) return;

    // file keys: prova de que cada layout de EncryptionKey foi reconhecido
    int layout_matches = 0;
    std::vector<std::string> layout_problems;
    for (const ExpectFile& want : expect.files) {
        const BackupEntry* entry = nullptr;
        for (const BackupEntry& candidate : enc.entries) {
            if (candidate.file_id == want.id) {
                entry = &candidate;
                break;
            }
        }
        if (entry == nullptr) {
            layout_problems.push_back(want.id + ": entrada ausente");
            continue;
        }
        struct Candidate {
            std::uint32_t class_id;
            Bytes wrapped;
            const char* layout;
        };
        std::vector<Candidate> candidates;
        const Bytes& blob = entry->encryption_key;
        if (blob.size() >= 4 && blob.size() - 4 >= 16 && (blob.size() - 4) % 8 == 0) {
            Bytes rest(blob.begin() + 4, blob.end());
            candidates.push_back({read_be32(blob.data()), rest, "class-prefix-be"});
            const std::uint32_t little = static_cast<std::uint32_t>(blob[0]) |
                                         (static_cast<std::uint32_t>(blob[1]) << 8) |
                                         (static_cast<std::uint32_t>(blob[2]) << 16) |
                                         (static_cast<std::uint32_t>(blob[3]) << 24);
            candidates.push_back({little, rest, "class-prefix-le"});
        }
        if (blob.size() >= 24 && blob.size() % 8 == 0) {
            candidates.push_back({entry->protection_class, blob, "no-prefix"});
        }
        bool matched = false;
        std::string used_layout;
        for (const Candidate& candidate : candidates) {
            const KeybagClass* klass = enc.keybag.protection_class(candidate.class_id);
            if (klass == nullptr || klass->class_key.empty()) continue;
            Bytes unwrapped;
            std::string ignored;
            if (aes_key_unwrap(klass->class_key, candidate.wrapped, unwrapped, ignored) &&
                hex_of(unwrapped) == want.key_hex) {
                matched = true;
                used_layout = candidate.layout;
                break;
            }
        }
        if (matched && used_layout == want.layout) {
            ++layout_matches;
        } else {
            layout_problems.push_back(want.id + ": layout " + used_layout + " esperado " +
                                      want.layout);
        }
    }
    report.check(layout_matches == static_cast<int>(expect.files.size()),
                 "file keys desembrulhadas e layout do EncryptionKey correto: " +
                     std::to_string(layout_matches) + " de " +
                     std::to_string(expect.files.size()),
                 layout_problems.empty() ? "" : layout_problems.front());

    // um dos arquivos carrega a chave via objeto {"NS.data": ...} do NSKeyedArchiver
    const std::string nsdata_id = expect.get("nsdata_wrapped_file");
    bool nsdata_ok = false;
    for (const BackupEntry& candidate : enc.entries) {
        if (candidate.file_id != nsdata_id) continue;
        nsdata_ok = candidate.metadata_ok && candidate.encryption_key.size() == 44;
    }
    report.check(nsdata_ok,
                 "EncryptionKey via objeto {NS.data} do NSKeyedArchiver");

    // decriptacao arquivo a arquivo, comparando com o original
    int decrypted_ok = 0;
    std::vector<std::string> decrypt_problems;
    for (const ExpectFile& want : expect.files) {
        const BackupEntry* entry = nullptr;
        for (const BackupEntry& candidate : enc.entries) {
            if (candidate.file_id == want.id) {
                entry = &candidate;
                break;
            }
        }
        if (entry == nullptr) continue;
        Bytes plain_bytes;
        std::string problem;
        if (!backup_read_entry(enc, *entry, plain_bytes, problem)) {
            decrypt_problems.push_back(want.domain + "/" + want.relative + ": " + problem);
            continue;
        }
        Bytes original;
        std::string ignored;
        if (!read_whole_file(join(join(join(fixture, "expected"), want.domain), want.relative),
                             original, ignored)) {
            decrypt_problems.push_back(want.domain + "/" + want.relative + ": original sumiu");
            continue;
        }
        if (plain_bytes.size() == original.size() &&
            (original.empty() ||
             std::memcmp(plain_bytes.data(), original.data(), original.size()) == 0) &&
            sha256_hex(plain_bytes) == want.sha_plain) {
            ++decrypted_ok;
        } else {
            decrypt_problems.push_back(want.domain + "/" + want.relative +
                                       ": conteudo difere do original");
        }
    }
    report.check(decrypted_ok == static_cast<int>(expect.files.size()),
                 "decriptacao AES-256-CBC: " + std::to_string(decrypted_ok) + " de " +
                     std::to_string(expect.files.size()) + " arquivos identicos ao original",
                 decrypt_problems.empty() ? "" : decrypt_problems.front());

    ExtractResult result;
    const bool extracted = backup_extract(enc, join(work, "out_enc"), result, error);
    report.check(extracted, "backup cifrado extraido", error);
    report.check(result.files_failed == 0,
                 "nenhuma falha na extracao cifrada (" +
                     std::to_string(result.files_written) + " arquivos escritos)");
    const ExtractionCheck comparison =
        compare_tree(join(work, "out_enc"), join(fixture, "expected"), expect);
    report.check(comparison.identical == comparison.total,
                 "backup cifrado: " + std::to_string(comparison.identical) + " de " +
                     std::to_string(comparison.total) + " arquivos byte a byte identicos",
                 comparison.mismatches.empty() ? "" : comparison.mismatches.front());

    const std::string manifest_path = join(work, "out_enc_manifest.json");
    const bool manifest_ok = backup_write_manifest_json(enc, result, manifest_path, error);
    report.check(manifest_ok, "manifesto JSON escrito", error);
    Bytes manifest_raw;
    if (manifest_ok && read_whole_file(manifest_path, manifest_raw, error)) {
        const std::string manifest(manifest_raw.begin(), manifest_raw.end());
        const std::size_t extracted_entries =
            count_occurrences(manifest, "\"status\": \"extracted\"");
        report.check(extracted_entries == expect.files.size(),
                     "manifesto JSON lista os " + std::to_string(expect.files.size()) +
                         " arquivos como extracted",
                     std::to_string(extracted_entries));
        report.check(manifest.find("\"module\": \"olho::ios\"") != std::string::npos &&
                         manifest.find("\"encrypted\": true") != std::string::npos &&
                         manifest.find(want_first_sha(expect)) != std::string::npos,
                     "manifesto JSON tem cabecalho e sha256");
    }
}

void test_negative(Report& report, const std::string& fixture, const std::string& work) {
    std::printf("\n== testes negativos: caminhos hostis e entradas invalidas ==\n");
    std::string error;
    Backup missing;
    report.check(!backup_open(join(fixture, "nao-existe"), missing, error),
                 "diretorio inexistente recusado", error);

    // entradas com traversal nao podem escapar do diretorio de saida
    Backup hostile;
    hostile.root = join(fixture, "plain");
    BackupEntry escape;
    escape.file_id = std::string(40, 'a');
    escape.domain = "HomeDomain";
    escape.relative_path = "../../fora.txt";
    escape.is_file = true;
    hostile.entries.push_back(escape);
    BackupEntry absolute;
    absolute.file_id = std::string(40, 'b');
    absolute.domain = "HomeDomain";
    absolute.relative_path = "C:/Windows/System32/evil.txt";
    absolute.is_file = true;
    hostile.entries.push_back(absolute);

    ExtractResult result;
    const bool ran = backup_extract(hostile, join(work, "out_hostile"), result, error);
    report.check(ran && result.files_failed == 0 && result.files_skipped == 2,
                 "entradas com '..' e caminho absoluto sao recusadas",
                 std::to_string(result.files_skipped) + " recusadas, erro: " + error);
}

void test_robustness(Report& report) {
    std::printf("\n== robustez: entrada arbitraria nos parsers ==\n");
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    auto next = [&state]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    int plist_ok = 0;
    int plist_rejected = 0;
    int keybag_ok = 0;
    int keybag_rejected = 0;
    int silent_failures = 0;
    for (int i = 0; i < 4000; ++i) {
        Bytes blob;
        const std::size_t size = static_cast<std::size_t>(next() % 200);
        for (std::size_t k = 0; k < size; ++k) {
            blob.push_back(static_cast<std::uint8_t>(next() & 0xFF));
        }
        if (i % 2 == 0 && size >= 8) {
            std::memcpy(blob.data(), "bplist00", 8);
        }
        PlistValue value;
        std::string error;
        if (plist_parse(blob, value, error)) {
            ++plist_ok;
        } else {
            ++plist_rejected;
            if (error.empty()) ++silent_failures;
        }
        Keybag keybag;
        std::string keybag_error;
        if (keybag_parse(blob, keybag, keybag_error)) {
            ++keybag_ok;
        } else {
            ++keybag_rejected;
            if (keybag_error.empty()) ++silent_failures;
        }
    }
    report.check(silent_failures == 0,
                 "8000 chamadas com blob aleatorio: sem crash e falha sempre com mensagem",
                 "plist aceitos/rejeitados " + std::to_string(plist_ok) + "/" +
                     std::to_string(plist_rejected) + ", keybag " +
                     std::to_string(keybag_ok) + "/" + std::to_string(keybag_rejected));
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root = (argc > 1) ? argv[1] : ".oracle_ios";
    const std::string fixture = join(root, "fixture");
    const std::string work = join(root, "out");

    std::printf("harness iOS - raiz %s\n", root.c_str());
    Report report;

    test_digests(report);
    test_plist(report, fixture);
    test_keybag(report, fixture);
    test_backup(report, fixture, work);
    test_negative(report, fixture, work);
    test_robustness(report);

    std::printf("\n===== resumo =====\n");
    std::printf("checks: %d de %d passaram, %d falharam\n", report.passed,
                report.passed + report.failed, report.failed);
    for (const std::string& item : report.failures) {
        std::printf("  falha: %s\n", item.c_str());
    }
    return report.failed == 0 ? 0 : 1;
}
