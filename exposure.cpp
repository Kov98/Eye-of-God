// language: C++, file: exposure.cpp, target: Windows 10/11
// Audits local exposure and cleans only local residue. Remote backups, cloud
// copies, router logs and provider logs are explicitly out of reach.

#include "exposure.hpp"

#include "artifacts.hpp"

#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace deep {
namespace {

struct Finding {
    std::string category;
    std::string path;
    std::uint64_t bytes = 0;
    bool local_deletable = false;
    std::string note;
};

bool path_exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring user_profile() {
    wchar_t buffer[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, buffer)))
        return buffer;
    return L"C:\\Users\\Public";
}

std::wstring local_appdata() {
    wchar_t buffer[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buffer)))
        return buffer;
    return {};
}

std::wstring program_data() {
    wchar_t buffer[MAX_PATH] = {};
    GetEnvironmentVariableW(L"ProgramData", buffer, MAX_PATH);
    return buffer;
}

std::uint64_t directory_size(const std::wstring& root, int depth) {
    if (depth > 6) return 0;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(art::join_path(root, L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    std::uint64_t total = 0;
    do {
        if (wcscmp(data.cFileName, L".") == 0 ||
            wcscmp(data.cFileName, L"..") == 0) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
        const std::wstring child = art::join_path(root, data.cFileName);
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            total += directory_size(child, depth + 1);
        else
            total += (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) |
                     data.nFileSizeLow;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return total;
}

std::uint64_t file_size(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return 0;
    return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) |
           data.nFileSizeLow;
}


void add_path(std::vector<Finding>& findings, const std::string& category,
              const std::wstring& path, bool deletable, const std::string& note) {
    if (!path_exists(path)) return;
    Finding finding;
    finding.category = category;
    finding.path = art::to_utf8(path);
    const DWORD attributes = GetFileAttributesW(path.c_str());
    finding.bytes = (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        ? directory_size(path, 0) : file_size(path);
    finding.local_deletable = deletable;
    finding.note = note;
    findings.push_back(finding);
}

std::string human_size(std::uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, units[unit]);
    return buffer;
}

bool run_capture(const std::wstring& command, std::string& output) {
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &attributes, 0)) return false;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = nullptr;
    PROCESS_INFORMATION process{};
    std::wstring mutable_command = command;
    const BOOL ok = CreateProcessW(nullptr, mutable_command.data(), nullptr,
        nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(write_pipe);
    if (!ok) {
        CloseHandle(read_pipe);
        return false;
    }
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &got, nullptr) && got > 0)
        output.append(buffer, got);
    WaitForSingleObject(process.hProcess, 30000);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    CloseHandle(read_pipe);
    return code == 0;
}

bool run_command(const std::wstring& command) {
    std::string ignored;
    return run_capture(command, ignored);
}

std::string gateway_address() {
    ULONG size = 0;
    if (GetAdaptersInfo(nullptr, &size) != ERROR_BUFFER_OVERFLOW) return {};
    std::vector<unsigned char> buffer(size);
    auto* info = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());
    if (GetAdaptersInfo(info, &size) != NO_ERROR) return {};
    for (auto* adapter = info; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->GatewayList.IpAddress.String[0] != '\0' &&
            std::strcmp(adapter->GatewayList.IpAddress.String, "0.0.0.0") != 0)
            return adapter->GatewayList.IpAddress.String;
    }
    return {};
}

