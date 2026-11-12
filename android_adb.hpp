// language: C++, file: android_adb.hpp, target: Windows 11 (MSVC / MinGW g++)
// Android logical acquisition over ADB. Links: kernel32 only.
//
// Everything provable without a handset is a pure function: parse_devices()
// takes the exact bytes 'adb devices -l' prints and returns typed records, so a
// transport bug and a parsing bug can never hide behind each other. The process
// layer only has to deliver those bytes intact, with a deadline: a hung adb
// server must not wedge an acquisition that is already under way in a lab.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace olho::android {

// adb prints the state verbatim; this is the closed set worth distinguishing.
// Anything else is kept as unknown with the raw text preserved, because a new
// transport state is evidence, not a crash.
enum class adb_state {
    device,
    unauthorized,
    offline,
    recovery,
    bootloader,
    sideload,
    authorizing,
    no_permissions,
    unknown
};

struct Device {
    std::string serial;
    adb_state state = adb_state::unknown;
    std::string state_text;      // exactly as adb printed it
    std::string model;           // model:Pixel_4  -> underscores are adb's escaping
    std::string product;
    std::string device;
    std::string transport_id;
    std::string usb;
    bool usable() const { return state == adb_state::device; }
};

const char* to_string(adb_state state);

// Outcome of one finished child process. started == true only means the process
// was created and reaped; exit_code carries success or failure.
struct ProcessResult {
    bool started = false;
    bool timed_out = false;
    std::uint32_t exit_code = 0;
    std::string out;
    std::string err;
    std::string error;           // set when started == false
    std::uint32_t elapsed_ms = 0;
};

// Progress taps: raw stdout chunks as they arrive, so a caller can render adb's
// own percentages instead of inventing an animation.
using chunk_fn = std::function<void(const std::string&)>;

// Runs a command line with stdout and stderr redirected to pipes and drained
// concurrently through a polling loop (no reader threads means no deadlock and
// no truncated output when the child fills both pipes). timeout_ms == 0 waits
// forever. On timeout the child is terminated and timed_out is set.
ProcessResult run_capture(const std::wstring& command_line, std::uint32_t timeout_ms,
                          const chunk_fn& on_stdout = nullptr);

// Absolute path of adb.exe: PATH first, then the ANDROID_HOME / ANDROID_SDK_ROOT
// / LOCALAPPDATA layouts. Absent adb is a reported error, never a crash.
bool locate_adb(std::wstring& path, std::string& error);

std::uint64_t file_size(const std::wstring& path);

struct Config {
    std::wstring adb_path;              // empty = locate on demand
    std::uint32_t default_timeout_ms = 20000;
    // A pull is bounded by bytes, not by a flat deadline: a 2 GB media tree on a
    // USB 2 port is slow, and killing it would look like a device fault.
    std::uint32_t pull_floor_ms = 60000;
    std::uint32_t pull_ms_per_mb = 4000;

    // Cached result of locate_adb(); filled by resolve().
    mutable std::wstring resolved;
    mutable std::string resolve_error;
};

// Resolves adb once and keeps the answer. Returns false with a clear message.
bool resolve(const Config& cfg, std::string& error);

// Pure: no process is spawned, no filesystem is touched. Returns false only when
// the text cannot be trusted as device-list output; devices parsed so far are
// still returned so the caller can log them as evidence.
bool parse_devices(const std::string& text, std::vector<Device>& devices, std::string& error);

struct DeviceList {
    bool ok = false;
    std::vector<Device> devices;
    std::vector<std::string> notices;    // daemon banners and adb warnings
    std::string raw;                     // verbatim, for the evidence log
    std::string error;
};

DeviceList list_devices(const Config& cfg, std::uint32_t timeout_ms = 0);

struct ShellResult {
    bool ok = false;                     // transport reached the device
    std::uint32_t exit_code = 0;         // exit status of the remote command
    std::string out;
    std::string err;
    std::string error;                   // transport-level failure
};

ShellResult shell(const Config& cfg, const std::string& serial, const std::string& command,
                  std::uint32_t timeout_ms = 0);

// getprop returns the value with the trailing newline removed. ok == false plus
// error when the transport failed; an empty value on a live device is a value.
bool getprop(const Config& cfg, const std::string& serial, const std::string& prop,
             std::string& value, std::string& error);

struct DeviceInfo {
    std::string serial;
    std::string model;
    std::string manufacturer;
    std::string device;
    std::string android_release;
    std::string build_id;
    int api_level = 0;
    std::string fingerprint;
    bool complete = false;               // every field the report needs is present
};

// Builds DeviceInfo from the device itself. Never guesses: a missing property
// stays empty and complete stays false.
bool probe(const Config& cfg, const std::string& serial, DeviceInfo& info, std::string& error);

struct PullStats {
    bool ok = false;
    std::uint64_t bytes = 0;             // local size after the copy
    std::uint64_t expected_bytes = 0;    // device-side size, 0 when unknown
    bool size_known = false;
    bool size_verified = false;
    std::uint32_t elapsed_ms = 0;
    std::string error;
};

using percent_fn = std::function<void(int)>;

// Copies one remote path to one local path. Success requires the local file to
// exist and, when the device reported a size, to match it: an interrupted pull
// that still exited 0 is a real adb behaviour and must not be reported as a
// recovered file.
PullStats pull(const Config& cfg, const std::string& serial, const std::string& remote,
               const std::wstring& local, std::uint32_t timeout_ms = 0,
               const percent_fn& on_percent = nullptr);

// Device-side file size via 'stat -c %s', falling back to 'wc -c'. Returns false
// when neither is available (very old toybox/busybox images).
bool remote_size(const Config& cfg, const std::string& serial, const std::string& remote,
                 std::uint64_t& size, std::string& error);

}  // namespace olho::android
