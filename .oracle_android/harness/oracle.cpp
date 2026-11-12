// language: C++, file: oracle.cpp, target: Windows 11
// Acceptance harness for the Android module. Three independent oracles:
//   1. parse_devices() against captured 'adb devices -l' transcripts
//   2. extract_ab() against fixtures produced by gen_ab.py (Python + .NET AES)
//   3. the process layer against fake_adb.exe, a real child process
// Nothing here trusts the module's own report: every extracted file is re-hashed
// and compared with the hash of the bytes that went into the archive.
#include "android_adb.hpp"
#include "android_backup.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& name, const std::string& detail = std::string()) {
    ++g_checks;
    if (condition) {
        std::printf("PASS  %s\n", name.c_str());
    } else {
        ++g_failures;
        std::printf("FAIL  %s%s%s\n", name.c_str(), detail.empty() ? "" : " :: ", detail.c_str());
    }
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n, nullptr,
                       nullptr);
    return out;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

// The same extended-length prefix the module uses: an extracted WhatsApp tree
// routinely exceeds MAX_PATH, and a harness that cannot read what the module
// wrote would report a false failure.
std::wstring long_path(const std::wstring& path) {
    if (path.empty()) return path;
    if (path.compare(0, 4, L"\\\\?\\") == 0) return path;
    std::wstring fixed;
    fixed.reserve(path.size() + 8);
    for (const wchar_t c : path) fixed.push_back(c == L'/' ? L'\\' : c);
    if (fixed.size() >= 2 && fixed[1] == L':') return L"\\\\?\\" + fixed;
    return fixed;
}

bool make_dirs(const std::wstring& path) {
    if (path.empty()) return false;
    if (CreateDirectoryW(long_path(path).c_str(), nullptr)) return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return true;
    const std::size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring::npos) return false;
    if (!make_dirs(path.substr(0, cut))) return false;
    return CreateDirectoryW(long_path(path).c_str(), nullptr) != 0 ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

bool read_text(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE file = CreateFileW(long_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char buffer[4096];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &got, nullptr)) break;
        if (got == 0) break;
        out.append(buffer, got);
    }
    CloseHandle(file);
    return true;
}

std::string trim(const std::string& text) {
    std::size_t begin = 0, end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) --end;
    return text.substr(begin, end - begin);
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            out.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(trim(current));
    return out;
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == separator) {
            out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    out.push_back(current);
    return out;
}

