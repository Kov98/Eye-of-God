#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <ipifcons.h>
#include <winevt.h>
#include <bcrypt.h>
#include <lmcons.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <cwchar>
#include <cwctype>
#include <cctype>
#include <cstring>
#include <conio.h>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "sdk.hpp"
#include "deep.hpp"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "xmllite.lib")
#pragma comment(lib, "uuid.lib")

namespace {

volatile LONG g_cancelRequested = FALSE;

bool cancellationRequested() {
    if (InterlockedCompareExchange(&g_cancelRequested, FALSE, FALSE) != FALSE) return true;
    if (_kbhit()) {
        const int key = _getch();
        if (key == 'q' || key == 'Q') {
            InterlockedExchange(&g_cancelRequested, TRUE);
            std::cout << "\nCancelamento solicitado; finalizando a fonte atual com status parcial.\n";
            return true;
        }
    }
    return InterlockedCompareExchange(&g_cancelRequested, FALSE, FALSE) != FALSE;
}

std::string toUtf8(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return "";
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return "";
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], required, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

bool chooseEvidenceFolder(std::wstring& folder, bool& cancelled, DWORD& error,
                          const wchar_t* title =
                              L"Selecione a pasta para inventario de metadados (somente leitura)") {
    cancelled = false;
    error = ERROR_SUCCESS;
    const HRESULT initialization = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE) {
        error = static_cast<DWORD>(HRESULT_CODE(initialization));
        return false;
    }
    const bool uninitialize = SUCCEEDED(initialization);
    BROWSEINFOW browse = {};
    browse.lpszTitle = title;
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE selection = SHBrowseForFolderW(&browse);
    if (selection == nullptr) {
        if (uninitialize) CoUninitialize();
        cancelled = true;
        return true;
    }

    std::vector<wchar_t> path(32768, L'\0');
    const bool resolved = SHGetPathFromIDListW(selection, &path[0]) != FALSE;
    if (!resolved) error = GetLastError();
    CoTaskMemFree(selection);
    if (uninitialize) CoUninitialize();
    if (!resolved) return false;
    folder.assign(&path[0]);
    return true;
}

struct CaseMetadata {
    std::string caseId;
    std::string collectedAtUtc;
    std::string completedAtUtc;
    std::string computer;
    std::string operatorName;
    std::string timeZoneState;
    std::string timeZoneStandardName;
    std::string timeZoneDaylightName;
    std::string timeZoneStatus;
    int utcOffsetMinutes = 0;
    std::string windowsProductName;
    std::string windowsDisplayVersion;
    std::string windowsBuild;
    std::string nativeArchitecture;
    std::string elevationStatus;
    std::wstring evidenceFolder;
    int selection = 0;
    bool customCollection = false;
    bool collectSystem = false;
    bool collectNetwork = false;
    bool collectProcesses = false;
    bool collectSoftware = false;
    bool collectStartup = false;
    bool collectLogs = false;
    bool collectServicesTasks = false;
    bool collectFiles = false;
    bool collectNtfsArtifacts = false;
    bool collectUserActivity = false;
    std::vector<std::wstring> eventChannels;
};

struct ProcessInfo {
    std::string name;
    DWORD parentPid;
};

using ProcessMap = std::map<DWORD, ProcessInfo>;
using NetworkAddressSet = std::set<std::string>;

bool snapshotProcesses(ProcessMap& processes, DWORD& error);
bool snapshotTcpRemoteAddresses(NetworkAddressSet& addresses, DWORD& error);

std::string registryString(HKEY root, const char* path, const char* name, REGSAM view);

std::string getNativeArchitectureName() {
    SYSTEM_INFO systemInfo = {};
    GetNativeSystemInfo(&systemInfo);
    switch (systemInfo.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
    case PROCESSOR_ARCHITECTURE_ARM64: return "ARM64";
    case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
    case PROCESSOR_ARCHITECTURE_IA64: return "IA64";
    default: return "unknown";
    }
}

std::string getElevationStatus() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return "unavailable_error_" + std::to_string(GetLastError());
    }
    TOKEN_ELEVATION elevation = {};
    DWORD returned = 0;
    const BOOL queried = GetTokenInformation(token, TokenElevation, &elevation,
                                              sizeof(elevation), &returned);
    const DWORD queryError = queried ? ERROR_SUCCESS : GetLastError();
    CloseHandle(token);
    if (!queried) return "unavailable_error_" + std::to_string(queryError);
    return elevation.TokenIsElevated ? "elevated" : "not_elevated";
}

void collectTimeZoneContext(CaseMetadata& metadata) {
    TIME_ZONE_INFORMATION timeZone = {};
    const DWORD timeZoneState = GetTimeZoneInformation(&timeZone);
    if (timeZoneState == TIME_ZONE_ID_INVALID) {
        metadata.timeZoneStatus = "unavailable_error_" + std::to_string(GetLastError());
        return;
    }

    switch (timeZoneState) {
    case TIME_ZONE_ID_STANDARD: metadata.timeZoneState = "standard"; break;
    case TIME_ZONE_ID_DAYLIGHT: metadata.timeZoneState = "daylight"; break;
    default: metadata.timeZoneState = "unknown_or_no_transition"; break;
    }
    metadata.timeZoneStandardName = toUtf8(timeZone.StandardName);
    metadata.timeZoneDaylightName = toUtf8(timeZone.DaylightName);
    LONG effectiveBias = timeZone.Bias;
    if (timeZoneState == TIME_ZONE_ID_STANDARD) {
        effectiveBias += timeZone.StandardBias;
    } else if (timeZoneState == TIME_ZONE_ID_DAYLIGHT) {
        effectiveBias += timeZone.DaylightBias;
    }
    metadata.utcOffsetMinutes = static_cast<int>(-effectiveBias);
    metadata.timeZoneStatus = "available";
}

std::string formatUtcTimestamp(const SYSTEMTIME& time) {
    std::ostringstream timestamp;
    timestamp << std::setfill('0') << std::setw(4) << time.wYear << '-'
              << std::setw(2) << time.wMonth << '-' << std::setw(2) << time.wDay << 'T'
              << std::setw(2) << time.wHour << ':' << std::setw(2) << time.wMinute
              << ':' << std::setw(2) << time.wSecond << '.' << std::setw(3)
              << time.wMilliseconds << 'Z';
    return timestamp.str();
}

std::string formatUtcOffset(int offsetMinutes) {
    const char sign = offsetMinutes < 0 ? '-' : '+';
    const unsigned int magnitude = static_cast<unsigned int>(
        offsetMinutes < 0 ? -offsetMinutes : offsetMinutes);
    std::ostringstream offset;
    offset << "UTC" << sign << std::setfill('0') << std::setw(2) << magnitude / 60
           << ':' << std::setw(2) << magnitude % 60;
    return offset.str();
}

std::string timeZoneDisplayName(const CaseMetadata& metadata) {
    const std::string& currentName = metadata.timeZoneState == "daylight"
        ? metadata.timeZoneDaylightName : metadata.timeZoneStandardName;
    return currentName.empty() ? "indisponivel" : currentName;
}

std::string timeZoneOffsetDisplay(const CaseMetadata& metadata) {
    return metadata.timeZoneStatus == "available"
        ? formatUtcOffset(metadata.utcOffsetMinutes) : "indisponivel";
}

std::size_t utf8SequenceLength(const std::string& value, std::size_t index) {
    const unsigned char first = static_cast<unsigned char>(value[index]);
    std::size_t length = 0;
    if (first >= 0xC2 && first <= 0xDF) length = 2;
    else if (first >= 0xE0 && first <= 0xEF) length = 3;
    else if (first >= 0xF0 && first <= 0xF4) length = 4;
    else return 0;

    if (index + length > value.size()) return 0;
    for (std::size_t offset = 1; offset < length; ++offset) {
        const unsigned char continuation = static_cast<unsigned char>(value[index + offset]);
        if (continuation < 0x80 || continuation > 0xBF) return 0;
    }

    const unsigned char second = static_cast<unsigned char>(value[index + 1]);
    if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second > 0x9F) ||
        (first == 0xF0 && second < 0x90) || (first == 0xF4 && second > 0x8F)) {
        return 0;
    }
    return length;
}

std::string jsonString(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (std::size_t index = 0; index < value.size();) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        switch (character) {
        case '"': out << "\\\""; ++index; break;
        case '\\': out << "\\\\"; ++index; break;
        case '\b': out << "\\b"; ++index; break;
        case '\f': out << "\\f"; ++index; break;
        case '\n': out << "\\n"; ++index; break;
        case '\r': out << "\\r"; ++index; break;
        case '\t': out << "\\t"; ++index; break;
        default:
            if (character < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned int>(character) << std::dec;
                ++index;
            } else if (character < 0x80) {
                out << static_cast<char>(character);
                ++index;
            } else {
                const std::size_t sequenceLength = utf8SequenceLength(value, index);
                if (sequenceLength == 0) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<unsigned int>(character) << std::dec;
                    ++index;
                } else {
                    out.write(value.data() + index, static_cast<std::streamsize>(sequenceLength));
                    index += sequenceLength;
                }
            }
        }
    }
    out << '"';
    return out.str();
}

CaseMetadata collectCaseMetadata(int selection) {
    SYSTEMTIME now = {};
    GetSystemTime(&now);
    std::ostringstream id;
    id << std::setfill('0') << std::setw(4) << now.wYear
       << std::setw(2) << now.wMonth << std::setw(2) << now.wDay << '_'
       << std::setw(2) << now.wHour << std::setw(2) << now.wMinute
       << std::setw(2) << now.wSecond << '_' << std::setw(3) << now.wMilliseconds << 'Z';
    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD computerLength = static_cast<DWORD>(sizeof(computer) / sizeof(computer[0]));
    const std::string computerName = GetComputerNameW(computer, &computerLength)
        ? toUtf8(computer) : "indisponivel";

    wchar_t user[UNLEN + 1] = {};
    DWORD userLength = static_cast<DWORD>(sizeof(user) / sizeof(user[0]));
    const std::string operatorName = GetUserNameW(user, &userLength)
        ? toUtf8(user) : "indisponivel";

    CaseMetadata metadata;
    metadata.caseId = "case_" + id.str();
    metadata.collectedAtUtc = formatUtcTimestamp(now);
    metadata.computer = computerName;
    metadata.operatorName = operatorName;
    collectTimeZoneContext(metadata);
    metadata.windowsProductName = registryString(
        HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        "ProductName", KEY_WOW64_64KEY);
    metadata.windowsDisplayVersion = registryString(
        HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        "DisplayVersion", KEY_WOW64_64KEY);
    metadata.windowsBuild = registryString(
        HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        "CurrentBuild", KEY_WOW64_64KEY);
    metadata.nativeArchitecture = getNativeArchitectureName();
    metadata.elevationStatus = getElevationStatus();
    metadata.selection = selection;
    return metadata;
}

std::vector<std::string> selectedSources(const CaseMetadata& metadata) {
    const int selection = metadata.selection;
    if (metadata.customCollection) {
        std::vector<std::string> sources;
        if (metadata.collectSystem) sources.push_back("system_and_disks");
        if (metadata.collectNetwork) sources.push_back("network_interfaces");
        if (metadata.collectProcesses) {
            sources.push_back("process_snapshot");
            sources.push_back("tcp_connections");
            sources.push_back("udp_endpoints");
        }
        if (metadata.collectSoftware) sources.push_back("installed_software_registry");
        if (metadata.collectStartup) sources.push_back("startup_registry");
        if (metadata.collectLogs) {
            for (const auto& channel : metadata.eventChannels) {
                sources.push_back("event_channel_" + toUtf8(channel.c_str()));
            }
            sources.push_back("native_evtx_exports");
            sources.push_back("event_xml_jsonl");
            sources.push_back("event_timeline");
            sources.push_back("event_timeline_summary");
            sources.push_back("process_snapshot_for_event_correlation");
            sources.push_back("active_tcp_snapshot_for_event_correlation");
        }
        if (metadata.collectServicesTasks) {
            sources.push_back("services_registry_inventory");
            sources.push_back("scheduled_task_files_metadata");
            sources.push_back("scheduled_task_xml_details");
        }
        if (metadata.collectFiles) {
            sources.push_back("user_selected_file_metadata_sha256");
        }
        if (metadata.collectNtfsArtifacts) {
            sources.push_back("windows_prefetch_inventory");
        }
        if (metadata.collectUserActivity) {
            sources.push_back("windows_recent_shortcuts");
            sources.push_back("automatic_jump_lists_inventory");
            sources.push_back("custom_jump_lists_inventory");
        }
        if (metadata.collectFiles || metadata.collectNtfsArtifacts) {
            sources.push_back("ntfs_alternate_data_streams");
        }
        return sources;
    }
    switch (selection) {
    case 1: return {"system_and_disks"};
    case 2: return {"network_interfaces"};
    case 3: return {"tcp_connections", "udp_endpoints", "process_snapshot"};
    case 4: return {"process_snapshot", "process_image_paths"};
    case 5: return {"installed_software_registry"};
    case 7: return {"startup_registry"};
    case 8:
        return {"security_event_log_preview", "event_channel_security",
                "event_channel_system", "event_channel_application",
                "event_channel_windows_defender", "event_channel_powershell",
                "event_channel_sysmon", "event_channel_task_scheduler",
                "event_channel_wmi_activity", "event_channel_rdp_sessions",
                "event_channel_rdp_connections", "event_channel_winrm",
                "event_channel_dns_client", "event_channel_network_profile",
                "event_channel_firewall", "native_evtx_exports",
                "event_xml_jsonl", "event_timeline", "event_timeline_summary",
                "process_snapshot_for_event_correlation",
                "active_tcp_snapshot_for_event_correlation"};
    case 9: return {"user_selected_file_metadata_sha256", "ntfs_alternate_data_streams"};
    case 6:
        return {"system_and_disks", "network_interfaces", "tcp_connections",
                "udp_endpoints", "process_snapshot", "process_image_paths",
                "installed_software_registry", "startup_registry",
                "security_event_log_preview",
                "event_channel_security", "event_channel_system",
                "event_channel_application", "event_channel_windows_defender",
                "event_channel_powershell", "event_channel_sysmon",
                "event_channel_task_scheduler", "event_channel_wmi_activity",
                "event_channel_rdp_sessions", "event_channel_rdp_connections",
                "event_channel_winrm", "event_channel_dns_client",
                "event_channel_network_profile", "event_channel_firewall",
                "native_evtx_exports", "event_xml_jsonl", "event_timeline",
                "event_timeline_summary", "process_snapshot_for_event_correlation",
                "active_tcp_snapshot_for_event_correlation",
                "services_registry_inventory", "scheduled_task_files_metadata",
                "scheduled_task_xml_details",
                "user_selected_file_metadata_sha256", "ntfs_alternate_data_streams",
                "windows_prefetch_inventory", "windows_recent_shortcuts",
                "automatic_jump_lists_inventory", "custom_jump_lists_inventory"};
    default: return {};
    }
}

std::string metadataJson(const CaseMetadata& metadata) {
    std::ostringstream out;
    out << "{\n"
        << "  \"case_id\": " << jsonString(metadata.caseId) << ",\n"
        << "  \"collection_started_at_utc\": " << jsonString(metadata.collectedAtUtc) << ",\n"
        << "  \"collection_completed_at_utc\": " << jsonString(metadata.completedAtUtc) << ",\n"
        << "  \"computer\": " << jsonString(metadata.computer) << ",\n"
        << "  \"operator\": " << jsonString(metadata.operatorName) << ",\n"
        << "  \"time_zone_status\": " << jsonString(metadata.timeZoneStatus) << ",\n"
        << "  \"time_zone_state\": " << jsonString(metadata.timeZoneState) << ",\n"
        << "  \"time_zone_standard_name\": "
        << jsonString(metadata.timeZoneStandardName) << ",\n"
        << "  \"time_zone_daylight_name\": "
        << jsonString(metadata.timeZoneDaylightName) << ",\n"
        << "  \"utc_offset_minutes\": " << metadata.utcOffsetMinutes << ",\n"
        << "  \"windows_product_name\": " << jsonString(metadata.windowsProductName) << ",\n"
        << "  \"windows_display_version\": "
        << jsonString(metadata.windowsDisplayVersion) << ",\n"
        << "  \"windows_build\": " << jsonString(metadata.windowsBuild) << ",\n"
        << "  \"native_architecture\": " << jsonString(metadata.nativeArchitecture) << ",\n"
        << "  \"elevation_status\": " << jsonString(metadata.elevationStatus) << ",\n"
        << "  \"evidence_folder\": " << jsonString(
               metadata.evidenceFolder.empty() ? "" : toUtf8(metadata.evidenceFolder.c_str())) << ",\n"
        << "  \"tool\": \"PC Local Diagnostics\",\n"
        << "  \"tool_version\": \"0.3.0\",\n"
        << "  \"selection\": " << metadata.selection << ",\n"
        << "  \"custom_collection\": " << (metadata.customCollection ? "true" : "false") << ",\n"
        << "  \"collect_ntfs_artifacts\": "
        << (metadata.collectNtfsArtifacts ? "true" : "false") << ",\n"
        << "  \"collect_user_activity_artifacts\": "
        << ((metadata.selection == 6 || metadata.collectUserActivity) ? "true" : "false")
        << ",\n"
        << "  \"sources_attempted\": [";
    const std::vector<std::string> sources = selectedSources(metadata);
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (index != 0) out << ", ";
        out << jsonString(sources[index]);
    }
    out << "],\n"
        << "  \"report\": ";
    return out.str();
}

bool writeNewFile(const std::wstring& path, const std::string& contents, DWORD& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }

    std::size_t offset = 0;
    while (offset < contents.size()) {
        const DWORD chunkSize = static_cast<DWORD>(
            (contents.size() - offset) > MAXDWORD ? MAXDWORD : (contents.size() - offset));
        DWORD written = 0;
        if (!WriteFile(file, contents.data() + offset, chunkSize, &written, nullptr)) {
            error = GetLastError();
            CloseHandle(file);
            return false;
        }
        if (written == 0) {
            error = ERROR_WRITE_FAULT;
            CloseHandle(file);
            return false;
        }
        offset += written;
    }

    if (!FlushFileBuffers(file)) {
        error = GetLastError();
        CloseHandle(file);
        return false;
    }
    if (!CloseHandle(file)) {
        error = GetLastError();
        return false;
    }
    return true;
}

bool sha256File(const std::wstring& path, std::string& digest, DWORD& error) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::vector<UCHAR> hashObject;
    std::vector<UCHAR> hashValue;
    bool succeeded = false;

    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = static_cast<DWORD>(status);
        return false;
    }

    DWORD objectLength = 0;
    DWORD resultLength = 0;
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                               &resultLength, 0);
    if (!BCRYPT_SUCCESS(status) || objectLength == 0) {
        error = BCRYPT_SUCCESS(status) ? ERROR_INVALID_DATA : static_cast<DWORD>(status);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }
    hashObject.resize(objectLength);

    DWORD hashLength = 0;
    status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                               reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength),
                               &resultLength, 0);
    if (!BCRYPT_SUCCESS(status) || hashLength == 0) {
        error = BCRYPT_SUCCESS(status) ? ERROR_INVALID_DATA : static_cast<DWORD>(status);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }
    hashValue.resize(hashLength);

    status = BCryptCreateHash(algorithm, &hash, &hashObject[0], objectLength, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = static_cast<DWORD>(status);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
    } else {
        UCHAR buffer[64 * 1024];
        succeeded = true;
        while (true) {
            DWORD bytesRead = 0;
            if (!ReadFile(file, buffer, sizeof(buffer), &bytesRead, nullptr)) {
                error = GetLastError();
                succeeded = false;
                break;
            }
            if (bytesRead == 0) break;
            status = BCryptHashData(hash, buffer, bytesRead, 0);
            if (!BCRYPT_SUCCESS(status)) {
                error = static_cast<DWORD>(status);
                succeeded = false;
                break;
            }
        }
    }

    if (succeeded) {
        status = BCryptFinishHash(hash, &hashValue[0], hashLength, 0);
        if (!BCRYPT_SUCCESS(status)) {
            error = static_cast<DWORD>(status);
            succeeded = false;
        }
    }
    if (file != INVALID_HANDLE_VALUE && !CloseHandle(file) && succeeded) {
        error = GetLastError();
        succeeded = false;
    }
    if (succeeded) {
        std::ostringstream hex;
        hex << std::hex << std::setfill('0');
        for (UCHAR byte : hashValue) {
            hex << std::setw(2) << static_cast<unsigned int>(byte);
        }
        digest = hex.str();
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return succeeded;
}

struct CaseFileRecord {
    std::wstring fileName;
    std::string status;
    std::string sha256;
    unsigned long long sizeBytes = 0;
    unsigned long long records = 0;
};

bool readRegistryString(HKEY key, const char* name, std::string& value);
std::wstring xmlElementText(const std::wstring& xml, const std::wstring& element);
std::wstring xmlAttribute(const std::wstring& tag, const std::wstring& attribute);
std::wstring xmlElementTag(const std::wstring& xml, const std::wstring& name);
std::string eventTimelineSortKey(const std::wstring& xml);
std::string eventTimelineJson(const std::wstring& xml, const std::wstring& channel,
                              const ProcessMap& processes, bool processSnapshotAvailable,
                              const NetworkAddressSet& remoteAddresses,
                              bool networkSnapshotAvailable,
                              const std::string& processSnapshotAtUtc,
                              const std::string& networkSnapshotAtUtc);
std::vector<std::pair<std::wstring, std::wstring>> xmlDataFields(const std::wstring& xml);

std::string formatFileTimeUtc(const FILETIME& time) {
    SYSTEMTIME utc = {};
    if (!FileTimeToSystemTime(&time, &utc)) return "";
    return formatUtcTimestamp(utc);
}

bool writeToOpenFile(HANDLE file, const std::string& contents, DWORD& error) {
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const DWORD chunkSize = static_cast<DWORD>(
            (contents.size() - offset) > MAXDWORD ? MAXDWORD : (contents.size() - offset));
        DWORD written = 0;
        if (!WriteFile(file, contents.data() + offset, chunkSize, &written, nullptr)) {
            error = GetLastError();
            return false;
        }
        if (written == 0) {
            error = ERROR_WRITE_FAULT;
            return false;
        }
        offset += written;
    }
    return true;
}

bool finishEvidenceFile(HANDLE& file, DWORD& error) {
    bool succeeded = true;
    if (!FlushFileBuffers(file)) {
        error = GetLastError();
        succeeded = false;
    }
    if (!CloseHandle(file) && succeeded) {
        error = GetLastError();
        succeeded = false;
    }
    file = INVALID_HANDLE_VALUE;
    return succeeded;
}

bool writeJsonLineToOpenFile(HANDLE file, const std::string& line, DWORD& error) {
    return writeToOpenFile(file, line + "\r\n", error);
}

struct TimelineEntry {
    std::string key;
    std::string json;
};

struct TimelineSorter {
    std::wstring directory;
    std::vector<TimelineEntry> entries;
    std::vector<std::wstring> runs;
    std::vector<std::wstring> allRunPaths;
    std::size_t bufferedBytes = 0;
    unsigned long long nextRunId = 0;
    unsigned long long recordCount = 0;
    unsigned long long invalidTimestampCount = 0;
    std::map<std::string, unsigned long long> eventsByHour;
    std::map<std::string, unsigned long long> eventsByChannel;
    std::map<std::string, unsigned long long> eventsByChannelAndId;
    std::string earliestTimestamp;
    std::string latestTimestamp;
    bool healthy = true;
};

std::wstring nextTimelineRunPath(TimelineSorter& sorter) {
    std::wostringstream name;
    name << sorter.directory << L"\\timeline_run_" << std::setfill(L'0')
         << std::setw(8) << sorter.nextRunId++ << L".tmp";
    const std::wstring path = name.str();
    sorter.allRunPaths.push_back(path);
    return path;
}

bool cleanupTimelineRuns(TimelineSorter& sorter, DWORD& error) {
    bool succeeded = true;
    for (const auto& path : sorter.allRunPaths) {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            const DWORD attributesError = GetLastError();
            if (attributesError == ERROR_FILE_NOT_FOUND ||
                attributesError == ERROR_PATH_NOT_FOUND) {
                continue;
            }
            error = attributesError;
            succeeded = false;
            continue;
        }
        if (!DeleteFileW(path.c_str())) {
            error = GetLastError();
            succeeded = false;
        }
    }
    return succeeded;
}

bool flushTimelineRun(TimelineSorter& sorter, DWORD& error) {
    if (sorter.entries.empty()) return true;
    std::sort(sorter.entries.begin(), sorter.entries.end(),
        [](const TimelineEntry& left, const TimelineEntry& right) {
            if (left.key != right.key) return left.key < right.key;
            return left.json < right.json;
        });

    const std::wstring path = nextTimelineRunPath(sorter);
    HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        sorter.healthy = false;
        return false;
    }

    bool succeeded = true;
    for (const auto& entry : sorter.entries) {
        if (!writeToOpenFile(output, entry.key + "\t" + entry.json + "\r\n", error)) {
            succeeded = false;
            break;
        }
    }
    if (!finishEvidenceFile(output, error)) succeeded = false;
    if (!succeeded) {
        DeleteFileW(path.c_str());
        sorter.healthy = false;
        return false;
    }

    sorter.runs.push_back(path);
    sorter.entries.clear();
    sorter.bufferedBytes = 0;
    return true;
}

