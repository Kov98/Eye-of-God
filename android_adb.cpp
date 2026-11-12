// language: C++, file: android_adb.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "android_adb.hpp"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace olho::android {
namespace {

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n,
                        nullptr, nullptr);
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

std::string win_error(DWORD code) {
    wchar_t* text = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string out;
    if (n != 0 && text != nullptr) {
        std::wstring w(text, n);
        while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r' || w.back() == L' ')) w.pop_back();
        out = narrow(w);
    }
    if (text) LocalFree(text);
    if (out.empty()) out = "Win32 error " + std::to_string(code);
    return out;
}

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

// Reads everything currently buffered on a pipe without blocking. Returns false
// once the write end is gone; *broken distinguishes that from an empty pipe.
void drain(HANDLE pipe, std::string& sink, bool& broken, const chunk_fn* tap) {
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            broken = true;
            return;
        }
        if (available == 0) return;
        char buf[16384];
        DWORD want = available < sizeof(buf) ? available : static_cast<DWORD>(sizeof(buf));
        DWORD got = 0;
        if (!ReadFile(pipe, buf, want, &got, nullptr) || got == 0) {
            broken = true;
            return;
        }
        sink.append(buf, got);
        if (tap != nullptr) (*tap)(std::string(buf, got));
    }
}

bool file_exists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

}  // namespace

const char* to_string(adb_state state) {
    switch (state) {
        case adb_state::device: return "device";
        case adb_state::unauthorized: return "unauthorized";
        case adb_state::offline: return "offline";
        case adb_state::recovery: return "recovery";
        case adb_state::bootloader: return "bootloader";
        case adb_state::sideload: return "sideload";
        case adb_state::authorizing: return "authorizing";
        case adb_state::no_permissions: return "no_permissions";
        case adb_state::unknown: return "unknown";
    }
    return "unknown";
}