void collect_browser_sync(std::vector<Finding>& findings) {
    const std::wstring local = local_appdata();
    const std::wstring paths[] = {
        art::join_path(local, L"Google\\Chrome\\User Data"),
        art::join_path(local, L"Microsoft\\Edge\\User Data")
    };
    for (const auto& root : paths) {
        if (!path_exists(root)) continue;
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW(art::join_path(root, L"*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
            if (wcscmp(data.cFileName, L".") == 0 ||
                wcscmp(data.cFileName, L"..") == 0) continue;
            const std::wstring sync = art::join_path(
                art::join_path(root, data.cFileName), L"Sync Data");
            if (path_exists(sync)) {
                add_path(findings, "browser-sync", sync, true,
                    "cache local de sincronizacao; a copia na conta continua");
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
}

void collect_vm_snapshots(std::vector<Finding>& findings) {
    const std::wstring profile = user_profile();
    const std::wstring roots[] = {
        art::join_path(profile, L"VirtualBox VMs"),
        art::join_path(profile, L"Documents\\Virtual Machines"),
        L"D:\\VMs", L"E:\\VMs"
    };
    const wchar_t* extensions[] = {L".vdi", L".vmdk", L".vhdx", L".vmsn",
                                   L".vmem", L".vbox"};
    for (const auto& root : roots) {
        if (!path_exists(root)) continue;
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW(art::join_path(root, L"*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(data.cFileName, L".") == 0 ||
                wcscmp(data.cFileName, L"..") == 0) continue;
            const std::wstring child = art::join_path(root, data.cFileName);
            const std::wstring lower = child;
            bool match = false;
            for (const auto* ext : extensions) {
                const std::size_t len = wcslen(ext);
                if (lower.size() >= len &&
                    _wcsicmp(lower.c_str() + lower.size() - len, ext) == 0)
                    match = true;
            }
            if (match)
                add_path(findings, "vm-snapshot", child, false,
                    "snapshot local; apagar exige decisao manual; remoto/immutavel nao e alcancado");
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
}

void add_known_locations(std::vector<Finding>& findings) {
    const std::wstring profile = user_profile();
    const std::wstring local = local_appdata();
    const std::wstring programdata = program_data();
    const std::wstring roots[] = {L"C:\\", L"D:\\", L"E:\\"};
    for (const auto& root : roots) {
        add_path(findings, "system-residue", art::join_path(root, L"Windows.old"),
                 true, "instalacao anterior do Windows");
        add_path(findings, "system-residue", art::join_path(root, L"$Windows.~BT"),
                 true, "arquivos de upgrade");
        add_path(findings, "system-residue", art::join_path(root, L"$Windows.~WS"),
                 true, "arquivos de upgrade");
        add_path(findings, "pagefile", art::join_path(root, L"hiberfil.sys"),
                 true, "hibernacao pode conter memoria/cookies");
        add_path(findings, "pagefile", art::join_path(root, L"pagefile.sys"),
                 true, "pagefile pode conter memoria/cookies");
        add_path(findings, "recycle-bin", art::join_path(root, L"$Recycle.Bin"),
                 true, "lixeira");
        add_path(findings, "local-backup", art::join_path(root, L"WindowsImageBackup"),
                 false, "backup de imagem; apagar exige decisao do usuario");
    }
    add_path(findings, "file-history", L"C:\\FileHistory", true,
             "historico de arquivos");
    add_path(findings, "file-history",
             art::join_path(local, L"Microsoft\\Windows\\FileHistory"), true,
             "historico de arquivos");
    const wchar_t* sync_names[] = {
        L"OneDrive", L"Google Drive", L"Dropbox", L"MEGA", L"pCloudDrive",
        L"iCloudDrive", L"Nextcloud", L"ownCloud"
    };
    for (const auto* name : sync_names)
        add_path(findings, "cloud-sync", art::join_path(profile, name), true,
                 "pasta local sincronizada; a copia na nuvem continua");
    add_path(findings, "cloud-sync",
             art::join_path(local, L"Google\\DriveFS"), true,
             "cache do Google Drive");
    add_path(findings, "cloud-sync",
             art::join_path(local, L"Microsoft\\OneDrive\\Cache"), true,
             "cache do OneDrive");
    add_path(findings, "cloud-sync",
             art::join_path(profile, L"Dropbox\\.dropbox.cache"), true,
             "cache do Dropbox");
    const wchar_t* backup_dirs[] = {
        L"Acronis", L"Macrium", L"Veeam", L"EaseUS", L"AOMEI"
    };
    for (const auto* name : backup_dirs) {
        add_path(findings, "backup-tool",
                 art::join_path(programdata, name), false,
                 "pasta de ferramenta de backup; nao apagada automaticamente");
        add_path(findings, "backup-tool",
                 art::join_path(profile, name), false,
                 "pasta de ferramenta de backup; nao apagada automaticamente");
    }
    collect_browser_sync(findings);
    collect_vm_snapshots(findings);
}

std::string build_markdown(const std::vector<Finding>& findings,
                           const std::string& vss, const std::string& gateway) {
    std::string text = "# Relatorio de exposicao residual\n\n";
    text += "Gerado: " + art::now_iso() + "\n\n";
    text += "## Gateway/roteador\n\n";
    text += gateway.empty() ? "- Gateway nao detectado.\n"
                            : "- Gateway: " + gateway + "\n";
    text += "- Limpar logs/cache do roteador exige login administrativo no "
            "proprio roteador; nao e feito por este binario.\n";
    text += "- Recomendado: desativar administracao remota, atualizar firmware, "
            "revisar encaminhamento de portas e limpar logs.\n\n";
    text += "## Shadow copies / restore points\n\n";
    text += vss.empty() ? "- Nao foi possivel listar (admin necessario).\n"
                        : "- Saida de vssadmin list shadows:\n```\n" + vss + "\n```\n";
    text += "\n## Itens locais encontrados\n\n";
    text += "| Categoria | Caminho | Tamanho | Apagavel local |\n";
    text += "|---|---|---:|---|\n";
    for (const auto& finding : findings) {
        text += "| " + finding.category + " | `" + finding.path + "` | " +
            human_size(finding.bytes) + " | " +
            (finding.local_deletable ? "sim" : "nao") + " |\n";
    }
    text += "\n## Fora do alcance local\n\n";
    text += "- backups offsite/imutaveis;\n";
    text += "- conta sincronizada na nuvem;\n";
    text += "- logs e cache do roteador;\n";
    text += "- logs remotos do provedor/ISP;\n";
    text += "- snapshots remotos/imutaveis.\n";
    text += "\n## Checklist assistido\n\n";
    text += "### Roteador\n";
    text += "- Abra http://" + (gateway.empty() ? std::string("GATEWAY") : gateway) + " e entre como admin.\n";
    text += "- Limpe logs/cache, desative administracao remota e UPnP se nao usar.\n";
    text += "- Atualize o firmware e revise port forwarding.\n\n";
    text += "### OneDrive / Microsoft\n";
    text += "- account.live.com/consent e dispositivos; remova o dispositivo.\n";
    text += "- onedrive.live.com: exclua a copia na nuvem e a lixeira.\n\n";
    text += "### Google\n";
    text += "- myaccount.google.com/device-activity: remova sessoes.\n";
    text += "- drive.google.com: exclua backups/copias e a lixeira.\n\n";
    text += "### Dropbox / outros\n";
    text += "- dropbox.com/account/security: remova dispositivos e sessoes.\n";
    text += "- Snapshots/versoes do provedor: immutable nao sai sem suporte.\n\n";
    text += "### Logs remotos\n";
    text += "- ISP/cloud/provedor: somente via suporte/ordem legal; o binario nao alcanca.\n";
    text += "\nPara esses itens, use o checklist do provedor/roteador. O binario "
            "nao apaga nada remoto.\n";
    return text;
}

void write_audit(const std::wstring& out_dir, const std::vector<Finding>& findings,
                 const std::string& vss, const std::string& gateway) {
    art::ensure_directory(out_dir);
    HANDLE audit = CreateFileW(art::join_path(out_dir, L"exposure_audit.jsonl").c_str(),
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (audit != INVALID_HANDLE_VALUE) {
        std::string lines;
        for (const auto& finding : findings) {
            lines += "{\"category\":" + art::json_escape(finding.category) +
                ",\"path\":" + art::json_escape(finding.path) +
                ",\"bytes\":" + std::to_string(finding.bytes) +
                ",\"local_deletable\":" +
                (finding.local_deletable ? "true" : "false") +
                ",\"note\":" + art::json_escape(finding.note) +
                ",\"completed_utc\":" + art::json_escape(art::now_iso()) + "}\r\n";
        }
        lines += "{\"category\":\"gateway\",\"path\":" +
            art::json_escape(gateway) + ",\"bytes\":0,\"local_deletable\":false,"
            "\"note\":\"configuracao externa\"}\r\n";
        DWORD written = 0;
        WriteFile(audit, lines.data(), static_cast<DWORD>(lines.size()), &written,
                  nullptr);
        CloseHandle(audit);
    }
    const std::string markdown = build_markdown(findings, vss, gateway);
    HANDLE report = CreateFileW(art::join_path(out_dir, L"exposure_report.md").c_str(),
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (report != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(report, markdown.data(), static_cast<DWORD>(markdown.size()),
                  &written, nullptr);
        CloseHandle(report);
    }
}

bool remove_tree(const std::wstring& path, std::string& error, int depth = 0) {
    if (depth > 32) {
        error = "recursao excessiva";
        return false;
    }
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(art::join_path(path, L"*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") == 0 ||
                wcscmp(data.cFileName, L"..") == 0) continue;
            const std::wstring child = art::join_path(path, data.cFileName);
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                remove_tree(child, error, depth + 1);
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!RemoveDirectoryW(path.c_str())) {
        error = "RemoveDirectory falhou, erro " + std::to_string(GetLastError());
        return false;
    }
    return true;
}

} // namespace

int exposure_audit(const std::wstring& out_dir) {
    std::vector<Finding> findings;
    add_known_locations(findings);
    std::string vss;
    const bool vss_ok = run_capture(L"vssadmin.exe list shadows", vss);
    if (!vss_ok && vss.empty()) vss = "vssadmin indisponivel (admin necessario)";
    const std::string gateway = gateway_address();
    write_audit(out_dir, findings, vss, gateway);
    std::cout << "exposure: itens=" << findings.size()
              << " gateway=" << (gateway.empty() ? "?" : gateway)
              << " out=" << art::to_utf8(out_dir) << "\n";
    for (const auto& finding : findings)
        std::cout << "  " << finding.category << " " << finding.path << " ("
                  << human_size(finding.bytes) << ")\n";
    std::cout << "exposure: auditoria em " << art::to_utf8(out_dir) << "\n";
    return 0;
}

int clean_local_residue(const std::wstring& out_dir, bool confirm,
                        bool include_sync_cache) {
    if (!confirm) {
        std::cerr << "clean-local-residue: pass --confirm CLEAN-RESIDUE\n";
        return 2;
    }
    art::ensure_directory(out_dir);
    HANDLE audit = CreateFileW(art::join_path(out_dir,
        L"residue_clean.jsonl").c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    auto record = [&](const std::string& target, bool ok,
                      const std::string& detail) {
        const std::string line = "{\"target\":" + art::json_escape(target) +
            ",\"ok\":" + (ok ? "true" : "false") + ",\"detail\":" +
            art::json_escape(detail) + ",\"completed_utc\":" +
            art::json_escape(art::now_iso()) + "}\r\n";
        if (audit != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(audit, line.data(), static_cast<DWORD>(line.size()),
                      &written, nullptr);
        }
    };
    std::string vss_output;
    const bool vss_ok = run_capture(L"vssadmin.exe delete shadows /all /quiet",
                                   vss_output);
    record("shadow-copies", vss_ok, vss_ok ? "delete shadows executado"
                                           : vss_output);
    std::string error;
    for (const wchar_t* path : {L"C:\\Windows.old", L"C:\\$Windows.~BT",
                                L"C:\\$Windows.~WS"}) {
        if (!path_exists(path)) continue;
        const bool ok = remove_tree(path, error);
        record(art::to_utf8(path), ok, error);
    }
    std::vector<std::wstring> history_paths;
    history_paths.push_back(L"C:\\FileHistory");
    history_paths.push_back(art::join_path(local_appdata(),
        L"Microsoft\\Windows\\FileHistory"));
    for (const auto& path : history_paths) {
        if (!path_exists(path)) continue;
        const bool ok = remove_tree(path, error);
        record(art::to_utf8(path), ok, error);
    }
    HRESULT recycle = SHEmptyRecycleBinW(nullptr, nullptr,
        SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    std::string recycle_detail = "SHEmptyRecycleBin HRESULT=" +
        std::to_string(recycle);
    if (FAILED(recycle)) {
        const bool fallback = run_command(
            L"cmd.exe /c rd /s /q \"C:\\$Recycle.Bin\"");
        if (fallback) recycle = S_OK;
        recycle_detail += fallback ? "; fallback rd ok" : "; fallback rd failed";
    }
    record("recycle-bin", SUCCEEDED(recycle), recycle_detail);
    if (include_sync_cache) {
        const wchar_t* caches[] = {
            L"Google\\DriveFS", L"Microsoft\\OneDrive\\Cache",
            L"Dropbox\\.dropbox.cache"
        };
        for (const auto* cache : caches) {
            const std::wstring path = art::join_path(local_appdata(), cache);
            if (!path_exists(path)) continue;
            const bool ok = remove_tree(path, error);
            record(art::to_utf8(path), ok, error);
        }
    }
    if (audit != INVALID_HANDLE_VALUE) CloseHandle(audit);
    std::cout << "clean-local-residue: audit=" << art::to_utf8(out_dir) << "\n";
    std::cout << "clean-local-residue: backups offsite, nuvem, roteador e logs "
                 "remotos NAO foram apagados\n";
    return 0;
}

} // namespace deep
