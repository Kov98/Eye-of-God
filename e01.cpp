// language: C++, file: e01.cpp, target: Windows 10/11
// Thin, auditable wrappers for the bundled libewf tools. No E01 parsing is
// reimplemented here: ewfacquire writes a real E01, ewfexport converts it back
// to raw, and the existing NTFS engine parses the raw image.

#include "e01.hpp"

#include "artifacts.hpp"

#include <windows.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace deep {
namespace {

std::wstring module_directory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring value(path);
    const std::size_t slash = value.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : value.substr(0, slash);
}

std::wstring find_tool(const std::wstring& name) {
    const std::wstring base = module_directory();
    const std::wstring candidates[] = {
        art::join_path(art::join_path(base, L"libewf"), name),
        art::join_path(art::join_path(base, L"third_party\\libewf"), name),
        art::join_path(base, name)
    };
    for (const auto& candidate : candidates) {
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            return candidate;
    }
    return {};
}

std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

bool run_tool(const std::wstring& tool, const std::wstring& arguments,
              const std::wstring& working_directory, DWORD& exit_code,
              std::wstring& error) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = quote(tool) + L" " + arguments;
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr,
                        working_directory.empty() ? nullptr : working_directory.c_str(),
                        &startup, &process)) {
        error = L"cannot start " + tool + L", error " +
            std::to_wstring(GetLastError());
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    if (exit_code != 0) {
        error = L"tool failed with exit code " + std::to_wstring(exit_code);
        return false;
    }
    return true;
}

} // namespace

int e01_create(const std::wstring& source, const std::wstring& out_base,
               std::wstring& error) {
    if (source.empty() || out_base.empty()) {
        error = L"source/out base is empty";
        return 2;
    }
    const std::wstring target = out_base + L".E01";
    if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) {
        error = L"target already exists: " + target;
        return 1;
    }
    const std::wstring tool = find_tool(L"ewfacquire.exe");
    if (tool.empty()) {
        error = L"ewfacquire.exe not found (libewf/ next to the executable)";
        return 1;
    }
    const std::size_t slash = source.find_last_of(L"\\/");
    const std::wstring source_dir = slash == std::wstring::npos ? L"." :
        source.substr(0, slash);
    const std::wstring source_name = slash == std::wstring::npos ? source :
        source.substr(slash + 1);
    const std::wstring arguments =
        L"-u -q -t " + quote(out_base) +
        L" -C \"Olho de Deus\" -D \"E01 acquisition\" -e \"operator\"" +
        L" -m fixed -M physical -c deflate -S 200MiB -b 64 -P 512" +
        L" -o 0 -B 0 " + quote(source_name);
    DWORD exit_code = 0;
    if (!run_tool(tool, arguments, source_dir, exit_code, error)) return 1;
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"ewfacquire finished but the E01 file is missing";
        return 1;
    }
    std::cout << "e01: created=" << art::to_utf8(target) << "\n";
    return 0;
}

int e01_export(const std::wstring& e01_file, const std::wstring& out_raw,
               std::wstring& error) {
    if (e01_file.empty() || out_raw.empty()) {
        error = L"E01/output path is empty";
        return 2;
    }
    std::wstring base = out_raw;
    if (base.size() >= 4 &&
        _wcsicmp(base.c_str() + base.size() - 4, L".raw") == 0)
        base.resize(base.size() - 4);
    const std::wstring produced = base + L".raw";
    if (GetFileAttributesW(produced.c_str()) != INVALID_FILE_ATTRIBUTES) {
        error = L"target already exists: " + produced;
        return 1;
    }
    const std::wstring tool = find_tool(L"ewfexport.exe");
    if (tool.empty()) {
        error = L"ewfexport.exe not found (libewf/ next to the executable)";
        return 1;
    }
    const std::size_t slash = e01_file.find_last_of(L"\\/");
    const std::wstring e01_dir = slash == std::wstring::npos ? L"." :
        e01_file.substr(0, slash);
    const std::wstring e01_name = slash == std::wstring::npos ? e01_file :
        e01_file.substr(slash + 1);
    const std::wstring arguments = L"-u -q -t " + quote(base) +
        L" -f raw -o 0 -B 0 " + quote(e01_name);
    DWORD exit_code = 0;
    if (!run_tool(tool, arguments, e01_dir, exit_code, error)) return 1;
    if (GetFileAttributesW(produced.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"ewfexport finished but the raw file is missing";
        return 1;
    }
    if (_wcsicmp(produced.c_str(), out_raw.c_str()) != 0) {
        if (!MoveFileExW(produced.c_str(), out_raw.c_str(),
                         MOVEFILE_REPLACE_EXISTING)) {
            error = L"cannot rename exported raw, error " +
                std::to_wstring(GetLastError());
            return 1;
        }
    }
    std::cout << "e01: exported=" << art::to_utf8(out_raw) << "\n";
    return 0;
}

} // namespace deep