std::uint64_t file_size(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

// ---------------------------------------------------------------------------
// process layer
// ---------------------------------------------------------------------------

ProcessResult run_capture(const std::wstring& command_line, std::uint32_t timeout_ms,
                          const chunk_fn& on_stdout) {
    ProcessResult result;
    if (command_line.empty()) {
        result.error = "linha de comando vazia";
        return result;
    }

    const std::uint64_t started_ms = now_ms();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
    if (!CreatePipe(&out_r, &out_w, &sa, 1 << 16)) {
        result.error = "CreatePipe(stdout): " + win_error(GetLastError());
        return result;
    }
    if (!CreatePipe(&err_r, &err_w, &sa, 1 << 16)) {
        result.error = "CreatePipe(stderr): " + win_error(GetLastError());
        CloseHandle(out_r);
        CloseHandle(out_w);
        return result;
    }
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    // stderr must not be the console: a child that writes a warning would block
    // forever on a full screen buffer if nobody drained the console handle.
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = (nul != INVALID_HANDLE_VALUE) ? nul : nullptr;
    si.hStdOutput = out_w;
    si.hStdError = err_w;

    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = command_line;  // CreateProcessW may write to the buffer
    const BOOL created = CreateProcessW(nullptr, &mutable_cmd[0], nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    // The child owns the write ends now; leaving ours open would keep the pipes
    // from ever reporting EOF.
    CloseHandle(out_w);
    CloseHandle(err_w);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);

    if (!created) {
        result.error = win_error(GetLastError());
        CloseHandle(out_r);
        CloseHandle(err_r);
        return result;
    }
    result.started = true;

    const std::uint64_t deadline = timeout_ms == 0 ? 0 : started_ms + timeout_ms;
    bool out_broken = false, err_broken = false;
    bool exited = false;
    // An empty std::function is not null through a pointer: it has to be tested
    // as a value, or every drain() call would throw bad_function_call.
    const chunk_fn* tap = on_stdout ? &on_stdout : nullptr;

    for (;;) {
        if (!out_broken) drain(out_r, result.out, out_broken, tap);
        if (!err_broken) drain(err_r, result.err, err_broken, nullptr);

        if (!exited) {
            const DWORD wait = WaitForSingleObject(pi.hProcess, 0);
            if (wait == WAIT_OBJECT_0) {
                exited = true;
            } else if (deadline != 0 && now_ms() > deadline) {
                TerminateProcess(pi.hProcess, 259);
                WaitForSingleObject(pi.hProcess, 2000);
                result.timed_out = true;
                exited = true;
            }
        }

        if (exited) {
            // Collect what is still in flight, then stop. A grandchild holding
            // the write end would keep the pipe open forever; the grace window
            // bounds that without ever blocking the caller.
            const std::uint64_t grace_end = now_ms() + 800;
            while (now_ms() < grace_end) {
                const std::size_t before = result.out.size() + result.err.size();
                if (!out_broken) drain(out_r, result.out, out_broken, tap);
                if (!err_broken) drain(err_r, result.err, err_broken, nullptr);
                const bool quiet = (result.out.size() + result.err.size()) == before;
                if (quiet && (out_broken || err_broken)) break;
                if (quiet) Sleep(10);
            }
            break;
        }
        Sleep(5);
    }

    DWORD code = 0;
    if (GetExitCodeProcess(pi.hProcess, &code)) result.exit_code = code;
    CloseHandle(out_r);
    CloseHandle(err_r);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    result.elapsed_ms = static_cast<std::uint32_t>(now_ms() - started_ms);
    return result;
}

bool locate_adb(std::wstring& path, std::string& error) {
    path.clear();
    wchar_t found[MAX_PATH * 2] = {0};
    const DWORD n = SearchPathW(nullptr, L"adb.exe", nullptr,
                                static_cast<DWORD>(sizeof(found) / sizeof(found[0])), found, nullptr);
    if (n > 0 && n < sizeof(found) / sizeof(found[0])) {
        path.assign(found, n);
        return true;
    }

    // SDK layouts, in the order Google's own installer creates them.
    std::vector<std::wstring> roots;
    for (const wchar_t* var : {L"ANDROID_SDK_ROOT", L"ANDROID_HOME"}) {
        wchar_t value[MAX_PATH] = {0};
        const DWORD got = GetEnvironmentVariableW(var, value, static_cast<DWORD>(MAX_PATH));
        if (got > 0 && got < MAX_PATH) roots.emplace_back(value);
    }
    wchar_t local[MAX_PATH] = {0};
    const DWORD got = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(MAX_PATH));
    if (got > 0 && got < MAX_PATH) roots.emplace_back(std::wstring(local) + L"\\Android\\Sdk");

    for (const auto& root : roots) {
        std::wstring candidate = root + L"\\platform-tools\\adb.exe";
        if (file_exists(candidate)) {
            path = candidate;
            return true;
        }
    }

    error =
        "adb.exe nao encontrado (PATH, ANDROID_SDK_ROOT, ANDROID_HOME, %LOCALAPPDATA%\\Android\\Sdk). "
        "Instale o Android Platform Tools e adicione platform-tools ao PATH, "
        "ou informe Config::adb_path.";
    return false;
}

bool resolve(const Config& cfg, std::string& error) {
    error.clear();
    if (!cfg.adb_path.empty()) {
        if (!file_exists(cfg.adb_path)) {
            error = "adb informado nao existe: " + narrow(cfg.adb_path);
            cfg.resolved.clear();
            cfg.resolve_error = error;
            return false;
        }
        cfg.resolved = cfg.adb_path;
        return true;
    }
    if (!cfg.resolved.empty() && file_exists(cfg.resolved)) return true;
    if (!locate_adb(cfg.resolved, error)) {
        cfg.resolve_error = error;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// device list parsing (pure)
// ---------------------------------------------------------------------------

namespace {

adb_state state_from_text(const std::string& text) {
    if (text == "device") return adb_state::device;
    if (text == "unauthorized") return adb_state::unauthorized;
    if (text == "offline") return adb_state::offline;
    if (text == "recovery") return adb_state::recovery;
    if (text == "bootloader" || text == "fastboot") return adb_state::bootloader;
    if (text == "sideload") return adb_state::sideload;
    if (text == "authorizing") return adb_state::authorizing;
    return adb_state::unknown;
}

// Splits on runs of whitespace. adb never quotes inside 'devices -l' output, so
// this is exact rather than lossy.
std::vector<std::string> split_ws(const std::string& line) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        tokens.push_back(line.substr(start, i - start));
    }
    return tokens;
}

bool looks_like_advisory(const std::string& line) {
    static const char* prefixes[] = {"adb server version", "adb: ", "error:", "cannot ",
                                     "warning:",         "* ",    "failed "};
    for (const char* prefix : prefixes) {
        if (line.compare(0, std::strlen(prefix), prefix) == 0) return true;
    }
    return false;
}

}  // namespace