bool addTimelineEvent(TimelineSorter& sorter, const std::wstring& xml,
                     const std::wstring& channel, DWORD& error,
                     const ProcessMap& processes, bool processSnapshotAvailable,
                     const NetworkAddressSet& remoteAddresses,
                     bool networkSnapshotAvailable,
                     const std::string& processSnapshotAtUtc,
                     const std::string& networkSnapshotAtUtc) {
    if (!sorter.healthy) return false;
    TimelineEntry entry;
    entry.key = eventTimelineSortKey(xml);
    entry.json = eventTimelineJson(xml, channel, processes, processSnapshotAvailable,
                                   remoteAddresses, networkSnapshotAvailable,
                                   processSnapshotAtUtc, networkSnapshotAtUtc);
    if (entry.key.empty() || entry.key[0] != '0') ++sorter.invalidTimestampCount;
    else {
        const std::string timestamp = entry.key.substr(1, 27);
        const std::string hour = timestamp.substr(0, 13);
        ++sorter.eventsByHour[hour];
        if (sorter.earliestTimestamp.empty() || timestamp < sorter.earliestTimestamp) {
            sorter.earliestTimestamp = timestamp;
        }
        if (sorter.latestTimestamp.empty() || timestamp > sorter.latestTimestamp) {
            sorter.latestTimestamp = timestamp;
        }
    }
    const std::string channelName = toUtf8(channel.c_str());
    const std::string eventId = toUtf8(xmlElementText(xml, L"EventID").c_str());
    ++sorter.eventsByChannel[channelName];
    ++sorter.eventsByChannelAndId[channelName + "|" + eventId];
    const std::size_t entryBytes = entry.key.size() + entry.json.size();
    const std::size_t chunkLimit = 8U * 1024U * 1024U;
    const std::size_t chunkEntryLimit = 50000;
    if (!sorter.entries.empty() &&
        (sorter.bufferedBytes + entryBytes > chunkLimit ||
         sorter.entries.size() >= chunkEntryLimit) &&
        !flushTimelineRun(sorter, error)) {
        return false;
    }
    sorter.bufferedBytes += entryBytes;
    sorter.entries.push_back(std::move(entry));
    ++sorter.recordCount;
    return sorter.bufferedBytes < chunkLimit && sorter.entries.size() < chunkEntryLimit
        ? true : flushTimelineRun(sorter, error);
}

struct TimelineRunReader {
    HANDLE file = INVALID_HANDLE_VALUE;
    char buffer[64 * 1024] = {};
    DWORD offset = 0;
    DWORD available = 0;
    bool eof = false;
    std::string pending;
};

bool readTimelineLine(TimelineRunReader& reader, std::string& line,
                      bool& hasLine, DWORD& error) {
    hasLine = false;
    while (true) {
        if (reader.offset == reader.available) {
            if (reader.eof) {
                if (reader.pending.empty()) return true;
                line.swap(reader.pending);
                hasLine = true;
                return true;
            }
            if (!ReadFile(reader.file, reader.buffer, sizeof(reader.buffer),
                          &reader.available, nullptr)) {
                error = GetLastError();
                return false;
            }
            reader.offset = 0;
            if (reader.available == 0) {
                reader.eof = true;
                continue;
            }
        }

        const char* segment = reader.buffer + reader.offset;
        const std::size_t segmentSize = reader.available - reader.offset;
        const char* lineEnd = static_cast<const char*>(
            std::memchr(segment, '\n', segmentSize));
        if (lineEnd != nullptr) {
            reader.pending.append(segment, static_cast<std::size_t>(lineEnd - segment));
            reader.offset += static_cast<DWORD>(lineEnd - segment) + 1;
            if (!reader.pending.empty() && reader.pending.back() == '\r') {
                reader.pending.pop_back();
            }
            line.swap(reader.pending);
            hasLine = true;
            return true;
        }
        reader.pending.append(segment, segmentSize);
        reader.offset = reader.available;
    }
}

struct TimelineQueueEntry {
    std::string key;
    std::string line;
    std::size_t readerIndex;
};

struct TimelineQueueEntryLater {
    bool operator()(const TimelineQueueEntry& left,
                    const TimelineQueueEntry& right) const {
        if (left.key != right.key) return left.key > right.key;
        if (left.line != right.line) return left.line > right.line;
        return left.readerIndex > right.readerIndex;
    }
};

bool mergeTimelineRuns(const std::vector<std::wstring>& inputs,
                       const std::wstring& outputPath, bool removeSortKey,
                       unsigned long long& outputCount, DWORD& error) {
    HANDLE output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                removeSortKey ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_TEMPORARY,
                                nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }

    std::vector<TimelineRunReader> readers;
    readers.reserve(inputs.size());
    bool succeeded = true;
    for (const auto& inputPath : inputs) {
        readers.emplace_back();
        TimelineRunReader& reader = readers.back();
        reader.file = CreateFileW(inputPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (reader.file == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            succeeded = false;
            break;
        }
    }

    std::priority_queue<TimelineQueueEntry, std::vector<TimelineQueueEntry>,
                        TimelineQueueEntryLater> pending;
    for (std::size_t index = 0; succeeded && index < readers.size(); ++index) {
        std::string line;
        bool hasLine = false;
        if (!readTimelineLine(readers[index], line, hasLine, error)) {
            succeeded = false;
            break;
        }
        if (!hasLine) continue;
        const std::size_t separator = line.find('\t');
        if (separator == std::string::npos) {
            error = ERROR_INVALID_DATA;
            succeeded = false;
            break;
        }
        pending.push(TimelineQueueEntry{
            line.substr(0, separator), std::move(line), index});
    }

    while (succeeded && !pending.empty()) {
        if (cancellationRequested()) {
            error = ERROR_CANCELLED;
            succeeded = false;
            break;
        }
        TimelineQueueEntry current = pending.top();
        pending.pop();
        const std::size_t separator = current.line.find('\t');
        const std::string outputLine = removeSortKey
            ? current.line.substr(separator + 1) : current.line;
        if (!writeJsonLineToOpenFile(output, outputLine, error)) {
            succeeded = false;
            break;
        }
        ++outputCount;

        std::string nextLine;
        bool hasNext = false;
        if (!readTimelineLine(readers[current.readerIndex], nextLine, hasNext, error)) {
            succeeded = false;
            break;
        }
        if (hasNext) {
            const std::size_t nextSeparator = nextLine.find('\t');
            if (nextSeparator == std::string::npos) {
                error = ERROR_INVALID_DATA;
                succeeded = false;
                break;
            }
            pending.push(TimelineQueueEntry{
                nextLine.substr(0, nextSeparator), std::move(nextLine), current.readerIndex});
        }
    }

    for (auto& reader : readers) {
        if (reader.file != INVALID_HANDLE_VALUE && !CloseHandle(reader.file) && succeeded) {
            error = GetLastError();
            succeeded = false;
        }
    }
    if (!finishEvidenceFile(output, error)) succeeded = false;
    if (!succeeded && !removeSortKey) DeleteFileW(outputPath.c_str());
    return succeeded;
}

bool writeSortedTimeline(TimelineSorter& sorter, const std::wstring& outputPath,
                         unsigned long long& outputCount, DWORD& error) {
    if (!sorter.healthy || !flushTimelineRun(sorter, error)) return false;
    std::vector<std::wstring> currentRuns = sorter.runs;
    const std::size_t mergeFanIn = 32;

    while (currentRuns.size() > mergeFanIn) {
        std::vector<std::wstring> nextRuns;
        for (std::size_t first = 0; first < currentRuns.size(); first += mergeFanIn) {
            if (cancellationRequested()) {
                error = ERROR_CANCELLED;
                sorter.healthy = false;
                return false;
            }
            const std::size_t last = (std::min)(first + mergeFanIn, currentRuns.size());
            const std::vector<std::wstring> group(
                currentRuns.begin() + first, currentRuns.begin() + last);
            const std::wstring mergedPath = nextTimelineRunPath(sorter);
            unsigned long long mergedCount = 0;
            if (!mergeTimelineRuns(group, mergedPath, false, mergedCount, error)) {
                sorter.healthy = false;
                return false;
            }
            for (const auto& inputPath : group) {
                if (!DeleteFileW(inputPath.c_str())) {
                    error = GetLastError();
                    sorter.healthy = false;
                    return false;
                }
            }
            nextRuns.push_back(mergedPath);
        }
        currentRuns.swap(nextRuns);
    }

    if (!mergeTimelineRuns(currentRuns, outputPath, true, outputCount, error)) {
        sorter.healthy = false;
        return false;
    }
    if (outputCount != sorter.recordCount) {
        error = ERROR_INVALID_DATA;
        sorter.healthy = false;
        return false;
    }
    for (const auto& inputPath : currentRuns) {
        if (!DeleteFileW(inputPath.c_str())) {
            error = GetLastError();
            sorter.healthy = false;
            return false;
        }
    }
    return true;
}

CaseFileRecord makeCaseFileRecord(const std::wstring& name, const std::wstring& path,
                                  const std::string& status, unsigned long long records,
                                  DWORD& error) {
    CaseFileRecord record;
    record.fileName = name;
    record.status = status;
    record.records = records;
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        error = GetLastError();
        record.status += "; metadata error " + std::to_string(error);
        return record;
    }
    record.sizeBytes = (static_cast<unsigned long long>(attributes.nFileSizeHigh) << 32)
        | attributes.nFileSizeLow;
    if (!sha256File(path, record.sha256, error)) {
        record.status += "; SHA-256 error " + std::to_string(error);
    }
    return record;
}

bool exportEventChannel(const std::wstring& channel, const std::wstring& fileName,
                        const std::wstring& path, TimelineSorter& timelineSorter,
                        const ProcessMap& processes, bool processSnapshotAvailable,
                        const NetworkAddressSet& remoteAddresses,
                        bool networkSnapshotAvailable,
                        const std::string& processSnapshotAtUtc,
                        const std::string& networkSnapshotAtUtc,
                        bool& timelineHealthy,
                        CaseFileRecord& fileRecord, DWORD& error) {
    error = ERROR_SUCCESS;
    std::cout << "Coletando eventos do canal " << toUtf8(channel.c_str()) << "...\n";
    fileRecord.fileName = fileName;
    HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        fileRecord.status = "create_failed_" + std::to_string(error);
        return false;
    }

    EVT_HANDLE query = EvtQuery(nullptr, channel.c_str(), L"*",
                                EvtQueryChannelPath | EvtQueryReverseDirection);
    unsigned long long eventCount = 0;
    bool complete = true;
    std::string status = "complete";
    if (query == nullptr) {
        error = GetLastError();
        status = "query_failed_" + std::to_string(error);
        complete = false;
        const std::string record = "{\"collection_error\": "
            + jsonString(status) + "}";
        if (!writeJsonLineToOpenFile(output, record, error)) complete = false;
    } else {
        while (true) {
            if (cancellationRequested()) {
                status = "cancelled_partial";
                complete = false;
                break;
            }
            EVT_HANDLE events[32] = {};
            DWORD returned = 0;
            if (!EvtNext(query, 32, events, 0, 0, &returned)) {
                const DWORD nextError = GetLastError();
                if (nextError != ERROR_NO_MORE_ITEMS) {
                    error = nextError;
                    status = "partial_read_error_" + std::to_string(nextError);
                    complete = false;
                }
                break;
            }

            for (DWORD i = 0; i < returned; ++i) {
                if (cancellationRequested()) {
                    status = "cancelled_partial";
                    complete = false;
                    for (DWORD remaining = i; remaining < returned; ++remaining) {
                        EvtClose(events[remaining]);
                    }
                    EvtClose(query);
                    query = nullptr;
                    break;
                }
                DWORD bufferUsed = 0;
                DWORD propertyCount = 0;
                const BOOL sizeQuery = EvtRender(nullptr, events[i], EvtRenderEventXml,
                                                 0, nullptr, &bufferUsed, &propertyCount);
                const DWORD sizeError = sizeQuery ? ERROR_SUCCESS : GetLastError();
                if (sizeQuery || sizeError != ERROR_INSUFFICIENT_BUFFER || bufferUsed == 0) {
                    error = sizeError;
                    status = "partial_render_error_" + std::to_string(sizeError);
                    complete = false;
                    EvtClose(events[i]);
                    continue;
                }

                std::vector<wchar_t> xmlBuffer(
                    (bufferUsed + sizeof(wchar_t) - 1) / sizeof(wchar_t), L'\0');
                if (!EvtRender(nullptr, events[i], EvtRenderEventXml,
                               static_cast<DWORD>(xmlBuffer.size() * sizeof(wchar_t)),
                               &xmlBuffer[0], &bufferUsed, &propertyCount)) {
                    error = GetLastError();
                    status = "partial_render_error_" + std::to_string(error);
                    complete = false;
                    EvtClose(events[i]);
                    continue;
                }
                EvtClose(events[i]);

                const std::wstring xml(&xmlBuffer[0]);
                const std::string record = "{\"channel\": "
                    + jsonString(toUtf8(channel.c_str())) + ", \"event_xml\": "
                    + jsonString(toUtf8(xml.c_str())) + "}";
                if (!writeJsonLineToOpenFile(output, record, error)) {
                    status = "partial_write_error_" + std::to_string(error);
                    complete = false;
                    for (DWORD remaining = i + 1; remaining < returned; ++remaining) {
                        EvtClose(events[remaining]);
                    }
                    EvtClose(query);
                    query = nullptr;
                    break;
                }
                if (timelineHealthy) {
                    if (!addTimelineEvent(timelineSorter, xml, channel, error,
                                          processes, processSnapshotAvailable,
                                          remoteAddresses, networkSnapshotAvailable,
                                          processSnapshotAtUtc, networkSnapshotAtUtc)) {
                        timelineHealthy = false;
                    }
                }
                ++eventCount;
                if (eventCount % 1000 == 0) {
                    std::cout << "  " << toUtf8(channel.c_str()) << ": "
                              << eventCount << " eventos coletados.\n";
                }
            }
            if (query == nullptr) break;
        }
    }

    if (query != nullptr) EvtClose(query);
    if (!finishEvidenceFile(output, error)) {
        status = "flush_or_close_error_" + std::to_string(error);
        complete = false;
    }
    fileRecord = makeCaseFileRecord(fileName, path, status, eventCount, error);
    std::cout << "  " << eventCount << " eventos; estado " << status << ".\n";
    return complete && !fileRecord.sha256.empty();
}

bool exportNativeEventLog(const std::wstring& channel, const std::wstring& fileName,
                          const std::wstring& path, CaseFileRecord& fileRecord,
                          DWORD& error) {
    error = ERROR_SUCCESS;
    fileRecord.fileName = fileName;
    std::cout << "Exportando log EVTX de " << toUtf8(channel.c_str()) << "...\n";
    if (!EvtExportLog(nullptr, channel.c_str(), L"*", path.c_str(), EvtExportLogChannelPath)) {
        error = GetLastError();
        const DWORD exportError = error;
        const std::string status = "evtx_export_failed_" + std::to_string(exportError);
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            fileRecord = makeCaseFileRecord(fileName, path, status, 0, error);
        } else {
            fileRecord.status = status;
        }
        return false;
    }

    fileRecord = makeCaseFileRecord(fileName, path, "evtx_exported", 0, error);
    if (fileRecord.sha256.empty()) return false;
    std::cout << "  EVTX exportado; tamanho " << fileRecord.sizeBytes << " bytes.\n";
    return true;
}

std::string fileTimeFromFindData(const FILETIME& time) {
    return formatFileTimeUtc(time);
}

bool getFileIdentity(const std::wstring& path, std::string& volumeSerial,
                     std::string& fileId, DWORD& linkCount, DWORD& error) {
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }

    BY_HANDLE_FILE_INFORMATION information = {};
    const bool success = GetFileInformationByHandle(file, &information) != FALSE;
    if (!success) {
        error = GetLastError();
        CloseHandle(file);
        return false;
    }
    if (!CloseHandle(file)) {
        error = GetLastError();
        return false;
    }

    std::ostringstream volume;
    volume << std::hex << std::setfill('0') << std::setw(8) << information.dwVolumeSerialNumber;
    std::ostringstream identifier;
    identifier << std::hex << std::setfill('0')
               << std::setw(8) << information.nFileIndexHigh
               << std::setw(8) << information.nFileIndexLow;
    volumeSerial = volume.str();
    fileId = identifier.str();
    linkCount = information.nNumberOfLinks;
    return true;
}

bool pathIsWithin(const std::wstring& path, const std::wstring& directory) {
    if (path.size() < directory.size() ||
        _wcsnicmp(path.c_str(), directory.c_str(), directory.size()) != 0) {
        return false;
    }
    return path.size() == directory.size() || path[directory.size()] == L'\\';
}

std::wstring fullPath(const std::wstring& path) {
    DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (required == 0) return L"";
    std::vector<wchar_t> buffer(required);
    DWORD written = GetFullPathNameW(path.c_str(), required, &buffer[0], nullptr);
    if (written == 0 || written >= required) return L"";
    std::wstring result(&buffer[0]);
    while (result.size() > 3 && result.back() == L'\\') result.pop_back();
    return result;
}

bool exportFileMetadata(const std::wstring& root, const std::wstring& excludedDirectory,
                        const std::wstring& outputPath, const std::wstring& outputName,
                        CaseFileRecord& fileRecord, DWORD& error) {
    error = ERROR_SUCCESS;
    fileRecord.fileName = outputName;
    HANDLE output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        fileRecord.status = "create_failed_" + std::to_string(error);
        return false;
    }

    const std::wstring normalizedRoot = fullPath(root);
    const std::wstring normalizedExcluded = fullPath(excludedDirectory);
    std::vector<std::wstring> pendingDirectories;
    unsigned long long fileCount = 0;
    unsigned long long directoryCount = 0;
    unsigned long long errorCount = 0;
    bool complete = true;
    std::cout << "Inventariando arquivos e calculando SHA-256 em: "
              << toUtf8(root.c_str()) << "\n";

    if (normalizedRoot.empty() || normalizedExcluded.empty()) {
        const std::string record = "{\"path\": " + jsonString(toUtf8(root.c_str()))
            + ", \"status\": \"path_normalization_failed\"}";
        if (!writeJsonLineToOpenFile(output, record, error)) complete = false;
        ++errorCount;
        complete = false;
    } else {
        pendingDirectories.push_back(normalizedRoot);
    }

    const DWORD rootAttributes = pendingDirectories.empty()
        ? INVALID_FILE_ATTRIBUTES
        : GetFileAttributesW(pendingDirectories.back().c_str());
    if (!pendingDirectories.empty() && rootAttributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD rootError = GetLastError();
        const std::string record = "{\"path\": " + jsonString(toUtf8(root.c_str()))
            + ", \"error\": " + jsonString(std::to_string(rootError)) + "}";
        if (!writeJsonLineToOpenFile(output, record, error)) {
            complete = false;
        }
        ++errorCount;
        complete = false;
        pendingDirectories.clear();
    } else if (!pendingDirectories.empty() &&
               (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        const std::string record = "{\"path\": " + jsonString(toUtf8(root.c_str()))
            + ", \"status\": \"root_reparse_point_not_followed\"}";
        if (!writeJsonLineToOpenFile(output, record, error)) {
            complete = false;
        } else if (pendingDirectories.empty()) {
            complete = false;
        }
        ++errorCount;
        complete = false;
        pendingDirectories.clear();
    }

    while (!pendingDirectories.empty() && !cancellationRequested()) {
        const std::wstring directory = pendingDirectories.back();
        pendingDirectories.pop_back();
        if (!normalizedExcluded.empty() && pathIsWithin(directory, normalizedExcluded)) continue;
        ++directoryCount;

        const std::wstring searchPath = directory + L"\\*";
        WIN32_FIND_DATAW entry = {};
        HANDLE search = FindFirstFileW(searchPath.c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) {
            const DWORD searchError = GetLastError();
            const std::string record = "{\"path\": "
                + jsonString(toUtf8(directory.c_str()))
                + ", \"error\": " + jsonString(std::to_string(searchError)) + "}";
            if (!writeJsonLineToOpenFile(output, record, error)) {
                complete = false;
                break;
            }
            ++errorCount;
            complete = false;
            continue;
        }

        do {
            if (cancellationRequested()) {
                complete = false;
                break;
            }
            if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0) {
                continue;
            }
            const std::wstring itemPath = directory + L"\\" + entry.cFileName;
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                    pendingDirectories.push_back(itemPath);
                } else {
                    const std::string record = "{\"path\": "
                        + jsonString(toUtf8(itemPath.c_str()))
                        + ", \"status\": \"directory_reparse_point_not_followed\"}";
                    if (!writeJsonLineToOpenFile(output, record, error)) {
                        complete = false;
                        break;
                    }
                }
                continue;
            }

            const bool reparsePoint = (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            std::string fileStatus;
            const auto appendFileStatus = [&fileStatus](const std::string& value) {
                if (!fileStatus.empty()) fileStatus += ";";
                fileStatus += value;
            };
            std::string volumeSerial;
            std::string fileId;
            DWORD hardLinkCount = 0;
            DWORD identityError = ERROR_SUCCESS;
            if (!getFileIdentity(itemPath, volumeSerial, fileId, hardLinkCount, identityError)) {
                appendFileStatus("identity_unavailable_" + std::to_string(identityError));
                ++errorCount;
                complete = false;
            }
            std::string fileHash;
            if (reparsePoint) {
                appendFileStatus("reparse_point_hash_skipped");
            }
            DWORD hashError = ERROR_SUCCESS;
            if (!reparsePoint && !sha256File(itemPath, fileHash, hashError)) {
                appendFileStatus("hash_failed_" + std::to_string(hashError));
                ++errorCount;
                complete = false;
            } else if (!reparsePoint) {
                WIN32_FILE_ATTRIBUTE_DATA afterHash = {};
                if (!GetFileAttributesExW(itemPath.c_str(), GetFileExInfoStandard, &afterHash)) {
                    appendFileStatus("metadata_after_hash_failed_" + std::to_string(GetLastError()));
                    ++errorCount;
                    complete = false;
                } else if (afterHash.nFileSizeHigh != entry.nFileSizeHigh ||
                           afterHash.nFileSizeLow != entry.nFileSizeLow ||
                           CompareFileTime(&afterHash.ftLastWriteTime, &entry.ftLastWriteTime) != 0) {
                    appendFileStatus("changed_during_collection");
                    ++errorCount;
                    complete = false;
                } else {
                    appendFileStatus("hashed");
                }
            }

            const unsigned long long size =
                (static_cast<unsigned long long>(entry.nFileSizeHigh) << 32) | entry.nFileSizeLow;
            const std::string record =
                "{\"path\": " + jsonString(toUtf8(itemPath.c_str())) +
                ", \"size_bytes\": " + std::to_string(size) +
                ", \"created_utc\": " + jsonString(fileTimeFromFindData(entry.ftCreationTime)) +
                ", \"modified_utc\": " + jsonString(fileTimeFromFindData(entry.ftLastWriteTime)) +
                ", \"accessed_utc\": " + jsonString(fileTimeFromFindData(entry.ftLastAccessTime)) +
                ", \"attributes\": " + std::to_string(entry.dwFileAttributes) +
                ", \"volume_serial\": " + jsonString(volumeSerial) +
                ", \"file_id\": " + jsonString(fileId) +
                ", \"hard_link_count\": " + std::to_string(hardLinkCount) +
                ", \"sha256\": " + jsonString(fileHash) +
                ", \"status\": " + jsonString(fileStatus) + "}";
            if (!writeJsonLineToOpenFile(output, record, error)) {
                complete = false;
                break;
            }
            ++fileCount;
            if (fileCount % 1000 == 0) {
                std::cout << "  Arquivos processados: " << fileCount << "\n";
            }
        } while (FindNextFileW(search, &entry));

        const bool cancelledDuringDirectory = cancellationRequested();
        const DWORD iterationError = GetLastError();
        FindClose(search);
        if (iterationError != ERROR_NO_MORE_FILES && !cancelledDuringDirectory) {
            const std::string record = "{\"path\": "
                + jsonString(toUtf8(directory.c_str()))
                + ", \"error\": " + jsonString(std::to_string(iterationError)) + "}";
            if (!writeJsonLineToOpenFile(output, record, error)) {
                complete = false;
                break;
            }
            ++errorCount;
            complete = false;
        }
        if (error != ERROR_SUCCESS) break;
    }

    if (cancellationRequested()) {
        ++errorCount;
        complete = false;
    }
    if (!finishEvidenceFile(output, error)) complete = false;
    const std::string status = complete
        ? std::string("complete") : (cancellationRequested()
            ? "cancelled_partial" : "partial_with_" + std::to_string(errorCount) + "_errors");
    fileRecord = makeCaseFileRecord(outputName, outputPath, status, fileCount, error);
    std::cout << "  " << fileCount << " arquivos em " << directoryCount
              << " pastas; erros/alteracoes: " << errorCount << ".\n";
    return complete && !fileRecord.sha256.empty();
}

bool exportOptionalDirectoryMetadata(const std::wstring& root,
                                     const std::wstring& excludedDirectory,
                                     const std::wstring& outputPath,
                                     const std::wstring& outputName,
                                     CaseFileRecord& fileRecord, DWORD& error) {
    const DWORD attributes = GetFileAttributesW(root.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD attributesError = GetLastError();
        if (attributesError != ERROR_FILE_NOT_FOUND &&
            attributesError != ERROR_PATH_NOT_FOUND) {
            return exportFileMetadata(root, excludedDirectory, outputPath, outputName,
                                      fileRecord, error);
        }
        std::ostringstream record;
        record << "{\"source_directory\": " << jsonString(toUtf8(root.c_str()))
               << ", \"status\": \"not_present\"}";
        if (!writeNewFile(outputPath, record.str() + "\r\n", error)) {
            fileRecord.fileName = outputName;
            fileRecord.status = "write_not_present_status_failed_" +
                std::to_string(error);
            return false;
        }
        fileRecord = makeCaseFileRecord(outputName, outputPath, "not_present", 0, error);
        return !fileRecord.sha256.empty();
    }
    return exportFileMetadata(root, excludedDirectory, outputPath, outputName,
                              fileRecord, error);
}

