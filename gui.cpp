// language: C++, file: gui.cpp, target: Windows 10/11
// Native Win32 GUI for the deep engine. It is organized as a left navigation
// rail (Pericia / Avancado / Sanitizacao / Auditoria) plus a persistent log.
// Every button builds the same CLI command line; the CLI remains the authority.

#include "gui.hpp"

#include <commdlg.h>
#include <shlobj.h>
#include <windows.h>


#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace deep {
namespace {

constexpr int ID_IMAGE = 1001;
constexpr int ID_OUT = 1002;
constexpr int ID_EVTX = 1003;
constexpr int ID_PREFETCH = 1004;
constexpr int ID_HIVE = 1005;
constexpr int ID_SRUM = 1006;
constexpr int ID_AFF4 = 1007;
constexpr int ID_E01 = 1008;
constexpr int ID_BROWSE_IMAGE = 1010;
constexpr int ID_BROWSE_OUT = 1011;
constexpr int ID_BROWSE_EVTX = 1012;
constexpr int ID_BROWSE_PREFETCH = 1013;
constexpr int ID_BROWSE_HIVE = 1014;
constexpr int ID_BROWSE_SRUM = 1015;
constexpr int ID_BROWSE_AFF4 = 1016;
constexpr int ID_BROWSE_E01 = 1017;
constexpr int ID_LIST = 1020;
constexpr int ID_RECOVER = 1021;
constexpr int ID_USN = 1022;
constexpr int ID_CARVE = 1023;
constexpr int ID_SECRETS = 1024;
constexpr int ID_SCAN = 1030;
constexpr int ID_ARTIFACTS = 1031;
constexpr int ID_IMAGE_CREATE = 1032;
constexpr int ID_AFF4_CREATE = 1033;
constexpr int ID_AFF4_VERIFY = 1034;
constexpr int ID_CLEAR = 1035;
constexpr int ID_E01_CREATE = 1036;
constexpr int ID_E01_SCAN = 1037;
constexpr int ID_EXPOSURE_AUDIT = 1038;
constexpr int ID_RESIDUE_CLEAN = 1039;

constexpr int ID_ADV_IMAGE = 1101;
constexpr int ID_ADV_OUT = 1102;
constexpr int ID_ADV_E01 = 1103;
constexpr int ID_ADV_VOLUME = 1104;
constexpr int ID_ADV_TARGET = 1105;
constexpr int ID_ADV_KEY = 1106;
constexpr int ID_ADV_COMMAND = 1107;
constexpr int ID_ADV_VERIFY_IMAGE = 1110;
constexpr int ID_ADV_VSS_LIST = 1111;
constexpr int ID_ADV_VSS_READ = 1112;
constexpr int ID_ADV_IMAGE_EVTX = 1113;
constexpr int ID_ADV_IMAGE_PREFETCH = 1114;
constexpr int ID_ADV_RECYCLE = 1115;
constexpr int ID_ADV_CASE_QUERY = 1116;
constexpr int ID_ADV_CASE_SUMMARY = 1117;
constexpr int ID_ADV_KEYGEN = 1118;
constexpr int ID_ADV_SIGN = 1119;
constexpr int ID_ADV_VERIFY_SIGN = 1120;
constexpr int ID_ADV_DEEP_USN = 1121;
constexpr int ID_ADV_LIST_DISKS = 1122;
constexpr int ID_ADV_RUN_COMMAND = 1123;

constexpr int ID_SAN_DISK = 1201;
constexpr int ID_SAN_SERIAL = 1202;
constexpr int ID_SAN_PASSWORD = 1203;
constexpr int ID_SAN_PASSES = 1204;
constexpr int ID_SAN_ACTION = 1205;
constexpr int ID_SAN_MODE = 1206;
constexpr int ID_SAN_LIST = 1210;
constexpr int ID_SAN_WIPE = 1211;
constexpr int ID_SAN_NVME = 1212;
constexpr int ID_SAN_ATA = 1213;
constexpr int ID_SAN_CLEAN = 1214;
constexpr int ID_SAN_OUT = 1215;

constexpr int ID_AUD_OUT = 1301;
constexpr int ID_AUD_AUDIT = 1302;
constexpr int ID_AUD_CLEAN = 1303;
constexpr int ID_WIPE_TARGET = 1501;
constexpr int ID_WIPE_OUT = 1502;
constexpr int ID_WIPE_MODE = 1503;
constexpr int ID_WIPE_FILE = 1510;
constexpr int ID_WIPE_FREE = 1511;
constexpr int ID_WIPE_BROWSER = 1512;
constexpr int ID_WIPE_HIVE = 1513;
constexpr int ID_WIPE_JOURNAL = 1514;
constexpr int ID_WIPE_ARTIFACTS = 1515;
constexpr int ID_PURGE_USN = 1516;
constexpr int ID_CLEAR_LOGS = 1517;
constexpr int ID_DELETE_VSS = 1518;
constexpr int ID_KERNEL_DISK = 1601;
constexpr int ID_KERNEL_OFFSET = 1602;
constexpr int ID_KERNEL_LENGTH = 1603;
constexpr int ID_KERNEL_OUT = 1604;
constexpr int ID_KERNEL_VERSION = 1610;
constexpr int ID_KERNEL_READ = 1611;
constexpr int ID_KERNEL_HELP = 1612;
constexpr int ID_NAV = 1400;
constexpr int ID_MODE = 1401;
constexpr UINT WM_APP_DONE = WM_APP + 1;

HWND g_image = nullptr;
HWND g_out = nullptr;
HWND g_evtx = nullptr;
HWND g_prefetch = nullptr;
HWND g_hive = nullptr;
HWND g_srum = nullptr;
HWND g_aff4 = nullptr;
HWND g_e01 = nullptr;
HWND g_log = nullptr;
HWND g_status = nullptr;
HWND g_nav = nullptr;
HWND g_mode = nullptr;
HWND g_adv_image = nullptr;
HWND g_adv_out = nullptr;
HWND g_adv_e01 = nullptr;
HWND g_adv_volume = nullptr;
HWND g_adv_target = nullptr;
HWND g_adv_key = nullptr;
HWND g_adv_command = nullptr;
HWND g_san_disk = nullptr;
HWND g_san_serial = nullptr;
HWND g_san_password = nullptr;
HWND g_san_passes = nullptr;
HWND g_san_action = nullptr;
HWND g_san_out = nullptr;
HWND g_aud_out = nullptr;
HWND g_wipe_target = nullptr;
HWND g_wipe_out = nullptr;
HWND g_kernel_disk = nullptr;
HWND g_kernel_offset = nullptr;
HWND g_kernel_length = nullptr;
HWND g_kernel_out = nullptr;
std::vector<HWND> g_buttons;
std::vector<HWND> g_forensic;
std::vector<HWND> g_advanced;
std::vector<HWND> g_sanitize;
std::vector<HWND> g_audit;
std::vector<HWND> g_wipe;
std::vector<HWND> g_kernel;
int g_category = 0;
HBRUSH g_bg = nullptr;
HBRUSH g_panel = nullptr;
HBRUSH g_edit_bg = nullptr;
HFONT g_font = nullptr;
HFONT g_font_title = nullptr;
std::vector<std::pair<HWND, RECT>> g_geometry;

std::wstring get_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, &text[0], length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

void append_log(const std::wstring& text) {
    const int length = GetWindowTextLengthW(g_log);
    if (length > 2 * 1024 * 1024) SetWindowTextW(g_log, L"");
    SendMessageW(g_log, EM_SETSEL, static_cast<WPARAM>(length),
                 static_cast<LPARAM>(length));
    SendMessageW(g_log, EM_REPLACESEL, FALSE,
                 reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
}

void set_running(bool running) {
    for (HWND button : g_buttons)
        EnableWindow(button, running ? FALSE : TRUE);
    SetWindowTextW(g_status, running ? L"Executando..." : L"Pronto.");
}

bool browse_file(HWND owner, HWND target, const wchar_t* filter) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return false;
    SetWindowTextW(target, file);
    return true;
}

bool browse_folder(HWND owner, HWND target) {
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.lpszTitle = L"Selecione a pasta";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (item == nullptr) return false;
    wchar_t path[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(item, path) != FALSE;
    CoTaskMemFree(item);
    if (ok) SetWindowTextW(target, path);
    return ok;
}

bool checked(HWND window, int id) {
    return SendMessageW(GetDlgItem(window, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

std::wstring cli_executable() {
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring path(self);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        const std::wstring candidate = path.substr(0, slash + 1) +
            L"OlhoDeDeus_deep.exe";
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            return candidate;
    }
    return path;
}

bool is_destructive(const std::wstring& command) {
    std::wstring lower;
    lower.reserve(command.size());
    for (wchar_t c : command) lower.push_back(static_cast<wchar_t>(towlower(c)));
    return lower.find(L"--wipe") != std::wstring::npos ||
        lower.find(L"--sanitize-disk") != std::wstring::npos ||
        lower.find(L"--nvme-sanitize") != std::wstring::npos ||
        lower.find(L"--ata-erase") != std::wstring::npos ||
        lower.find(L"--clean-local-residue") != std::wstring::npos ||
        lower.find(L"--purge-usn") != std::wstring::npos ||
        lower.find(L"--clear-logs") != std::wstring::npos ||
        lower.find(L"--delete-vss") != std::wstring::npos;
}

bool confirm_destructive(HWND window, const wchar_t* what) {
    return MessageBoxW(window, what, L"Olho de Deus - confirmacao",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

std::wstring build_command(HWND window, int action) {
    const std::wstring executable = cli_executable();
    std::wstring command = quote(executable);
    const std::wstring image = get_text(g_image);
    const std::wstring out = get_text(g_out);
    const std::wstring evtx = get_text(g_evtx);
    const std::wstring prefetch = get_text(g_prefetch);
    const std::wstring hive = get_text(g_hive);
    const std::wstring srum = get_text(g_srum);
    const std::wstring aff4 = get_text(g_aff4);
    const std::wstring e01 = get_text(g_e01);
    if (action == ID_SCAN) {
        if (image.empty() || out.empty()) {
            MessageBoxW(window, L"Informe a imagem e a pasta de saida.",
                        L"Olho de Deus", MB_ICONWARNING);
            return {};
        }
        command += L" --deep-scan " + quote(image) + L" --out " + quote(out);
        if (checked(window, ID_LIST)) command += L" --list";
        if (checked(window, ID_RECOVER)) command += L" --recover";
        if (checked(window, ID_USN)) command += L" --usn";
        if (checked(window, ID_CARVE)) command += L" --carve";
        return command;
    }
    if (action == ID_ARTIFACTS) {
        if (out.empty()) {
            MessageBoxW(window, L"Informe a pasta de saida.", L"Olho de Deus",
                        MB_ICONWARNING);
            return {};
        }
        command += L" --artifacts --out " + quote(out);
        if (!evtx.empty()) command += L" --evtx-file " + quote(evtx);
        if (!prefetch.empty()) command += L" --prefetch-dir " + quote(prefetch);
        if (!hive.empty()) command += L" --hive-file " + quote(hive);
        if (!srum.empty()) command += L" --srum-db " + quote(srum);
        if (checked(window, ID_SECRETS)) command += L" --browser-secrets";
        return command;
    }
    if (action == ID_IMAGE_CREATE) {
        if (image.empty() || out.empty()) return {};
        command += L" --image-create " + quote(image) + L" --out " +
            quote(out + L"\\image.raw");
        return command;
    }
    if (action == ID_AFF4_CREATE) {
        if (image.empty() || out.empty()) return {};
        command += L" --aff4-zip " + quote(image) + L" --out " +
            quote(out + L"\\image.aff4");
        return command;
    }
    if (action == ID_AFF4_VERIFY) {
        if (aff4.empty()) {
            MessageBoxW(window, L"Informe o arquivo .aff4.", L"Olho de Deus",
                        MB_ICONWARNING);
            return {};
        }
        command += L" --aff4-zip-verify " + quote(aff4);
        return command;
    }
    if (action == ID_E01_CREATE) {
        if (image.empty() || out.empty()) return {};
        command += L" --e01-create " + quote(image) + L" --out " +
            quote(out + L"\\image");
        return command;
    }
    if (action == ID_E01_SCAN) {
        if (e01.empty() || out.empty()) return {};
        command += L" --e01-scan " + quote(e01) + L" --out " + quote(out);
        return command;
    }
    if (action == ID_EXPOSURE_AUDIT || action == ID_AUD_AUDIT) {
        const std::wstring target = action == ID_AUD_AUDIT ? get_text(g_aud_out) : out;
        if (target.empty()) return {};
        command += L" --exposure-audit --out " + quote(target);
        return command;
    }
    if (action == ID_RESIDUE_CLEAN || action == ID_AUD_CLEAN || action == ID_SAN_CLEAN) {
        const std::wstring target = action == ID_AUD_CLEAN ?
            get_text(g_aud_out) : (action == ID_SAN_CLEAN ?
            get_text(g_san_out) : out);
        if (target.empty()) return {};
        command += L" --mode sanitize --clean-local-residue --out " +
            quote(target) + L" --confirm CLEAN-RESIDUE";
        return command;
    }

    const std::wstring adv_image = get_text(g_adv_image);
    const std::wstring adv_out = get_text(g_adv_out);
    const std::wstring adv_e01 = get_text(g_adv_e01);
    const std::wstring adv_volume = get_text(g_adv_volume);
    const std::wstring adv_target = get_text(g_adv_target);
    const std::wstring adv_key = get_text(g_adv_key);
    if (action == ID_ADV_VERIFY_IMAGE) {
        if (adv_image.empty()) return {};
        command += L" --image-verify " + quote(adv_image);
        return command;
    }
    if (action == ID_ADV_VSS_LIST) {
        command += L" --vss-list";
        return command;
    }
    if (action == ID_ADV_VSS_READ) {
        if (adv_volume.empty() || adv_out.empty()) return {};
        command += L" --vss-read " + quote(adv_volume) + L" --out " +
            quote(adv_out + L"\\vss");
        return command;
    }
    if (action == ID_ADV_IMAGE_EVTX) {
        if (adv_image.empty() || adv_out.empty()) return {};
        command += L" --image-evtx " + quote(adv_image) + L" --out " +
            quote(adv_out);
        return command;
    }
    if (action == ID_ADV_IMAGE_PREFETCH) {
        if (adv_image.empty() || adv_out.empty()) return {};
        command += L" --image-prefetch " + quote(adv_image) + L" --out " +
            quote(adv_out);
        return command;
    }
    if (action == ID_ADV_RECYCLE) {
        if (adv_target.empty() || adv_out.empty()) return {};
        command += L" --recycle-recover " + quote(adv_target) + L" --out " +
            quote(adv_out);
        return command;
    }
    if (action == ID_ADV_CASE_QUERY) {
        if (adv_target.empty()) return {};
        command += L" --case-query " + quote(adv_target);
        return command;
    }
    if (action == ID_ADV_CASE_SUMMARY) {
        if (adv_target.empty()) return {};
        command += L" --case-summary " + quote(adv_target);
        return command;
    }
    if (action == ID_ADV_KEYGEN) {
        if (adv_key.empty()) return {};
        command += L" --keygen " + quote(adv_key);
        return command;
    }
    if (action == ID_ADV_SIGN) {
        if (adv_target.empty() || adv_key.empty()) return {};
        command += L" --sign-manifest " + quote(adv_target) + L" --key " +
            quote(adv_key);
        return command;
    }
    if (action == ID_ADV_VERIFY_SIGN) {
        if (adv_target.empty() || adv_key.empty()) return {};
        command += L" --verify-manifest " + quote(adv_target) + L" --key " +
            quote(adv_key);
        return command;
    }
    if (action == ID_ADV_DEEP_USN) {
        if (adv_target.empty() || adv_out.empty()) return {};
        command += L" --deep-usn-file " + quote(adv_target) + L" --out " +
            quote(adv_out);
        return command;
    }
    if (action == ID_ADV_LIST_DISKS) {
        command += L" --list-disks";
        return command;
    }
    if (action == ID_ADV_RUN_COMMAND) {
        const std::wstring raw = get_text(g_adv_command);
        if (raw.empty()) return {};
        command += L" " + raw;
        return command;
    }

    if (action == ID_SAN_LIST) {
        command += L" --list-disks";
        return command;
    }
    const std::wstring disk = get_text(g_san_disk);
    const std::wstring serial = get_text(g_san_serial);
    const std::wstring password = get_text(g_san_password);
    const std::wstring passes = get_text(g_san_passes);
    const std::wstring mode = get_text(g_san_action);
    const std::wstring san_out = get_text(g_san_out);
    if (action == ID_SAN_WIPE) {
        if (disk.empty() || serial.empty()) return {};
        command += L" --mode sanitize --sanitize-disk " + disk +
            L" --serial " + quote(serial);
        if (!san_out.empty()) command += L" --out " + quote(san_out);
        if (!passes.empty()) command += L" --passes " + passes;
        if (checked(window, ID_SAN_MODE)) command += L" --verify";
        command += L" --confirm WIPE-DISK";
        return command;
    }
    if (action == ID_SAN_NVME) {
        if (disk.empty() || serial.empty()) return {};
        command += L" --mode sanitize --nvme-sanitize " + disk +
            L" --serial " + quote(serial);
        command += L" --action " + (mode.empty() ? L"block" : mode);
        command += L" --confirm NVME-SANITIZE";
        return command;
    }
    const std::wstring wipe_target = get_text(g_wipe_target);
    const std::wstring wipe_out = get_text(g_wipe_out);
    if (action == ID_WIPE_FILE || action == ID_WIPE_FREE) {
        if (wipe_target.empty() || wipe_out.empty()) return {};
        command += L" --mode sanitize " +
            (action == ID_WIPE_FILE ? std::wstring(L"--wipe-file ") :
                                      std::wstring(L"--wipe-free ")) +
            quote(wipe_target) + L" --out " + quote(wipe_out) +
            L" --confirm WIPE";
        return command;
    }
    if (action == ID_WIPE_BROWSER) {
        if (wipe_target.empty() || wipe_out.empty()) return {};
        command += L" --mode sanitize --wipe-browser --browser-profile " +
            quote(wipe_target) + L" --out " + quote(wipe_out) +
            L" --confirm WIPE";
        return command;
    }
    if (action == ID_WIPE_HIVE) {
        if (wipe_target.empty() || wipe_out.empty()) return {};
        command += L" --mode sanitize --wipe-hive " + quote(wipe_target) +
            L" --out " + quote(wipe_out) + L" --confirm WIPE";
        return command;
    }
    if (action == ID_WIPE_JOURNAL) {
        if (wipe_target.empty() || wipe_out.empty()) return {};
        command += L" --mode sanitize --wipe-journal " + quote(wipe_target) +
            L" --out " + quote(wipe_out) + L" --confirm WIPE";
        return command;
    }
    if (action == ID_WIPE_ARTIFACTS) {
        command += L" --mode sanitize --wipe-artifacts --confirm WIPE";
        return command;
    }
    if (action == ID_PURGE_USN) {
        if (wipe_target.empty()) return {};
        command += L" --mode sanitize --purge-usn " + quote(wipe_target) +
            L" --confirm WIPE";
        return command;
    }
    if (action == ID_CLEAR_LOGS) {
        command += L" --mode sanitize --clear-logs --confirm WIPE";
        return command;
    }
    if (action == ID_DELETE_VSS) {
        command += L" --mode sanitize --delete-vss --confirm WIPE";
        return command;
    }
    if (action == ID_KERNEL_VERSION) {
        command += L" --kernel-version";
        return command;
    }
    if (action == ID_KERNEL_HELP) {
        command += L" --deep-help";
        return command;
    }
    if (action == ID_KERNEL_READ) {
        const std::wstring kdisk = get_text(g_kernel_disk);
        const std::wstring koffset = get_text(g_kernel_offset);
        const std::wstring klength = get_text(g_kernel_length);
        const std::wstring kout = get_text(g_kernel_out);
        if (kdisk.empty() || klength.empty() || kout.empty()) return {};
        command += L" --kernel-read " + kdisk + L" --offset " +
            (koffset.empty() ? L"0" : koffset) + L" --length " + klength +
            L" --out " + quote(kout);
        return command;
    }
    if (action == ID_SAN_ATA) {
        if (disk.empty() || serial.empty()) return {};
        command += L" --mode sanitize --ata-erase " + disk +
            L" --serial " + quote(serial);
        if (!password.empty()) command += L" --password " + quote(password);
        if (checked(window, ID_SAN_MODE)) command += L" --enhanced";
        command += L" --confirm ATA-ERASE";
        return command;
    }
    return {};
}

struct Job {
    HWND window = nullptr;
    std::wstring command;
};

DWORD WINAPI worker(void* parameter) {
    auto* job = static_cast<Job*>(parameter);
    wchar_t temp_path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp_path);
    const std::wstring log_path = std::wstring(temp_path) + L"eog_gui_" +
        std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::to_wstring(GetTickCount64()) + L".log";
    HANDLE output = CreateFileW(log_path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::wstring mutable_command = job->command;
    const BOOL created = CreateProcessW(nullptr, mutable_command.data(), nullptr,
        nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    std::wstring text;
    if (!created) {
        text = L"Falha ao criar o processo: " + std::to_wstring(GetLastError()) +
            L"\r\n";
    } else {
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
        HANDLE input = CreateFileW(log_path.c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (input != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size{};
            GetFileSizeEx(input, &size);
            std::string raw(static_cast<std::size_t>(size.QuadPart), '\0');
            DWORD read = 0;
            if (!raw.empty())
                ReadFile(input, raw.data(), static_cast<DWORD>(raw.size()), &read,
                         nullptr);
            raw.resize(read);
            int wide = MultiByteToWideChar(CP_UTF8, 0, raw.data(),
                static_cast<int>(raw.size()), nullptr, 0);
            if (wide > 0) {
                text.resize(static_cast<std::size_t>(wide));
                MultiByteToWideChar(CP_UTF8, 0, raw.data(),
                    static_cast<int>(raw.size()), &text[0], wide);
            } else {
                wide = MultiByteToWideChar(CP_ACP, 0, raw.data(),
                    static_cast<int>(raw.size()), nullptr, 0);
                text.resize(static_cast<std::size_t>(wide));
                MultiByteToWideChar(CP_ACP, 0, raw.data(),
                    static_cast<int>(raw.size()), &text[0], wide);
            }
            CloseHandle(input);
        }
        text += L"\r\n[exit code: " + std::to_wstring(code) + L"]\r\n";
    }
    DeleteFileW(log_path.c_str());
    PostMessageW(job->window, WM_APP_DONE, 0,
                 reinterpret_cast<LPARAM>(new std::wstring(text)));
    delete job;
    return 0;
}

void start_job(HWND window, int action) {
    const std::wstring command = build_command(window, action);
    if (command.empty()) return;
    append_log(L"> " + command + L"\r\n");
    set_running(true);
    auto* job = new Job{window, command};
    HANDLE thread = CreateThread(nullptr, 0, worker, job, 0, nullptr);
    if (thread == nullptr) {
        delete job;
        set_running(false);
        append_log(L"Falha ao criar thread.\r\n");
        return;
    }
    CloseHandle(thread);
}

void show_category(int index) {
    g_category = index;
    const std::vector<HWND>* groups[] = {&g_forensic, &g_advanced, &g_sanitize,
                                         &g_audit, &g_wipe, &g_kernel};
    for (int i = 0; i < 6; ++i)
        for (HWND control : *groups[i])
            ShowWindow(control, i == index ? SW_SHOW : SW_HIDE);
    if (g_nav != nullptr) SendMessageW(g_nav, LB_SETCURSEL, index, 0);
    if (g_mode != nullptr)
        SetWindowTextW(g_mode, (index == 2 || index == 4) ? L"Modo: SANITIZACAO"
                                          : L"Modo: PERICIA");
}

HWND make_control(HWND window, const wchar_t* cls, const wchar_t* text,
                  DWORD style, int x, int y, int w, int h, int id,
                  std::vector<HWND>& group) {
    HWND control = CreateWindowExW(
        (wcscmp(cls, L"EDIT") == 0) ? WS_EX_CLIENTEDGE : 0, cls, text,
        WS_CHILD | style, x, y, w, h, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)),
        nullptr);
    SendMessageW(control, WM_SETFONT,
        reinterpret_cast<WPARAM>(g_font ? g_font : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    g_geometry.push_back({control, RECT{x, y, x + w, y + h}});
    group.push_back(control);
    return control;
}

void layout_controls(HWND window) {
    if (g_log == nullptr || g_status == nullptr) return;
    RECT client{};
    GetClientRect(window, &client);
    const int width = (std::max)(1100, static_cast<int>(client.right));
    const int height = (std::max)(760, static_cast<int>(client.bottom));
    const double scale = (std::min)(1.55, (std::max)(1.0,
        (std::min)(width / 1100.0, height / 780.0)));
    int max_bottom = 480;
    for (const auto& entry : g_geometry) {
        RECT desired{
            static_cast<LONG>(entry.second.left * scale),
            static_cast<LONG>(entry.second.top * scale),
            static_cast<LONG>(entry.second.right * scale),
            static_cast<LONG>(entry.second.bottom * scale)
        };
        const int w = desired.right - desired.left;
        const int h = desired.bottom - desired.top;
        MoveWindow(entry.first, desired.left, desired.top, w, h, TRUE);
        if (desired.bottom > max_bottom) max_bottom = desired.bottom;
    }
    const int status_y = (std::min)(height - 90, max_bottom + 18);
    const int log_y = status_y + 26;
    if (g_nav != nullptr)
        MoveWindow(g_nav, 12, 56, static_cast<int>(170 * scale),
                   (std::max)(160, height - 70), TRUE);
    MoveWindow(g_status, static_cast<int>(196 * scale), status_y,
               (std::max)(400, width - static_cast<int>(196 * scale) - 16), 24,
               TRUE);
    MoveWindow(g_log, static_cast<int>(196 * scale), log_y,
               (std::max)(400, width - static_cast<int>(196 * scale) - 16),
               (std::max)(100, height - log_y - 12), TRUE);
    const int edit_ids[] = {
        ID_IMAGE, ID_OUT, ID_EVTX, ID_PREFETCH, ID_HIVE, ID_SRUM, ID_AFF4,
        ID_E01, ID_ADV_IMAGE, ID_ADV_OUT, ID_ADV_E01, ID_ADV_VOLUME,
        ID_ADV_TARGET, ID_ADV_KEY, ID_ADV_COMMAND, ID_SAN_DISK,
        ID_SAN_SERIAL, ID_SAN_PASSWORD, ID_SAN_PASSES, ID_SAN_ACTION,
        ID_SAN_OUT, ID_AUD_OUT, ID_WIPE_TARGET, ID_WIPE_OUT, ID_KERNEL_DISK,
        ID_KERNEL_OFFSET, ID_KERNEL_LENGTH, ID_KERNEL_OUT
    };
    for (int id : edit_ids) {
        HWND control = GetDlgItem(window, id);
        if (control == nullptr) continue;
        RECT rect{};
        GetWindowRect(control, &rect);
        POINT point{rect.left, rect.top};
        ScreenToClient(window, &point);
        const int control_height = rect.bottom - rect.top;
        MoveWindow(control, point.x, point.y,
                   (std::max)(240, width - static_cast<int>(point.x) - 170), control_height, TRUE);
    }
    for (int id = ID_BROWSE_IMAGE; id <= ID_BROWSE_E01; ++id) {
        HWND control = GetDlgItem(window, id);
        if (control == nullptr) continue;
        RECT rect{};
        GetWindowRect(control, &rect);
        POINT point{rect.left, rect.top};
        ScreenToClient(window, &point);
        MoveWindow(control, width - 140, point.y, 124, 30, TRUE);
    }
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam,
                             LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(reinterpret_cast<HDC>(wparam), &rect,
                 g_bg ? g_bg : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, RGB(220, 230, 238));
        SetBkColor(dc, RGB(10, 14, 18));
        return reinterpret_cast<LRESULT>(g_bg);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, RGB(230, 240, 248));
        SetBkColor(dc, RGB(16, 23, 30));
        return reinterpret_cast<LRESULT>(g_edit_bg ? g_edit_bg : g_panel);
    }
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, RGB(220, 230, 238));
        SetBkColor(dc, RGB(16, 23, 30));
        return reinterpret_cast<LRESULT>(g_edit_bg ? g_edit_bg : g_panel);
    }
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (item == nullptr) break;
        if (item->CtlType != ODT_BUTTON) break;
        const bool pressed = (item->itemState & ODS_SELECTED) != 0;
        const bool disabled = (item->itemState & ODS_DISABLED) != 0;
        const COLORREF fill = disabled ? RGB(28, 34, 40) :
            (pressed ? RGB(26, 92, 72) : RGB(22, 31, 40));
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(item->hDC, &item->rcItem, brush);
        DeleteObject(brush);
        FrameRect(item->hDC, &item->rcItem,
                  reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, disabled ? RGB(150, 160, 170) : RGB(255, 255, 255));
        wchar_t text[256] = {};
        GetWindowTextW(item->hwndItem, text, 256);
        DrawTextW(item->hDC, text, -1, &item->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return TRUE;
    }
    case WM_SIZE:
        layout_controls(window);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = 1100;
        info->ptMinTrackSize.y = 760;
        return 0;
    }
    case WM_CREATE: {
        const HINSTANCE instance =
            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
        HWND title = CreateWindowW(L"STATIC", L"OLHO DE DEUS  |  DEEP ENGINE",
            WS_CHILD | WS_VISIBLE, 14, 10, 760, 30, window, nullptr, instance,
            nullptr);
        SendMessageW(title, WM_SETFONT,
            reinterpret_cast<WPARAM>(g_font_title ? g_font_title : g_font), TRUE);
        g_mode = CreateWindowW(L"STATIC", L"Modo: PERICIA", WS_CHILD | WS_VISIBLE,
            820, 14, 250, 22, window, nullptr, instance, nullptr);
        SendMessageW(g_mode, WM_SETFONT,
            reinterpret_cast<WPARAM>(g_font ? g_font : GetStockObject(DEFAULT_GUI_FONT)), TRUE);

        g_nav = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 12, 56, 170, 320,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_NAV)),
            instance, nullptr);
        SendMessageW(g_nav, WM_SETFONT,
            reinterpret_cast<WPARAM>(g_font ? g_font : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        for (const wchar_t* item : {L"Pericia", L"Avancado", L"Sanitizacao",
                                    L"Auditoria", L"Wipe", L"Kernel"})
            SendMessageW(g_nav, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(item));
        SendMessageW(g_nav, LB_SETCURSEL, 0, 0);

        const int x = 196;
        const int label_w = 106;
        const int edit_x = 308;
        const int edit_w = 640;
        const int browse_x = 958;
        auto label = [&](std::vector<HWND>& group, int y, const wchar_t* text) {
            make_control(window, L"STATIC", text, WS_VISIBLE, x, y, label_w, 22,
                         -1, group);
        };
        auto edit = [&](std::vector<HWND>& group, int id, int y) {
            return make_control(window, L"EDIT", L"", WS_VISIBLE | ES_AUTOHSCROLL,
                                edit_x, y, edit_w, 24, id, group);
        };
        auto browse = [&](std::vector<HWND>& group, int id, int y) {
            make_control(window, L"BUTTON", L"Escolher...", WS_VISIBLE | BS_OWNERDRAW,
                         browse_x, y, 100, 24, id, group);
        };
        auto button = [&](std::vector<HWND>& group, int id, int bx, int by,
                          int bw, const wchar_t* text) {
            HWND handle = make_control(window, L"BUTTON", text,
                WS_VISIBLE | BS_OWNERDRAW, bx, by, bw, 28, id, group);
            g_buttons.push_back(handle);
        };
        auto check = [&](std::vector<HWND>& group, int id, int cx, int cy,
                         const wchar_t* text) {
            make_control(window, L"BUTTON", text,
                WS_VISIBLE | BS_AUTOCHECKBOX, cx, cy, 140, 24, id, group);
        };
        auto field = [&](std::vector<HWND>& group, int id, int y,
                         const wchar_t* text) {
            label(group, y, text);
            return edit(group, id, y);
        };

        // Pericia
        g_image = field(g_forensic, ID_IMAGE, 62, L"Imagem/disco:");
        browse(g_forensic, ID_BROWSE_IMAGE, 62);
        g_out = field(g_forensic, ID_OUT, 94, L"Pasta de saida:");
        browse(g_forensic, ID_BROWSE_OUT, 94);
        g_evtx = field(g_forensic, ID_EVTX, 126, L"EVTX:");
        browse(g_forensic, ID_BROWSE_EVTX, 126);
        g_prefetch = field(g_forensic, ID_PREFETCH, 158, L"Prefetch dir:");
        browse(g_forensic, ID_BROWSE_PREFETCH, 158);
        g_hive = field(g_forensic, ID_HIVE, 190, L"Hive:");
        browse(g_forensic, ID_BROWSE_HIVE, 190);
        g_srum = field(g_forensic, ID_SRUM, 222, L"SRUM:");
        browse(g_forensic, ID_BROWSE_SRUM, 222);
        g_aff4 = field(g_forensic, ID_AFF4, 254, L"AFF4:");
        browse(g_forensic, ID_BROWSE_AFF4, 254);
        g_e01 = field(g_forensic, ID_E01, 286, L"E01:");
        browse(g_forensic, ID_BROWSE_E01, 286);
        check(g_forensic, ID_LIST, 308, 318, L"Listar MFT");
        check(g_forensic, ID_RECOVER, 430, 318, L"Recuperar");
        check(g_forensic, ID_USN, 552, 318, L"USN");
        check(g_forensic, ID_CARVE, 674, 318, L"Carving");
        check(g_forensic, ID_SECRETS, 796, 318, L"Browser secrets");
        button(g_forensic, ID_SCAN, 308, 350, 152, L"Escanear imagem");
        button(g_forensic, ID_ARTIFACTS, 468, 350, 152, L"Coletar artefatos");
        button(g_forensic, ID_IMAGE_CREATE, 628, 350, 152, L"Criar raw");
        button(g_forensic, ID_AFF4_CREATE, 788, 350, 152, L"Criar AFF4");
        button(g_forensic, ID_AFF4_VERIFY, 308, 386, 152, L"Verificar AFF4");
        button(g_forensic, ID_E01_CREATE, 468, 386, 152, L"Criar E01");
        button(g_forensic, ID_E01_SCAN, 628, 386, 152, L"Escanear E01");
        button(g_forensic, ID_CLEAR, 788, 386, 152, L"Limpar log");

        // Avancado
        g_adv_image = field(g_advanced, ID_ADV_IMAGE, 62, L"Imagem:");
        g_adv_out = field(g_advanced, ID_ADV_OUT, 94, L"Pasta de saida:");
        g_adv_e01 = field(g_advanced, ID_ADV_E01, 126, L"E01:");
        g_adv_volume = field(g_advanced, ID_ADV_VOLUME, 158, L"Volume VSS:");
        g_adv_target = field(g_advanced, ID_ADV_TARGET, 190, L"Alvo/caso:");
        g_adv_key = field(g_advanced, ID_ADV_KEY, 222, L"Chave .priv/.pub:");
        label(g_advanced, 254, L"Comando CLI:");
        g_adv_command = make_control(window, L"EDIT", L"",
            WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, edit_x,
            254, edit_w, 64, ID_ADV_COMMAND, g_advanced);
        button(g_advanced, ID_ADV_VERIFY_IMAGE, 308, 330, 150, L"Verificar imagem");
        button(g_advanced, ID_ADV_VSS_LIST, 468, 330, 150, L"VSS list");
        button(g_advanced, ID_ADV_VSS_READ, 628, 330, 150, L"VSS read");
        button(g_advanced, ID_ADV_IMAGE_EVTX, 788, 330, 150, L"EVTX da imagem");
        button(g_advanced, ID_ADV_IMAGE_PREFETCH, 308, 366, 150, L"Prefetch da imagem");
        button(g_advanced, ID_ADV_RECYCLE, 468, 366, 150, L"Recover recycle");
        button(g_advanced, ID_ADV_CASE_QUERY, 628, 366, 150, L"Case query");
        button(g_advanced, ID_ADV_CASE_SUMMARY, 788, 366, 150, L"Case summary");
        button(g_advanced, ID_ADV_KEYGEN, 308, 402, 150, L"Keygen");
        button(g_advanced, ID_ADV_SIGN, 468, 402, 150, L"Assinar manifesto");
        button(g_advanced, ID_ADV_VERIFY_SIGN, 628, 402, 150, L"Verificar assinatura");
        button(g_advanced, ID_ADV_DEEP_USN, 788, 402, 150, L"Deep USN file");
        button(g_advanced, ID_ADV_LIST_DISKS, 308, 438, 150, L"Listar discos");
        button(g_advanced, ID_ADV_RUN_COMMAND, 468, 438, 310, L"Executar comando CLI");

        // Sanitizacao
        g_san_disk = field(g_sanitize, ID_SAN_DISK, 62, L"Disco (numero):");
        g_san_serial = field(g_sanitize, ID_SAN_SERIAL, 94, L"Serial exato:");
        g_san_password = field(g_sanitize, ID_SAN_PASSWORD, 126, L"Senha ATA (opcional):");
        g_san_passes = field(g_sanitize, ID_SAN_PASSES, 158, L"Passes (1-7):");
        g_san_action = field(g_sanitize, ID_SAN_ACTION, 190, L"NVMe action:");
        g_san_out = field(g_sanitize, ID_SAN_OUT, 222, L"Pasta de saida:");
        check(g_sanitize, ID_SAN_MODE, 308, 254,
              L"Confirmo modo sanitizacao / verify / enhanced");
        button(g_sanitize, ID_SAN_LIST, 308, 290, 150, L"Listar discos");
        button(g_sanitize, ID_SAN_WIPE, 468, 290, 170, L"Sanitizar disco");
        button(g_sanitize, ID_SAN_NVME, 648, 290, 170, L"NVMe sanitize");
        button(g_sanitize, ID_SAN_ATA, 828, 290, 170, L"ATA secure erase");
        button(g_sanitize, ID_SAN_CLEAN, 308, 326, 200, L"Limpar residuos locais");

        // Auditoria
        g_aud_out = field(g_audit, ID_AUD_OUT, 62, L"Pasta de saida:");
        button(g_audit, ID_AUD_AUDIT, 308, 100, 190, L"Auditar exposicao");
        button(g_audit, ID_AUD_CLEAN, 508, 100, 190, L"Limpar residuos");

        // Wipe
        g_wipe_target = field(g_wipe, ID_WIPE_TARGET, 62, L"Alvo/perfil:");
        g_wipe_out = field(g_wipe, ID_WIPE_OUT, 94, L"Pasta de saida:");
        check(g_wipe, ID_WIPE_MODE, 308, 126,
              L"Confirmo modo sanitizacao");
        button(g_wipe, ID_WIPE_FILE, 308, 158, 150, L"Wipe arquivo");
        button(g_wipe, ID_WIPE_FREE, 468, 158, 150, L"Wipe espaco livre");
        button(g_wipe, ID_WIPE_BROWSER, 628, 158, 150, L"Wipe browser");
        button(g_wipe, ID_WIPE_HIVE, 788, 158, 150, L"Wipe hive");
        button(g_wipe, ID_WIPE_JOURNAL, 308, 194, 150, L"Wipe journal");
        button(g_wipe, ID_WIPE_ARTIFACTS, 468, 194, 150, L"Wipe artefatos");
        button(g_wipe, ID_PURGE_USN, 628, 194, 150, L"Purge USN");
        button(g_wipe, ID_CLEAR_LOGS, 788, 194, 150, L"Limpar logs");
        button(g_wipe, ID_DELETE_VSS, 308, 230, 150, L"Apagar VSS");

        // Kernel
        g_kernel_disk = field(g_kernel, ID_KERNEL_DISK, 62, L"Disco:");
        g_kernel_offset = field(g_kernel, ID_KERNEL_OFFSET, 94, L"Offset:");
        g_kernel_length = field(g_kernel, ID_KERNEL_LENGTH, 126, L"Tamanho:");
        g_kernel_out = field(g_kernel, ID_KERNEL_OUT, 158, L"Arquivo de saida:");
        button(g_kernel, ID_KERNEL_VERSION, 308, 194, 190, L"Versao do driver");
        button(g_kernel, ID_KERNEL_READ, 498, 194, 190, L"Ler raw via kernel");
        button(g_kernel, ID_KERNEL_HELP, 688, 194, 190, L"Ajuda CLI completa");

        g_status = CreateWindowW(L"STATIC", L"Pronto.", WS_CHILD | WS_VISIBLE,
            196, 480, 840, 22, window, nullptr, instance, nullptr);
        SendMessageW(g_status, WM_SETFONT,
            reinterpret_cast<WPARAM>(g_font ? g_font : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        g_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_READONLY | WS_VSCROLL, 196, 506, 862, 236, window, nullptr,
            instance, nullptr);
        SendMessageW(g_log, WM_SETFONT,
            reinterpret_cast<WPARAM>(g_font ? g_font : GetStockObject(DEFAULT_GUI_FONT)), TRUE);

        show_category(0);
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int code = HIWORD(wparam);
        if (id == ID_NAV && code == LBN_SELCHANGE) {
            show_category(static_cast<int>(SendMessageW(g_nav, LB_GETCURSEL, 0, 0)));
            return 0;
        }
        if (id == ID_BROWSE_IMAGE)
            browse_file(window, g_image, L"Imagem\0*.raw;*.img;*.dd\0Todos\0*.*\0");
        else if (id == ID_BROWSE_OUT || id == ID_AUD_OUT)
            browse_folder(window, id == ID_BROWSE_OUT ? g_out : g_aud_out);
        else if (id == ID_BROWSE_EVTX)
            browse_file(window, g_evtx, L"EVTX\0*.evtx\0Todos\0*.*\0");
        else if (id == ID_BROWSE_PREFETCH)
            browse_folder(window, g_prefetch);
        else if (id == ID_BROWSE_HIVE)
            browse_file(window, g_hive, L"Hive\0*.*\0Todos\0*.*\0");
        else if (id == ID_BROWSE_SRUM)
            browse_file(window, g_srum, L"SRUM\0SRUDB.dat\0Todos\0*.*\0");
        else if (id == ID_BROWSE_AFF4)
            browse_file(window, g_aff4, L"AFF4\0*.aff4\0Todos\0*.*\0");
        else if (id == ID_BROWSE_E01)
            browse_file(window, g_e01, L"E01\0*.E01\0Todos\0*.*\0");
        else if (id == ID_CLEAR)
            SetWindowTextW(g_log, L"");
        else if (id == ID_MODE) {
            SetWindowTextW(g_mode, L"Modo: SANITIZACAO");
        } else if (id == ID_SAN_WIPE || id == ID_SAN_NVME || id == ID_SAN_ATA ||
                   id == ID_SAN_CLEAN || id == ID_AUD_CLEAN ||
                   id == ID_RESIDUE_CLEAN || id == ID_WIPE_FILE ||
                   id == ID_WIPE_FREE || id == ID_WIPE_BROWSER ||
                   id == ID_WIPE_HIVE || id == ID_WIPE_JOURNAL ||
                   id == ID_WIPE_ARTIFACTS || id == ID_PURGE_USN ||
                   id == ID_CLEAR_LOGS || id == ID_DELETE_VSS) {
            const bool mode_ok = g_category == 4
                ? checked(window, ID_WIPE_MODE)
                : (!(g_category == 2) || checked(window, ID_SAN_MODE));
            if (!mode_ok) {
                MessageBoxW(window, L"Marque a confirmacao de modo sanitizacao.",
                            L"Olho de Deus", MB_ICONWARNING);
                return 0;
            }
            if (!confirm_destructive(window,
                L"Operacao destrutiva e irreversivel. Continuar?")) return 0;
            start_job(window, id);
        } else if (id == ID_ADV_RUN_COMMAND) {
            const std::wstring raw = get_text(g_adv_command);
            if (is_destructive(raw) &&
                !confirm_destructive(window,
                    L"O comando contem operacao destrutiva. Continuar?")) return 0;
            start_job(window, id);
        } else if (id >= ID_SCAN) {
            start_job(window, id);
        }
        return 0;
    }
    case WM_APP_DONE: {
        auto* text = reinterpret_cast<std::wstring*>(lparam);
        append_log(*text);
        delete text;
        set_running(false);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
    return 0;
}

} // namespace

int run_gui(HINSTANCE instance) {
    if (g_bg == nullptr) g_bg = CreateSolidBrush(RGB(10, 14, 18));
    if (g_panel == nullptr) g_panel = CreateSolidBrush(RGB(20, 27, 34));
    if (g_edit_bg == nullptr) g_edit_bg = CreateSolidBrush(RGB(16, 23, 30));
    if (g_font == nullptr) {
        const int dpi = GetDeviceCaps(GetDC(nullptr), LOGPIXELSY);
        g_font = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
            L"Segoe UI");
        g_font_title = CreateFontW(-MulDiv(22, dpi, 72), 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
            L"Segoe UI Semibold");
    }
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"OlhoDeDeusGui";
    window_class.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    window_class.hbrBackground = g_bg;
    RegisterClassW(&window_class);
    HWND window = CreateWindowExW(0, window_class.lpszClassName,
        L"Olho de Deus - Deep Engine", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 780, nullptr, nullptr, instance,
        nullptr);
    if (window == nullptr) return 1;
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm != nullptr) {
        using DwmSet = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto set = reinterpret_cast<DwmSet>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
        const BOOL dark = TRUE;
        if (set != nullptr) set(window, 20, &dark, sizeof(dark));
    }
    ShowWindow(window, SW_SHOWMAXIMIZED);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

} // namespace deep