bool parse_devices(const std::string& text, std::vector<Device>& devices, std::string& error) {
    devices.clear();
    error.clear();

    if (trim(text).empty()) {
        // adb always prints the header, even with zero devices attached. Silence
        // means the transport never ran, and reporting "0 devices" would be a
        // false negative on evidence.
        error = "saida vazia: adb devices nao produziu nem o cabecalho";
        return false;
    }

    // adb prints this header even with nothing attached, so its presence is what
    // separates "no devices" from "the command never ran".
    const char* const kHeader = "List of devices attached";
    const std::size_t header_len = std::strlen(kHeader);

    bool saw_header = false;
    for (const auto& raw_line : split_lines(text)) {
        const std::string line = trim(raw_line);
        if (line.empty()) continue;
        if (line.compare(0, header_len, kHeader) == 0) {
            saw_header = true;
            continue;
        }
        if (line[0] == '*') continue;
        if (looks_like_advisory(line)) continue;

        const std::vector<std::string> tokens = split_ws(line);
        if (tokens.size() < 2) {
            error = "linha de dispositivo sem estado: '" + line + "'";
            return false;
        }

        Device device;
        device.serial = tokens[0];

        std::size_t next = 2;
        if (tokens[1] == "no" && tokens.size() >= 3 && tokens[2] == "permissions") {
            // 'no permissions (user in plugdev group; ...)' - the explanation
            // carries spaces, so the whole remainder is the state text.
            device.state = adb_state::no_permissions;
            const std::size_t at = line.find("no permissions");
            device.state_text = at == std::string::npos ? "no permissions" : line.substr(at);
            next = tokens.size();
        } else {
            device.state_text = tokens[1];
            device.state = state_from_text(tokens[1]);
        }

        for (std::size_t i = next; i < tokens.size(); ++i) {
            const std::size_t colon = tokens[i].find(':');
            if (colon == std::string::npos) continue;
            const std::string key = tokens[i].substr(0, colon);
            const std::string value = tokens[i].substr(colon + 1);
            if (key == "model") device.model = value;
            else if (key == "product") device.product = value;
            else if (key == "device") device.device = value;
            else if (key == "transport_id") device.transport_id = value;
            else if (key == "usb") device.usb = value;
        }
        devices.push_back(device);
    }

    if (!saw_header) {
        error = "cabecalho 'List of devices attached' ausente na saida";
        return false;
    }
    return true;
}

DeviceList list_devices(const Config& cfg, std::uint32_t timeout_ms) {
    DeviceList list;
    std::string error;
    if (!resolve(cfg, error)) {
        list.error = error;
        return list;
    }

    const std::wstring command = L"\"" + cfg.resolved + L"\" devices -l";
    const ProcessResult run = run_capture(command, timeout_ms == 0 ? cfg.default_timeout_ms : timeout_ms);
    list.raw = run.out;
    if (!run.started) {
        list.error = "falha ao executar adb: " + run.error;
        return list;
    }
    if (run.timed_out) {
        list.error = "adb devices -l excedeu o tempo limite";
        return list;
    }
    if (!run.err.empty()) list.notices.push_back(trim(run.err));

    std::string parse_error;
    const bool parsed = parse_devices(run.out, list.devices, parse_error);
    if (!parsed) {
        list.error = parse_error;
        return list;
    }
    if (run.exit_code != 0) {
        // adb returns non-zero for a dead server; the device list may still be
        // readable, so it is reported, not discarded.
        list.notices.push_back("adb devices saiu com codigo " + std::to_string(run.exit_code));
    }
    list.ok = true;
    return list;
}

// ---------------------------------------------------------------------------
// commands
// ---------------------------------------------------------------------------

