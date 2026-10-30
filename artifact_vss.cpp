#include "artifact_vss.hpp"

#include "artifacts.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace deep {
namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r'))
        ++begin;
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r'))
        --end;
    return text.substr(begin, end - begin);
}

bool starts_with_ci(const std::string& text, const std::string& prefix) {
    if (text.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    }
    return true;
}

struct Shadow {
    std::string id;
    std::string volume;
    std::string originating_machine;
    std::string service_machine;
    std::string provider;
    std::string attributes;
    std::string creation_time;
    std::string exposure_time;
};

void emit(const Shadow& shadow, const ArtifactSink& sink, int& rows) {
    if (shadow.id.empty() && shadow.volume.empty()) return;
    ArtifactRow row;
    row.type = "vss";
    row.source = "vssadmin list shadows";
    row.key = shadow.id;
    row.path = shadow.volume;
    row.name = shadow.id;
    row.data = "{\"id\":" + art::json_escape(shadow.id) +
        ",\"volume\":" + art::json_escape(shadow.volume) +
        ",\"originating_machine\":" + art::json_escape(shadow.originating_machine) +
        ",\"service_machine\":" + art::json_escape(shadow.service_machine) +
        ",\"provider\":" + art::json_escape(shadow.provider) +
        ",\"attributes\":" + art::json_escape(shadow.attributes) +
        ",\"creation_time\":" + art::json_escape(shadow.creation_time) +
        ",\"exposure_time\":" + art::json_escape(shadow.exposure_time) + "}";
    sink(row);
    ++rows;
}

bool capture_command(const std::wstring& command, std::string& output,
                     std::wstring& error) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE read_pipe = nullptr, write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &attributes, 0)) {
        error = L"CreatePipe failed";
        return false;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::wstring mutable_command = command;
    const BOOL created = CreateProcessW(nullptr, mutable_command.data(), nullptr,
        nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(write_pipe);
    if (!created) {
        CloseHandle(read_pipe);
        error = L"CreateProcess failed: " + std::to_wstring(GetLastError());
        return false;
    }
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        output.append(buffer, read);
    WaitForSingleObject(process.hProcess, 30000);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    CloseHandle(read_pipe);
    if (code != 0) {
        error = L"vssadmin exit code " + std::to_wstring(code);
        return false;
    }
    return true;
}

} // namespace

int parse_vss_text(const std::string& text, const ArtifactSink& sink,
                   std::wstring& error) {
    (void)error;
    int rows = 0;
    Shadow current;
    bool active = false;
    std::size_t position = 0;
    while (position <= text.size()) {
        const std::size_t end = text.find_first_of("\r\n", position);
        const std::string line = trim(text.substr(position,
            end == std::string::npos ? std::string::npos : end - position));
        position = end == std::string::npos ? text.size() + 1 : end + 1;
        if (line.empty()) continue;
        auto field = [&](const char* en, const char* pt) -> std::string {
            const std::string e(en), p(pt);
            if (starts_with_ci(line, e)) return trim(line.substr(e.size()));
            if (starts_with_ci(line, p)) return trim(line.substr(p.size()));
            return {};
        };
        std::string value;
        if (!(value = field("Shadow Copy ID:", "ID da cópia de sombra:")).empty()) {
            if (active) emit(current, sink, rows);
            current = Shadow{};
            current.id = value;
            active = true;
        } else if (!(value = field("Shadow Copy Volume:",
                                   "Volume da cópia de sombra:")).empty()) {
            current.volume = value;
        } else if (!(value = field("Originating Machine:",
                                   "Computador de origem:")).empty()) {
            current.originating_machine = value;
        } else if (!(value = field("Service Machine:",
                                   "Computador de serviço:")).empty()) {
            current.service_machine = value;
        } else if (!(value = field("Provider:", "Provedor:")).empty()) {
            current.provider = value;
        } else if (!(value = field("Attributes:", "Atributos:")).empty()) {
            current.attributes = value;
        } else if (!(value = field("Creation Time:", "Hora de criação:")).empty()) {
            current.creation_time = value;
        } else if (!(value = field("Exposure Time:", "Hora de exposição:")).empty()) {
            current.exposure_time = value;
        }
    }
    if (active) emit(current, sink, rows);
    if (rows == 0) {
        error = L"no shadow copies found in vssadmin output";
        return -1;
    }
    return rows;
}

int collect_vss(const ArtifactSink& sink, std::wstring& error) {
    std::string output;
    if (!capture_command(L"vssadmin.exe list shadows", output, error)) return -1;
    // vssadmin writes OEM/ANSI on a console; convert to UTF-8 before parsing
    // the field labels, which are ASCII in both English and Portuguese.
    return parse_vss_text(output, sink, error);
}

} // namespace deep