std::vector<std::wstring> list_by_extension(const std::wstring& dir, const std::wstring& extension) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(long_path(dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return names;
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        if (name.size() >= extension.size() &&
            name.compare(name.size() - extension.size(), extension.size(), extension) == 0) {
            names.push_back(name);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(names.begin(), names.end());
    return names;
}

void collect_relative(const std::wstring& root, const std::wstring& prefix,
                      std::vector<std::string>& out) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(long_path(root + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring relative = prefix.empty() ? name : prefix + L"\\" + name;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            collect_relative(root + L"\\" + name, relative, out);
        } else {
            out.push_back(narrow(relative));
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

std::uint64_t size_of(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(long_path(path).c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

struct PropertyList {
    std::string password;
    std::string container;
    std::string manifest;
    std::string rejected_list;
    std::string error_contains;
    std::string encoding;
    int files = -1;
    int rejected = -1;
    int rejected_min = -1;
    bool integrity = false;
    bool has_integrity = false;
    bool ok = false;

    void load(const std::string& text) {
        for (const auto& line : split_lines(text)) {
            if (line.empty()) continue;
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            const std::string value = line.substr(eq + 1);
            if (key == "ok") ok = (value == "1");
            else if (key == "password") password = value;
            else if (key == "container") container = value;
            else if (key == "manifest") manifest = value;
            else if (key == "rejected_list") rejected_list = value;
            else if (key == "error_contains") error_contains = value;
            else if (key == "encoding") encoding = value;
            else if (key == "files") files = std::atoi(value.c_str());
            else if (key == "rejected") rejected = std::atoi(value.c_str());
            else if (key == "rejected_min") rejected_min = std::atoi(value.c_str());
            else if (key == "integrity") { integrity = (value == "1"); has_integrity = true; }
        }
    }
};

struct DeviceExpectation {
    bool ok = false;
    std::string error_contains;
    struct Row {
        std::string serial, state, model, product, device;
    };
    std::vector<Row> rows;
};

DeviceExpectation load_device_expectation(const std::string& text) {
    DeviceExpectation expectation;
    for (const auto& line : split_lines(text)) {
        if (line.empty()) continue;
        if (line.compare(0, 4, "dev=") == 0) {
            const auto fields = split(line.substr(4), '|');
            DeviceExpectation::Row row;
            if (fields.size() > 0) row.serial = fields[0];
            if (fields.size() > 1) row.state = fields[1];
            if (fields.size() > 2) row.model = fields[2];
            if (fields.size() > 3) row.product = fields[3];
            if (fields.size() > 4) row.device = fields[4];
            expectation.rows.push_back(row);
        } else if (line.compare(0, 3, "ok=") == 0) {
            expectation.ok = line.substr(3) == "1";
        } else if (line.compare(0, 15, "error_contains=") == 0) {
            expectation.error_contains = line.substr(15);
        }
    }
    return expectation;
}

// ---------------------------------------------------------------------------

void run_device_parser_tests(const std::wstring& fixtures) {
    std::printf("\n== parser de 'adb devices -l' (transcricoes reais)\n");
    const std::wstring dir = fixtures + L"\\adb";
    const auto files = list_by_extension(dir, L".txt");
    check(!files.empty(), "adb: amostras encontradas", std::to_string(files.size()));
    for (const auto& file : files) {
        const std::string stem = narrow(file.substr(0, file.size() - 4));
        std::string text, expect_text;
        if (!read_text(dir + L"\\" + file, text)) {
            check(false, "adb/" + stem, "nao foi possivel ler a amostra");
            continue;
        }
        if (!read_text(dir + L"\\" + widen(stem) + L".expect", expect_text)) {
            check(false, "adb/" + stem, "expectativa ausente");
            continue;
        }
        const DeviceExpectation expectation = load_device_expectation(expect_text);

        std::vector<olho::android::Device> devices;
        std::string error;
        const bool parsed = olho::android::parse_devices(text, devices, error);

        check(parsed == expectation.ok, "adb/" + stem + " ok",
              "esperado " + std::to_string(expectation.ok) + ", obtido " + std::to_string(parsed) +
                  " (" + error + ")");
        check(devices.size() == expectation.rows.size(),
              "adb/" + stem + " quantidade",
              std::to_string(devices.size()) + " != " + std::to_string(expectation.rows.size()));
        if (!expectation.error_contains.empty()) {
            check(error.find(expectation.error_contains) != std::string::npos,
                  "adb/" + stem + " mensagem de erro", error);
        }
        const std::size_t limit = std::min(devices.size(), expectation.rows.size());
        for (std::size_t i = 0; i < limit; ++i) {
            const auto& actual = devices[i];
            const auto& wanted = expectation.rows[i];
            const std::string label = "adb/" + stem + " [" + std::to_string(i) + "]";
            check(actual.serial == wanted.serial, label + " serial",
                  actual.serial + " != " + wanted.serial);
            check(std::string(olho::android::to_string(actual.state)) == wanted.state,
                  label + " estado", std::string(olho::android::to_string(actual.state)) + " != " + wanted.state);
            check(actual.model == wanted.model, label + " model", actual.model + " != " + wanted.model);
            check(actual.product == wanted.product, label + " product",
                  actual.product + " != " + wanted.product);
            check(actual.device == wanted.device, label + " device",
                  actual.device + " != " + wanted.device);
        }
    }
}

void run_ab_tests(const std::wstring& fixtures, const std::wstring& out_root,
                  std::uint64_t& total_files, std::uint64_t& total_matched) {
    std::printf("\n== .ab: cabecalho, chave mestra, decifragem, tar\n");
    const auto files = list_by_extension(fixtures, L".ab");
    check(!files.empty(), "ab: fixtures encontradas", std::to_string(files.size()));
    for (const auto& file : files) {
        const std::string stem = narrow(file.substr(0, file.size() - 3));
        std::string expect_text;
        if (!read_text(fixtures + L"\\" + widen(stem) + L".expect", expect_text)) {
            check(false, "ab/" + stem, "expectativa ausente");
            continue;
        }
        PropertyList expectation;
        expectation.load(expect_text);

        olho::android::AbStats stats;
        const std::wstring target = out_root + L"\\ab\\" + widen(stem);
        const ULONGLONG started = GetTickCount64();
        const bool extracted =
            olho::android::extract_ab(fixtures + L"\\" + file, target, expectation.password, stats);
        const ULONGLONG elapsed = GetTickCount64() - started;

        if (!expectation.ok) {
            check(!extracted, "ab/" + stem + " recusado", "aceitou um arquivo invalido");
            if (extracted) continue;
            check(!stats.error.empty(), "ab/" + stem + " mensagem presente");
            std::printf("  %-22s recusado: %s\n", stem.c_str(), stats.error.c_str());
            if (!expectation.error_contains.empty()) {
                check(stats.error.find(expectation.error_contains) != std::string::npos,
                      "ab/" + stem + " mensagem", stats.error);
            }
            continue;
        }

        check(extracted, "ab/" + stem + " extraido", stats.error);
        if (!extracted) continue;
        check(stats.master.verified || !stats.header.encrypted, "ab/" + stem + " chave mestra verificada");
        if (!expectation.container.empty() || stats.header.compressed) {
            check(stats.container == expectation.container, "ab/" + stem + " container",
                  stats.container + " != " + expectation.container);
        }
        if (expectation.has_integrity) {
            check(stats.integrity_ok == expectation.integrity, "ab/" + stem + " integridade");
            if (expectation.integrity) {
                check(stats.checksum_expected == stats.checksum_computed,
                      "ab/" + stem + " checksum igual",
                      std::to_string(stats.checksum_expected) + " vs " +
                          std::to_string(stats.checksum_computed));
            }
        }
        if (!expectation.encoding.empty()) {
            check(stats.master.encoding == expectation.encoding, "ab/" + stem + " codificacao PBKDF2",
                  stats.master.encoding + " != " + expectation.encoding);
        }
        if (expectation.files >= 0) {
            check(static_cast<int>(stats.tar.files) == expectation.files, "ab/" + stem + " contagem de arquivos",
                  std::to_string(stats.tar.files) + " != " + std::to_string(expectation.files));
        }
        if (expectation.rejected >= 0) {
            check(static_cast<int>(stats.tar.rejected.size()) == expectation.rejected,
                  "ab/" + stem + " rejeitados",
                  std::to_string(stats.tar.rejected.size()) + " != " +
                      std::to_string(expectation.rejected));
        }
        if (expectation.rejected_min >= 0) {
            check(static_cast<int>(stats.tar.rejected.size()) >= expectation.rejected_min,
                  "ab/" + stem + " rejeitados (minimo)",
                  std::to_string(stats.tar.rejected.size()) + " < " +
                      std::to_string(expectation.rejected_min));
        }
        if (!expectation.rejected_list.empty()) {
            std::string list_text;
            read_text(fixtures + L"\\" + widen(expectation.rejected_list), list_text);
            std::vector<std::string> wanted;
            for (const auto& line : split_lines(list_text)) {
                if (!line.empty()) wanted.push_back(line);
            }
            // Subset check: the reader may refuse extra parent-directory entries,
            // but every name listed here must be among the refusals.
            std::string missing;
            for (const auto& entry : wanted) {
                if (std::find(stats.tar.rejected.begin(), stats.tar.rejected.end(), entry) ==
                    stats.tar.rejected.end()) {
                    missing = entry;
                    break;
                }
            }
            check(missing.empty(), "ab/" + stem + " nomes perigosos recusados",
                  "nao recusado: " + missing);
        }

        // Byte-exactness against the hashes of the source tree.
        std::string manifest_text;
        if (!read_text(fixtures + L"\\" + widen(expectation.manifest), manifest_text)) {
            check(false, "ab/" + stem + " manifesto", "manifesto ausente");
            continue;
        }
        const auto rows = split_lines(manifest_text);
        std::size_t matched = 0, sized = 0, modes = 0, checked = 0;
        for (const auto& row : rows) {
            if (row.empty()) continue;
            const auto fields = split(row, '\t');
            if (fields.size() < 4) continue;
            ++checked;
            std::string relative = fields[0];
            for (char& c : relative) {
                if (c == '/') c = '\\';
            }
            const std::wstring path = target + L"\\" + widen(relative);
            std::string hash, hash_error;
            if (!olho::android::sha256_file(path, hash, hash_error)) {
                std::printf("FAIL  ab/%s hash %s :: %s\n", stem.c_str(), relative.c_str(),
                            hash_error.c_str());
                ++g_failures;
                ++g_checks;
                continue;
            }
            if (hash == fields[2]) ++matched;
            else {
                std::printf("FAIL  ab/%s hash %s :: %s != %s\n", stem.c_str(), relative.c_str(),
                            hash.c_str(), fields[2].c_str());
                ++g_failures;
                ++g_checks;
            }
            if (size_of(path) == std::strtoull(fields[1].c_str(), nullptr, 10)) ++sized;

            const unsigned mode = static_cast<unsigned>(std::strtoul(fields[3].c_str(), nullptr, 8));
            const bool want_readonly = (mode & 0200u) == 0;
            const DWORD attributes = GetFileAttributesW(long_path(path).c_str());
            const bool is_readonly = (attributes & FILE_ATTRIBUTE_READONLY) != 0;
            if (is_readonly == want_readonly) ++modes;
            else {
                std::printf("FAIL  ab/%s modo %s :: readonly=%d esperado %d\n", stem.c_str(),
                            relative.c_str(), static_cast<int>(is_readonly),
                            static_cast<int>(want_readonly));
                ++g_failures;
                ++g_checks;
            }
        }
        total_files += static_cast<std::uint64_t>(checked);
        total_matched += static_cast<std::uint64_t>(matched);
        const double seconds = static_cast<double>(elapsed) / 1000.0;
        const double mb_per_second = seconds > 0.0
                                         ? static_cast<double>(stats.tar.bytes_written) /
                                               (1024.0 * 1024.0) / seconds
                                         : 0.0;
        std::printf("  %-22s %zu/%zu arquivos identicos, %zu/%zu tamanhos, %zu/%zu modos, "
                    "%llu entradas tar, %llu bytes, %llu ms (%.1f MB/s)\n",
                    stem.c_str(), matched, checked, sized, checked, modes, checked,
                    static_cast<unsigned long long>(stats.tar.entries),
                    static_cast<unsigned long long>(stats.tar.bytes_written),
                    static_cast<unsigned long long>(elapsed), mb_per_second);
        check(matched == checked, "ab/" + stem + " todos os hashes");
        check(sized == checked, "ab/" + stem + " todos os tamanhos");
        check(modes == checked, "ab/" + stem + " todos os modos");

        // No file may exist that the archive did not carry.
        std::vector<std::string> on_disk;
        collect_relative(target, L"", on_disk);
        std::vector<std::string> expected_paths;
        for (const auto& row : rows) {
            if (row.empty()) continue;
            std::string relative = split(row, '\t')[0];
            for (char& c : relative) {
                if (c == '/') c = '\\';
            }
            expected_paths.push_back(relative);
        }
        std::sort(expected_paths.begin(), expected_paths.end());
        std::sort(on_disk.begin(), on_disk.end());
        check(on_disk.size() == expected_paths.size(), "ab/" + stem + " sem arquivos extras",
              std::to_string(on_disk.size()) + " no disco vs " + std::to_string(expected_paths.size()) +
                  " no manifesto");
        if (on_disk.size() == expected_paths.size() && on_disk != expected_paths) {
            std::string detail;
            for (std::size_t i = 0; i < on_disk.size(); ++i) {
                if (on_disk[i] != expected_paths[i]) {
                    detail = on_disk[i] + " vs " + expected_paths[i];
                    break;
                }
            }
            check(false, "ab/" + stem + " nomes conferem", detail);
        }
    }
}

// Tar slip is only disproved by looking outside the extraction root.
void run_slip_tests(const std::wstring& out_root) {
    std::printf("\n== contencao de caminhos perigosos\n");
    struct Probe {
        const wchar_t* path;
        const char* what;
    };
    const Probe probes[] = {
        {L"\\ab\\evil.txt", "../evil.txt fora da raiz"},
        {L"\\abs.txt", "/abs.txt absoluto"},
        {L"\\ab\\v5_slip\\safe\\escape.txt", "safe/nested/../escape.txt normalizado"},
        {L"\\ab\\v5_slip\\win.txt", "C:\\win.txt com drive"},
    };
    for (const auto& probe : probes) {
        const std::wstring full = out_root + probe.path;
        check(GetFileAttributesW(long_path(full).c_str()) == INVALID_FILE_ATTRIBUTES,
              std::string("tar-slip bloqueado: ") + probe.what, narrow(full));
    }
    check(GetFileAttributesW(long_path(out_root + L"\\ab\\v5_slip\\safe\\ok.txt").c_str()) !=
              INVALID_FILE_ATTRIBUTES,
          "tar-slip: o membro legitimo foi extraido");
}

void run_process_tests(const std::wstring& fake_adb, const std::wstring& out_root) {
    std::printf("\n== camada de processo (adb de mentira, processo real)\n");
    make_dirs(out_root + L"\\pull");
    make_dirs(out_root + L"\\pull\\dir com espaco");

    olho::android::Config cfg;
    cfg.adb_path = fake_adb;
    cfg.default_timeout_ms = 8000;

    const auto list = olho::android::list_devices(cfg, 8000);
    check(list.ok, "adb devices -l via pipe", list.error);
    check(list.devices.size() == 2, "adb devices -l: 2 dispositivos",
          std::to_string(list.devices.size()));
    if (list.devices.size() == 2) {
        check(list.devices[0].usable(), "adb devices -l: primeiro utilizavel");
        check(list.devices[1].state == olho::android::adb_state::unauthorized,
              "adb devices -l: segundo unauthorized");
    }
    check(!list.notices.empty(), "adb devices -l: banner de stderr capturado");

    olho::android::Config missing;
    missing.adb_path = L"Z:\\nao\\existe\\adb.exe";
    const auto absent = olho::android::list_devices(missing, 2000);
    check(!absent.ok && absent.error.find("nao existe") != std::string::npos,
          "adb ausente: erro claro, sem crash", absent.error);

    std::string value, error;
    check(olho::android::getprop(cfg, "SERIAL", "ro.build.version.sdk", value, error) && value == "34",
          "getprop ro.build.version.sdk", value + " " + error);

    olho::android::DeviceInfo info;
    const bool probed = olho::android::probe(cfg, "SERIAL", info, error);
    check(probed && info.api_level == 34 && info.model == "Pixel_Fake" && info.complete,
          "probe: api/model/manufacturer", std::to_string(info.api_level) + " " + info.model);

    const ULONGLONG started = GetTickCount64();
    const auto hang = olho::android::shell(cfg, "HANG", "id", 700);
    const ULONGLONG elapsed = GetTickCount64() - started;
    check(!hang.ok && hang.error.find("tempo limite") != std::string::npos,
          "timeout interrompe o comando", hang.error);
    check(elapsed < 4000, "timeout respeitado",
          std::to_string(static_cast<unsigned long long>(elapsed)) + " ms");

    const auto bad_exit = olho::android::shell(cfg, "ERR", "pm list packages", 5000);
    check(bad_exit.ok && bad_exit.exit_code == 3, "exit code preservado",
          std::to_string(bad_exit.exit_code));
    check(bad_exit.err.find("offline") != std::string::npos, "stderr capturado", bad_exit.err);

    std::uint64_t remote = 0;
    check(olho::android::remote_size(cfg, "SERIAL", "/sdcard/x.bin", remote, error) && remote == 4096,
          "remote_size via stat", std::to_string(remote));

    int percent_hits = 0;
    int last_percent = -1;
    const auto pulled = olho::android::pull(cfg, "SERIAL", "/sdcard/x.bin",
                                            out_root + L"\\pull\\x.bin", 10000,
                                            [&](int percent) {
                                                ++percent_hits;
                                                last_percent = percent;
                                            });
    check(pulled.ok && pulled.bytes == 4096 && pulled.size_verified,
          "pull: tamanho conferido com o dispositivo",
          std::to_string(pulled.bytes) + " " + pulled.error);
    check(percent_hits > 0 && last_percent == 100, "pull: progresso reportado",
          std::to_string(percent_hits) + " eventos, ultimo " + std::to_string(last_percent));

    const auto short_pull = olho::android::pull(cfg, "SHORTPULL", "/sdcard/x.bin",
                                                out_root + L"\\pull\\short.bin", 10000);
    check(!short_pull.ok && short_pull.error.find("divergente") != std::string::npos,
          "pull truncado e recusado", short_pull.error);

    const auto spaced = olho::android::pull(cfg, "SERIAL", "/sdcard/dir com espaco/x.bin",
                                            out_root + L"\\pull\\dir com espaco\\x.bin", 10000);
    check(spaced.ok, "pull: caminhos com espaco", spaced.error);

    const auto trailing = olho::android::pull(cfg, "SERIAL", "/sdcard/x.bin",
                                              out_root + L"\\pull\\x_trailing\\", 10000);
    check(trailing.ok, "pull: barra final nao escapa o fechamento da aspa", trailing.error);
}

void run_whatsapp_tests(const std::wstring& fixtures, const std::wstring& out_root) {
    std::printf("\n== coleta alvo WhatsApp\n");
    std::string error;

    const std::wstring backup_root = out_root + L"\\ab\\wa_case_v5";
    olho::android::WaCase from_backup;
    check(olho::android::locate_whatsapp(backup_root, from_backup, error),
          "backup: msgstore.db localizado", error);
    check(!from_backup.wal.path.empty(), "backup: msgstore.db-wal localizado");
    check(from_backup.key_file.size == 32 || from_backup.key_file.size == 0,
          "backup: chave com 32 bytes", std::to_string(from_backup.key_file.size));
    check(from_backup.media_dir.empty(), "backup: Media ausente (armazenamento externo)");

    olho::android::CaseStats from_backup_case;
    const bool built = olho::android::build_whatsapp_case(backup_root, out_root + L"\\case_from_backup",
                                                          "adb-backup", from_backup_case);
    check(built, "caso a partir do backup montado", from_backup_case.error);
    check(from_backup_case.files == 3, "caso a partir do backup: 3 arquivos",
          std::to_string(from_backup_case.files));
    check(from_backup_case.command.find("wa_recover.exe") != std::string::npos &&
              from_backup_case.command.find("msgstore.db") != std::string::npos,
          "comando wa_recover gerado", from_backup_case.command);

    olho::android::CaseStats pulled_case;
    const bool pulled_ok = olho::android::build_whatsapp_case(
        fixtures + L"\\pulled", out_root + L"\\case_from_pulled", "arvore-puxada", pulled_case);
    check(pulled_ok, "caso a partir da arvore puxada", pulled_case.error);
    check(pulled_case.media_files == 3, "arvore puxada: 3 arquivos de midia",
          std::to_string(pulled_case.media_files));

    std::string expect_text;
    if (read_text(fixtures + L"\\pulled.expect", expect_text)) {
        for (const auto& line : split_lines(expect_text)) {
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string role = line.substr(0, eq);
            const std::string wanted = line.substr(eq + 1);
            std::wstring relative;
            if (role == "msgstore") relative = L"whatsapp\\msgstore.db";
            else if (role == "wal") relative = L"whatsapp\\msgstore.db-wal";
            else if (role == "key") relative = L"whatsapp\\key";
            else if (role == "image") relative = L"whatsapp\\Media\\WhatsApp Images\\IMG-20240101-WA0001.jpg";
            else if (role == "video") relative = L"whatsapp\\Media\\WhatsApp Video\\VID-20240101-WA0001.mp4";
            else continue;
            std::string hash, hash_error;
            if (!olho::android::sha256_file(out_root + L"\\case_from_pulled\\" + relative, hash,
                                            hash_error)) {
                check(false, "caso/" + role + " presente", hash_error);
                continue;
            }
            check(hash == wanted, "caso/" + role + " identico a origem", hash + " != " + wanted);
        }
    }

    olho::wa::key_material key;
    check(olho::android::load_wa_key(out_root + L"\\case_from_pulled\\whatsapp\\key", key, error) &&
              key.raw_key.size() == 32,
          "load_wa_key: 32 bytes para o wa_recover", error);

    std::string manifest;
    check(read_text(out_root + L"\\case_from_pulled\\case_manifest.json", manifest) &&
              manifest.find("wa_recover_command") != std::string::npos,
          "case_manifest.json escrito");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        std::printf("uso: %ls <fixtures> <saida> <fake_adb.exe>\n", argv[0]);
        return 2;
    }
    const std::wstring fixtures = argv[1];
    const std::wstring out_root = argv[2];
    const std::wstring fake_adb = argv[3];

    std::printf("oracle Android -- fixtures: %s\n", narrow(fixtures).c_str());
    run_device_parser_tests(fixtures);
    std::uint64_t total_files = 0, total_matched = 0;
    run_ab_tests(fixtures, out_root, total_files, total_matched);
    run_slip_tests(out_root);
    run_process_tests(fake_adb, out_root);
    run_whatsapp_tests(fixtures, out_root);

    std::printf("\n== resumo\n");
    std::printf("arquivos extraidos e comparados por sha256: %llu de %llu identicos\n",
                static_cast<unsigned long long>(total_matched),
                static_cast<unsigned long long>(total_files));
    std::printf("verificacoes: %d, falhas: %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