namespace {

// Device-side paths are consumed by a POSIX shell, not by CreateProcess: single
// quotes are the only reliable way to keep a path with spaces or '$' intact.
std::string quote_remote(const std::string& path) {
    std::string out = "'";
    for (const char c : path) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out += "'";
    return out;
}

// CreateProcess quoting: wrap in double quotes, escape inner quotes.
std::wstring quote_win(const std::wstring& value) {
    std::wstring out = L"\"";
    for (const wchar_t c : value) {
        if (c == L'"') out += L"\\\"";
        else out.push_back(c);
    }
    out += L"\"";
    return out;
}

}  // namespace

ShellResult shell(const Config& cfg, const std::string& serial, const std::string& command,
                  std::uint32_t timeout_ms) {
    ShellResult result;
    std::string error;
    if (!resolve(cfg, error)) {
        result.error = error;
        return result;
    }
    if (serial.empty()) {
        result.error = "serial do dispositivo nao informado";
        return result;
    }

    const std::wstring cmd = L"\"" + cfg.resolved + L"\" -s " + quote_win(widen(serial)) +
                             L" shell " + quote_win(widen(command));
    const ProcessResult run = run_capture(cmd, timeout_ms == 0 ? cfg.default_timeout_ms : timeout_ms);
    if (!run.started) {
        result.error = "falha ao executar adb: " + run.error;
        return result;
    }
    if (run.timed_out) {
        result.error = "comando excedeu o tempo limite: " + command;
        return result;
    }
    result.out = run.out;
    result.err = run.err;
    result.exit_code = run.exit_code;
    result.ok = true;
    return result;
}

bool getprop(const Config& cfg, const std::string& serial, const std::string& prop,
             std::string& value, std::string& error) {
    value.clear();
    error.clear();
    if (prop.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
        std::string::npos) {
        error = "nome de propriedade invalido: " + prop;
        return false;
    }
    const ShellResult run = shell(cfg, serial, "getprop " + prop);
    if (!run.ok) {
        error = run.error;
        return false;
    }
    value = trim(run.out);
    return true;
}

bool probe(const Config& cfg, const std::string& serial, DeviceInfo& info, std::string& error) {
    info = DeviceInfo{};
    info.serial = serial;

    struct prop_map {
        const char* prop;
        std::string* slot;
    };
    const prop_map wanted[] = {
        {"ro.product.model", &info.model},
        {"ro.product.manufacturer", &info.manufacturer},
        {"ro.product.device", &info.device},
        {"ro.build.version.release", &info.android_release},
        {"ro.build.id", &info.build_id},
        {"ro.build.fingerprint", &info.fingerprint},
    };

    int failures = 0;
    for (const auto& item : wanted) {
        std::string value;
        std::string prop_error;
        if (!getprop(cfg, serial, item.prop, value, prop_error)) {
            if (failures == 0) error = prop_error;
            ++failures;
            continue;
        }
        *item.slot = value;
    }
    if (failures == static_cast<int>(sizeof(wanted) / sizeof(wanted[0]))) {
        if (error.empty()) error = "nenhuma propriedade pode ser lida do dispositivo";
        return false;
    }

    std::string sdk;
    std::string sdk_error;
    if (getprop(cfg, serial, "ro.build.version.sdk", sdk, sdk_error)) {
        if (!sdk.empty() && sdk.find_first_not_of("0123456789") == std::string::npos) {
            info.api_level = std::atoi(sdk.c_str());
        }
    }
    info.complete = !info.model.empty() && info.api_level > 0;
    return true;
}

bool remote_size(const Config& cfg, const std::string& serial, const std::string& remote,
                 std::uint64_t& size, std::string& error) {
    size = 0;
    error.clear();
    const std::string quoted = quote_remote(remote);

    ShellResult run = shell(cfg, serial, "stat -c %s " + quoted);
    if (run.ok && run.exit_code == 0) {
        const std::string text = trim(run.out);
        if (!text.empty() && text.find_first_not_of("0123456789") == std::string::npos) {
            size = std::strtoull(text.c_str(), nullptr, 10);
            return true;
        }
    }
    run = shell(cfg, serial, "wc -c < " + quoted);
    if (run.ok && run.exit_code == 0) {
        const std::string text = trim(run.out);
        std::size_t i = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
        if (i > 0) {
            size = std::strtoull(text.substr(0, i).c_str(), nullptr, 10);
            return true;
        }
    }
    error = "o dispositivo nao informou tamanho para " + remote + " (stat e wc indisponiveis)";
    return false;
}