bool exportAlternateDataStreams(const std::wstring& root,
                                const std::wstring& excludedDirectory,
                                const std::wstring& outputPath,
                                CaseFileRecord& fileRecord, DWORD& error) {
    error = ERROR_SUCCESS;
    fileRecord.fileName = L"alternate_data_streams.jsonl";
    HANDLE output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        fileRecord.status = "create_failed_" + std::to_string(error);
        return false;
    }

    const std::wstring normalizedRoot = fullPath(root);
    const std::wstring normalizedExcluded = fullPath(excludedDirectory);
    std::vector<std::wstring> pendingDirectories;
    const DWORD rootAttributes = normalizedRoot.empty()
        ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(normalizedRoot.c_str());
    bool complete = true;
    unsigned long long recordCount = 0;
    unsigned long long streamCount = 0;
    unsigned long long issueCount = 0;
    if (normalizedRoot.empty() || normalizedExcluded.empty() ||
        rootAttributes == INVALID_FILE_ATTRIBUTES ||
        (rootAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        const DWORD rootError = normalizedRoot.empty() || normalizedExcluded.empty()
            ? ERROR_INVALID_NAME : rootAttributes == INVALID_FILE_ATTRIBUTES
            ? GetLastError() : ERROR_INVALID_DATA;
        const std::string record = "{\"root\": " + jsonString(toUtf8(root.c_str()))
            + ", \"status\": "
            + jsonString(normalizedRoot.empty() || normalizedExcluded.empty()
                ? "path_normalization_failed"
                : rootAttributes == INVALID_FILE_ATTRIBUTES
                ? "root_unavailable_" + std::to_string(rootError)
                : ((rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
                    ? "root_reparse_point_not_followed" : "root_not_a_directory"))
            + "}";
        if (writeJsonLineToOpenFile(output, record, error)) ++recordCount;
        complete = false;
        ++issueCount;
    } else {
        pendingDirectories.push_back(normalizedRoot);
    }

    while (!pendingDirectories.empty() && !cancellationRequested()) {
        const std::wstring directory = pendingDirectories.back();
        pendingDirectories.pop_back();
        if (pathIsWithin(directory, normalizedExcluded)) continue;
        const std::wstring searchPath = directory + L"\\*";
        WIN32_FIND_DATAW entry = {};
        HANDLE search = FindFirstFileW(searchPath.c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) {
            const DWORD searchError = GetLastError();
            const std::string record = "{\"path\": " + jsonString(toUtf8(directory.c_str()))
                + ", \"enumeration_error\": " + jsonString(std::to_string(searchError)) + "}";
            if (!writeJsonLineToOpenFile(output, record, error)) {
                complete = false;
                break;
            }
            ++recordCount;
            ++issueCount;
            complete = false;
            continue;
        }

        do {
            if (cancellationRequested()) {
                complete = false;
                break;
            }
            if (wcscmp(entry.cFileName, L".") == 0 ||
                wcscmp(entry.cFileName, L"..") == 0) continue;
            const std::wstring itemPath = directory + L"\\" + entry.cFileName;
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                    pendingDirectories.push_back(itemPath);
                } else {
                    const std::string record = "{\"path\": "
                        + jsonString(toUtf8(itemPath.c_str()))
                        + ", \"status\": \"reparse_point_not_followed\"}";
                    if (!writeJsonLineToOpenFile(output, record, error)) {
                        complete = false;
                        break;
                    }
                    ++recordCount;
                }
                continue;
            }
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                const std::string record = "{\"path\": "
                    + jsonString(toUtf8(itemPath.c_str()))
                    + ", \"status\": \"reparse_point_not_followed\"}";
                if (!writeJsonLineToOpenFile(output, record, error)) {
                    complete = false;
                    break;
                }
                ++recordCount;
                continue;
            }

            WIN32_FIND_STREAM_DATA stream = {};
            HANDLE streamSearch = FindFirstStreamW(itemPath.c_str(), FindStreamInfoStandard,
                                                   &stream, 0);
            if (streamSearch == INVALID_HANDLE_VALUE) {
                const DWORD streamError = GetLastError();
                if (streamError != ERROR_HANDLE_EOF) {
                    const std::string record = "{\"path\": "
                        + jsonString(toUtf8(itemPath.c_str()))
                        + ", \"stream_enumeration_error\": "
                        + jsonString(std::to_string(streamError)) + "}";
                    if (!writeJsonLineToOpenFile(output, record, error)) {
                        complete = false;
                        break;
                    }
                    ++recordCount;
                    ++issueCount;
                    complete = false;
                }
                continue;
            }

            do {
                if (wcscmp(stream.cStreamName, L"::$DATA") != 0) {
                    const std::wstring streamPath = itemPath + stream.cStreamName;
                    std::string digest;
                    DWORD hashError = ERROR_SUCCESS;
                    const bool hashed = sha256File(streamPath, digest, hashError);
                    const unsigned long long size =
                        (static_cast<unsigned long long>(stream.StreamSize.HighPart) << 32)
                        | static_cast<unsigned long long>(stream.StreamSize.LowPart);
                    std::ostringstream record;
                    record << "{\"path\": " << jsonString(toUtf8(itemPath.c_str()))
                           << ", \"stream_name\": "
                           << jsonString(toUtf8(stream.cStreamName))
                           << ", \"size_bytes\": " << size
                           << ", \"sha256\": " << jsonString(digest)
                           << ", \"status\": "
                           << jsonString(hashed ? "hashed" :
                               "hash_failed_" + std::to_string(hashError)) << "}";
                    if (!writeJsonLineToOpenFile(output, record.str(), error)) {
                        complete = false;
                        break;
                    }
                    ++recordCount;
                    ++streamCount;
                    if (!hashed) {
                        complete = false;
                        ++issueCount;
                    }
                    if (streamCount % 250 == 0) {
                        std::cout << "  Fluxos alternativos processados: "
                                  << streamCount << "\n";
                    }
                }
            } while (FindNextStreamW(streamSearch, &stream));
            const DWORD streamIterationError = GetLastError();
            FindClose(streamSearch);
            if (error != ERROR_SUCCESS) {
                complete = false;
                break;
            }
            if (streamIterationError != ERROR_HANDLE_EOF &&
                streamIterationError != ERROR_NO_MORE_FILES) {
                ++issueCount;
                complete = false;
            }
        } while (FindNextFileW(search, &entry));
        const DWORD iterationError = GetLastError();
        FindClose(search);
        if (error != ERROR_SUCCESS) {
            complete = false;
            break;
        }
        if (iterationError != ERROR_NO_MORE_FILES && !cancellationRequested()) {
            ++issueCount;
            complete = false;
        }
    }
    if (cancellationRequested()) {
        complete = false;
        ++issueCount;
    }
    if (!finishEvidenceFile(output, error)) {
        complete = false;
        ++issueCount;
    }
    const std::string status = complete
        ? std::string("complete") : (cancellationRequested()
            ? "cancelled_partial" : "partial_with_" + std::to_string(issueCount) + "_issues");
    fileRecord = makeCaseFileRecord(L"alternate_data_streams.jsonl", outputPath,
                                    status, recordCount, error);
    return complete && !fileRecord.sha256.empty();
}

struct ScheduledTaskXmlItem {
    std::wstring type;
    std::vector<std::pair<std::wstring, std::wstring>> fields;
};

bool isTaskAction(const std::wstring& name) {
    return name == L"Exec" || name == L"ComHandler" || name == L"SendEmail" ||
           name == L"ShowMessage";
}

bool isTaskTrigger(const std::wstring& name) {
    return name == L"BootTrigger" || name == L"CalendarTrigger" ||
           name == L"EventTrigger" || name == L"IdleTrigger" ||
           name == L"LogonTrigger" || name == L"RegistrationTrigger" ||
           name == L"SessionStateChangeTrigger" || name == L"TimeTrigger";
}

void appendTaskXmlValue(ScheduledTaskXmlItem& item, const std::wstring& name,
                        const std::wstring& value) {
    if (!item.fields.empty() && item.fields.back().first == name) {
        item.fields.back().second += value;
    } else {
        item.fields.emplace_back(name, value);
    }
}

std::string scheduledTaskItemJson(const ScheduledTaskXmlItem& item) {
    std::ostringstream json;
    json << "{\"type\": " << jsonString(toUtf8(item.type.c_str())) << ", \"fields\": [";
    for (std::size_t index = 0; index < item.fields.size(); ++index) {
        if (index != 0) json << ", ";
        json << "{\"name\": " << jsonString(toUtf8(item.fields[index].first.c_str()))
             << ", \"value\": " << jsonString(toUtf8(item.fields[index].second.c_str())) << "}";
    }
    json << "]}";
    return json.str();
}

HRESULT parseScheduledTaskXml(const std::wstring& path,
                              std::vector<ScheduledTaskXmlItem>& actions,
                              std::vector<ScheduledTaskXmlItem>& triggers) {
    IStream* input = nullptr;
    HRESULT result = SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_NONE,
                                            FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &input);
    if (FAILED(result)) return result;

    IXmlReader* reader = nullptr;
    result = CreateXmlReader(__uuidof(IXmlReader),
                             reinterpret_cast<void**>(&reader), nullptr);
    if (FAILED(result)) {
        input->Release();
        return result;
    }
    result = reader->SetInput(input);
    input->Release();
    if (FAILED(result)) {
        reader->Release();
        return result;
    }

    std::vector<std::wstring> elements;
    ScheduledTaskXmlItem action;
    ScheduledTaskXmlItem trigger;
    UINT actionDepth = 0;
    UINT triggerDepth = 0;
    bool actionActive = false;
    bool triggerActive = false;
    XmlNodeType nodeType = XmlNodeType_None;
    while ((result = reader->Read(&nodeType)) == S_OK) {
        if (nodeType == XmlNodeType_Element) {
            const wchar_t* localName = nullptr;
            UINT nameLength = 0;
            UINT depth = 0;
            const BOOL emptyElement = reader->IsEmptyElement();
            if (FAILED(reader->GetLocalName(&localName, &nameLength)) ||
                FAILED(reader->GetDepth(&depth))) {
                result = E_FAIL;
                break;
            }
            const std::wstring name(localName, nameLength);
            if (!actionActive && isTaskAction(name)) {
                action.type = name;
                actionDepth = depth;
                actionActive = true;
            }
            if (!triggerActive && isTaskTrigger(name)) {
                trigger.type = name;
                triggerDepth = depth;
                triggerActive = true;
            }
            if (!emptyElement) {
                elements.push_back(name);
            } else {
                if (actionActive && actionDepth == depth && action.type == name) {
                    actions.push_back(std::move(action));
                    action = ScheduledTaskXmlItem();
                    actionActive = false;
                }
                if (triggerActive && triggerDepth == depth && trigger.type == name) {
                    triggers.push_back(std::move(trigger));
                    trigger = ScheduledTaskXmlItem();
                    triggerActive = false;
                }
            }
        } else if (nodeType == XmlNodeType_Text || nodeType == XmlNodeType_CDATA) {
            const wchar_t* value = nullptr;
            UINT valueLength = 0;
            if (FAILED(reader->GetValue(&value, &valueLength))) {
                result = E_FAIL;
                break;
            }
            bool hasNonWhitespace = false;
            for (UINT index = 0; index < valueLength; ++index) {
                if (!iswspace(value[index])) {
                    hasNonWhitespace = true;
                    break;
                }
            }
            if (hasNonWhitespace && !elements.empty()) {
                const std::wstring text(value, valueLength);
                if (actionActive) appendTaskXmlValue(action, elements.back(), text);
                if (triggerActive) appendTaskXmlValue(trigger, elements.back(), text);
            }
        } else if (nodeType == XmlNodeType_EndElement) {
            const wchar_t* localName = nullptr;
            UINT nameLength = 0;
            UINT depth = 0;
            if (FAILED(reader->GetLocalName(&localName, &nameLength)) ||
                FAILED(reader->GetDepth(&depth))) {
                result = E_FAIL;
                break;
            }
            const std::wstring name(localName, nameLength);
            if (actionActive && actionDepth == depth && action.type == name) {
                actions.push_back(std::move(action));
                action = ScheduledTaskXmlItem();
                actionActive = false;
            }
            if (triggerActive && triggerDepth == depth && trigger.type == name) {
                triggers.push_back(std::move(trigger));
                trigger = ScheduledTaskXmlItem();
                triggerActive = false;
            }
            if (!elements.empty()) elements.pop_back();
        }
    }
    if (result == S_FALSE) result = S_OK;
    reader->Release();
    return result;
}

std::string scheduledTaskRecordJson(const std::wstring& path,
                                    const std::vector<ScheduledTaskXmlItem>& actions,
                                    const std::vector<ScheduledTaskXmlItem>& triggers) {
    std::ostringstream json;
    json << "{\"path\": " << jsonString(toUtf8(path.c_str())) << ", \"actions\": [";
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (index != 0) json << ", ";
        json << scheduledTaskItemJson(actions[index]);
    }
    json << "], \"triggers\": [";
    for (std::size_t index = 0; index < triggers.size(); ++index) {
        if (index != 0) json << ", ";
        json << scheduledTaskItemJson(triggers[index]);
    }
    json << "]}";
    return json.str();
}

bool exportScheduledTaskDetails(const std::wstring& root, const std::wstring& outputPath,
                                CaseFileRecord& fileRecord, DWORD& error) {
    error = ERROR_SUCCESS;
    fileRecord.fileName = L"scheduled_tasks_details.jsonl";
    HANDLE output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        fileRecord.status = "create_failed_" + std::to_string(error);
        return false;
    }

    const HRESULT initialization = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(initialization);
    bool complete = true;
    unsigned long long recordCount = 0;
    unsigned long long issueCount = 0;
    if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE) {
        error = static_cast<DWORD>(initialization);
        complete = false;
        ++issueCount;
        const std::string record = "{\"collection_error\": "
            + jsonString("COM_initialization_failed_" + std::to_string(error)) + "}";
        if (writeJsonLineToOpenFile(output, record, error)) {
            ++recordCount;
        } else {
            ++issueCount;
        }
    } else {
        std::vector<std::wstring> pendingDirectories;
        const DWORD rootAttributes = GetFileAttributesW(root.c_str());
        if (rootAttributes == INVALID_FILE_ATTRIBUTES ||
            (rootAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            const DWORD rootError = rootAttributes == INVALID_FILE_ATTRIBUTES
                ? GetLastError() : ERROR_INVALID_DATA;
            std::string status;
            if (rootAttributes == INVALID_FILE_ATTRIBUTES) {
                status = "root_unavailable_" + std::to_string(rootError);
            } else if ((rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                status = "root_reparse_point_not_followed";
            } else {
                status = "root_not_a_directory";
            }
            const std::string record = "{\"path\": " + jsonString(toUtf8(root.c_str()))
                + ", \"status\": " + jsonString(status) + "}";
            if (writeJsonLineToOpenFile(output, record, error)) {
                ++recordCount;
            } else {
                complete = false;
            }
            complete = false;
            ++issueCount;
        } else {
            pendingDirectories.push_back(root);
        }
        while (!pendingDirectories.empty() && !cancellationRequested()) {
            const std::wstring directory = pendingDirectories.back();
            pendingDirectories.pop_back();
            const std::wstring searchPath = directory + L"\\*";
            WIN32_FIND_DATAW entry = {};
            HANDLE search = FindFirstFileW(searchPath.c_str(), &entry);
            if (search == INVALID_HANDLE_VALUE) {
                const DWORD searchError = GetLastError();
                const std::string record = "{\"path\": "
                    + jsonString(toUtf8(directory.c_str()))
                    + ", \"enumeration_error\": " + jsonString(std::to_string(searchError)) + "}";
                if (!writeJsonLineToOpenFile(output, record, error)) {
                    complete = false;
                    break;
                }
                ++recordCount;
                complete = false;
                ++issueCount;
                continue;
            }

            do {
                if (cancellationRequested()) {
                    complete = false;
                    break;
                }
                if (wcscmp(entry.cFileName, L".") == 0 ||
                    wcscmp(entry.cFileName, L"..") == 0) continue;
                const std::wstring itemPath = directory + L"\\" + entry.cFileName;
                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                        pendingDirectories.push_back(itemPath);
                    } else {
                        const std::string record = "{\"path\": "
                            + jsonString(toUtf8(itemPath.c_str()))
                            + ", \"status\": \"directory_reparse_point_not_followed\"}";
                        if (!writeJsonLineToOpenFile(output, record, error)) {
                            complete = false;
                            break;
                        }
                        ++recordCount;
                        ++issueCount;
                        complete = false;
                    }
                    continue;
                }

                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                    const std::string record = "{\"path\": "
                        + jsonString(toUtf8(itemPath.c_str()))
                        + ", \"status\": \"reparse_point_not_parsed\"}";
                    if (!writeJsonLineToOpenFile(output, record, error)) {
                        complete = false;
                        break;
                    }
                    ++recordCount;
                    ++issueCount;
                    complete = false;
                    continue;
                }

                std::vector<ScheduledTaskXmlItem> actions;
                std::vector<ScheduledTaskXmlItem> triggers;
                const HRESULT parseResult = parseScheduledTaskXml(itemPath, actions, triggers);
                std::string record;
                if (FAILED(parseResult)) {
                    const DWORD parseError = static_cast<DWORD>(parseResult);
                    record = "{\"path\": " + jsonString(toUtf8(itemPath.c_str()))
                        + ", \"parse_error\": " + jsonString(std::to_string(parseError)) + "}";
                    complete = false;
                    ++issueCount;
                } else {
                    record = scheduledTaskRecordJson(itemPath, actions, triggers);
                }
                if (!writeJsonLineToOpenFile(output, record, error)) {
                    complete = false;
                    break;
                }
                ++recordCount;
                if (recordCount % 250 == 0) {
                    std::cout << "  Tarefas agendadas analisadas: " << recordCount << "\n";
                }
            } while (FindNextFileW(search, &entry));

            const DWORD iterationError = GetLastError();
            FindClose(search);
            if (error != ERROR_SUCCESS) {
                complete = false;
                break;
            }
            if (iterationError != ERROR_NO_MORE_FILES) {
                const std::string record = "{\"path\": "
                    + jsonString(toUtf8(directory.c_str()))
                    + ", \"enumeration_error\": "
                    + jsonString(std::to_string(iterationError)) + "}";
                if (!writeJsonLineToOpenFile(output, record, error)) {
                    complete = false;
                    break;
                }
                ++recordCount;
                complete = false;
                ++issueCount;
            }
        }
    }
    if (uninitialize) CoUninitialize();
    if (cancellationRequested()) {
        complete = false;
        ++issueCount;
    }
    if (!finishEvidenceFile(output, error)) {
        complete = false;
        ++issueCount;
    }

    const std::string status = complete
        ? std::string("complete") : "partial_with_" + std::to_string(issueCount) + "_issues";
    fileRecord = makeCaseFileRecord(L"scheduled_tasks_details.jsonl", outputPath,
                                    status, recordCount, error);
    return complete && !fileRecord.sha256.empty();
}

bool exportRecentShortcuts(const std::wstring& outputPath,
                          CaseFileRecord& fileRecord, DWORD& error) {
    error = ERROR_SUCCESS;
    fileRecord.fileName = L"recent_shortcuts.jsonl";
    HANDLE output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        fileRecord.status = "create_failed_" + std::to_string(error);
        return false;
    }

    wchar_t recentFolder[MAX_PATH] = {};
    const HRESULT recentFolderResult = SHGetFolderPathW(
        nullptr, CSIDL_RECENT, nullptr, SHGFP_TYPE_CURRENT, recentFolder);
    if (FAILED(recentFolderResult)) {
        error = static_cast<DWORD>(recentFolderResult);
        const std::string record = "{\"collection_error\": "
            + jsonString("recent_folder_unavailable_" +
                         std::to_string(static_cast<unsigned long>(recentFolderResult))) + "}";
        if (!writeJsonLineToOpenFile(output, record, error)) {
            finishEvidenceFile(output, error);
            fileRecord.status = "recent_folder_unavailable_and_record_write_failed_" +
                std::to_string(error);
            return false;
        }
        if (!finishEvidenceFile(output, error)) {
            fileRecord.status = "recent_folder_unavailable_and_close_failed_" +
                std::to_string(error);
            return false;
        }
        fileRecord = makeCaseFileRecord(L"recent_shortcuts.jsonl", outputPath,
            "recent_folder_unavailable_" +
                std::to_string(static_cast<unsigned long>(recentFolderResult)),
            1, error);
        return false;
    }

    const HRESULT initialization = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initialization);
    bool complete = true;
    unsigned long long recordCount = 0;
    unsigned long long issueCount = 0;
    if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE) {
        error = static_cast<DWORD>(initialization);
        const std::string record = "{\"collection_error\": "
            + jsonString("COM_initialization_failed_" + std::to_string(error)) + "}";
        if (writeJsonLineToOpenFile(output, record, error)) ++recordCount;
        complete = false;
        ++issueCount;
    } else {
        const std::wstring searchPath = std::wstring(recentFolder) + L"\\*.lnk";
        WIN32_FIND_DATAW entry = {};
        HANDLE search = FindFirstFileW(searchPath.c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) {
            const DWORD searchError = GetLastError();
            if (searchError != ERROR_FILE_NOT_FOUND &&
                searchError != ERROR_PATH_NOT_FOUND &&
                searchError != ERROR_NO_MORE_FILES) {
                error = searchError;
                complete = false;
                ++issueCount;
                const std::string record = "{\"enumeration_error\": "
                    + jsonString(std::to_string(searchError)) + "}";
                if (writeJsonLineToOpenFile(output, record, error)) ++recordCount;
            }
        } else {
            do {
                if (cancellationRequested()) {
                    complete = false;
                    ++issueCount;
                    break;
                }
                if ((entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                               FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
                    continue;
                }
                const std::wstring linkPath =
                    std::wstring(recentFolder) + L"\\" + entry.cFileName;
                std::string digest;
                DWORD hashError = ERROR_SUCCESS;
                const bool hashAvailable = sha256File(linkPath, digest, hashError);

                std::string parseStatus = "parsed_without_resolution";
                std::wstring targetPath;
                std::wstring arguments;
                std::wstring workingDirectory;
                std::wstring description;
                std::wstring iconPath;
                bool targetPathAvailable = false;
                bool iconLocationAvailable = false;
                bool showCommandAvailable = false;
                bool hotkeyAvailable = false;
                int iconIndex = 0;
                int showCommand = 0;
                WORD hotkey = 0;
                IShellLinkW* shellLink = nullptr;
                HRESULT parseResult = CoCreateInstance(
                    CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                    IID_IShellLinkW, reinterpret_cast<void**>(&shellLink));
                if (SUCCEEDED(parseResult)) {
                    IPersistFile* persistFile = nullptr;
                    parseResult = shellLink->QueryInterface(
                        IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
                    if (SUCCEEDED(parseResult)) {
                        parseResult = persistFile->Load(linkPath.c_str(), STGM_READ);
                        if (SUCCEEDED(parseResult)) {
                            std::vector<wchar_t> text(32768, L'\0');
                            if (SUCCEEDED(shellLink->GetPath(
                                    &text[0], static_cast<int>(text.size()), nullptr,
                                    SLGP_RAWPATH))) {
                                targetPath.assign(&text[0]);
                                targetPathAvailable = !targetPath.empty();
                            }
                            std::fill(text.begin(), text.end(), L'\0');
                            if (SUCCEEDED(shellLink->GetArguments(
                                    &text[0], static_cast<int>(text.size())))) {
                                arguments.assign(&text[0]);
                            }
                            std::fill(text.begin(), text.end(), L'\0');
                            if (SUCCEEDED(shellLink->GetWorkingDirectory(
                                    &text[0], static_cast<int>(text.size())))) {
                                workingDirectory.assign(&text[0]);
                            }
                            std::fill(text.begin(), text.end(), L'\0');
                            if (SUCCEEDED(shellLink->GetDescription(
                                    &text[0], static_cast<int>(text.size())))) {
                                description.assign(&text[0]);
                            }
                            std::fill(text.begin(), text.end(), L'\0');
                            if (SUCCEEDED(shellLink->GetIconLocation(
                                    &text[0], static_cast<int>(text.size()), &iconIndex))) {
                                iconPath.assign(&text[0]);
                                iconLocationAvailable = true;
                            }
                            showCommandAvailable =
                                SUCCEEDED(shellLink->GetShowCmd(&showCommand));
                            hotkeyAvailable =
                                SUCCEEDED(shellLink->GetHotkey(&hotkey));
                        }
                        persistFile->Release();
                    }
                    shellLink->Release();
                }
                if (FAILED(parseResult)) {
                    parseStatus = "parse_failed_" +
                        std::to_string(static_cast<unsigned long>(parseResult));
                    complete = false;
                    ++issueCount;
                }
                if (!hashAvailable) {
                    parseStatus += "; hash_before_failed_" + std::to_string(hashError);
                    complete = false;
                    ++issueCount;
                }

                WIN32_FILE_ATTRIBUTE_DATA afterParse = {};
                const bool metadataAvailable = GetFileAttributesExW(
                    linkPath.c_str(), GetFileExInfoStandard, &afterParse) != FALSE;
                const DWORD metadataError = metadataAvailable ? ERROR_SUCCESS : GetLastError();
                if (!metadataAvailable ||
                    afterParse.nFileSizeHigh != entry.nFileSizeHigh ||
                    afterParse.nFileSizeLow != entry.nFileSizeLow ||
                    CompareFileTime(&afterParse.ftLastWriteTime, &entry.ftLastWriteTime) != 0) {
                    parseStatus += metadataAvailable
                        ? "; changed_during_collection"
                        : "; metadata_after_parse_failed_" + std::to_string(metadataError);
                    complete = false;
                    ++issueCount;
                }
                if (hashAvailable && metadataAvailable) {
                    std::string digestAfterParse;
                    DWORD hashAfterError = ERROR_SUCCESS;
                    if (!sha256File(linkPath, digestAfterParse, hashAfterError)) {
                        parseStatus += "; hash_after_failed_" +
                            std::to_string(hashAfterError);
                        complete = false;
                        ++issueCount;
                    } else if (digestAfterParse != digest) {
                        parseStatus += "; content_changed_during_collection";
                        complete = false;
                        ++issueCount;
                    }
                }

                std::ostringstream record;
                record << "{\"shortcut_path\": " << jsonString(toUtf8(linkPath.c_str()))
                       << ", \"size_bytes\": "
                       << ((static_cast<unsigned long long>(entry.nFileSizeHigh) << 32) |
                           entry.nFileSizeLow)
                       << ", \"created_utc\": "
                       << jsonString(fileTimeFromFindData(entry.ftCreationTime))
                       << ", \"modified_utc\": "
                       << jsonString(fileTimeFromFindData(entry.ftLastWriteTime))
                       << ", \"sha256\": " << jsonString(hashAvailable ? digest : "")
                       << ", \"target_path_raw\": "
                       << jsonString(toUtf8(targetPath.c_str()))
                       << ", \"target_path_available\": "
                       << (targetPathAvailable ? "true" : "false")
                       << ", \"arguments\": " << jsonString(toUtf8(arguments.c_str()))
                       << ", \"working_directory\": "
                       << jsonString(toUtf8(workingDirectory.c_str()))
                       << ", \"description\": " << jsonString(toUtf8(description.c_str()))
                       << ", \"icon_path\": " << jsonString(toUtf8(iconPath.c_str()))
                       << ", \"icon_location_available\": "
                       << (iconLocationAvailable ? "true" : "false")
                       << ", \"icon_index\": ";
                if (iconLocationAvailable) record << iconIndex;
                else record << "null";
                record << ", \"show_command\": ";
                if (showCommandAvailable) record << showCommand;
                else record << "null";
                record << ", \"show_command_available\": "
                       << (showCommandAvailable ? "true" : "false")
                       << ", \"hotkey\": ";
                if (hotkeyAvailable) record << hotkey;
                else record << "null";
                record << ", \"hotkey_available\": "
                       << (hotkeyAvailable ? "true" : "false")
                       << ", \"resolution_performed\": false"
                       << ", \"status\": " << jsonString(parseStatus) << "}";
                if (!writeJsonLineToOpenFile(output, record.str(), error)) {
                    complete = false;
                    ++issueCount;
                    break;
                }
                ++recordCount;
            } while (FindNextFileW(search, &entry));
            const DWORD iterationError = GetLastError();
            FindClose(search);
            if (iterationError != ERROR_NO_MORE_FILES && !cancellationRequested()) {
                error = iterationError;
                complete = false;
                ++issueCount;
            }
        }
    }

    if (uninitialize) CoUninitialize();
    if (cancellationRequested()) {
        complete = false;
        ++issueCount;
    }
    if (!finishEvidenceFile(output, error)) {
        complete = false;
        ++issueCount;
    }
    const std::string status = complete
        ? std::string("complete") : (cancellationRequested()
            ? "cancelled_partial" : "partial_with_" + std::to_string(issueCount) + "_issues");
    fileRecord = makeCaseFileRecord(L"recent_shortcuts.jsonl", outputPath,
                                    status, recordCount, error);
    return complete && !fileRecord.sha256.empty();
}

bool exportServices(const std::wstring& outputPath, CaseFileRecord& fileRecord, DWORD& error) {
    error = ERROR_SUCCESS;
    fileRecord.fileName = L"services.jsonl";
    HANDLE output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        fileRecord.status = "create_failed_" + std::to_string(error);
        return false;
    }

    HKEY services = nullptr;
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services", 0, KEY_READ | KEY_WOW64_64KEY, &services);
    unsigned long long count = 0;
    unsigned long long serviceErrors = 0;
    bool complete = status == ERROR_SUCCESS;
    if (complete) {
        for (DWORD index = 0;; ++index) {
            if (cancellationRequested()) {
                complete = false;
                ++serviceErrors;
                break;
            }
            wchar_t serviceName[512] = {};
            DWORD nameLength = static_cast<DWORD>(sizeof(serviceName) / sizeof(serviceName[0]));
            status = RegEnumKeyExW(services, index, serviceName, &nameLength,
                                   nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status != ERROR_SUCCESS) {
                error = static_cast<DWORD>(status);
                complete = false;
                ++serviceErrors;
                continue;
            }

            HKEY service = nullptr;
            const LONG openStatus = RegOpenKeyExW(services, serviceName, 0, KEY_READ, &service);
            if (openStatus != ERROR_SUCCESS) {
                const std::string record = "{\"service_name\": "
                    + jsonString(toUtf8(serviceName)) + ", \"registry_error\": "
                    + jsonString(std::to_string(openStatus)) + "}";
                if (!writeJsonLineToOpenFile(output, record, error)) {
                    complete = false;
                    break;
                }
                ++serviceErrors;
                complete = false;
                continue;
            }
            std::string displayName;
            std::string imagePath;
            std::string account;
            readRegistryString(service, "DisplayName", displayName);
            readRegistryString(service, "ImagePath", imagePath);
            readRegistryString(service, "ObjectName", account);
            DWORD startType = 0;
            DWORD typeSize = sizeof(startType);
            DWORD serviceType = 0;
            DWORD serviceTypeSize = sizeof(serviceType);
            const bool hasStart = RegQueryValueExA(service, "Start", nullptr, nullptr,
                reinterpret_cast<LPBYTE>(&startType), &typeSize) == ERROR_SUCCESS;
            const bool hasType = RegQueryValueExA(service, "Type", nullptr, nullptr,
                reinterpret_cast<LPBYTE>(&serviceType), &serviceTypeSize) == ERROR_SUCCESS;
            RegCloseKey(service);

            std::ostringstream record;
            record << "{\"service_name\": " << jsonString(toUtf8(serviceName))
                   << ", \"display_name\": " << jsonString(displayName)
                   << ", \"image_path\": " << jsonString(imagePath)
                   << ", \"account\": " << jsonString(account)
                   << ", \"start_type\": ";
            if (hasStart) record << startType;
            else record << "null";
            record << ", \"service_type\": ";
            if (hasType) record << serviceType;
            else record << "null";
            record << "}";
            if (!writeJsonLineToOpenFile(output, record.str(), error)) {
                complete = false;
                break;
            }
            ++count;
            if (count % 250 == 0) {
                std::cout << "  Servicos inventariados: " << count << "\n";
            }
        }
        RegCloseKey(services);
    } else {
        error = static_cast<DWORD>(status);
        const std::string errorRecord = "{\"collection_error\": "
            + jsonString(std::to_string(status)) + "}";
        if (!writeJsonLineToOpenFile(output, errorRecord, error)) {
            complete = false;
        }
    }

    if (!finishEvidenceFile(output, error)) complete = false;
    if (serviceErrors != 0) {
        complete = false;
    }
    const std::string collectionStatus = complete
        ? std::string("complete")
        : (cancellationRequested() ? "cancelled_partial"
            : "partial_or_access_error_" + std::to_string(serviceErrors));
    fileRecord = makeCaseFileRecord(L"services.jsonl", outputPath,
                                    collectionStatus, count, error);
    return complete && !fileRecord.sha256.empty();
}

