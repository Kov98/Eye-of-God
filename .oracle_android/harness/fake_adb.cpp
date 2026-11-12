// language: C++, file: fake_adb.cpp, target: Windows 11
// A stand-in adb.exe for the process layer. It is not a mock library: the real
// module spawns it with CreateProcessW, reads its pipes and reaps it, so the
// transport code path is exercised end to end without a handset.
//
// Behaviour is selected by the serial, which is how a real device would differ:
//   -s HANG      ... sleeps past any sane deadline  (timeout path)
//   -s ERR       ... writes to stderr, exits 3      (stderr + exit code path)
//   -s SHORTPULL ... writes fewer bytes than it claims (pull verification)
//   anything else answers like a live device.
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

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

void print_out(const char* text) {
    std::fwrite(text, 1, std::strlen(text), stdout);
    std::fflush(stdout);
}

void print_err(const char* text) {
    std::fwrite(text, 1, std::strlen(text), stderr);
    std::fflush(stderr);
}

std::string device_payload(int bytes, unsigned seed) {
    std::string out;
    out.reserve(static_cast<std::size_t>(bytes));
    unsigned state = seed;
    for (int i = 0; i < bytes; ++i) {
        state = state * 1664525u + 1013904223u;
        out.push_back(static_cast<char>(state >> 24));
    }
    return out;
}

bool write_file(const std::wstring& path, const std::string& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const BOOL ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &put, nullptr);
    CloseHandle(file);
    return ok != 0 && put == data.size();
}

std::string prop_value(const std::string& prop) {
    if (prop == "ro.product.model") return "Pixel_Fake";
    if (prop == "ro.product.manufacturer") return "Google";
    if (prop == "ro.product.device") return "fakefish";
    if (prop == "ro.build.version.sdk") return "34";
    if (prop == "ro.build.version.release") return "14";
    if (prop == "ro.build.id") return "FAKE1";
    if (prop == "ro.build.fingerprint") return "google/fakefish/fakefish:14/FAKE1/1:user/release-keys";
    return "";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(narrow(argv[i]));

    if (args.empty()) {
        print_err("fake_adb: sem argumentos\n");
        return 2;
    }
    if (args[0] == "devices") {
        print_err("* daemon not running; starting now at tcp:5037\n");
        print_err("* daemon started successfully\n");
        print_out("List of devices attached\n");
        print_out("emulator-5554          device product:sdk_gphone64_x86_64 model:sdk_gphone64_x86_64 device:emu64xa transport_id:1\n");
        print_out("0123456789ABCDEF       unauthorized usb:1-2 transport_id:2\n");
        print_out("\n");
        return 0;
    }

    std::string serial;
    std::size_t index = 1;
    if (args.size() >= 2 && args[0] == "-s") {
        serial = args[1];
        index = 2;
    }
    if (index >= args.size()) return 2;

    const std::string verb = args[index];
    if (verb == "shell") {
        std::string command;
        for (std::size_t i = index + 1; i < args.size(); ++i) {
            if (!command.empty()) command.push_back(' ');
            command += args[i];
        }
        if (serial == "HANG") {
            Sleep(6000);
            return 1;
        }
        if (serial == "ERR") {
            print_err("error: device offline\n");
            return 3;
        }
        if (command.find("getprop") == 0) {
            const std::string prop = command.size() > 8 ? command.substr(8) : std::string();
            const std::string value = prop_value(prop);
            std::printf("%s\n", value.c_str());
            return 0;
        }
        if (command.find("stat -c %s") == 0) {
            print_out("4096\n");
            return 0;
        }
        if (command.find("wc -c") == 0) {
            print_out("4096\n");
            return 0;
        }
        print_out("\n");
        return 0;
    }

    if (verb == "pull") {
        if (args.size() < index + 3) {
            print_err("fake_adb: pull sem argumentos\n");
            return 2;
        }
        const std::wstring local = widen(args[index + 2]);
        const int bytes = serial == "SHORTPULL" ? 100 : 4096;
        const std::string payload = device_payload(bytes, serial == "SHORTPULL" ? 7u : 3u);
        if (!write_file(local, payload)) {
            print_err("fake_adb: nao consegui escrever o destino\n");
            return 1;
        }
        print_out("[ 50%] ");
        print_out("[100%] 1 file pulled, 0 skipped.\n");
        return 0;
    }

    print_err("fake_adb: verbo desconhecido\n");
    return 2;
}