PullStats pull(const Config& cfg, const std::string& serial, const std::string& remote,
               const std::wstring& local, std::uint32_t timeout_ms, const percent_fn& on_percent) {
    PullStats stats;
    std::string error;
    if (!resolve(cfg, error)) {
        stats.error = error;
        return stats;
    }
    if (serial.empty()) {
        stats.error = "serial do dispositivo nao informado";
        return stats;
    }
    if (remote.empty() || local.empty()) {
        stats.error = "caminho remoto ou local vazio";
        return stats;
    }

    std::uint64_t expected = 0;
    stats.size_known = remote_size(cfg, serial, remote, expected, error);
    if (stats.size_known) stats.expected_bytes = expected;

    std::uint32_t budget = timeout_ms;
    if (budget == 0) {
        budget = cfg.pull_floor_ms;
        if (stats.size_known) {
            const std::uint64_t mb = stats.expected_bytes / (1024 * 1024) + 1;
            const std::uint64_t scaled = mb * cfg.pull_ms_per_mb;
            if (scaled > budget && scaled < 0x7FFFFFFF) budget = static_cast<std::uint32_t>(scaled);
        }
    }

    // A trailing backslash right before the closing quote would be read as an
    // escaped quote by the child's command-line parser.
    std::wstring local_clean = local;
    while (!local_clean.empty() && (local_clean.back() == L'\\' || local_clean.back() == L'/')) {
        local_clean.pop_back();
    }
    if (local_clean.empty()) {
        stats.error = "caminho local invalido";
        return stats;
    }

    // adb never creates the destination directory and its own message is
    // cryptic; say what is actually wrong before spawning anything.
    {
        const std::size_t cut = local_clean.find_last_of(L"\\/");
        if (cut != std::wstring::npos) {
            const std::wstring parent = local_clean.substr(0, cut);
            const DWORD attributes = GetFileAttributesW(parent.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                stats.error = "diretorio de destino nao existe: " + narrow(parent);
                return stats;
            }
        }
    }

    const std::wstring cmd = L"\"" + cfg.resolved + L"\" -s " + quote_win(widen(serial)) +
                             L" pull " + quote_win(widen(remote)) + L" " + quote_win(local_clean);

    int last_percent = -1;
    chunk_fn tap;
    if (on_percent) {
        tap = [&](const std::string& chunk) {
            // adb prints '[ 45%]' on old platform-tools and only a final summary
            // line on new ones; both are handled by scanning for 'N%'.
            for (std::size_t i = 0; i < chunk.size(); ++i) {
                if (chunk[i] != '%') continue;
                std::size_t begin = i;
                while (begin > 0 && chunk[begin - 1] >= '0' && chunk[begin - 1] <= '9') --begin;
                if (begin == i) continue;
                const int value = std::atoi(chunk.substr(begin, i - begin).c_str());
                if (value < 0 || value > 100 || value == last_percent) continue;
                last_percent = value;
                on_percent(value);
            }
        };
    }

    const ProcessResult run = run_capture(cmd, budget, tap);
    stats.elapsed_ms = run.elapsed_ms;
    if (!run.started) {
        stats.error = "falha ao executar adb pull: " + run.error;
        return stats;
    }
    if (run.timed_out) {
        stats.error = "adb pull excedeu o tempo limite";
        return stats;
    }

    if (!file_exists(local_clean)) {
        stats.error = "adb pull nao produziu arquivo local (" + narrow(local_clean) + ")";
        if (!trim(run.err).empty()) stats.error += ": " + trim(run.err);
        return stats;
    }
    stats.bytes = file_size(local_clean);

    if (run.exit_code != 0) {
        stats.error = "adb pull saiu com codigo " + std::to_string(run.exit_code) + ": " + trim(run.err);
        return stats;
    }
    if (stats.size_known) {
        stats.size_verified = (stats.bytes == stats.expected_bytes);
        if (!stats.size_verified) {
            stats.error = "tamanho divergente: dispositivo " + std::to_string(stats.expected_bytes) +
                          " bytes, local " + std::to_string(stats.bytes) + " bytes";
            return stats;
        }
    }
    if (on_percent && last_percent < 100) on_percent(100);
    stats.ok = true;
    return stats;
}

}  // namespace olho::android