std::string timelineSummaryJson(const TimelineSorter& sorter) {
    std::ostringstream json;
    json << "{\n"
         << "  \"total_events\": " << sorter.recordCount << ",\n"
         << "  \"events_with_valid_utc_timestamp\": "
         << (sorter.recordCount - sorter.invalidTimestampCount) << ",\n"
         << "  \"events_with_invalid_timestamp\": " << sorter.invalidTimestampCount << ",\n"
         << "  \"earliest_timestamp_utc\": " << jsonString(sorter.earliestTimestamp) << ",\n"
         << "  \"latest_timestamp_utc\": " << jsonString(sorter.latestTimestamp) << ",\n"
         << "  \"events_by_hour_utc\": {";
    bool first = true;
    for (const auto& entry : sorter.eventsByHour) {
        json << (first ? "\n" : ",\n") << "    " << jsonString(entry.first)
             << ": " << entry.second;
        first = false;
    }
    if (!first) json << '\n';
    json << "  },\n  \"events_by_channel\": {";
    first = true;
    for (const auto& entry : sorter.eventsByChannel) {
        json << (first ? "\n" : ",\n") << "    " << jsonString(entry.first)
             << ": " << entry.second;
        first = false;
    }
    if (!first) json << '\n';
    json << "  },\n  \"events_by_channel_and_id\": [";
    first = true;
    for (const auto& entry : sorter.eventsByChannelAndId) {
        const std::size_t separator = entry.first.find('|');
        const std::string channel = entry.first.substr(0, separator);
        const std::string eventId = separator == std::string::npos
            ? "" : entry.first.substr(separator + 1);
        json << (first ? "\n" : ",\n")
             << "    {\"channel\": " << jsonString(channel)
             << ", \"event_id\": " << jsonString(eventId)
             << ", \"count\": " << entry.second << "}";
        first = false;
    }
    if (!first) json << '\n';
    json << "  ]\n}\n";
    return json.str();
}

std::string currentUtcTimestamp() {
    SYSTEMTIME now = {};
    GetSystemTime(&now);
    return formatUtcTimestamp(now);
}

bool writeCorrelationSnapshots(const std::wstring& caseDirectory,
                               const ProcessMap& processes, bool processSnapshotAvailable,
                               DWORD processSnapshotError, const std::string& processSnapshotAtUtc,
                               const NetworkAddressSet& remoteAddresses,
                               bool networkSnapshotAvailable, DWORD networkSnapshotError,
                               const std::string& networkSnapshotAtUtc,
                               std::vector<CaseFileRecord>& files, DWORD& error) {
    std::ostringstream processJsonl;
    unsigned long long processCount = 0;
    if (processSnapshotAvailable) {
        for (const auto& entry : processes) {
            processJsonl << "{\"snapshot_at_utc\": " << jsonString(processSnapshotAtUtc)
                         << ", \"pid\": " << entry.first
                         << ", \"name\": " << jsonString(entry.second.name)
                         << ", \"parent_pid\": " << entry.second.parentPid << "}\n";
            ++processCount;
        }
    } else {
        processJsonl << "{\"snapshot_error\": "
                     << jsonString(std::to_string(processSnapshotError)) << "}\n";
    }

    const std::wstring processPath =
        caseDirectory + L"\\process_snapshot_for_correlation.jsonl";
    if (!writeNewFile(processPath, processJsonl.str(), error)) return false;
    CaseFileRecord processRecord = makeCaseFileRecord(
        L"process_snapshot_for_correlation.jsonl", processPath,
        processSnapshotAvailable ? "complete" :
            "snapshot_failed_" + std::to_string(processSnapshotError),
        processCount, error);
    if (processRecord.sha256.empty()) return false;
    files.push_back(processRecord);

    std::ostringstream networkJsonl;
    unsigned long long addressCount = 0;
    if (networkSnapshotAvailable) {
        for (const auto& address : remoteAddresses) {
            networkJsonl << "{\"snapshot_at_utc\": " << jsonString(networkSnapshotAtUtc)
                         << ", \"active_tcp_remote_address\": "
                         << jsonString(address) << "}\n";
            ++addressCount;
        }
    } else {
        networkJsonl << "{\"snapshot_error\": "
                     << jsonString(std::to_string(networkSnapshotError)) << "}\n";
    }
    const std::wstring networkPath =
        caseDirectory + L"\\network_snapshot_for_correlation.jsonl";
    if (!writeNewFile(networkPath, networkJsonl.str(), error)) return false;
    CaseFileRecord networkRecord = makeCaseFileRecord(
        L"network_snapshot_for_correlation.jsonl", networkPath,
        networkSnapshotAvailable ? "complete" :
            "snapshot_failed_" + std::to_string(networkSnapshotError),
        addressCount, error);
    if (networkRecord.sha256.empty()) return false;
    files.push_back(networkRecord);
    return processSnapshotAvailable && networkSnapshotAvailable;
}

bool exportSupplementalEvidence(const CaseMetadata& metadata,
                               const std::wstring& caseDirectory,
                               std::vector<CaseFileRecord>& files,
                               std::string& summary, DWORD& error) {
    bool complete = true;
    std::ostringstream result;
    const bool includeAll = metadata.selection == 6;
    const bool includeLogs = includeAll || metadata.selection == 8 ||
        (metadata.customCollection && metadata.collectLogs);
    const bool includeFiles = includeAll || metadata.selection == 9 ||
        (metadata.customCollection && metadata.collectFiles);
    const bool includeServicesTasks = includeAll ||
        (metadata.customCollection && metadata.collectServicesTasks);
    const bool includeNtfs = includeAll ||
        (metadata.customCollection && metadata.collectNtfsArtifacts);
    const bool includeUserActivity = includeAll ||
        (metadata.customCollection && metadata.collectUserActivity);

    if (includeLogs) {
        result << "Eventos exportados como EVTX pelo Event Log API e XML renderizado; os EVTX "
               << "sao exportacoes do log ativo, nao copias byte a byte dos arquivos-fonte.\n";
        struct LogChannel {
            const wchar_t* name;
            const wchar_t* jsonName;
            const wchar_t* evtxName;
        };
        const LogChannel channels[] = {
            {L"Security", L"events_security.jsonl", L"events_security.evtx"},
            {L"System", L"events_system.jsonl", L"events_system.evtx"},
            {L"Application", L"events_application.jsonl", L"events_application.evtx"},
            {L"Microsoft-Windows-Windows Defender/Operational",
             L"events_defender.jsonl", L"events_defender.evtx"},
            {L"Microsoft-Windows-PowerShell/Operational",
             L"events_powershell.jsonl", L"events_powershell.evtx"},
            {L"Microsoft-Windows-Sysmon/Operational",
             L"events_sysmon.jsonl", L"events_sysmon.evtx"},
            {L"Microsoft-Windows-TaskScheduler/Operational",
             L"events_taskscheduler.jsonl", L"events_taskscheduler.evtx"},
            {L"Microsoft-Windows-WMI-Activity/Operational",
             L"events_wmi_activity.jsonl", L"events_wmi_activity.evtx"},
            {L"Microsoft-Windows-TerminalServices-LocalSessionManager/Operational",
             L"events_rdp_sessions.jsonl", L"events_rdp_sessions.evtx"},
            {L"Microsoft-Windows-TerminalServices-RemoteConnectionManager/Operational",
             L"events_rdp_connections.jsonl", L"events_rdp_connections.evtx"},
            {L"Microsoft-Windows-WinRM/Operational",
             L"events_winrm.jsonl", L"events_winrm.evtx"},
            {L"Microsoft-Windows-DNS-Client/Operational",
             L"events_dns_client.jsonl", L"events_dns_client.evtx"},
            {L"Microsoft-Windows-NetworkProfile/Operational",
             L"events_network_profile.jsonl", L"events_network_profile.evtx"},
            {L"Microsoft-Windows-Windows Firewall With Advanced Security/Firewall",
             L"events_firewall.jsonl", L"events_firewall.evtx"}
        };
        TimelineSorter timelineSorter;
        timelineSorter.directory = caseDirectory;
        bool timelineHealthy = true;
        ProcessMap correlationProcesses;
        NetworkAddressSet correlationRemoteAddresses;
        DWORD processSnapshotError = ERROR_SUCCESS;
        DWORD networkSnapshotError = ERROR_SUCCESS;
        const std::string processSnapshotAtUtc = currentUtcTimestamp();
        const bool processSnapshotAvailable =
            snapshotProcesses(correlationProcesses, processSnapshotError);
        const std::string networkSnapshotAtUtc = currentUtcTimestamp();
        const bool networkSnapshotAvailable =
            snapshotTcpRemoteAddresses(correlationRemoteAddresses, networkSnapshotError);
        const bool snapshotsExported = writeCorrelationSnapshots(
            caseDirectory, correlationProcesses, processSnapshotAvailable,
            processSnapshotError, processSnapshotAtUtc, correlationRemoteAddresses,
            networkSnapshotAvailable, networkSnapshotError, networkSnapshotAtUtc,
            files, error);
        result << "Snapshots de correlacao: processos "
               << (processSnapshotAvailable ? std::to_string(correlationProcesses.size())
                    : "indisponiveis, erro " + std::to_string(processSnapshotError))
               << "; enderecos TCP remotos "
               << (networkSnapshotAvailable
                    ? std::to_string(correlationRemoteAddresses.size())
                    : "indisponiveis, erro " + std::to_string(networkSnapshotError))
               << ". Correspondencias na timeline sao pistas de snapshots atuais, "
               << "nao comprovacao historica.\n";
        if (!snapshotsExported) complete = false;
        for (const auto& channel : channels) {
            if (metadata.customCollection &&
                std::find(metadata.eventChannels.begin(), metadata.eventChannels.end(),
                          channel.name) == metadata.eventChannels.end()) {
                continue;
            }
            if (cancellationRequested()) {
                CaseFileRecord nativeLog;
                nativeLog.fileName = channel.evtxName;
                nativeLog.status = "cancelled_before_collection";
                files.push_back(nativeLog);
                CaseFileRecord record;
                record.fileName = channel.jsonName;
                record.status = "cancelled_before_collection";
                files.push_back(record);
                complete = false;
                continue;
            }
            CaseFileRecord nativeLog;
            const bool nativeExported = exportNativeEventLog(
                channel.name, channel.evtxName,
                caseDirectory + L"\\" + channel.evtxName, nativeLog, error);
            result << "Log EVTX " << toUtf8(channel.name) << ": "
                   << nativeLog.status << ".\n";
            files.push_back(nativeLog);
            if (!nativeExported) complete = false;

            CaseFileRecord record;
            const std::wstring path = caseDirectory + L"\\" + channel.jsonName;
            const bool captured = exportEventChannel(channel.name, channel.jsonName,
                                                      path, timelineSorter, correlationProcesses,
                                                      processSnapshotAvailable,
                                                      correlationRemoteAddresses,
                                                      networkSnapshotAvailable,
                                                      processSnapshotAtUtc,
                                                      networkSnapshotAtUtc, timelineHealthy,
                                                      record, error);
            result << "Log XML " << toUtf8(channel.name) << ": "
                   << record.records << " eventos, estado " << record.status << ".\n";
            files.push_back(record);
            if (!captured) complete = false;
        }
        unsigned long long timelineRecordCount = 0;
        const std::wstring timelinePath = caseDirectory + L"\\timeline.jsonl";
        if (timelineHealthy) {
            timelineHealthy = writeSortedTimeline(
                timelineSorter, timelinePath, timelineRecordCount, error);
        }
        const bool tempRunsCleaned = cleanupTimelineRuns(timelineSorter, error);
        if (!tempRunsCleaned) {
            timelineHealthy = false;
            result << "Arquivos temporarios da ordenacao podem ter permanecido no diretorio do caso.\n";
        }
        if (!timelineHealthy) {
            complete = false;
            result << "Linha do tempo: falha na coleta, ordenacao global ou gravacao (erro "
                   << error << ").\n";
        }
        if (GetFileAttributesW(timelinePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            CaseFileRecord timeline = makeCaseFileRecord(
                L"timeline.jsonl", timelinePath,
                timelineHealthy ? (cancellationRequested()
                    ? "cancelled_partial_but_sorted_by_utc_timestamp"
                    : "globally_sorted_by_utc_timestamp_invalid_last")
                                : (cancellationRequested()
                                    ? "cancelled_partial" : "partial_sort_or_write_failure"),
                timelineRecordCount, error);
            timeline.records = timelineRecordCount;
            if (timeline.sha256.empty()) complete = false;
            result << "Linha do tempo: eventos ordenados globalmente por horario UTC; "
                   << "inclui canal, evento, provedor, registro, SID do emissor, PID/TID do "
                   << "provedor, atividade e campos EventData/Name. O XML completo permanece "
                   << "nos arquivos por canal; registros sem timestamp valido ao final: "
                   << timelineSorter.invalidTimestampCount << ".\n";
            if (!timelineHealthy) complete = false;
            files.push_back(timeline);
        } else {
            complete = false;
            CaseFileRecord timeline;
            timeline.fileName = L"timeline.jsonl";
            timeline.status = "sort_or_create_failed_" + std::to_string(error);
            files.push_back(timeline);
        }

        const std::wstring summaryPath = caseDirectory + L"\\timeline_summary.json";
        const std::string summaryContent = timelineSummaryJson(timelineSorter);
        if (writeNewFile(summaryPath, summaryContent, error)) {
            CaseFileRecord timelineSummary = makeCaseFileRecord(
                L"timeline_summary.json", summaryPath,
                cancellationRequested() ? "partial_due_to_cancellation" : "complete",
                timelineSorter.recordCount, error);
            if (timelineSummary.sha256.empty()) complete = false;
            result << "Resumo temporal: " << timelineSorter.recordCount << " eventos; "
                   << timelineSorter.eventsByHour.size() << " intervalos horarios UTC; "
                   << timelineSorter.eventsByChannel.size() << " canais; timestamps invalidos: "
                   << timelineSorter.invalidTimestampCount << ".\n";
            files.push_back(timelineSummary);
        } else {
            complete = false;
            CaseFileRecord timelineSummary;
            timelineSummary.fileName = L"timeline_summary.json";
            timelineSummary.status = "write_failed_" + std::to_string(error);
            files.push_back(timelineSummary);
            result << "Resumo temporal: falha ao gravar (erro " << error << ").\n";
        }
    }

    if (includeServicesTasks) {
        CaseFileRecord services;
        if (!exportServices(caseDirectory + L"\\services.jsonl", services, error)) {
            complete = false;
        }
        result << "Servicos registrados: " << services.records
               << ", estado " << services.status << ".\n";
        files.push_back(services);

        wchar_t windowsDirectory[MAX_PATH] = {};
        const UINT windowsLength = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
        if (windowsLength == 0 || windowsLength >= MAX_PATH) {
            error = GetLastError();
            result << "Tarefas agendadas: falha ao localizar pasta do Windows (erro "
                   << error << ").\n";
            complete = false;
            CaseFileRecord tasks;
            tasks.fileName = L"scheduled_tasks.jsonl";
            tasks.status = "windows_directory_unavailable_" + std::to_string(error);
            files.push_back(tasks);
            CaseFileRecord taskDetails;
            taskDetails.fileName = L"scheduled_tasks_details.jsonl";
            taskDetails.status = "windows_directory_unavailable_" + std::to_string(error);
            files.push_back(taskDetails);
        } else {
            CaseFileRecord tasks;
            const std::wstring taskRoot = std::wstring(windowsDirectory) + L"\\System32\\Tasks";
            if (!exportFileMetadata(taskRoot, caseDirectory,
                    caseDirectory + L"\\scheduled_tasks.jsonl", L"scheduled_tasks.jsonl",
                    tasks, error)) {
                complete = false;
            }
            result << "Arquivos de tarefas agendadas: " << tasks.records
                   << ", estado " << tasks.status << ".\n";
            files.push_back(tasks);

            CaseFileRecord taskDetails;
            if (!exportScheduledTaskDetails(taskRoot,
                    caseDirectory + L"\\scheduled_tasks_details.jsonl",
                    taskDetails, error)) {
                complete = false;
            }
            result << "Definicoes XML de tarefas agendadas: " << taskDetails.records
                   << ", estado " << taskDetails.status
                   << "; inclui acoes e gatilhos.\n";
            files.push_back(taskDetails);
        }
    }

    if (includeFiles) {
        if (metadata.evidenceFolder.empty()) {
            result << "Metadados de arquivos: nenhuma pasta foi selecionada.\n";
            complete = false;
        } else {
            CaseFileRecord fileMetadata;
            if (!exportFileMetadata(metadata.evidenceFolder, caseDirectory,
                    caseDirectory + L"\\files.jsonl", L"files.jsonl",
                    fileMetadata, error)) {
                complete = false;
            }
            result << "Arquivos inventariados em " << toUtf8(metadata.evidenceFolder.c_str())
                   << ": " << fileMetadata.records << ", estado "
                   << fileMetadata.status << ".\n";
            files.push_back(fileMetadata);
        }
    }

    if (includeNtfs) {
        wchar_t windowsDirectory[MAX_PATH] = {};
        const UINT windowsLength = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
        if (windowsLength == 0 || windowsLength >= MAX_PATH) {
            error = GetLastError();
            CaseFileRecord prefetch;
            prefetch.fileName = L"prefetch.jsonl";
            prefetch.status = "windows_directory_unavailable_" + std::to_string(error);
            files.push_back(prefetch);
            result << "Prefetch: falha ao localizar pasta do Windows (erro "
                   << error << ").\n";
            complete = false;
        } else {
            CaseFileRecord prefetch;
            const std::wstring prefetchRoot =
                std::wstring(windowsDirectory) + L"\\Prefetch";
            if (!exportFileMetadata(prefetchRoot, caseDirectory,
                    caseDirectory + L"\\prefetch.jsonl", L"prefetch.jsonl",
                    prefetch, error)) {
                complete = false;
            }
            result << "Prefetch: " << prefetch.records
                   << " arquivos, estado " << prefetch.status << ".\n";
            files.push_back(prefetch);
        }
    }

    if (includeNtfs || includeFiles) {
        if (metadata.evidenceFolder.empty()) {
            result << "Fluxos alternativos NTFS: nenhuma pasta foi selecionada.\n";
            if (includeNtfs) complete = false;
        } else {
            CaseFileRecord streams;
            if (!exportAlternateDataStreams(metadata.evidenceFolder, caseDirectory,
                    caseDirectory + L"\\alternate_data_streams.jsonl",
                    streams, error)) {
                complete = false;
            }
            result << "Fluxos alternativos NTFS: " << streams.records
                   << " registros, estado " << streams.status << ".\n";
            files.push_back(streams);
        }
    }

    if (includeUserActivity) {
        CaseFileRecord recentShortcuts;
        if (!exportRecentShortcuts(caseDirectory + L"\\recent_shortcuts.jsonl",
                                   recentShortcuts, error)) {
            complete = false;
        }
        result << "Atalhos recentes: " << recentShortcuts.records
               << ", estado " << recentShortcuts.status
               << "; destinos nao foram resolvidos nem executados.\n";
        files.push_back(recentShortcuts);

        wchar_t appData[MAX_PATH] = {};
        const HRESULT appDataResult = SHGetFolderPathW(
            nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData);
        if (FAILED(appDataResult)) {
            const std::string status = "appdata_unavailable_" +
                std::to_string(static_cast<unsigned long>(appDataResult));
            for (const wchar_t* name : {L"automatic_destinations.jsonl",
                                        L"custom_destinations.jsonl"}) {
                CaseFileRecord jumpLists;
                jumpLists.fileName = name;
                jumpLists.status = status;
                files.push_back(jumpLists);
            }
            result << "Jump Lists: pasta AppData indisponivel; erro "
                   << static_cast<unsigned long>(appDataResult) << ".\n";
            complete = false;
        } else {
            const std::wstring recentRoot =
                std::wstring(appData) + L"\\Microsoft\\Windows\\Recent";
            const std::wstring jumpListFolders[] = {
                recentRoot + L"\\AutomaticDestinations",
                recentRoot + L"\\CustomDestinations"
            };
            const wchar_t* outputNames[] = {
                L"automatic_destinations.jsonl",
                L"custom_destinations.jsonl"
            };
            for (std::size_t index = 0; index < 2; ++index) {
                CaseFileRecord jumpLists;
                const std::wstring outputName = outputNames[index];
                if (!exportOptionalDirectoryMetadata(jumpListFolders[index], caseDirectory,
                        caseDirectory + L"\\" + outputName, outputName,
                        jumpLists, error)) {
                    complete = false;
                }
                result << (index == 0 ? "Jump Lists automaticas: " : "Jump Lists personalizadas: ")
                       << jumpLists.records << ", estado " << jumpLists.status
                       << "; inventario/hash dos arquivos, sem interpretar o formato binario.\n";
                files.push_back(jumpLists);
            }
        }
    }

    summary = result.str();
    return complete;
}

bool exportCase(const CaseMetadata& metadata, const std::string& report,
                std::wstring& caseDirectory, std::string& errorMessage) {
    DWORD currentLength = GetCurrentDirectoryW(0, nullptr);
    if (currentLength == 0) {
        errorMessage = "Falha ao obter pasta atual (erro " + std::to_string(GetLastError()) + ").";
        return false;
    }
    std::vector<wchar_t> currentDirectory(currentLength);
    currentLength = GetCurrentDirectoryW(currentLength, &currentDirectory[0]);
    if (currentLength == 0 || currentLength >= currentDirectory.size()) {
        errorMessage = "Falha ao obter pasta atual (erro " + std::to_string(GetLastError()) + ").";
        return false;
    }

    std::wstring casesDirectory(&currentDirectory[0]);
    casesDirectory += L"\\cases";
    if (!CreateDirectoryW(casesDirectory.c_str(), nullptr)) {
        const DWORD createError = GetLastError();
        const DWORD attributes = GetFileAttributesW(casesDirectory.c_str());
        if (createError != ERROR_ALREADY_EXISTS || attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            errorMessage = "Falha ao criar pasta cases (erro " + std::to_string(createError) + ").";
            return false;
        }
    }

    std::wstring caseName(metadata.caseId.begin(), metadata.caseId.end());
    CaseMetadata exportedMetadata = metadata;
    bool created = false;
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        std::wstring candidate = casesDirectory + L"\\" + caseName;
        if (attempt != 0) {
            candidate += L"_" + std::to_wstring(attempt);
            exportedMetadata.caseId = metadata.caseId + "_" + std::to_string(attempt);
        }
        if (CreateDirectoryW(candidate.c_str(), nullptr)) {
            caseDirectory = candidate;
            created = true;
            break;
        }
        const DWORD createError = GetLastError();
        if (createError != ERROR_ALREADY_EXISTS) {
            errorMessage = "Falha ao criar pasta do caso (erro " + std::to_string(createError) + ").";
            return false;
        }
    }
    if (!created) {
        errorMessage = "Nao foi possivel reservar um nome exclusivo para o caso.";
        return false;
    }

    std::vector<CaseFileRecord> evidenceFiles;
    std::string evidenceSummary;
    bool collectionComplete = exportSupplementalEvidence(
        exportedMetadata, caseDirectory, evidenceFiles, evidenceSummary, currentLength);
    if (cancellationRequested()) collectionComplete = false;

    SYSTEMTIME completed = {};
    GetSystemTime(&completed);
    exportedMetadata.completedAtUtc = formatUtcTimestamp(completed);
    std::string textReport = "CASO: " + exportedMetadata.caseId + "\n"
        + "Inicio da coleta (UTC): " + exportedMetadata.collectedAtUtc + "\n"
        + "Fim da coleta de fontes (UTC): " + exportedMetadata.completedAtUtc + "\n"
        + "Computador: " + exportedMetadata.computer + "\n"
        + "Operador: " + exportedMetadata.operatorName + "\n"
        + "Fuso horario: " + timeZoneDisplayName(exportedMetadata) + " ("
        + timeZoneOffsetDisplay(exportedMetadata) + "; "
        + exportedMetadata.timeZoneStatus + ")\n"
        + "Windows: " + exportedMetadata.windowsProductName + " "
        + exportedMetadata.windowsDisplayVersion + " (build "
        + exportedMetadata.windowsBuild + ")\n"
        + "Arquitetura nativa: " + exportedMetadata.nativeArchitecture + "\n"
        + "Elevacao do processo: " + exportedMetadata.elevationStatus + "\n"
        + "Aviso: inventario ao vivo; nao e imagem forense nem garante cadeia de custodia.\n\n"
        + report + "\n=== ARQUIVOS DE EVIDENCIA EXPORTADOS ===\n" + evidenceSummary;
    const std::wstring textPath = caseDirectory + L"\\report.txt";
    if (!writeNewFile(textPath, textReport, currentLength)) {
        errorMessage = "Falha ao gravar report.txt (erro " + std::to_string(currentLength) + ").";
        return false;
    }

    std::string jsonReport = metadataJson(exportedMetadata);
    jsonReport += jsonString(textReport) + "\n}\n";
    const std::wstring jsonPath = caseDirectory + L"\\report.json";
    if (!writeNewFile(jsonPath, jsonReport, currentLength)) {
        errorMessage = "Falha ao gravar report.json (erro " + std::to_string(currentLength) + ").";
        return false;
    }

    std::vector<CaseFileRecord> allFiles;
    CaseFileRecord textRecord = makeCaseFileRecord(
        L"report.txt", textPath, "complete", 0, currentLength);
    CaseFileRecord jsonRecord = makeCaseFileRecord(
        L"report.json", jsonPath, "complete", 0, currentLength);
    allFiles.push_back(textRecord);
    allFiles.push_back(jsonRecord);
    allFiles.insert(allFiles.end(), evidenceFiles.begin(), evidenceFiles.end());

    bool filesVerified = true;
    for (CaseFileRecord& record : allFiles) {
        std::wstring path = caseDirectory + L"\\" + record.fileName;
        std::string verificationHash;
        if (record.sha256.empty() || !sha256File(path, verificationHash, currentLength) ||
            verificationHash != record.sha256) {
            record.status += "; post_write_verification_failed";
            filesVerified = false;
        }
    }
    if (textRecord.sha256.empty() || jsonRecord.sha256.empty()) {
        errorMessage = "Falha ao calcular SHA-256 (erro " + std::to_string(currentLength) + ").";
        return false;
    }

    std::ostringstream manifest;
    manifest << "{\n"
             << "  \"case_id\": " << jsonString(exportedMetadata.caseId) << ",\n"
             << "  \"collection_started_at_utc\": "
             << jsonString(exportedMetadata.collectedAtUtc) << ",\n"
             << "  \"collection_completed_at_utc\": "
             << jsonString(exportedMetadata.completedAtUtc) << ",\n"
             << "  \"computer\": " << jsonString(exportedMetadata.computer) << ",\n"
             << "  \"operator\": " << jsonString(exportedMetadata.operatorName) << ",\n"
             << "  \"time_zone_status\": "
             << jsonString(exportedMetadata.timeZoneStatus) << ",\n"
             << "  \"time_zone_state\": "
             << jsonString(exportedMetadata.timeZoneState) << ",\n"
             << "  \"time_zone_standard_name\": "
             << jsonString(exportedMetadata.timeZoneStandardName) << ",\n"
             << "  \"time_zone_daylight_name\": "
             << jsonString(exportedMetadata.timeZoneDaylightName) << ",\n"
             << "  \"utc_offset_minutes\": "
             << exportedMetadata.utcOffsetMinutes << ",\n"
             << "  \"windows_product_name\": "
             << jsonString(exportedMetadata.windowsProductName) << ",\n"
             << "  \"windows_display_version\": "
             << jsonString(exportedMetadata.windowsDisplayVersion) << ",\n"
             << "  \"windows_build\": " << jsonString(exportedMetadata.windowsBuild) << ",\n"
             << "  \"native_architecture\": "
             << jsonString(exportedMetadata.nativeArchitecture) << ",\n"
             << "  \"elevation_status\": "
             << jsonString(exportedMetadata.elevationStatus) << ",\n"
             << "  \"selection\": " << exportedMetadata.selection << ",\n"
             << "  \"collect_user_activity_artifacts\": "
             << ((exportedMetadata.selection == 6 ||
                  exportedMetadata.collectUserActivity) ? "true" : "false") << ",\n"
             << "  \"acquisition_type\": \"live_system_triage\",\n"
             << "  \"sources_attempted\": [";
    const std::vector<std::string> sources = selectedSources(exportedMetadata);
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (index != 0) manifest << ", ";
        manifest << jsonString(sources[index]);
    }
    manifest << "],\n"
             << "  \"collection_status\": "
             << jsonString(cancellationRequested() ? "cancelled_partial" :
                           collectionComplete ? "complete" : "partial")
             << ",\n"
             << "  \"hash_algorithm\": \"SHA-256\",\n"
             << "  \"collection_complete\": " << (collectionComplete ? "true" : "false") << ",\n"
             << "  \"verified_after_write\": " << (filesVerified ? "true" : "false") << ",\n"
             << "  \"manifest_note\": \"Hashes cobrem todos os arquivos listados; o manifesto nao inclui o proprio hash nem autentica a coleta. Inventario ao vivo; arquivos EVTX sao exportacoes da API do Event Log, nao copias byte a byte; leitura de arquivos pode atualizar last-access conforme a configuracao do Windows.\",\n"
             << "  \"source_errors\": [";
    bool firstSourceError = true;
    for (const CaseFileRecord& record : allFiles) {
        const bool acceptableStatus = record.status == "complete" ||
            record.status == "not_present" ||
            record.status.find("globally_sorted_by_utc_timestamp") == 0;
        if (acceptableStatus) continue;
        manifest << (firstSourceError ? "\n" : ",\n")
                 << "    {\"source\": " << jsonString(toUtf8(record.fileName.c_str()))
                 << ", \"status\": " << jsonString(record.status) << "}";
        firstSourceError = false;
    }
    if (!firstSourceError) manifest << '\n';
    manifest << "  ],\n"
             << "  \"files\": [\n";
    for (std::size_t index = 0; index < allFiles.size(); ++index) {
        const CaseFileRecord& record = allFiles[index];
        manifest << "    {\"name\": " << jsonString(toUtf8(record.fileName.c_str()))
                 << ", \"size_bytes\": " << record.sizeBytes
                 << ", \"records\": " << record.records
                 << ", \"status\": " << jsonString(record.status)
                 << ", \"sha256\": " << jsonString(record.sha256) << "}"
                 << (index + 1 == allFiles.size() ? "\n" : ",\n");
    }
    manifest << "  ]\n}\n";
    if (!writeNewFile(caseDirectory + L"\\manifest.json", manifest.str(), currentLength)) {
        errorMessage = "Relatorios gravados, mas falha ao gravar manifest.json (erro "
            + std::to_string(currentLength) + ").";
        return false;
    }
    if (!collectionComplete) {
        errorMessage = "Caso exportado com coleta parcial. Consulte status e erros em manifest.json.";
        return false;
    }
    if (!filesVerified) {
        errorMessage = "Caso exportado, mas a verificacao SHA-256 falhou em um ou mais arquivos.";
        return false;
    }
    return true;
}

bool readRegistryString(HKEY key, const char* name, std::string& value) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExA(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return false;
    }

    std::vector<char> buffer(static_cast<std::size_t>(size) + 1, '\0');
    if (RegQueryValueExA(key, name, nullptr, &type,
                         reinterpret_cast<LPBYTE>(&buffer[0]), &size) != ERROR_SUCCESS) {
        return false;
    }

    value.assign(&buffer[0]);
    return true;
}

std::string registryString(HKEY root, const char* path, const char* name, REGSAM view = 0) {
    HKEY key = nullptr;
    if (RegOpenKeyExA(root, path, 0, KEY_READ | view, &key) != ERROR_SUCCESS) {
        return "";
    }

    std::string value;
    readRegistryString(key, name, value);
    RegCloseKey(key);
    return value;
}

std::string adapterStatus(IF_OPER_STATUS status) {
    switch (status) {
    case IfOperStatusUp: return "conectado";
    case IfOperStatusDown: return "desconectado";
    case IfOperStatusTesting: return "em teste";
    case IfOperStatusUnknown: return "desconhecido";
    case IfOperStatusDormant: return "inativo";
    case IfOperStatusNotPresent: return "ausente";
    case IfOperStatusLowerLayerDown: return "camada inferior desconectada";
    default: return "outro";
    }
}

std::string socketAddress(const SOCKADDR* address) {
    if (address == nullptr) {
        return "desconhecido";
    }

    char buffer[INET6_ADDRSTRLEN] = {};
    if (address->sa_family == AF_INET) {
        const SOCKADDR_IN* ipv4 = reinterpret_cast<const SOCKADDR_IN*>(address);
        return InetNtopA(AF_INET, const_cast<IN_ADDR*>(&ipv4->sin_addr), buffer, sizeof(buffer))
            ? buffer : "IP indisponivel";
    }
    if (address->sa_family == AF_INET6) {
        const SOCKADDR_IN6* ipv6 = reinterpret_cast<const SOCKADDR_IN6*>(address);
        return InetNtopA(AF_INET6, const_cast<IN6_ADDR*>(&ipv6->sin6_addr), buffer, sizeof(buffer))
            ? buffer : "IP indisponivel";
    }
    return "familia de IP desconhecida";
}

std::string tcpState(DWORD state) {
    switch (state) {
    case MIB_TCP_STATE_CLOSED: return "fechada";
    case MIB_TCP_STATE_LISTEN: return "aguardando";
    case MIB_TCP_STATE_SYN_SENT: return "conectando";
    case MIB_TCP_STATE_SYN_RCVD: return "conexao recebida";
    case MIB_TCP_STATE_ESTAB: return "estabelecida";
    case MIB_TCP_STATE_FIN_WAIT1: return "encerrando";
    case MIB_TCP_STATE_FIN_WAIT2: return "encerrando";
    case MIB_TCP_STATE_CLOSE_WAIT: return "aguardando encerramento";
    case MIB_TCP_STATE_CLOSING: return "encerrando";
    case MIB_TCP_STATE_LAST_ACK: return "ultimo encerramento";
    case MIB_TCP_STATE_TIME_WAIT: return "aguardando liberacao";
    case MIB_TCP_STATE_DELETE_TCB: return "removendo";
    default: return "desconhecido";
    }
}

bool snapshotProcesses(ProcessMap& processes, DWORD& error) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }

    PROCESSENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    if (!Process32First(snapshot, &entry)) {
        error = GetLastError();
        CloseHandle(snapshot);
        return false;
    }

    do {
        processes.emplace(entry.th32ProcessID,
                          ProcessInfo{entry.szExeFile, entry.th32ParentProcessID});
    } while (Process32Next(snapshot, &entry));
    const DWORD iterationError = GetLastError();
    CloseHandle(snapshot);
    if (iterationError != ERROR_NO_MORE_FILES) {
        error = iterationError;
        processes.clear();
        return false;
    }
    return true;
}

bool snapshotTcpRemoteAddresses(NetworkAddressSet& addresses, DWORD& error) {
    addresses.clear();
    bool complete = true;
    const int families[] = {AF_INET, AF_INET6};
    for (int family : families) {
        DWORD size = 0;
        DWORD status = GetExtendedTcpTable(
            nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
        if (status == NO_ERROR && size == 0) continue;
        if (status != ERROR_INSUFFICIENT_BUFFER || size == 0) {
            error = status;
            complete = false;
            continue;
        }
        std::vector<char> buffer(size);
        status = GetExtendedTcpTable(&buffer[0], &size, FALSE, family,
                                     TCP_TABLE_OWNER_PID_ALL, 0);
        if (status != NO_ERROR) {
            error = status;
            complete = false;
            continue;
        }
        if (family == AF_INET) {
            const MIB_TCPTABLE_OWNER_PID* table =
                reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(&buffer[0]);
            for (DWORD index = 0; index < table->dwNumEntries; ++index) {
                const MIB_TCPROW_OWNER_PID& row = table->table[index];
                if (row.dwState == MIB_TCP_STATE_LISTEN || row.dwRemoteAddr == 0) continue;
                IN_ADDR remote = {};
                remote.S_un.S_addr = row.dwRemoteAddr;
                char text[INET_ADDRSTRLEN] = {};
                if (InetNtopA(AF_INET, &remote, text, sizeof(text)) != nullptr) {
                    addresses.insert(text);
                }
            }
        } else {
            const MIB_TCP6TABLE_OWNER_PID* table =
                reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(&buffer[0]);
            for (DWORD index = 0; index < table->dwNumEntries; ++index) {
                const MIB_TCP6ROW_OWNER_PID& row = table->table[index];
                if (row.dwState == MIB_TCP_STATE_LISTEN) continue;
                IN6_ADDR remote = {};
                memcpy(&remote, row.ucRemoteAddr, sizeof(remote));
                if (IN6_IS_ADDR_UNSPECIFIED(&remote)) continue;
                char text[INET6_ADDRSTRLEN] = {};
                if (InetNtopA(AF_INET6, &remote, text, sizeof(text)) != nullptr) {
                    addresses.insert(text);
                }
            }
        }
    }
    return complete;
}

std::string processLabel(DWORD pid, const ProcessMap& processes, bool available) {
    const auto process = processes.find(pid);
    if (process != processes.end()) {
        return process->second.name + " (PID " + std::to_string(pid) + ")";
    }
    if (!available) {
        return "PID " + std::to_string(pid) + " (nome indisponivel)";
    }
    return "PID " + std::to_string(pid) + " (processo nao encontrado)";
}

std::string reportSystem() {
    std::ostringstream out;
    char computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD nameSize = sizeof(computerName);
    if (GetComputerNameA(computerName, &nameSize)) {
        out << "Computador: " << computerName << '\n';
    }

    const std::string osName = registryString(
        HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        "ProductName", KEY_WOW64_64KEY);
    const std::string displayVersion = registryString(
        HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        "DisplayVersion", KEY_WOW64_64KEY);
    const std::string build = registryString(
        HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        "CurrentBuild", KEY_WOW64_64KEY);
    out << "Windows: " << (osName.empty() ? "informacao indisponivel" : osName);
    if (!displayVersion.empty()) out << " " << displayVersion;
    if (!build.empty()) out << " (build " << build << ")";
    out << '\n';

    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);
    SYSTEM_INFO nativeSystemInfo = {};
    GetNativeSystemInfo(&nativeSystemInfo);
    const char* architecture = "desconhecida";
    switch (nativeSystemInfo.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: architecture = "x64"; break;
    case PROCESSOR_ARCHITECTURE_ARM64: architecture = "ARM64"; break;
    case PROCESSOR_ARCHITECTURE_INTEL: architecture = "x86"; break;
    case PROCESSOR_ARCHITECTURE_IA64: architecture = "IA64"; break;
    }
    out << "Processadores logicos: " << systemInfo.dwNumberOfProcessors << '\n';
    out << "Arquitetura nativa: " << architecture << '\n';

    CaseMetadata timeZoneContext;
    collectTimeZoneContext(timeZoneContext);
    out << "Fuso horario: " << timeZoneDisplayName(timeZoneContext) << " ("
        << timeZoneOffsetDisplay(timeZoneContext) << "; "
        << timeZoneContext.timeZoneStatus << ")\n";

    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation = {};
        DWORD returned = 0;
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned)) {
            out << "Processo elevado: " << (elevation.TokenIsElevated ? "sim" : "nao") << '\n';
        } else {
            out << "Processo elevado: indisponivel (erro "
                << GetLastError() << ")\n";
        }
        CloseHandle(token);
    } else {
        out << "Processo elevado: indisponivel (erro " << GetLastError() << ")\n";
    }

    MEMORYSTATUSEX memory = {};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        out << "Memoria fisica: " << (memory.ullTotalPhys / (1024 * 1024)) << " MB total, "
            << (memory.ullAvailPhys / (1024 * 1024)) << " MB disponivel\n";
    }

    const ULONGLONG uptimeHours = GetTickCount64() / (1000ULL * 60 * 60);
    out << "Tempo ligado: " << (uptimeHours / 24) << " dias, "
        << (uptimeHours % 24) << " horas\n";

    const DWORD requiredDriveCharacters = GetLogicalDriveStringsW(0, nullptr);
    if (requiredDriveCharacters == 0) {
        out << "Volumes: falha ao enumerar unidades (erro " << GetLastError() << ")\n";
        return out.str();
    }
    std::vector<wchar_t> driveBuffer(
        static_cast<std::size_t>(requiredDriveCharacters) + 1, L'\0');
    const DWORD writtenDriveCharacters = GetLogicalDriveStringsW(
        static_cast<DWORD>(driveBuffer.size()), &driveBuffer[0]);
    if (writtenDriveCharacters == 0 || writtenDriveCharacters >= driveBuffer.size()) {
        out << "Volumes: falha ao enumerar unidades (erro " << GetLastError() << ")\n";
        return out.str();
    }

    out << "Volumes logicos (serial abaixo identifica o volume, nao o disco fisico):\n";
    bool foundDrive = false;
    for (const wchar_t* root = &driveBuffer[0]; *root != L'\0';
         root += wcslen(root) + 1) {
        foundDrive = true;
        const UINT driveType = GetDriveTypeW(root);
        const char* driveTypeName = "desconhecido";
        switch (driveType) {
        case DRIVE_UNKNOWN: driveTypeName = "desconhecido"; break;
        case DRIVE_NO_ROOT_DIR: driveTypeName = "sem raiz"; break;
        case DRIVE_REMOVABLE: driveTypeName = "removivel"; break;
        case DRIVE_FIXED: driveTypeName = "fixo"; break;
        case DRIVE_REMOTE: driveTypeName = "rede"; break;
        case DRIVE_CDROM: driveTypeName = "CD/DVD"; break;
        case DRIVE_RAMDISK: driveTypeName = "RAM disk"; break;
        }

        wchar_t volumeLabel[MAX_PATH] = {};
        wchar_t fileSystemName[MAX_PATH] = {};
        DWORD volumeSerial = 0;
        DWORD maximumComponentLength = 0;
        DWORD fileSystemFlags = 0;
        const BOOL volumeInformationAvailable = GetVolumeInformationW(
            root, volumeLabel, MAX_PATH, &volumeSerial, &maximumComponentLength,
            &fileSystemFlags, fileSystemName, MAX_PATH);

        out << "  " << toUtf8(root) << " (" << driveTypeName << ")";
        if (volumeInformationAvailable) {
            std::ostringstream serial;
            serial << std::uppercase << std::hex << std::setfill('0')
                   << std::setw(8) << volumeSerial;
            out << " | rotulo: "
                << (volumeLabel[0] == L'\0' ? "sem rotulo" : toUtf8(volumeLabel))
                << " | sistema de arquivos: "
                << (fileSystemName[0] == L'\0' ? "indisponivel" : toUtf8(fileSystemName))
                << " | serial do volume: " << serial.str()
                << " | max. componente: " << maximumComponentLength
                << " | flags: 0x" << std::uppercase << std::hex << fileSystemFlags
                << std::dec;
        } else {
            out << " | metadados do volume indisponiveis (erro "
                << GetLastError() << ")";
        }

        ULARGE_INTEGER available = {};
        ULARGE_INTEGER total = {};
        ULARGE_INTEGER free = {};
        if (GetDiskFreeSpaceExW(root, &available, &total, &free)) {
            out << " | " << (free.QuadPart / (1024ULL * 1024 * 1024))
                << " GB livres de " << (total.QuadPart / (1024ULL * 1024 * 1024))
                << " GB";
        } else {
            out << " | espaco disponivel indisponivel (erro " << GetLastError() << ")";
        }

        DWORD sectorsPerCluster = 0;
        DWORD bytesPerSector = 0;
        DWORD freeClusters = 0;
        DWORD totalClusters = 0;
        if (GetDiskFreeSpaceW(root, &sectorsPerCluster, &bytesPerSector,
                              &freeClusters, &totalClusters)) {
            const unsigned long long clusterBytes =
                static_cast<unsigned long long>(sectorsPerCluster) * bytesPerSector;
            out << " | cluster: " << clusterBytes << " bytes"
                << " | clusters livres: " << freeClusters
                << " de " << totalClusters;
        }
        out << '\n';
    }
    if (!foundDrive) out << "  Nenhum volume logico encontrado.\n";
    return out.str();
}

std::string reportNetworkAdapters() {
    std::ostringstream out;
    ULONG size = 0;
    ULONG status = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &size);
    if (status != ERROR_BUFFER_OVERFLOW) {
        out << "Falha ao consultar interfaces de rede (codigo " << status << ").\n";
        return out.str();
    }

    std::vector<char> buffer(size);
    IP_ADAPTER_ADDRESSES* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(&buffer[0]);
    status = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapter, &size);
    if (status != NO_ERROR) {
        out << "Falha ao consultar interfaces de rede (codigo " << status << ").\n";
        return out.str();
    }

    for (; adapter != nullptr; adapter = adapter->Next) {
        out << toUtf8(adapter->FriendlyName) << " [" << adapterStatus(adapter->OperStatus) << "]\n";
        out << "  Descricao: " << toUtf8(adapter->Description) << '\n';
        for (IP_ADAPTER_UNICAST_ADDRESS* address = adapter->FirstUnicastAddress;
             address != nullptr; address = address->Next) {
            out << "  IP: " << socketAddress(address->Address.lpSockaddr) << '\n';
        }
        for (IP_ADAPTER_GATEWAY_ADDRESS* gateway = adapter->FirstGatewayAddress;
             gateway != nullptr; gateway = gateway->Next) {
            out << "  Gateway: " << socketAddress(gateway->Address.lpSockaddr) << '\n';
        }
        for (IP_ADAPTER_DNS_SERVER_ADDRESS* dns = adapter->FirstDnsServerAddress;
             dns != nullptr; dns = dns->Next) {
            out << "  DNS: " << socketAddress(dns->Address.lpSockaddr) << '\n';
        }
    }
    return out.str();
}

std::string reportTcpConnections(const ProcessMap& processes, bool processListAvailable) {
    std::ostringstream out;
    DWORD size = 0;
    DWORD status = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (status == ERROR_INSUFFICIENT_BUFFER && size > 0) {
        std::vector<char> buffer(size);
        status = GetExtendedTcpTable(&buffer[0], &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        if (status == NO_ERROR) {
            const MIB_TCPTABLE_OWNER_PID* table =
                reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(&buffer[0]);
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_TCPROW_OWNER_PID& row = table->table[i];
                IN_ADDR local = {};
                IN_ADDR remote = {};
                local.S_un.S_addr = row.dwLocalAddr;
                remote.S_un.S_addr = row.dwRemoteAddr;
                char localIp[INET_ADDRSTRLEN] = {};
                char remoteIp[INET_ADDRSTRLEN] = {};
                InetNtopA(AF_INET, &local, localIp, sizeof(localIp));
                InetNtopA(AF_INET, &remote, remoteIp, sizeof(remoteIp));
                out << "TCP " << localIp << ':' << ntohs(static_cast<u_short>(row.dwLocalPort))
                    << " -> " << remoteIp << ':' << ntohs(static_cast<u_short>(row.dwRemotePort))
                    << " | " << tcpState(row.dwState) << " | "
                    << processLabel(row.dwOwningPid, processes, processListAvailable) << '\n';
            }
        } else {
            out << "Falha ao consultar conexoes TCP IPv4 (codigo " << status << ").\n";
        }
    } else if (status != NO_ERROR) {
        out << "Falha ao consultar conexoes TCP IPv4 (codigo " << status << ").\n";
    }

    size = 0;
    status = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    if (status == ERROR_INSUFFICIENT_BUFFER && size > 0) {
        std::vector<char> buffer(size);
        status = GetExtendedTcpTable(&buffer[0], &size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
        if (status == NO_ERROR) {
            const MIB_TCP6TABLE_OWNER_PID* table =
                reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(&buffer[0]);
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_TCP6ROW_OWNER_PID& row = table->table[i];
                char localIp[INET6_ADDRSTRLEN] = {};
                char remoteIp[INET6_ADDRSTRLEN] = {};
                InetNtopA(AF_INET6, const_cast<BYTE*>(row.ucLocalAddr), localIp, sizeof(localIp));
                InetNtopA(AF_INET6, const_cast<BYTE*>(row.ucRemoteAddr), remoteIp, sizeof(remoteIp));
                out << "TCP6 [" << localIp << "]:" << ntohs(static_cast<u_short>(row.dwLocalPort))
                    << " -> [" << remoteIp << "]:"
                    << ntohs(static_cast<u_short>(row.dwRemotePort))
                    << " | " << tcpState(row.dwState) << " | "
                    << processLabel(row.dwOwningPid, processes, processListAvailable) << '\n';
            }
        } else {
            out << "Falha ao consultar conexoes TCP IPv6 (codigo " << status << ").\n";
        }
    } else if (status != NO_ERROR) {
        out << "Falha ao consultar conexoes TCP IPv6 (codigo " << status << ").\n";
    }

    if (out.str().empty()) out << "Nenhuma conexao TCP encontrada.\n";
    return out.str();
}

std::string reportUdpEndpoints(const ProcessMap& processes, bool processListAvailable) {
    std::ostringstream out;
    DWORD size = 0;
    DWORD status = GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (status == ERROR_INSUFFICIENT_BUFFER && size > 0) {
        std::vector<char> buffer(size);
        status = GetExtendedUdpTable(&buffer[0], &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
        if (status == NO_ERROR) {
            const MIB_UDPTABLE_OWNER_PID* table =
                reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(&buffer[0]);
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_UDPROW_OWNER_PID& row = table->table[i];
                IN_ADDR local = {};
                local.S_un.S_addr = row.dwLocalAddr;
                char localIp[INET_ADDRSTRLEN] = {};
                InetNtopA(AF_INET, &local, localIp, sizeof(localIp));
                out << "UDP " << localIp << ':' << ntohs(static_cast<u_short>(row.dwLocalPort))
                    << " | " << processLabel(row.dwOwningPid, processes, processListAvailable) << '\n';
            }
        } else {
            out << "Falha ao consultar portas UDP IPv4 (codigo " << status << ").\n";
        }
    } else if (status != NO_ERROR) {
        out << "Falha ao consultar portas UDP IPv4 (codigo " << status << ").\n";
    }

    size = 0;
    status = GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    if (status == ERROR_INSUFFICIENT_BUFFER && size > 0) {
        std::vector<char> buffer(size);
        status = GetExtendedUdpTable(&buffer[0], &size, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
        if (status == NO_ERROR) {
            const MIB_UDP6TABLE_OWNER_PID* table =
                reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(&buffer[0]);
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_UDP6ROW_OWNER_PID& row = table->table[i];
                char localIp[INET6_ADDRSTRLEN] = {};
                InetNtopA(AF_INET6, const_cast<BYTE*>(row.ucLocalAddr), localIp, sizeof(localIp));
                out << "UDP6 [" << localIp << "]:"
                    << ntohs(static_cast<u_short>(row.dwLocalPort))
                    << " | " << processLabel(row.dwOwningPid, processes, processListAvailable) << '\n';
            }
        } else {
            out << "Falha ao consultar portas UDP IPv6 (codigo " << status << ").\n";
        }
    } else if (status != NO_ERROR) {
        out << "Falha ao consultar portas UDP IPv6 (codigo " << status << ").\n";
    }

    if (out.str().empty()) out << "Nenhuma porta UDP encontrada.\n";
    return out.str();
}

std::string reportProcesses(const ProcessMap& processes, bool processListAvailable,
                            DWORD snapshotError) {
    std::ostringstream out;
    if (!processListAvailable) {
        out << "Falha ao listar processos (codigo " << snapshotError << ").\n";
        return out.str();
    }

    for (const auto& entry : processes) {
        out << entry.second.name << " | PID " << entry.first;
        const auto parent = processes.find(entry.second.parentPid);
        if (parent != processes.end()) {
            out << " | pai " << parent->second.name << " (PID " << entry.second.parentPid << ")";
        } else {
            out << " | PID do pai " << entry.second.parentPid;
        }

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.first);
        if (process == nullptr) {
            out << " | caminho indisponivel (erro " << GetLastError() << ")";
        } else {
            std::vector<char> imagePath(32768, '\0');
            DWORD pathLength = static_cast<DWORD>(imagePath.size());
            if (QueryFullProcessImageNameA(process, 0, &imagePath[0], &pathLength)) {
                out << " | caminho " << std::string(&imagePath[0], pathLength);
            } else {
                out << " | caminho indisponivel (erro " << GetLastError() << ")";
            }
            CloseHandle(process);
        }
        out << '\n';
    }
    return out.str();
}

void appendInstalledSoftware(std::ostringstream& out, HKEY root, const char* rootName,
                            REGSAM view) {
    const char* path = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    HKEY key = nullptr;
    if (RegOpenKeyExA(root, path, 0, KEY_READ | view, &key) != ERROR_SUCCESS) {
        return;
    }

    for (DWORD index = 0;; ++index) {
        char subkeyName[512] = {};
        DWORD nameSize = sizeof(subkeyName);
        const LONG result = RegEnumKeyExA(key, index, subkeyName, &nameSize, nullptr,
                                          nullptr, nullptr, nullptr);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS) continue;

        HKEY appKey = nullptr;
        if (RegOpenKeyExA(key, subkeyName, 0, KEY_READ | view, &appKey) != ERROR_SUCCESS) {
            continue;
        }

        std::string name;
        std::string version;
        std::string publisher;
        readRegistryString(appKey, "DisplayName", name);
        readRegistryString(appKey, "DisplayVersion", version);
        readRegistryString(appKey, "Publisher", publisher);
        if (!name.empty()) {
            out << name << " | versao " << (version.empty() ? "nao informada" : version)
                << " | fabricante " << (publisher.empty() ? "nao informado" : publisher)
                << " | " << rootName << '\n';
        }
        RegCloseKey(appKey);
    }
    RegCloseKey(key);
}

std::string reportSoftware() {
    std::ostringstream out;
    appendInstalledSoftware(out, HKEY_LOCAL_MACHINE, "computador", KEY_WOW64_64KEY);
    appendInstalledSoftware(out, HKEY_LOCAL_MACHINE, "computador 32-bit", KEY_WOW64_32KEY);
    appendInstalledSoftware(out, HKEY_CURRENT_USER, "usuario", KEY_WOW64_64KEY);
    appendInstalledSoftware(out, HKEY_CURRENT_USER, "usuario 32-bit", KEY_WOW64_32KEY);
    if (out.str().empty()) out << "Nenhum software listado ou acesso indisponivel.\n";
    return out.str();
}

void appendStartupEntries(std::ostringstream& out, HKEY root, const char* rootName,
                          const char* runKey, REGSAM view) {
    HKEY key = nullptr;
    if (RegOpenKeyExA(root, runKey, 0, KEY_READ | view, &key) != ERROR_SUCCESS) {
        return;
    }

    for (DWORD index = 0;; ++index) {
        char valueName[512] = {};
        DWORD nameSize = sizeof(valueName);
        const LONG result = RegEnumValueA(key, index, valueName, &nameSize, nullptr,
                                          nullptr, nullptr, nullptr);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS) continue;

        std::string command;
        if (readRegistryString(key, valueName, command)) {
            out << valueName << " | " << command << " | " << rootName
                << " | " << (std::string(runKey).find("RunOnce") != std::string::npos
                                  ? "executa uma vez" : "inicializacao") << '\n';
        }
    }
    RegCloseKey(key);
}

std::string reportStartup() {
    std::ostringstream out;
    const char* run = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    const char* runOnce = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    appendStartupEntries(out, HKEY_LOCAL_MACHINE, "computador", run, KEY_WOW64_64KEY);
    appendStartupEntries(out, HKEY_LOCAL_MACHINE, "computador 32-bit", run, KEY_WOW64_32KEY);
    appendStartupEntries(out, HKEY_CURRENT_USER, "usuario", run, KEY_WOW64_64KEY);
    appendStartupEntries(out, HKEY_CURRENT_USER, "usuario 32-bit", run, KEY_WOW64_32KEY);
    appendStartupEntries(out, HKEY_LOCAL_MACHINE, "computador", runOnce, KEY_WOW64_64KEY);
    appendStartupEntries(out, HKEY_LOCAL_MACHINE, "computador 32-bit", runOnce, KEY_WOW64_32KEY);
    appendStartupEntries(out, HKEY_CURRENT_USER, "usuario", runOnce, KEY_WOW64_64KEY);
    appendStartupEntries(out, HKEY_CURRENT_USER, "usuario 32-bit", runOnce, KEY_WOW64_32KEY);
    if (out.str().empty()) out << "Nenhuma entrada encontrada ou acesso indisponivel.\n";
    return out.str();
}

std::wstring xmlElementText(const std::wstring& xml, const std::wstring& element) {
    const std::wstring opening = L"<" + element;
    const std::size_t start = xml.find(opening);
    if (start == std::wstring::npos) return L"";
    const std::size_t contentStart = xml.find(L'>', start);
    if (contentStart == std::wstring::npos) return L"";
    const std::size_t end = xml.find(L"</" + element + L">", contentStart + 1);
    if (end == std::wstring::npos) return L"";
    return xml.substr(contentStart + 1, end - contentStart - 1);
}

std::wstring xmlAttribute(const std::wstring& tag, const std::wstring& attribute) {
    const std::wstring prefix = attribute + L"=";
    const std::size_t start = tag.find(prefix);
    if (start == std::wstring::npos) return L"";
    const std::size_t quoteStart = start + prefix.size();
    if (quoteStart >= tag.size() || (tag[quoteStart] != L'"' && tag[quoteStart] != L'\'')) {
        return L"";
    }
    const wchar_t quote = tag[quoteStart];
    const std::size_t valueStart = quoteStart + 1;
    const std::size_t end = tag.find(quote, valueStart);
    return end == std::wstring::npos ? L"" : tag.substr(valueStart, end - valueStart);
}

std::wstring xmlElementTag(const std::wstring& xml, const std::wstring& name) {
    const std::wstring opening = L"<" + name;
    std::size_t start = 0;
    while ((start = xml.find(opening, start)) != std::wstring::npos) {
        const std::size_t boundary = start + opening.size();
        if (boundary < xml.size() &&
            (xml[boundary] == L' ' || xml[boundary] == L'\t' ||
             xml[boundary] == L'\r' || xml[boundary] == L'\n' ||
             xml[boundary] == L'>' || xml[boundary] == L'/')) {
            const std::size_t end = xml.find(L'>', boundary);
            return end == std::wstring::npos ? L"" : xml.substr(start, end - start + 1);
        }
        start = boundary;
    }
    return L"";
}

std::string eventTimelineSortKey(const std::wstring& xml) {
    const std::wstring timeTag = xmlElementTag(xml, L"TimeCreated");
    const std::string timestamp = toUtf8(xmlAttribute(timeTag, L"SystemTime").c_str());
    const std::string invalidKey = "1" + timestamp + toUtf8(
        xmlElementText(xml, L"EventRecordID").c_str());
    if (timestamp.size() < 20 || timestamp[4] != '-' || timestamp[7] != '-' ||
        timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':') {
        return invalidKey;
    }
    const std::size_t fractionStart = timestamp[19] == '.' ? 20 : timestamp.size();
    const std::size_t zonePosition = timestamp.find('Z', fractionStart);
    if (zonePosition == std::string::npos || zonePosition + 1 != timestamp.size()) {
        return invalidKey;
    }
    for (std::size_t i = 0; i < 19; ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
        if (timestamp[i] < '0' || timestamp[i] > '9') return invalidKey;
    }

    std::string fraction;
    if (fractionStart != timestamp.size()) {
        fraction = timestamp.substr(fractionStart, zonePosition - fractionStart);
        if (fraction.empty()) return invalidKey;
        for (char digit : fraction) {
            if (digit < '0' || digit > '9') return invalidKey;
        }
        if (fraction.size() > 7) fraction.resize(7);
    }
    fraction.append(7 - fraction.size(), '0');
    return "0" + timestamp.substr(0, 19) + "." + fraction;
}

bool parseEventProcessId(const std::wstring& value, DWORD& pid) {
    const wchar_t* start = value.c_str();
    int base = 10;
    if (value.size() > 2 && value[0] == L'0' &&
        (value[1] == L'x' || value[1] == L'X')) {
        start += 2;
        base = 16;
    }
    if (*start == L'\0') return false;
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(start, &end, base);
    if (end == start || *end != L'\0' || parsed > MAXDWORD) return false;
    pid = static_cast<DWORD>(parsed);
    return true;
}

std::string normalizeNetworkAddress(std::string address) {
    std::transform(address.begin(), address.end(), address.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    const std::string mappedPrefix = "::ffff:";
    if (address.compare(0, mappedPrefix.size(), mappedPrefix) == 0) {
        address.erase(0, mappedPrefix.size());
    }
    return address;
}

std::string eventTimelineJson(const std::wstring& xml, const std::wstring& channel,
                              const ProcessMap& processes, bool processSnapshotAvailable,
                              const NetworkAddressSet& remoteAddresses,
                              bool networkSnapshotAvailable,
                              const std::string& processSnapshotAtUtc,
                              const std::string& networkSnapshotAtUtc) {
    const std::wstring system = xmlElementText(xml, L"System");
    const std::wstring timeTag = xmlElementTag(xml, L"TimeCreated");
    const std::wstring providerTag = xmlElementTag(xml, L"Provider");
    const std::wstring executionTag = xmlElementTag(xml, L"Execution");
    const std::wstring securityTag = xmlElementTag(xml, L"Security");
    const std::wstring correlationTag = xmlElementTag(xml, L"Correlation");
    const std::vector<std::pair<std::wstring, std::wstring>> fields = xmlDataFields(xml);
    const std::set<std::wstring> processFieldNames = {
        L"ProcessId", L"ProcessID", L"NewProcessId", L"SourceProcessId",
        L"TargetProcessId", L"CallerProcessId", L"ClientProcessId"
    };
    const std::set<std::wstring> addressFieldNames = {
        L"IpAddress", L"SourceAddress", L"RemoteAddress", L"ClientAddress",
        L"SourceIp", L"SourceIP", L"ClientAddressIp"
    };

    std::ostringstream record;
    record << "{\"timestamp_utc\": " << jsonString(toUtf8(xmlAttribute(timeTag, L"SystemTime").c_str()))
           << ", \"channel\": " << jsonString(toUtf8(channel.c_str()))
           << ", \"event_id\": " << jsonString(toUtf8(xmlElementText(xml, L"EventID").c_str()))
           << ", \"provider\": " << jsonString(toUtf8(xmlAttribute(providerTag, L"Name").c_str()))
           << ", \"computer\": " << jsonString(toUtf8(xmlElementText(system, L"Computer").c_str()))
           << ", \"record_id\": " << jsonString(toUtf8(xmlElementText(xml, L"EventRecordID").c_str()))
           << ", \"event_user_sid\": "
           << jsonString(toUtf8(xmlAttribute(securityTag, L"UserID").c_str()))
           << ", \"provider_process_id\": "
           << jsonString(toUtf8(xmlAttribute(executionTag, L"ProcessID").c_str()))
           << ", \"provider_thread_id\": "
           << jsonString(toUtf8(xmlAttribute(executionTag, L"ThreadID").c_str()))
           << ", \"activity_id\": " << jsonString(toUtf8(xmlAttribute(correlationTag, L"ActivityID").c_str()))
           << ", \"event_data\": [";
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) record << ", ";
        record << "{\"name\": " << jsonString(toUtf8(fields[index].first.c_str()))
               << ", \"value\": " << jsonString(toUtf8(fields[index].second.c_str())) << "}";
    }
    record << "], \"correlations\": {"
           << "\"historical_relationship_confirmed\": false"
           << ", \"process_snapshot_at_utc\": " << jsonString(processSnapshotAtUtc)
           << ", \"process_snapshot_available\": "
           << (processSnapshotAvailable ? "true" : "false")
           << ", \"process_candidates\": [";
    bool firstCandidate = true;
    for (const auto& field : fields) {
        if (processFieldNames.find(field.first) == processFieldNames.end()) continue;
        DWORD pid = 0;
        if (!parseEventProcessId(field.second, pid)) continue;
        if (!firstCandidate) record << ", ";
        firstCandidate = false;
        const auto process = processes.find(pid);
        record << "{\"event_field\": " << jsonString(toUtf8(field.first.c_str()))
               << ", \"pid\": " << pid
               << ", \"matched_in_live_snapshot\": "
               << (processSnapshotAvailable && process != processes.end() ? "true" : "false")
               << ", \"process_name\": "
               << jsonString(processSnapshotAvailable && process != processes.end()
                    ? process->second.name : "")
               << ", \"parent_pid\": ";
        if (processSnapshotAvailable && process != processes.end()) {
            record << process->second.parentPid;
        } else {
            record << "null";
        }
        record << "}";
    }
    record << "], \"network_snapshot_at_utc\": " << jsonString(networkSnapshotAtUtc)
           << ", \"network_snapshot_available\": "
           << (networkSnapshotAvailable ? "true" : "false")
           << ", \"active_tcp_remote_address_candidates\": [";
    firstCandidate = true;
    for (const auto& field : fields) {
        if (addressFieldNames.find(field.first) == addressFieldNames.end()) continue;
        const std::string address = normalizeNetworkAddress(toUtf8(field.second.c_str()));
        if (address.empty() || address == "-" || address == "0.0.0.0") continue;
        if (!firstCandidate) record << ", ";
        firstCandidate = false;
        record << "{\"event_field\": " << jsonString(toUtf8(field.first.c_str()))
               << ", \"address\": " << jsonString(address)
               << ", \"matched_in_live_tcp_snapshot\": "
               << (networkSnapshotAvailable &&
                   remoteAddresses.find(address) != remoteAddresses.end() ? "true" : "false")
               << "}";
    }
    record << "], \"interpretation\": "
           << jsonString("Current snapshots are candidate matches only, not proof of historical linkage.")
           << "}}";
    return record.str();
}

std::wstring decodeXmlText(std::wstring text) {
    const std::pair<const wchar_t*, const wchar_t*> entities[] = {
        {L"&amp;", L"&"}, {L"&lt;", L"<"}, {L"&gt;", L">"},
        {L"&quot;", L"\""}, {L"&apos;", L"'"}
    };
    for (const auto& entity : entities) {
        std::size_t position = 0;
        const std::wstring encoded = entity.first;
        const std::wstring decoded = entity.second;
        while ((position = text.find(encoded, position)) != std::wstring::npos) {
            text.replace(position, encoded.size(), decoded);
            position += decoded.size();
        }
    }
    return text;
}

std::vector<std::pair<std::wstring, std::wstring>> xmlDataFields(const std::wstring& xml) {
    std::vector<std::pair<std::wstring, std::wstring>> fields;
    std::size_t position = 0;
    while ((position = xml.find(L"<Data ", position)) != std::wstring::npos) {
        const std::size_t tagEnd = xml.find(L'>', position);
        if (tagEnd == std::wstring::npos) break;
        const std::wstring tag = xml.substr(position, tagEnd - position + 1);
        const std::size_t end = xml.find(L"</Data>", tagEnd + 1);
        if (end == std::wstring::npos) break;
        const std::wstring name = xmlAttribute(tag, L"Name");
        const std::wstring value = decodeXmlText(xml.substr(tagEnd + 1, end - tagEnd - 1));
        if (!name.empty() && !value.empty() && value != L"-") {
            fields.emplace_back(name, value);
        }
        position = end + 7;
    }
    return fields;
}

const wchar_t* securityEventName(const std::wstring& id) {
    if (id == L"4624") return L"logon bem-sucedido";
    if (id == L"4625") return L"falha de logon";
    if (id == L"4634") return L"sessao encerrada";
    if (id == L"4647") return L"logoff iniciado pelo usuario";
    if (id == L"4672") return L"logon com privilegios especiais";
    if (id == L"4697") return L"servico instalado";
    if (id == L"4698") return L"tarefa agendada criada";
    if (id == L"4702") return L"tarefa agendada atualizada";
    if (id == L"4719") return L"politica de auditoria alterada";
    if (id == L"4720") return L"conta de usuario criada";
    if (id == L"4722") return L"conta de usuario habilitada";
    if (id == L"4723" || id == L"4724") return L"senha de conta alterada ou redefinida";
    if (id == L"4725") return L"conta de usuario desabilitada";
    if (id == L"4726") return L"conta de usuario excluida";
    if (id == L"4732") return L"membro adicionado a grupo local";
    if (id == L"4733") return L"membro removido de grupo local";
    if (id == L"1102") return L"log de auditoria apagado";
    return L"evento de seguranca";
}

std::string reportSecurityEvents() {
    std::ostringstream out;
    const wchar_t* query =
        L"*[System[(EventID=4624 or EventID=4625 or EventID=4634 or EventID=4647 or "
        L"EventID=4672 or EventID=4697 or EventID=4698 or EventID=4702 or EventID=4719 or "
        L"EventID=4720 or EventID=4722 or EventID=4723 or EventID=4724 or EventID=4725 or "
        L"EventID=4726 or EventID=4732 or EventID=4733 or EventID=1102)]]";
    EVT_HANDLE queryHandle = EvtQuery(nullptr, L"Security", query,
                                      EvtQueryChannelPath | EvtQueryReverseDirection);
    if (queryHandle == nullptr) {
        out << "Nao foi possivel consultar o log Security (erro " << GetLastError()
            << "). Execute como administrador e confirme que a auditoria esta habilitada.\n";
        return out.str();
    }

    out << "Previa de eventos selecionados do log Security (ate 200; horario UTC).\n"
        << "Ao salvar o caso, logs de seguranca, sistema, PowerShell, Sysmon, tarefas, WMI, "
        << "RDP, WinRM, DNS, perfil de rede e firewall sao exportados quando disponiveis, "
        << "sem limite em EVTX e XML renderizado.\n";
    unsigned int count = 0;
    while (count < 200) {
        EVT_HANDLE events[16] = {};
        DWORD returned = 0;
        const DWORD requested = (200 - count < 16) ? (200 - count) : 16;
        if (!EvtNext(queryHandle, requested, events, 0, 0, &returned)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) {
                out << "Falha ao ler mais eventos (erro " << error << ").\n";
            }
            break;
        }

        for (DWORD i = 0; i < returned; ++i) {
            DWORD bufferUsed = 0;
            DWORD propertyCount = 0;
            const BOOL sizeQuery = EvtRender(nullptr, events[i], EvtRenderEventXml, 0, nullptr,
                                             &bufferUsed, &propertyCount);
            const DWORD sizeQueryError = sizeQuery ? ERROR_SUCCESS : GetLastError();
            if (sizeQuery || sizeQueryError != ERROR_INSUFFICIENT_BUFFER || bufferUsed == 0) {
                out << "Nao foi possivel renderizar um evento (erro " << sizeQueryError << ").\n";
                EvtClose(events[i]);
                continue;
            }

            std::vector<wchar_t> xmlBuffer(
                (bufferUsed + sizeof(wchar_t) - 1) / sizeof(wchar_t), L'\0');
            if (!EvtRender(nullptr, events[i], EvtRenderEventXml,
                           static_cast<DWORD>(xmlBuffer.size() * sizeof(wchar_t)),
                           &xmlBuffer[0], &bufferUsed, &propertyCount)) {
                out << "Nao foi possivel renderizar um evento (erro " << GetLastError() << ").\n";
                EvtClose(events[i]);
                continue;
            }
            EvtClose(events[i]);

            const std::wstring xml(&xmlBuffer[0]);
            const std::wstring id = xmlElementText(xml, L"EventID");
            const std::wstring system = xmlElementText(xml, L"System");
            const std::wstring time = xmlAttribute(system, L"SystemTime");
            const std::wstring recordId = xmlElementText(xml, L"EventRecordID");
            out << "\n[" << toUtf8(id.c_str()) << "] "
                << toUtf8(securityEventName(id)) << " | "
                << (time.empty() ? "horario indisponivel" : toUtf8(time.c_str()))
                << " | registro " << (recordId.empty() ? "?" : toUtf8(recordId.c_str()));

            for (const auto& field : xmlDataFields(xml)) {
                out << " | " << toUtf8(field.first.c_str()) << ": "
                    << toUtf8(field.second.c_str());
            }
            out << '\n';
            ++count;
        }
    }

    if (count == 0) {
        out << "\nNenhum evento correspondente foi encontrado. O log pode estar vazio, "
            << "a auditoria pode estar desabilitada ou o acesso pode exigir administrador.\n";
    } else if (count == 200) {
        out << "\nLimite de 200 eventos atingido; os mais antigos nao foram incluidos.\n";
    }
    EvtClose(queryHandle);
    return out.str();
}

bool readYesNo(const std::string& prompt, bool& value) {
    while (true) {
        std::cout << prompt << " (s/N): ";
        std::string input;
        if (!std::getline(std::cin, input)) return false;
        if (input.empty() || input == "n" || input == "N" ||
            input == "nao" || input == "não") {
            value = false;
            return true;
        }
        if (input == "s" || input == "S" || input == "sim" ||
            input == "SIM") {
            value = true;
            return true;
        }
        std::cout << "Digite s ou n.\n";
    }
}

struct DirectFileCandidate {
    std::wstring name;
    std::wstring path;
    unsigned long long sizeBytes = 0;
};

std::string quarantineCaseName() {
    SYSTEMTIME now = {};
    GetSystemTime(&now);
    std::ostringstream name;
    name << "case_" << std::setfill('0') << std::setw(4) << now.wYear
         << std::setw(2) << now.wMonth << std::setw(2) << now.wDay << '_'
         << std::setw(2) << now.wHour << std::setw(2) << now.wMinute
         << std::setw(2) << now.wSecond << '_' << std::setw(3)
         << now.wMilliseconds << 'Z';
    return name.str();
}

bool pathHasComponent(const std::wstring& path, const std::wstring& component) {
    for (std::size_t position = 0; position + component.size() <= path.size(); ++position) {
        if (_wcsnicmp(path.c_str() + position, component.c_str(), component.size()) != 0) {
            continue;
        }
        const bool startsComponent = position == 0 || path[position - 1] == L'\\';
        const std::size_t end = position + component.size();
        const bool endsComponent = end == path.size() || path[end] == L'\\';
        if (startsComponent && endsComponent) return true;
    }
    return false;
}

std::wstring joinPath(const std::wstring& directory, const std::wstring& name) {
    if (!directory.empty() && directory.back() == L'\\') return directory + name;
    return directory + L"\\" + name;
}

bool isDirectoryWithoutReparsePoint(const std::wstring& path, DWORD& error) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        error = GetLastError();
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        error = ERROR_INVALID_DATA;
        return false;
    }
    return true;
}

bool listDirectFiles(const std::wstring& directory,
                     std::vector<DirectFileCandidate>& files, DWORD& error) {
    files.clear();
    error = ERROR_SUCCESS;
    const std::wstring normalizedDirectory = fullPath(directory);
    if (normalizedDirectory.empty() ||
        !isDirectoryWithoutReparsePoint(normalizedDirectory, error)) {
        if (error == ERROR_SUCCESS) error = ERROR_INVALID_NAME;
        return false;
    }

    WIN32_FIND_DATAW entry = {};
    const std::wstring searchPath = joinPath(normalizedDirectory, L"*");
    HANDLE search = FindFirstFileW(searchPath.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }

    do {
        if (wcscmp(entry.cFileName, L".") == 0 ||
            wcscmp(entry.cFileName, L"..") == 0 ||
            wcscmp(entry.cFileName, L"quarantine_audit.jsonl") == 0 ||
            (entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                       FILE_ATTRIBUTE_REPARSE_POINT |
                                       FILE_ATTRIBUTE_HIDDEN |
                                       FILE_ATTRIBUTE_SYSTEM)) != 0) {
            continue;
        }
        DirectFileCandidate candidate;
        candidate.name = entry.cFileName;
        candidate.path = joinPath(normalizedDirectory, candidate.name);
        candidate.sizeBytes =
            (static_cast<unsigned long long>(entry.nFileSizeHigh) << 32) |
            entry.nFileSizeLow;
        files.push_back(candidate);
    } while (FindNextFileW(search, &entry));

    const DWORD iterationError = GetLastError();
    FindClose(search);
    if (iterationError != ERROR_NO_MORE_FILES) {
        error = iterationError;
        return false;
    }
    std::sort(files.begin(), files.end(),
        [](const DirectFileCandidate& left, const DirectFileCandidate& right) {
            return _wcsicmp(left.name.c_str(), right.name.c_str()) < 0;
        });
    return true;
}

bool parseFileSelection(const std::string& input, std::size_t fileCount,
                        std::vector<std::size_t>& selected) {
    selected.clear();
    std::string normalizedInput = input;
    std::transform(normalizedInput.begin(), normalizedInput.end(),
                   normalizedInput.begin(), [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (normalizedInput == "all" || normalizedInput == "todos") {
        for (std::size_t index = 0; index < fileCount; ++index) {
            selected.push_back(index);
        }
        return !selected.empty();
    }

    std::replace(normalizedInput.begin(), normalizedInput.end(), ',', ' ');
    std::istringstream parser(normalizedInput);
    std::string token;
    while (parser >> token) {
        std::istringstream numberParser(token);
        std::size_t number = 0;
        char extra = '\0';
        if (!(numberParser >> number) || (numberParser >> extra) ||
            number == 0 || number > fileCount) {
            selected.clear();
            return false;
        }
        const std::size_t index = number - 1;
        if (std::find(selected.begin(), selected.end(), index) == selected.end()) {
            selected.push_back(index);
        }
    }
    return !selected.empty();
}

bool promptForFileSelection(const std::vector<DirectFileCandidate>& files,
                            std::vector<std::size_t>& selected) {
    for (std::size_t index = 0; index < files.size(); ++index) {
        std::cout << "  " << (index + 1) << ". " << toUtf8(files[index].name.c_str())
                  << " (" << files[index].sizeBytes << " bytes)\n";
    }
    while (true) {
        std::cout << "Numeros separados por virgula, ou 'todos': ";
        std::string input;
        if (!std::getline(std::cin, input)) return false;
        if (parseFileSelection(input, files.size(), selected)) return true;
        std::cout << "Selecao invalida ou vazia; informe numeros entre 1 e "
                  << files.size() << ".\n";
    }
}

bool confirmExactOperation(const std::string& operationName) {
    std::cout << "Para confirmar, digite " << operationName << ": ";
    std::string confirmation;
    return std::getline(std::cin, confirmation) && confirmation == operationName;
}

bool appendQuarantineAudit(HANDLE audit, const std::string& action,
                           const std::wstring& fileName,
                           const std::wstring& sourcePath,
                           const std::wstring& currentPath,
                           const std::string& digest,
                           const std::string& status, DWORD& error) {
    std::ostringstream record;
    record << "{\"timestamp_utc\": " << jsonString(currentUtcTimestamp())
           << ", \"action\": " << jsonString(action)
           << ", \"quarantined_name\": " << jsonString(toUtf8(fileName.c_str()))
           << ", \"source_path\": " << jsonString(toUtf8(sourcePath.c_str()))
           << ", \"current_path\": " << jsonString(toUtf8(currentPath.c_str()))
           << ", \"sha256\": " << jsonString(digest)
           << ", \"status\": " << jsonString(status) << "}";
    if (!writeJsonLineToOpenFile(audit, record.str(), error)) return false;
    if (!FlushFileBuffers(audit)) {
        error = GetLastError();
        return false;
    }
    return true;
}

bool isValidQuarantineDirectory(const std::wstring& directory,
                                std::wstring& normalizedDirectory,
                                std::wstring& auditPath, DWORD& error) {
    normalizedDirectory = fullPath(directory);
    if (normalizedDirectory.empty() ||
        !isDirectoryWithoutReparsePoint(normalizedDirectory, error)) {
        if (error == ERROR_SUCCESS) error = ERROR_INVALID_NAME;
        return false;
    }
    const std::size_t separator = normalizedDirectory.find_last_of(L'\\');
    const std::wstring parent = separator == std::wstring::npos
        ? L"" : normalizedDirectory.substr(0, separator);
    const std::size_t parentSeparator = parent.find_last_of(L'\\');
    const std::wstring parentName = parentSeparator == std::wstring::npos
        ? parent : parent.substr(parentSeparator + 1);
    if (_wcsicmp(parentName.c_str(), L"PC_Local_Quarantine") != 0) {
        error = ERROR_INVALID_DATA;
        return false;
    }
    if (!isDirectoryWithoutReparsePoint(parent, error)) return false;

    auditPath = joinPath(normalizedDirectory, L"quarantine_audit.jsonl");
    const DWORD auditAttributes = GetFileAttributesW(auditPath.c_str());
    if (auditAttributes == INVALID_FILE_ATTRIBUTES ||
        (auditAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        error = auditAttributes == INVALID_FILE_ATTRIBUTES
            ? GetLastError() : ERROR_INVALID_DATA;
        return false;
    }
    return true;
}

bool loadVerifiedQuarantineHashes(const std::wstring& auditPath,
                                  std::map<std::string, std::string>& hashes,
                                  DWORD& error) {
    hashes.clear();
    HANDLE audit = CreateFileW(auditPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (audit == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }

    bool headerValid = false;
    std::string pending;
    char buffer[8192];
    bool succeeded = true;
    auto processLine = [&](const std::string& line) {
        if (!headerValid) {
            headerValid = line.find("\"format\": \"PC_LOCAL_QUARANTINE_V1\"")
                != std::string::npos;
            return;
        }
        if (line.find("\"action\": \"quarantine\"") == std::string::npos ||
            (line.find("\"status\": \"quarantined_verified\"") == std::string::npos &&
             line.find("\"status\": \"move_intent\"") == std::string::npos)) {
            return;
        }
        const std::string namePrefix = "\"quarantined_name\": ";
        const std::size_t nameStart = line.find(namePrefix);
        const std::string hashPrefix = "\"sha256\": \"";
        if (nameStart == std::string::npos) return;

        const std::size_t nameValueStart = nameStart + namePrefix.size();
        if (nameValueStart >= line.size() || line[nameValueStart] != '"') return;
        std::size_t nameValueEnd = nameValueStart + 1;
        bool escaped = false;
        for (; nameValueEnd < line.size(); ++nameValueEnd) {
            if (escaped) {
                escaped = false;
            } else if (line[nameValueEnd] == '\\') {
                escaped = true;
            } else if (line[nameValueEnd] == '"') {
                break;
            }
        }
        if (nameValueEnd >= line.size()) return;
        const std::size_t hashStart = line.find(hashPrefix, nameValueEnd + 1);
        if (hashStart == std::string::npos) return;
        const std::size_t hashValueStart = hashStart + hashPrefix.size();
        if (hashValueStart + 64 >= line.size()) return;
        const std::string hash = line.substr(hashValueStart, 64);
        if (line[hashValueStart + 64] != '"') return;
        for (char character : hash) {
            if (!std::isxdigit(static_cast<unsigned char>(character))) return;
        }
        const std::string nameJson = line.substr(nameValueStart, nameValueEnd - nameValueStart + 1);
        hashes[nameJson] = hash;
    };

    while (succeeded) {
        DWORD bytesRead = 0;
        if (!ReadFile(audit, buffer, sizeof(buffer), &bytesRead, nullptr)) {
            error = GetLastError();
            succeeded = false;
            break;
        }
        if (bytesRead == 0) break;
        for (DWORD index = 0; index < bytesRead; ++index) {
            if (buffer[index] == '\n') {
                if (!pending.empty() && pending.back() == '\r') pending.pop_back();
                processLine(pending);
                pending.clear();
            } else {
                pending.push_back(buffer[index]);
            }
        }
    }
    if (succeeded && !pending.empty()) processLine(pending);
    if (!CloseHandle(audit) && succeeded) {
        error = GetLastError();
        succeeded = false;
    }
    if (!succeeded) return false;
    if (!headerValid) {
        error = ERROR_INVALID_DATA;
        return false;
    }
    return true;
}

bool runFileQuarantine() {
    std::wstring sourceDirectory;
    bool cancelled = false;
    DWORD error = ERROR_SUCCESS;
    if (!chooseEvidenceFolder(sourceDirectory, cancelled, error,
            L"Selecione a pasta de origem; apenas arquivos diretos visiveis serao listados")) {
        std::cerr << "Falha ao selecionar pasta (erro " << error << ").\n";
        return false;
    }
    if (cancelled) {
        std::cout << "Operacao cancelada.\n";
        return true;
    }
    sourceDirectory = fullPath(sourceDirectory);
    if (sourceDirectory.empty() ||
        pathHasComponent(sourceDirectory, L"PC_Local_Quarantine")) {
        std::cerr << "Pasta invalida ou dentro da quarentena.\n";
        return false;
    }

    std::vector<DirectFileCandidate> files;
    if (!listDirectFiles(sourceDirectory, files, error)) {
        std::cerr << "Falha ao listar arquivos diretos (erro " << error << ").\n";
        return false;
    }
    if (files.empty()) {
        std::cout << "Nenhum arquivo direto elegivel. Pastas, itens ocultos, itens de sistema "
                  << "e pontos de reparse sao ignorados.\n";
        return true;
    }

    std::cout << "Quarentena reversivel; nenhum conteudo sera apagado. Sao considerados "
              << "apenas arquivos diretos visiveis sem hard links ou pontos de reparse. "
              << "A movimentacao pode interromper programas que estejam usando os arquivos. Origem: "
              << toUtf8(sourceDirectory.c_str()) << "\n";
    std::vector<std::size_t> selected;
    if (!promptForFileSelection(files, selected)) return false;
    std::cout << "Selecionados " << selected.size()
              << " arquivos. Nenhum destino existente sera sobrescrito.\n";
    if (!confirmExactOperation("QUARENTENAR")) {
        std::cout << "Nenhum arquivo foi movimentado.\n";
        return true;
    }

    const std::wstring quarantineRoot =
        joinPath(sourceDirectory, L"PC_Local_Quarantine");
    if (!CreateDirectoryW(quarantineRoot.c_str(), nullptr)) {
        const DWORD createError = GetLastError();
        if (createError != ERROR_ALREADY_EXISTS ||
            !isDirectoryWithoutReparsePoint(quarantineRoot, error)) {
            std::cerr << "Falha ao preparar quarentena (erro "
                      << (createError == ERROR_ALREADY_EXISTS ? error : createError) << ").\n";
            return false;
        }
    }

    std::wstring quarantineDirectory;
    bool directoryCreated = false;
    DWORD createError = ERROR_SUCCESS;
    const std::string baseCaseName = quarantineCaseName();
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        std::string caseName = baseCaseName;
        if (attempt != 0) caseName += "_" + std::to_string(attempt);
        quarantineDirectory = joinPath(
            quarantineRoot, std::wstring(caseName.begin(), caseName.end()));
        if (CreateDirectoryW(quarantineDirectory.c_str(), nullptr)) {
            directoryCreated = true;
            break;
        }
        createError = GetLastError();
        if (createError != ERROR_ALREADY_EXISTS) break;
    }
    if (!directoryCreated) {
        std::cerr << "Falha ao criar pasta exclusiva de quarentena (erro "
                  << createError << ").\n";
        return false;
    }
    const std::wstring auditPath = joinPath(quarantineDirectory, L"quarantine_audit.jsonl");
    HANDLE audit = CreateFileW(auditPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (audit == INVALID_HANDLE_VALUE) {
        std::cerr << "Falha ao criar registro de auditoria (erro "
                  << GetLastError() << "); nenhum arquivo foi movimentado.\n";
        return false;
    }
    std::ostringstream header;
    header << "{\"format\": \"PC_LOCAL_QUARANTINE_V1\", \"created_at_utc\": "
           << jsonString(currentUtcTimestamp()) << ", \"source_directory\": "
           << jsonString(toUtf8(sourceDirectory.c_str()))
           << ", \"hash_scope\": \"default_data_stream_only\"}";
    bool complete = writeJsonLineToOpenFile(audit, header.str(), error);
    if (complete && !FlushFileBuffers(audit)) {
        error = GetLastError();
        complete = false;
    }
    if (!complete) {
        if (error == ERROR_SUCCESS) error = GetLastError();
        CloseHandle(audit);
        std::cerr << "Falha ao inicializar auditoria (erro " << error
                  << "); nenhum arquivo foi movimentado.\n";
        return false;
    }

    unsigned int moved = 0;
    for (std::size_t index : selected) {
        if (cancellationRequested()) {
            complete = false;
            break;
        }
        const DirectFileCandidate& file = files[index];
        std::string volumeSerial;
        std::string fileId;
        DWORD linkCount = 0;
        DWORD identityError = ERROR_SUCCESS;
        if (!getFileIdentity(file.path, volumeSerial, fileId, linkCount, identityError)) {
            complete = false;
            if (!appendQuarantineAudit(audit, "quarantine", file.name, file.path,
                    file.path, "", "identity_check_failed_" +
                        std::to_string(identityError), error)) {
                break;
            }
            std::cerr << "Identidade do arquivo indisponivel; mantido na origem: "
                      << toUtf8(file.name.c_str()) << ".\n";
            continue;
        }
        if (linkCount > 1) {
            complete = false;
            if (!appendQuarantineAudit(audit, "quarantine", file.name, file.path,
                    file.path, "", "multiple_hard_links_" +
                        std::to_string(linkCount), error)) {
                break;
            }
            std::cerr << "Arquivo com multiplos hard links; mantido na origem: "
                      << toUtf8(file.name.c_str()) << ".\n";
            continue;
        }
        std::string digest;
        error = ERROR_SUCCESS;
        if (!sha256File(file.path, digest, error)) {
            complete = false;
            if (!appendQuarantineAudit(audit, "quarantine", file.name, file.path,
                    file.path, "", "hash_before_failed_" + std::to_string(error), error)) {
                break;
            }
            std::cerr << "Hash falhou; arquivo mantido na origem: "
                      << toUtf8(file.name.c_str()) << ".\n";
            continue;
        }

        const std::wstring destination = joinPath(quarantineDirectory, file.name);
        if (!appendQuarantineAudit(audit, "quarantine", file.name, file.path,
                destination, digest, "move_intent", error)) {
            complete = false;
            break;
        }
        if (!MoveFileExW(file.path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
            const DWORD moveError = GetLastError();
            if (!appendQuarantineAudit(audit, "quarantine", file.name, file.path,
                    file.path, digest, "move_failed_" + std::to_string(moveError), error)) {
                complete = false;
                break;
            }
            std::cerr << "Nao foi possivel movimentar " << toUtf8(file.name.c_str())
                      << " (erro " << moveError << "); original mantido.\n";
            complete = false;
            continue;
        }

        std::string movedDigest;
        error = ERROR_SUCCESS;
        const bool movedHashAvailable = sha256File(destination, movedDigest, error);
        if (!movedHashAvailable || movedDigest != digest) {
            const DWORD verifyError = movedHashAvailable ? ERROR_CRC : error;
            if (!appendQuarantineAudit(audit, "quarantine", file.name, file.path,
                destination, digest, "verification_failed_" +
                    std::to_string(verifyError), error)) {
                std::cerr << "Falha ao registrar resultado da verificacao (erro "
                          << error << "). ";
            }
            std::cerr << "Arquivo movimentado, mas verificacao falhou; mantenha a quarentena "
                      << "intacta: " << toUtf8(destination.c_str()) << ".\n";
            complete = false;
            break;
        }
        if (!appendQuarantineAudit(audit, "quarantine", file.name, file.path,
                destination, digest, "quarantined_verified", error)) {
            std::cerr << "Arquivo em quarentena, mas houve falha ao gravar o resultado no "
                      << "log. Nao mova nem altere: " << toUtf8(destination.c_str()) << ".\n";
            complete = false;
            break;
        }
        ++moved;
        std::cout << "Quarentenado e verificado: " << toUtf8(file.name.c_str()) << "\n";
    }

    if (cancellationRequested()) complete = false;
    const std::string finalStatus = cancellationRequested()
        ? "cancelled_partial" : complete ? "complete" : "partial";
    if (!appendQuarantineAudit(audit, "quarantine_session", L"", sourceDirectory,
            quarantineDirectory, "", finalStatus, error)) {
        complete = false;
    }
    if (!finishEvidenceFile(audit, error)) complete = false;
    if (!complete) {
        std::cerr << "Quarentena incompleta; consulte o registro e os caminhos informados. "
                  << "Nenhum arquivo foi apagado.\n";
    }
    std::cout << moved << " arquivo(s) movimentado(s) e verificado(s). Registro: "
              << toUtf8(auditPath.c_str()) << "\n";
    return complete;
}

bool runQuarantineRestore() {
    std::wstring quarantineDirectory;
    bool cancelled = false;
    DWORD error = ERROR_SUCCESS;
    if (!chooseEvidenceFolder(quarantineDirectory, cancelled, error,
            L"Selecione a pasta de caso dentro de PC_Local_Quarantine para restaurar")) {
        std::cerr << "Falha ao selecionar pasta (erro " << error << ").\n";
        return false;
    }
    if (cancelled) {
        std::cout << "Restauracao cancelada.\n";
        return true;
    }

    std::wstring normalizedQuarantine;
    std::wstring auditPath;
    if (!isValidQuarantineDirectory(quarantineDirectory, normalizedQuarantine,
                                    auditPath, error)) {
        std::cerr << "Pasta nao parece uma quarentena valida (erro " << error << ").\n";
        return false;
    }

    std::map<std::string, std::string> verifiedHashes;
    if (!loadVerifiedQuarantineHashes(auditPath, verifiedHashes, error)) {
        std::cerr << "Falha ao ler auditoria da quarentena (erro " << error << ").\n";
        return false;
    }
    if (verifiedHashes.empty()) {
        std::cout << "A auditoria nao contem arquivos verificados para restaurar.\n";
        return true;
    }

    std::vector<DirectFileCandidate> files;
    if (!listDirectFiles(normalizedQuarantine, files, error)) {
        std::cerr << "Falha ao listar arquivos da quarentena (erro " << error << ").\n";
        return false;
    }
    files.erase(std::remove_if(files.begin(), files.end(),
        [&verifiedHashes](const DirectFileCandidate& file) {
            return verifiedHashes.find(jsonString(toUtf8(file.name.c_str()))) ==
                verifiedHashes.end();
        }), files.end());
    if (files.empty()) {
        std::cout << "Nenhum arquivo auditado e verificado esta presente na quarentena.\n";
        return true;
    }

    std::wstring destinationDirectory;
    if (!chooseEvidenceFolder(destinationDirectory, cancelled, error,
            L"Selecione a pasta de destino para restaurar os arquivos")) {
        std::cerr << "Falha ao selecionar destino (erro " << error << ").\n";
        return false;
    }
    if (cancelled) {
        std::cout << "Restauracao cancelada.\n";
        return true;
    }
    destinationDirectory = fullPath(destinationDirectory);
    if (destinationDirectory.empty()) {
        std::cerr << "Destino invalido: nao foi possivel normalizar o caminho.\n";
        return false;
    }
    if (pathHasComponent(destinationDirectory, L"PC_Local_Quarantine") ||
        pathIsWithin(destinationDirectory, normalizedQuarantine)) {
        std::cerr << "Destino invalido: escolha uma pasta fora da quarentena.\n";
        return false;
    }
    if (!isDirectoryWithoutReparsePoint(destinationDirectory, error)) {
        std::cerr << "Destino invalido ou ponto de reparse (erro "
                  << error << ").\n";
        return false;
    }

    std::cout << "A restauracao nao sobrescreve destinos existentes; conflitos e arquivos "
              << "alterados serao mantidos na quarentena. O destino deve estar no mesmo volume. Destino: "
              << toUtf8(destinationDirectory.c_str()) << "\n";
    std::vector<std::size_t> selected;
    if (!promptForFileSelection(files, selected)) return false;
    std::cout << "Selecionados " << selected.size() << " arquivos.\n";
    if (!confirmExactOperation("RESTAURAR")) {
        std::cout << "Nenhum arquivo foi restaurado.\n";
        return true;
    }

    HANDLE audit = CreateFileW(auditPath.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (audit == INVALID_HANDLE_VALUE) {
        std::cerr << "Falha ao abrir auditoria para adicionar restauracoes (erro "
                  << GetLastError() << "). Nenhum arquivo foi movido.\n";
        return false;
    }

    bool complete = true;
    unsigned int restored = 0;
    for (std::size_t index : selected) {
        if (cancellationRequested()) {
            complete = false;
            break;
        }
        const DirectFileCandidate& file = files[index];
        const auto expectedHash =
            verifiedHashes.find(jsonString(toUtf8(file.name.c_str())));
        if (expectedHash == verifiedHashes.end()) continue;
        std::string currentHash;
        error = ERROR_SUCCESS;
        const bool currentHashAvailable = sha256File(file.path, currentHash, error);
        if (!currentHashAvailable ||
            currentHash != expectedHash->second) {
            const DWORD hashError = currentHashAvailable ? ERROR_CRC : error;
            if (!appendQuarantineAudit(audit, "restore", file.name, file.path,
                file.path, currentHash, "source_hash_mismatch_" +
                    std::to_string(hashError), error)) {
                complete = false;
                break;
            }
            std::cerr << "Arquivo alterado ou ilegivel; mantido na quarentena: "
                      << toUtf8(file.name.c_str()) << ".\n";
            complete = false;
            continue;
        }

        const std::wstring destination = joinPath(destinationDirectory, file.name);
        if (!appendQuarantineAudit(audit, "restore", file.name, file.path,
                destination, currentHash, "move_intent", error)) {
            complete = false;
            break;
        }
        if (!MoveFileExW(file.path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
            const DWORD moveError = GetLastError();
            if (!appendQuarantineAudit(audit, "restore", file.name, file.path,
                    file.path, currentHash, "restore_failed_" +
                        std::to_string(moveError), error)) {
                complete = false;
                break;
            }
            if (moveError == ERROR_NOT_SAME_DEVICE) {
                std::cerr << "Restauracao exige destino no mesmo volume; arquivo mantido "
                          << "na quarentena: " << toUtf8(file.name.c_str()) << ".\n";
            } else {
                std::cerr << "Restauracao falhou (erro " << moveError
                          << "); arquivo mantido na quarentena: "
                          << toUtf8(file.name.c_str()) << ".\n";
            }
            complete = false;
            continue;
        }

        std::string restoredHash;
        error = ERROR_SUCCESS;
        const bool restoredHashAvailable = sha256File(destination, restoredHash, error);
        if (!restoredHashAvailable ||
            restoredHash != expectedHash->second) {
            const DWORD verifyError = restoredHashAvailable ? ERROR_CRC : error;
            if (!appendQuarantineAudit(audit, "restore", file.name, file.path,
                destination, expectedHash->second, "verification_failed_" +
                    std::to_string(verifyError), error)) {
                std::cerr << "Falha ao registrar resultado da verificacao (erro "
                          << error << "). ";
            }
            std::cerr << "Arquivo restaurado, mas a verificacao falhou: "
                      << toUtf8(destination.c_str()) << ".\n";
            complete = false;
            break;
        }
        if (!appendQuarantineAudit(audit, "restore", file.name, file.path,
                destination, expectedHash->second, "restored_verified", error)) {
            std::cerr << "Arquivo restaurado, mas o log nao foi atualizado; verifique "
                      << toUtf8(destination.c_str()) << ".\n";
            complete = false;
            break;
        }
        ++restored;
        std::cout << "Restaurado e verificado: " << toUtf8(file.name.c_str()) << "\n";
    }
    if (cancellationRequested()) complete = false;
    const std::string finalStatus = cancellationRequested()
        ? "cancelled_partial" : complete ? "complete" : "partial";
    if (!appendQuarantineAudit(audit, "restore_session", L"", normalizedQuarantine,
            destinationDirectory, "", finalStatus, error)) {
        complete = false;
    }
    if (!finishEvidenceFile(audit, error)) complete = false;
    if (!complete) {
        std::cerr << "Restauracao incompleta; consulte o registro de auditoria e "
                  << "os caminhos informados.\n";
    }
    std::cout << restored << " arquivo(s) restaurado(s). Auditoria: "
              << toUtf8(auditPath.c_str()) << "\n";
    return complete;
}

std::vector<std::wstring> availableEventChannels() {
    return {
        L"Security",
        L"System",
        L"Application",
        L"Microsoft-Windows-Windows Defender/Operational",
        L"Microsoft-Windows-PowerShell/Operational",
        L"Microsoft-Windows-Sysmon/Operational",
        L"Microsoft-Windows-TaskScheduler/Operational",
        L"Microsoft-Windows-WMI-Activity/Operational",
        L"Microsoft-Windows-TerminalServices-LocalSessionManager/Operational",
        L"Microsoft-Windows-TerminalServices-RemoteConnectionManager/Operational",
        L"Microsoft-Windows-WinRM/Operational",
        L"Microsoft-Windows-DNS-Client/Operational",
        L"Microsoft-Windows-NetworkProfile/Operational",
        L"Microsoft-Windows-Windows Firewall With Advanced Security/Firewall"
    };
}

std::vector<std::wstring> channelsForCollection(const CaseMetadata& metadata) {
    if (metadata.customCollection) return metadata.eventChannels;
    if (metadata.selection == 6 || metadata.selection == 8) return availableEventChannels();
    return {};
}

void printSpaceEstimate(const CaseMetadata& metadata) {
    const std::vector<std::wstring> channels = channelsForCollection(metadata);
    unsigned long long logBytes = 0;
    unsigned int unavailableChannels = 0;
    for (const auto& channel : channels) {
        EVT_HANDLE log = EvtOpenLog(nullptr, channel.c_str(), EvtOpenChannelPath);
        if (log == nullptr) {
            ++unavailableChannels;
            continue;
        }
        EVT_VARIANT property = {};
        DWORD used = 0;
        if (EvtGetLogInfo(log, EvtLogFileSize, sizeof(property), &property, &used) &&
            property.Type == EvtVarTypeUInt64 &&
            property.UInt64Val <= (std::numeric_limits<unsigned long long>::max() - logBytes)) {
            logBytes += property.UInt64Val;
        } else {
            ++unavailableChannels;
        }
        EvtClose(log);
    }

    const unsigned long long overhead = 16ULL * 1024ULL * 1024ULL;
    const unsigned long long estimatedLogOutput =
        logBytes > (std::numeric_limits<unsigned long long>::max() - overhead) / 3
            ? std::numeric_limits<unsigned long long>::max()
            : logBytes * 3 + overhead;
    wchar_t currentDirectory[32768] = {};
    const DWORD directoryLength = GetCurrentDirectoryW(
        static_cast<DWORD>(sizeof(currentDirectory) / sizeof(currentDirectory[0])),
        currentDirectory);
    ULARGE_INTEGER available = {};
    const bool hasSpaceInfo = directoryLength != 0 &&
        directoryLength < sizeof(currentDirectory) / sizeof(currentDirectory[0]) &&
        GetDiskFreeSpaceExW(currentDirectory, &available, nullptr, nullptr) != FALSE;

    std::cout << "\nEstimativa orientativa de espaco para o caso: "
              << (estimatedLogOutput / (1024ULL * 1024ULL)) << " MiB "
              << "(3x tamanho atual dos logs acessiveis + 16 MiB de margem). "
              << "Isto nao e limite superior: XML renderizado pode expandir mais. "
              << "Arquivos selecionados sao inventariados e hasheados, nao copiados.\n";
    if (unavailableChannels != 0) {
        std::cout << "Tamanho indisponivel para " << unavailableChannels
                  << " canal(is); estimativa incompleta.\n";
    }
    if (hasSpaceInfo) {
        std::cout << "Espaco livre na unidade de destino atual: "
                  << (available.QuadPart / (1024ULL * 1024ULL)) << " MiB.\n";
        if (available.QuadPart < estimatedLogOutput) {
            std::cout << "Aviso: o espaco livre pode ser insuficiente para os logs.\n";
        }
    } else {
        std::cout << "Espaco livre no destino: indisponivel.\n";
    }
}

bool configureCustomCollection(CaseMetadata& metadata) {
    metadata.customCollection = true;
    metadata.selection = 10;
    if (!readYesNo("Coletar sistema e discos", metadata.collectSystem) ||
        !readYesNo("Coletar interfaces de rede e IPs", metadata.collectNetwork) ||
        !readYesNo("Coletar processos e endpoints TCP/UDP", metadata.collectProcesses) ||
        !readYesNo("Coletar software instalado", metadata.collectSoftware) ||
        !readYesNo("Coletar entradas de inicializacao", metadata.collectStartup) ||
        !readYesNo("Coletar canais de eventos do Windows",
                   metadata.collectLogs) ||
        !readYesNo("Coletar servicos e tarefas agendadas",
                   metadata.collectServicesTasks) ||
        !readYesNo("Inventariar e calcular SHA-256 dos arquivos de uma pasta",
                   metadata.collectFiles) ||
        !readYesNo("Coletar fluxos NTFS e inventario do Prefetch",
                   metadata.collectNtfsArtifacts) ||
        !readYesNo("Coletar atalhos recentes e inventario de Jump Lists",
                   metadata.collectUserActivity)) {
        return false;
    }

    if (metadata.collectLogs) {
        const std::vector<std::wstring> channels = availableEventChannels();
        std::cout << "Selecione os canais de eventos (numeros separados por virgula, "
                  << "0 para nenhum, ou 'todos'):\n";
        for (std::size_t index = 0; index < channels.size(); ++index) {
            std::cout << "  " << (index + 1) << ". " << toUtf8(channels[index].c_str()) << '\n';
        }
        while (true) {
            std::cout << "Canais: ";
            std::string input;
            if (!std::getline(std::cin, input)) return false;
            if (input == "todos" || input == "TODOS" || input == "all") {
                metadata.eventChannels = channels;
                break;
            }
            std::replace(input.begin(), input.end(), ',', ' ');
            std::istringstream parser(input);
            std::vector<std::size_t> selected;
            std::size_t channelNumber = 0;
            bool valid = true;
            while (parser >> channelNumber) {
                if (channelNumber == 0) continue;
                if (channelNumber > channels.size()) {
                    valid = false;
                    break;
                }
                if (std::find(selected.begin(), selected.end(), channelNumber) == selected.end()) {
                    selected.push_back(channelNumber);
                }
            }
            if (!parser.eof()) valid = false;
            if (!valid) {
                std::cout << "Selecao invalida; use numeros entre 1 e "
                          << channels.size() << ".\n";
                continue;
            }
            metadata.eventChannels.clear();
            for (std::size_t number : selected) {
                metadata.eventChannels.push_back(channels[number - 1]);
            }
            if (metadata.eventChannels.empty()) metadata.collectLogs = false;
            break;
        }
    }

    const bool anySelected = metadata.collectSystem || metadata.collectNetwork ||
        metadata.collectProcesses ||
        metadata.collectSoftware || metadata.collectStartup || metadata.collectLogs ||
        metadata.collectServicesTasks || metadata.collectFiles ||
        metadata.collectNtfsArtifacts || metadata.collectUserActivity;
    if (!anySelected) {
        std::cout << "Nenhuma categoria foi selecionada.\n";
        return false;
    }
    return true;
}

std::string makeReport(const CaseMetadata& metadata) {
    std::ostringstream report;
    const int choice = metadata.selection;
    const bool custom = metadata.customCollection;
    const bool includeSystem = custom ? metadata.collectSystem : choice == 1 || choice == 6;
    const bool includeNetwork = custom ? metadata.collectNetwork : choice == 2 || choice == 6;
    const bool includeNetworkDetails = custom
        ? metadata.collectProcesses : choice == 3 || choice == 6;
    const bool includeProcesses = custom
        ? metadata.collectProcesses : choice == 4 || choice == 6;
    const bool includeSoftware = custom ? metadata.collectSoftware : choice == 5 || choice == 6;
    const bool includeStartup = custom ? metadata.collectStartup : choice == 7 || choice == 6;
    const bool includeSecurityPreview = custom
        ? metadata.collectLogs &&
            std::find(metadata.eventChannels.begin(), metadata.eventChannels.end(),
                      L"Security") != metadata.eventChannels.end()
        : choice == 8 || choice == 6;
    ProcessMap processes;
    DWORD processSnapshotError = ERROR_SUCCESS;
    const bool needProcesses = includeNetworkDetails || includeProcesses;
    const bool processListAvailable =
        !needProcesses || snapshotProcesses(processes, processSnapshotError);
    if (includeSystem) report << "=== SISTEMA ===\n" << reportSystem();
    if (includeNetwork) report << "\n=== INTERFACES E IPS ===\n" << reportNetworkAdapters();
    if (includeNetworkDetails) {
        report << "\n=== CONEXOES TCP ===\n"
               << reportTcpConnections(processes, processListAvailable);
        report << "\n=== PORTAS UDP LOCAIS ===\n"
               << reportUdpEndpoints(processes, processListAvailable);
    }
    if (includeProcesses) {
        report << "\n=== PROCESSOS ===\n"
               << reportProcesses(processes, processListAvailable, processSnapshotError);
    }
    if (includeSoftware) report << "\n=== SOFTWARE INSTALADO ===\n" << reportSoftware();
    if (includeStartup) report << "\n=== INICIALIZACAO ===\n" << reportStartup();
    if (includeSecurityPreview) {
        report << "\n=== LOGONS E EVENTOS DE SEGURANCA ===\n" << reportSecurityEvents();
    }
    if (custom && metadata.collectServicesTasks) {
        report << "\n=== SERVICOS E TAREFAS AGENDADAS ===\n"
               << "Os detalhes das fontes selecionadas estarao nos arquivos JSONL do caso.\n";
    }
    if (choice == 9) {
        report << "=== METADADOS DE ARQUIVOS ===\n"
               << "A coleta percorre somente a pasta escolhida, calcula SHA-256 e inventaria fluxos alternativos; nao copia o conteudo.\n"
               << "A leitura pode atualizar a data de ultimo acesso conforme a configuracao do Windows.\n";
    }
    if (custom && (metadata.collectFiles || metadata.collectNtfsArtifacts)) {
        report << "\n=== ARQUIVOS E ARTEFATOS NTFS ===\n"
               << "A leitura limita-se a pasta selecionada; fluxos alternativos e hashes podem levar tempo.\n";
    }
    if (custom && metadata.collectUserActivity) {
        report << "\n=== ATIVIDADE RECENTE DO USUARIO ===\n"
               << "Atalhos LNK recentes serao interpretados sem resolver ou executar destinos; "
               << "Jump Lists serao inventariadas e hasheadas, sem parsing do formato binario.\n";
    }
    return report.str();
}

void printConsoleColor(const std::string& text, WORD color) {
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info = {};
    if (console == INVALID_HANDLE_VALUE ||
        !GetConsoleScreenBufferInfo(console, &info)) {
        std::cout << text;
        return;
    }

    SetConsoleTextAttribute(console, color);
    std::cout << text;
    SetConsoleTextAttribute(console, info.wAttributes);
}

void printMenuOption(const char* number, const char* description) {
    printConsoleColor(std::string("  [") + number + "]", FOREGROUND_GREEN |
                      FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    std::cout << " " << description << '\n';
}

void clearConsoleWindow(HANDLE console, const CONSOLE_SCREEN_BUFFER_INFO& info) {
    const DWORD width = static_cast<DWORD>(
        info.srWindow.Right - info.srWindow.Left + 1);
    DWORD written = 0;
    for (SHORT row = info.srWindow.Top; row <= info.srWindow.Bottom; ++row) {
        const COORD start = {info.srWindow.Left, row};
        FillConsoleOutputCharacterW(console, L' ', width, start, &written);
        FillConsoleOutputAttribute(console, info.wAttributes, width, start, &written);
    }

    const COORD origin = {info.srWindow.Left, info.srWindow.Top};
    SetConsoleCursorPosition(console, origin);
}

void printStartupScreen() {
    std::vector<std::string> artwork = {
        "             .-=========-.",
        "          .-'   .---.   '-.",
        "        .'     /     \\     '.",
        "       /      |  .-.  |      \\",
        "      |       | ( o ) |       |",
        "       \\      |  '-'  |      /",
        "        '.     \\_____/     .'",
        "          '-.___________.-'"
    };
    const std::map<char, std::vector<std::string>> font = {
        {'D', {"#### ", "#   #", "#   #", "#   #", "#### "}},
        {'E', {"#####", "#    ", "#### ", "#    ", "#####"}},
        {'H', {"#   #", "#   #", "#####", "#   #", "#   #"}},
        {'L', {"#    ", "#    ", "#    ", "#    ", "#####"}},
        {'O', {" ### ", "#   #", "#   #", "#   #", " ### "}},
        {'S', {" ####", "#    ", " ### ", "    #", "#### "}},
        {'U', {"#   #", "#   #", "#   #", "#   #", " ### "}}
    };
    std::vector<std::string> titleRows(5);
    for (const std::string& word : {std::string("OLHO"), std::string("DE"),
                                    std::string("DEUS")}) {
        if (!titleRows[0].empty()) {
            for (std::string& row : titleRows) row += "   ";
        }
        for (std::size_t character = 0; character < word.size(); ++character) {
            if (character != 0) {
                for (std::string& row : titleRows) row += ' ';
            }
            const std::vector<std::string>& glyph = font.at(word[character]);
            for (std::size_t row = 0; row < titleRows.size(); ++row) {
                titleRows[row] += glyph[row];
            }
        }
    }
    artwork.push_back("");
    artwork.insert(artwork.end(), titleRows.begin(), titleRows.end());
    artwork.insert(artwork.end(), {
        "          TRIAGEM FORENSE LOCAL",
        "       Pressione Enter para continuar"
    });
    const WORD logoColor = FOREGROUND_GREEN | FOREGROUND_BLUE |
                           FOREGROUND_INTENSITY;
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info = {};
    if (console == INVALID_HANDLE_VALUE ||
        !GetConsoleScreenBufferInfo(console, &info)) {
        for (std::size_t index = 0; index < artwork.size(); ++index) {
            if (index < 14) printConsoleColor(artwork[index], logoColor);
            else std::cout << artwork[index];
            std::cout << '\n';
        }
        return;
    }

    clearConsoleWindow(console, info);
    const int windowWidth = info.srWindow.Right - info.srWindow.Left + 1;
    const int windowHeight = info.srWindow.Bottom - info.srWindow.Top + 1;
    const int visibleLines = std::min(windowHeight, static_cast<int>(artwork.size()));
    const std::size_t firstArtworkLine =
        (artwork.size() - static_cast<std::size_t>(visibleLines)) / 2;
    const int firstRow = info.srWindow.Top +
        std::max(0, (windowHeight - visibleLines) / 2);
    for (int lineIndex = 0; lineIndex < visibleLines; ++lineIndex) {
        const std::size_t index = firstArtworkLine +
                                  static_cast<std::size_t>(lineIndex);
        std::string line = artwork[index];
        if (line.size() > static_cast<std::size_t>(windowWidth)) {
            line.resize(static_cast<std::size_t>(windowWidth));
        }
        const int column = info.srWindow.Left +
            std::max(0, (windowWidth - static_cast<int>(line.size())) / 2);
        const COORD position = {
            static_cast<SHORT>(column),
            static_cast<SHORT>(firstRow + lineIndex)
        };
        SetConsoleCursorPosition(console, position);
        if (index < 14) {
            printConsoleColor(line, logoColor);
        } else {
            std::cout << line;
        }
    }

    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD inputMode = 0;
    if (input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &inputMode)) {
        std::string ignored;
        std::getline(std::cin, ignored);
        GetConsoleScreenBufferInfo(console, &info);
        clearConsoleWindow(console, info);
    } else {
        std::cout << '\n';
    }
}

void printMenu() {
    const WORD headingColor = FOREGROUND_GREEN | FOREGROUND_BLUE |
                              FOREGROUND_INTENSITY;
    const WORD sectionColor = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    const WORD warningColor = FOREGROUND_RED | FOREGROUND_GREEN |
                              FOREGROUND_INTENSITY;

    std::cout << "\n";
    printConsoleColor("  DIAGNOSTICO LOCAL DE PC\n", headingColor);
    std::cout << "  Triagem local | coleta preservativa | Windows\n\n";

    printConsoleColor("  INVESTIGAR\n", sectionColor);
    printMenuOption("1", "Sistema e discos");
    printMenuOption("2", "Interfaces de rede e IPs");
    printMenuOption("3", "Conexoes TCP e portas UDP locais");
    printMenuOption("4", "Processos");
    printMenuOption("5", "Softwares instalados");

    std::cout << '\n';
    printConsoleColor("  COLETAR E ANALISAR\n", sectionColor);
    printMenuOption("6", "Coleta completa (logs, Prefetch, LNK e Jump Lists)");
    printMenuOption("7", "Programas na inicializacao");
    printMenuOption("8", "Logons e eventos de seguranca (requer permissao)");
    printMenuOption("9", "Metadados e SHA-256 de arquivos (escolher pasta)");
    printMenuOption("10", "Coleta personalizada (categorias e canais)");

    std::cout << '\n';
    printConsoleColor("  PRESERVAR ARQUIVOS\n", sectionColor);
    printMenuOption("11", "Mover arquivos selecionados para quarentena reversivel");
    printMenuOption("12", "Restaurar arquivos de uma quarentena");

    std::cout << '\n';
    printConsoleColor("  DEEP / SANITIZACAO\n", sectionColor);
    printMenuOption("13", "NTFS profundo: MFT, apagados, USN, carving");
    printMenuOption("14", "Carving de espaco nao alocado e slack");
    printMenuOption("15", "Sanitizar arquivo ou pasta (wipe)");
    printMenuOption("16", "Sanitizar espaco livre, USN, logs e artefatos");
    printMenuOption("17", "Sanitizar copias de sombra (VSS)");
    printMenuOption("18", "Artefatos: registro, navegador, EVTX, lixeira, LNK, Jump, Prefetch, Shellbags");
    printMenuOption("19", "Imagem raw com SHA-256 e manifesto (aquisição)");
    printMenuOption("20", "Hive offline (regf): SYSTEM/SOFTWARE/SAM/SECURITY/NTUSER");
    printMenuOption("21", "Amcache + ShimCache (execução e arquivos)");
    printMenuOption("22", "Assinar/verificar manifesto (ECDSA P-256)");
    printMenuOption("23", "SRUM (ESE): uso de rede e de aplicativos");
    printMenuOption("24", "Sanitizar hive offline (células livres)");
    printMenuOption("25", "VSS: enumerate shadow copies (admin)");
    printMenuOption("26", "EVTX de imagem NTFS (MFT -> parser)");
    printMenuOption("27", "Prefetch de imagem NTFS (MFT -> parser)");
    printMenuOption("28", "Recuperar conteudo da lixeira ($R)");
    printMenuOption("29", "Browser secrets (DPAPI/AES-GCM)");
    printMenuOption("30", "Zerar $LogFile e $UsnJrnl:$J (imagem)");
    printMenuOption("31", "BAM/DAM offline (SYSTEM hive)");
    printMenuOption("32", "Ler shadow copy VSS (admin)");
    printMenuOption("33", "Sanitizar perfil de navegador");
    printMenuOption("34", "Criar/verificar container AFF4-L");
    printMenuOption("35", "Criar AFF4-L ZIP64 unico");
    printMenuOption("36", "Verificar AFF4-L ZIP64");
    printMenuOption("37", "Listar discos fisicos");
    printMenuOption("38", "Sanitizar disco fisico (offline)");
    printMenuOption("39", "Interface grafica (GUI)");
    printMenuOption("40", "NVMe Sanitize (block/crypto)");
    printMenuOption("41", "Kernel driver: versao/leitura raw");
    printMenuOption("42", "E01: criar/exportar com libewf");
    printMenuOption("43", "E01: escanear imagem");
    printMenuOption("44", "ATA Secure Erase (SATA)");
    printMenuOption("45", "Auditar exposicao residual");
    printMenuOption("46", "Limpar residuos locais");
    printMenuOption("47", "Alternar modo pericia/sanitizacao");

    std::cout << '\n';
    printConsoleColor("  Os logs do Windows sao preservados; a ferramenta nao os limpa.\n",
                      warningColor);
    printMenuOption("0", "Sair");
    std::cout << "\n  Escolha uma opcao: ";
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc > 1) {
        const int deep_result = deep::run_cli(argc, argv);
        if (deep_result >= 0) return deep_result;
    }
    printStartupScreen();
    sdk::client sdkClient;
    if (!sdkClient.conectar()) {
        std::cerr << "Erro ao iniciar o SDK.\n";
        return 1;
    }

    std::cout << "\nAviso: o sdk.hpp atual e demonstrativo e nao envia dados pela rede.\n"
              << "Relatorios so serao passados ao SDK se voce confirmar.\n";

    std::string input;
    while (true) {
        printMenu();
        if (!std::getline(std::cin, input)) break;

        std::istringstream parser(input);
        int choice = -1;
        char extra = '\0';
        if (!(parser >> choice) || (parser >> extra) || choice < 0 || choice > 47) {
            std::cout << "Opcao invalida.\n";
            continue;
        }
        if (choice == 0) break;
        if (choice == 11 || choice == 12) {
            InterlockedExchange(&g_cancelRequested, FALSE);
            if (choice == 11) {
                runFileQuarantine();
            } else {
                runQuarantineRestore();
            }
            if (!std::cin) break;
            continue;
        }
        if (choice == 47) {
            const bool next = !deep::sanitize_mode_enabled();
            deep::set_sanitize_mode(next);
            std::cout << "Modo atual: " << (next ? "SANITIZACAO" : "PERICIA")
                      << "\n";
            continue;
        }
        if (choice >= 13 && choice <= 46) {
            deep::interactive(choice);
            if (!std::cin) break;
            continue;
        }

        CaseMetadata metadata = collectCaseMetadata(choice);
        if (choice == 10 && !configureCustomCollection(metadata)) {
            if (!std::cin) break;
            continue;
        }
        if (choice == 6 || choice == 9 ||
            (metadata.customCollection &&
             (metadata.collectFiles || metadata.collectNtfsArtifacts))) {
            std::cout << "Selecione a pasta de arquivos para inventariar.\n";
            bool cancelled = false;
            DWORD folderError = ERROR_SUCCESS;
            if (!chooseEvidenceFolder(metadata.evidenceFolder, cancelled, folderError)) {
                std::cerr << "Falha ao abrir seletor de pasta (erro "
                          << folderError << ").\n";
                continue;
            }
            if (cancelled) {
                std::cout << "Selecao cancelada; nenhuma coleta foi iniciada.\n";
                continue;
            }
        }
        const std::string report = makeReport(metadata);
        SYSTEMTIME completed = {};
        GetSystemTime(&completed);
        metadata.completedAtUtc = formatUtcTimestamp(completed);
        std::cout << "\n" << report;
        if (choice == 6 || choice == 8 ||
            (metadata.customCollection && metadata.collectLogs)) {
            std::cout << "\nA exportacao inclui todos os eventos acessiveis dos canais selecionados, sem limite, em EVTX e XML; "
                      << "pode gerar arquivos grandes e conter dados pessoais. Canais ausentes "
                      << "ou sem permissao serao marcados como erro.\n";
        }
        if (choice == 6 ||
            (metadata.customCollection && metadata.collectUserActivity)) {
            std::cout << "A coleta de atividade recente pode conter caminhos, nomes de arquivos, "
                      << "argumentos de atalhos e informacoes pessoais do usuario.\n";
        }
        if (choice == 6 || choice == 9 ||
            (metadata.customCollection &&
             (metadata.collectFiles || metadata.collectNtfsArtifacts))) {
            if (choice == 6) {
                std::cout << "A coleta le os arquivos da pasta escolhida e as definicoes de "
                          << "tarefas agendadas; pode demorar e atualizar last-access.\n";
            } else {
                std::cout << "A coleta le todos os arquivos da pasta escolhida para calcular "
                          << "hashes; pode demorar e atualizar last-access.\n";
            }
        }
        std::cout << "Salvar este caso localmente? (s/N): ";
        std::string saveConfirmation;
        if (!std::getline(std::cin, saveConfirmation)) break;
        if (saveConfirmation == "s" || saveConfirmation == "S") {
            printSpaceEstimate(metadata);
            bool proceedWithCollection = false;
            if (!readYesNo("Prosseguir com a coleta", proceedWithCollection)) break;
            if (!proceedWithCollection) {
                std::cout << "Coleta cancelada antes de iniciar.\n";
                continue;
            }
            InterlockedExchange(&g_cancelRequested, FALSE);
            std::cout << "Coleta iniciada. Pressione Q a qualquer momento para cancelar "
                      << "com seguranca; os arquivos parciais serao marcados.\n";
            std::wstring caseDirectory;
            std::string exportError;
            if (exportCase(metadata, report, caseDirectory, exportError)) {
                std::cout << "Caso exportado e hashes verificados em: "
                          << toUtf8(caseDirectory.c_str()) << '\n';
            } else {
                std::cerr << "Exportacao incompleta: " << exportError << '\n';
                if (!caseDirectory.empty()) {
                    std::cerr << "Arquivos parciais, se houver, estao em: "
                              << toUtf8(caseDirectory.c_str()) << '\n';
                }
            }
        } else {
            std::cout << "Relatorio nao foi salvo em disco.\n";
        }

        std::cout << "\nEnviar este relatorio ao SDK? (s/N): ";
        std::string confirmation;
        if (!std::getline(std::cin, confirmation)) break;
        if (confirmation == "s" || confirmation == "S") {
            const std::string response = sdkClient.enviar(report);
            if (response.empty()) {
                std::cerr << "O SDK retornou uma resposta vazia.\n";
            } else {
                std::cout << "Relatorio processado pela implementacao atual do SDK.\n";
            }
        } else {
            std::cout << "Relatorio nao enviado ao SDK.\n";
        }
    }

    sdkClient.desconectar();
    return 0;
#else
    std::cerr << "Este programa requer Windows.\n";
    return 1;
#endif
}
