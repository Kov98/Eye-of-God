// language: C++, file: gui_imgui.cpp, target: Windows 10/11
// Modern Dear ImGui/DirectX 11 shell for the deep engine. The CLI remains the
// authority; every action here builds and runs the same OlhoDeDeus_deep.exe
// command line. No GPL assets: the UI uses ImGui (MIT) and system fonts.

#include "gui.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>
#include <windows.h>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace deep {
namespace {

// ---------------------------------------------------------------------------
// State

struct App {
    std::array<char, 512> image{};
    std::array<char, 512> out{};
    std::array<char, 512> evtx{};
    std::array<char, 512> prefetch{};
    std::array<char, 512> hive{};
    std::array<char, 512> srum{};
    std::array<char, 512> aff4{};
    std::array<char, 512> e01{};
    bool list = true;
    bool recover = true;
    bool usn = false;
    bool carve = false;
    bool secrets = false;

    std::array<char, 512> adv_image{};
    std::array<char, 512> adv_out{};
    std::array<char, 512> adv_e01{};
    std::array<char, 512> adv_volume{};
    std::array<char, 512> adv_target{};
    std::array<char, 512> adv_key{};
    std::array<char, 1024> adv_command{};

    std::array<char, 64> san_disk{};
    std::array<char, 256> san_serial{};
    std::array<char, 256> san_password{};
    std::array<char, 64> san_passes{};
    std::array<char, 64> san_action{};
    std::array<char, 512> san_out{};
    bool san_verify = false;
    bool san_enhanced = false;
    bool san_enabled = false;

    std::array<char, 512> aud_out{};
    std::array<char, 512> wipe_target{};
    std::array<char, 512> wipe_out{};
    bool wipe_enabled = false;
    std::array<char, 64> kernel_disk{};
    std::array<char, 64> kernel_offset{};
    std::array<char, 64> kernel_length{};
    std::array<char, 512> kernel_out{};

    int section = 0;
    bool running = false;
    std::string log;
    std::mutex log_mutex;
    int pending_action = -1;
    bool pending_destructive = false;
};

App g_app;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool g_swap_ok = false;
ImFont* g_font_body = nullptr;
ImFont* g_font_title = nullptr;
ImFont* g_font_small = nullptr;

enum Action {
    A_SCAN = 1, A_ARTIFACTS, A_RAW, A_AFF4_CREATE, A_AFF4_VERIFY,
    A_E01_CREATE, A_E01_SCAN, A_EXPOSURE, A_RESIDUE,
    A_IMAGE_VERIFY, A_VSS_LIST, A_VSS_READ, A_IMAGE_EVTX, A_IMAGE_PREFETCH,
    A_RECYCLE, A_CASE_QUERY, A_CASE_SUMMARY, A_KEYGEN, A_SIGN, A_VERIFY_SIGN,
    A_DEEP_USN, A_LIST_DISKS,
    A_SAN_WIPE, A_SAN_NVME, A_SAN_ATA, A_SAN_CLEAN,
    A_AUD_AUDIT, A_AUD_CLEAN,
    A_WIPE_FILE, A_WIPE_FREE, A_WIPE_BROWSER, A_WIPE_HIVE, A_WIPE_JOURNAL,
    A_WIPE_ARTIFACTS, A_PURGE_USN, A_CLEAR_LOGS, A_DELETE_VSS,
    A_KERNEL_VERSION, A_KERNEL_READ, A_KERNEL_HELP
};

std::string narrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring widen(const char* value) {
    if (value == nullptr || *value == '\0') return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), size);
    if (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::wstring cli_executable() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring value(path);
    const std::size_t slash = value.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return value;
    const std::wstring sibling = value.substr(0, slash + 1) + L"OlhoDeDeus_deep.exe";
    return GetFileAttributesW(sibling.c_str()) != INVALID_FILE_ATTRIBUTES
        ? sibling : value;
}

void add_log(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_app.log_mutex);
    g_app.log += text;
    if (g_app.log.size() > 512 * 1024)
        g_app.log.erase(0, g_app.log.size() - 512 * 1024);
}

// ---------------------------------------------------------------------------
// Worker

void run_command(std::wstring command) {
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring file = std::wstring(temp) + L"eog_imgui_" +
        std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::to_wstring(GetTickCount64()) + L".log";
    HANDLE output = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (!ok) {
        add_log("Falha ao criar processo, erro " + std::to_string(GetLastError()) + "\r\n");
        g_app.running = false;
        return;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    std::string text;
    HANDLE input = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        GetFileSizeEx(input, &size);
        text.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        if (!text.empty()) ReadFile(input, text.data(),
            static_cast<DWORD>(text.size()), &read, nullptr);
        text.resize(read);
        CloseHandle(input);
    }
    DeleteFileW(file.c_str());
    add_log(text);
    add_log("\r\n[exit " + std::to_string(code) + "]\r\n");
    g_app.running = false;
}

bool destructive(Action action) {
    switch (action) {
    case A_RESIDUE:
    case A_SAN_WIPE:
    case A_SAN_NVME:
    case A_SAN_ATA:
    case A_SAN_CLEAN:
    case A_AUD_CLEAN:
    case A_WIPE_FILE:
    case A_WIPE_FREE:
    case A_WIPE_BROWSER:
    case A_WIPE_HIVE:
    case A_WIPE_JOURNAL:
    case A_WIPE_ARTIFACTS:
    case A_PURGE_USN:
    case A_CLEAR_LOGS:
    case A_DELETE_VSS:
        return true;
    default:
        return false;
    }
}

std::wstring build(Action action) {
    std::wstring cmd = quote(cli_executable());
    const std::wstring image = widen(g_app.image.data());
    const std::wstring out = widen(g_app.out.data());
    const std::wstring evtx = widen(g_app.evtx.data());
    const std::wstring prefetch = widen(g_app.prefetch.data());
    const std::wstring hive = widen(g_app.hive.data());
    const std::wstring srum = widen(g_app.srum.data());
    const std::wstring aff4 = widen(g_app.aff4.data());
    const std::wstring e01 = widen(g_app.e01.data());
    switch (action) {
    case A_SCAN:
        if (image.empty() || out.empty()) return {};
        cmd += L" --deep-scan " + quote(image) + L" --out " + quote(out);
        if (g_app.list) cmd += L" --list";
        if (g_app.recover) cmd += L" --recover";
        if (g_app.usn) cmd += L" --usn";
        if (g_app.carve) cmd += L" --carve";
        return cmd;
    case A_ARTIFACTS:
        if (out.empty()) return {};
        cmd += L" --artifacts --out " + quote(out);
        if (!evtx.empty()) cmd += L" --evtx-file " + quote(evtx);
        if (!prefetch.empty()) cmd += L" --prefetch-dir " + quote(prefetch);
        if (!hive.empty()) cmd += L" --hive-file " + quote(hive);
        if (!srum.empty()) cmd += L" --srum-db " + quote(srum);
        if (g_app.secrets) cmd += L" --browser-secrets";
        return cmd;
    case A_RAW:
        if (image.empty() || out.empty()) return {};
        cmd += L" --image-create " + quote(image) + L" --out " +
            quote(out + L"\\image.raw");
        return cmd;
    case A_AFF4_CREATE:
        if (image.empty() || out.empty()) return {};
        cmd += L" --aff4-zip " + quote(image) + L" --out " +
            quote(out + L"\\image.aff4");
        return cmd;
    case A_AFF4_VERIFY:
        if (aff4.empty()) return {};
        cmd += L" --aff4-zip-verify " + quote(aff4);
        return cmd;
    case A_E01_CREATE:
        if (image.empty() || out.empty()) return {};
        cmd += L" --e01-create " + quote(image) + L" --out " +
            quote(out + L"\\image");
        return cmd;
    case A_E01_SCAN:
        if (e01.empty() || out.empty()) return {};
        cmd += L" --e01-scan " + quote(e01) + L" --out " + quote(out);
        return cmd;
    case A_EXPOSURE:
    case A_AUD_AUDIT: {
        const std::wstring target = action == A_AUD_AUDIT ?
            widen(g_app.aud_out.data()) : out;
        if (target.empty()) return {};
        cmd += L" --exposure-audit --out " + quote(target);
        return cmd;
    }
    case A_RESIDUE:
    case A_AUD_CLEAN:
    case A_SAN_CLEAN: {
        const std::wstring target = action == A_SAN_CLEAN ?
            widen(g_app.san_out.data()) :
            (action == A_AUD_CLEAN ? widen(g_app.aud_out.data()) : out);
        if (target.empty()) return {};
        cmd += L" --mode sanitize --clean-local-residue --out " +
            quote(target) + L" --confirm CLEAN-RESIDUE";
        return cmd;
    }
    case A_IMAGE_VERIFY:
        if (widen(g_app.adv_image.data()).empty()) return {};
        cmd += L" --image-verify " + quote(widen(g_app.adv_image.data()));
        return cmd;
    case A_VSS_LIST:
        cmd += L" --vss-list";
        return cmd;
    case A_VSS_READ: {
        const std::wstring volume = widen(g_app.adv_volume.data());
        const std::wstring dir = widen(g_app.adv_out.data());
        if (volume.empty() || dir.empty()) return {};
        cmd += L" --vss-read " + quote(volume) + L" --out " +
            quote(dir + L"\\vss");
        return cmd;
    }
    case A_IMAGE_EVTX:
    case A_IMAGE_PREFETCH: {
        const std::wstring target = widen(g_app.adv_image.data());
        const std::wstring dir = widen(g_app.adv_out.data());
        if (target.empty() || dir.empty()) return {};
        cmd += action == A_IMAGE_EVTX ? L" --image-evtx " : L" --image-prefetch ";
        cmd += quote(target) + L" --out " + quote(dir);
        return cmd;
    }
    case A_RECYCLE: {
        const std::wstring target = widen(g_app.adv_target.data());
        const std::wstring dir = widen(g_app.adv_out.data());
        if (target.empty() || dir.empty()) return {};
        cmd += L" --recycle-recover " + quote(target) + L" --out " + quote(dir);
        return cmd;
    }
    case A_CASE_QUERY:
    case A_CASE_SUMMARY: {
        const std::wstring target = widen(g_app.adv_target.data());
        if (target.empty()) return {};
        cmd += action == A_CASE_QUERY ? L" --case-query " : L" --case-summary ";
        cmd += quote(target);
        return cmd;
    }
    case A_KEYGEN: {
        const std::wstring key = widen(g_app.adv_key.data());
        if (key.empty()) return {};
        cmd += L" --keygen " + quote(key);
        return cmd;
    }
    case A_SIGN:
    case A_VERIFY_SIGN: {
        const std::wstring target = widen(g_app.adv_target.data());
        const std::wstring key = widen(g_app.adv_key.data());
        if (target.empty() || key.empty()) return {};
        cmd += action == A_SIGN ? L" --sign-manifest " : L" --verify-manifest ";
        cmd += quote(target) + L" --key " + quote(key);
        return cmd;
    }
    case A_DEEP_USN: {
        const std::wstring target = widen(g_app.adv_target.data());
        const std::wstring dir = widen(g_app.adv_out.data());
        if (target.empty() || dir.empty()) return {};
        cmd += L" --deep-usn-file " + quote(target) + L" --out " + quote(dir);
        return cmd;
    }
    case A_LIST_DISKS:
        cmd += L" --list-disks";
        return cmd;
    case A_KERNEL_VERSION:
        cmd += L" --kernel-version";
        return cmd;
    case A_KERNEL_HELP:
        cmd += L" --deep-help";
        return cmd;
    case A_KERNEL_READ: {
        const std::wstring disk = widen(g_app.kernel_disk.data());
        const std::wstring offset = widen(g_app.kernel_offset.data());
        const std::wstring length = widen(g_app.kernel_length.data());
        const std::wstring file = widen(g_app.kernel_out.data());
        if (disk.empty() || length.empty() || file.empty()) return {};
        cmd += L" --kernel-read " + disk + L" --offset " +
            (offset.empty() ? L"0" : offset) + L" --length " + length +
            L" --out " + quote(file);
        return cmd;
    }
    case A_SAN_WIPE: {
        const std::wstring disk = widen(g_app.san_disk.data());
        const std::wstring serial = widen(g_app.san_serial.data());
        const std::wstring dir = widen(g_app.san_out.data());
        if (disk.empty() || serial.empty()) return {};
        cmd += L" --mode sanitize --sanitize-disk " + disk + L" --serial " +
            quote(serial);
        if (!dir.empty()) cmd += L" --out " + quote(dir);
        if (g_app.san_passes[0]) cmd += L" --passes " + widen(g_app.san_passes.data());
        if (g_app.san_verify) cmd += L" --verify";
        cmd += L" --confirm WIPE-DISK";
        return cmd;
    }
    case A_SAN_NVME: {
        const std::wstring disk = widen(g_app.san_disk.data());
        const std::wstring serial = widen(g_app.san_serial.data());
        if (disk.empty() || serial.empty()) return {};
        cmd += L" --mode sanitize --nvme-sanitize " + disk + L" --serial " +
            quote(serial) + L" --action " +
            (g_app.san_action[0] ? widen(g_app.san_action.data()) : L"block") +
            L" --confirm NVME-SANITIZE";
        return cmd;
    }
    case A_SAN_ATA: {
        const std::wstring disk = widen(g_app.san_disk.data());
        const std::wstring serial = widen(g_app.san_serial.data());
        if (disk.empty() || serial.empty()) return {};
        cmd += L" --mode sanitize --ata-erase " + disk + L" --serial " +
            quote(serial);
        if (g_app.san_password[0])
            cmd += L" --password " + quote(widen(g_app.san_password.data()));
        if (g_app.san_enhanced) cmd += L" --enhanced";
        cmd += L" --confirm ATA-ERASE";
        return cmd;
    }
    case A_WIPE_FILE:
    case A_WIPE_FREE:
    case A_WIPE_BROWSER:
    case A_WIPE_HIVE:
    case A_WIPE_JOURNAL: {
        const std::wstring target = widen(g_app.wipe_target.data());
        const std::wstring dir = widen(g_app.wipe_out.data());
        if (target.empty() || dir.empty()) return {};
        cmd += L" --mode sanitize ";
        switch (action) {
        case A_WIPE_FILE: cmd += L"--wipe-file "; break;
        case A_WIPE_FREE: cmd += L"--wipe-free "; break;
        case A_WIPE_BROWSER: cmd += L"--wipe-browser --browser-profile "; break;
        case A_WIPE_HIVE: cmd += L"--wipe-hive "; break;
        default: cmd += L"--wipe-journal "; break;
        }
        cmd += quote(target) + L" --out " + quote(dir) + L" --confirm WIPE";
        return cmd;
    }
    case A_WIPE_ARTIFACTS:
        cmd += L" --mode sanitize --wipe-artifacts --confirm WIPE";
        return cmd;
    case A_PURGE_USN: {
        const std::wstring target = widen(g_app.wipe_target.data());
        if (target.empty()) return {};
        cmd += L" --mode sanitize --purge-usn " + quote(target) +
            L" --confirm WIPE";
        return cmd;
    }
    case A_CLEAR_LOGS:
        cmd += L" --mode sanitize --clear-logs --confirm WIPE";
        return cmd;
    case A_DELETE_VSS:
        cmd += L" --mode sanitize --delete-vss --confirm WIPE";
        return cmd;
    default:
        return {};
    }
}

void begin_action(Action action) {
    std::wstring command = build(action);
    if (command.empty()) {
        add_log("Preencha os campos obrigatorios para esta acao.\r\n");
        return;
    }
    if (destructive(action)) {
        g_app.pending_action = action;
        g_app.pending_destructive = true;
        ImGui::OpenPopup("Confirmar operacao");
        return;
    }
    add_log("> " + narrow(command) + "\r\n");
    g_app.running = true;
    std::thread([command]() { run_command(command); }).detach();
}

void confirm_pending() {
    const int action = g_app.pending_action;
    g_app.pending_action = -1;
    g_app.pending_destructive = false;
    if (action < 0) return;
    const std::wstring command = build(static_cast<Action>(action));
    if (command.empty()) return;
    add_log("> " + narrow(command) + "\r\n");
    g_app.running = true;
    std::thread([command]() { run_command(command); }).detach();
}

// ---------------------------------------------------------------------------
// File dialogs

bool pick_file(HWND owner, char* target, size_t size, const wchar_t* filter) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return false;
    const std::string utf8 = narrow(file);
    std::snprintf(target, size, "%s", utf8.c_str());
    return true;
}

bool pick_folder(HWND owner, char* target, size_t size) {
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.lpszTitle = L"Selecione a pasta";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (item == nullptr) return false;
    wchar_t path[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(item, path) != FALSE;
    CoTaskMemFree(item);
    if (ok) {
        const std::string utf8 = narrow(path);
        std::snprintf(target, size, "%s", utf8.c_str());
    }
    return ok;
}

// ---------------------------------------------------------------------------
// UI helpers

constexpr ImVec4 kBg(0.035f, 0.047f, 0.059f, 1.0f);
constexpr ImVec4 kPanel(0.070f, 0.090f, 0.110f, 1.0f);
constexpr ImVec4 kCard(0.085f, 0.110f, 0.135f, 1.0f);
constexpr ImVec4 kAccent(0.00f, 0.55f, 0.90f, 1.0f);
constexpr ImVec4 kText(0.90f, 0.94f, 0.97f, 1.0f);
constexpr ImVec4 kMuted(0.55f, 0.63f, 0.70f, 1.0f);

void text_muted(const char* text) {
    ImGui::TextColored(kMuted, "%s", text);
}

void section_title(const char* title, const char* subtitle) {
    ImGui::PushFont(g_font_title);
    ImGui::TextColored(kText, "%s", title);
    ImGui::PopFont();
    text_muted(subtitle);
    ImGui::Spacing();
}

void card_begin(const char* id, float height = 0.0f) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kCard);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::BeginChild(id, ImVec2(0, height), true,
        ImGuiWindowFlags_AlwaysUseWindowPadding);
}

void card_end() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

bool path_field(const char* label, char* value, size_t size, bool folder,
                const wchar_t* filter) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(150.0f);
    ImGui::SetNextItemWidth(-92.0f);
    ImGui::PushID(label);
    const bool changed = ImGui::InputText("##value", value, size);
    ImGui::SameLine();
    const bool clicked = ImGui::Button("...", ImVec2(78, 0));
    ImGui::PopID();
    if (clicked) {
        if (folder) pick_folder(ImGui::GetMainViewport()->PlatformHandle, value, size);
        else pick_file(ImGui::GetMainViewport()->PlatformHandle, value, size, filter);
    }
    return changed || clicked;
}

bool action_button(const char* label, Action action, float width = 0.0f) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09f, 0.13f, 0.17f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.35f, 0.52f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
    const bool clicked = ImGui::Button(label, ImVec2(width, 32));
    ImGui::PopStyleColor(3);
    if (clicked) begin_action(action);
    return clicked;
}

void browse_row(const char* label, char* value, size_t size, bool folder) {
    path_field(label, value, size, folder, L"Todos\0*.*\0");
}

void render_pericia() {
    section_title("Pericia", "Aquisicao, artefatos e empacotamento do caso");
    card_begin("card_forensic", 360);
    browse_row("Imagem / disco", g_app.image.data(), g_app.image.size(), false);
    browse_row("Pasta de saida", g_app.out.data(), g_app.out.size(), true);
    browse_row("EVTX", g_app.evtx.data(), g_app.evtx.size(), false);
    browse_row("Prefetch dir", g_app.prefetch.data(), g_app.prefetch.size(), true);
    browse_row("Hive", g_app.hive.data(), g_app.hive.size(), false);
    browse_row("SRUM", g_app.srum.data(), g_app.srum.size(), false);
    browse_row("AFF4", g_app.aff4.data(), g_app.aff4.size(), false);
    browse_row("E01", g_app.e01.data(), g_app.e01.size(), false);
    ImGui::Spacing();
    ImGui::Checkbox("Listar MFT", &g_app.list);
    ImGui::SameLine();
    ImGui::Checkbox("Recuperar", &g_app.recover);
    ImGui::SameLine();
    ImGui::Checkbox("USN", &g_app.usn);
    ImGui::SameLine();
    ImGui::Checkbox("Carving", &g_app.carve);
    ImGui::SameLine();
    ImGui::Checkbox("Browser secrets", &g_app.secrets);
    ImGui::Spacing();
    action_button("Escanear imagem", A_SCAN, 180);
    ImGui::SameLine();
    action_button("Coletar artefatos", A_ARTIFACTS, 180);
    ImGui::SameLine();
    action_button("Criar raw", A_RAW, 150);
    ImGui::SameLine();
    action_button("Criar AFF4", A_AFF4_CREATE, 150);
    action_button("Verificar AFF4", A_AFF4_VERIFY, 180);
    ImGui::SameLine();
    action_button("Criar E01", A_E01_CREATE, 150);
    ImGui::SameLine();
    action_button("Escanear E01", A_E01_SCAN, 160);
    card_end();
}

void render_advanced() {
    section_title("Avancado", "VSS, imagem, caso, assinatura e ferramentas de baixo nivel");
    card_begin("card_advanced", 410);
    browse_row("Imagem", g_app.adv_image.data(), g_app.adv_image.size(), false);
    browse_row("Pasta de saida", g_app.adv_out.data(), g_app.adv_out.size(), true);
    browse_row("E01", g_app.adv_e01.data(), g_app.adv_e01.size(), false);
    browse_row("Volume VSS", g_app.adv_volume.data(), g_app.adv_volume.size(), false);
    browse_row("Alvo / caso", g_app.adv_target.data(), g_app.adv_target.size(), false);
    browse_row("Chave .priv/.pub", g_app.adv_key.data(), g_app.adv_key.size(), false);
    ImGui::Spacing();
    action_button("Verificar imagem", A_IMAGE_VERIFY, 180);
    ImGui::SameLine();
    action_button("VSS list", A_VSS_LIST, 150);
    ImGui::SameLine();
    action_button("VSS read", A_VSS_READ, 150);
    ImGui::SameLine();
    action_button("EVTX da imagem", A_IMAGE_EVTX, 180);
    action_button("Prefetch da imagem", A_IMAGE_PREFETCH, 190);
    ImGui::SameLine();
    action_button("Recover recycle", A_RECYCLE, 180);
    ImGui::SameLine();
    action_button("Case query", A_CASE_QUERY, 150);
    ImGui::SameLine();
    action_button("Case summary", A_CASE_SUMMARY, 170);
    action_button("Keygen", A_KEYGEN, 140);
    ImGui::SameLine();
    action_button("Assinar manifesto", A_SIGN, 190);
    ImGui::SameLine();
    action_button("Verificar assinatura", A_VERIFY_SIGN, 190);
    ImGui::SameLine();
    action_button("Deep USN file", A_DEEP_USN, 170);
    action_button("Listar discos", A_LIST_DISKS, 170);
    ImGui::SameLine();
    text_muted("Comando CLI livre:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(360);
    ImGui::InputText("##raw", g_app.adv_command.data(), g_app.adv_command.size());
    ImGui::SameLine();
    if (ImGui::Button("Executar", ImVec2(110, 0))) {
        const std::wstring raw = widen(g_app.adv_command.data());
        if (!raw.empty()) {
            std::wstring command = quote(cli_executable()) + L" " + raw;
            add_log("> " + narrow(command) + "\r\n");
            g_app.running = true;
            std::thread([command]() { run_command(command); }).detach();
        }
    }
    card_end();
}

void render_sanitize() {
    section_title("Sanitizacao", "Acoes destrutivas: o modo sanitizacao e obrigatorio");
    card_begin("card_sanitize", 300);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.25f, 1.0f));
    ImGui::TextWrapped("ATENCAO: sanitizacao e irreversivel. Nuvem, backup offsite, roteador e logs remotos nao sao apagados.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Text("Disco (numero)");
    ImGui::SameLine(150);
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("##disk", g_app.san_disk.data(), g_app.san_disk.size());
    ImGui::SameLine();
    if (ImGui::Button("Listar discos", ImVec2(130, 0))) begin_action(A_LIST_DISKS);
    ImGui::Text("Serial exato");
    ImGui::SameLine(150);
    ImGui::SetNextItemWidth(360);
    ImGui::InputText("##serial", g_app.san_serial.data(), g_app.san_serial.size());
    ImGui::Text("Senha ATA");
    ImGui::SameLine(150);
    ImGui::SetNextItemWidth(360);
    ImGui::InputText("##atapass", g_app.san_password.data(),
        g_app.san_password.size(), ImGuiInputTextFlags_Password);
    ImGui::Text("Passes");
    ImGui::SameLine(150);
    ImGui::SetNextItemWidth(100);
    ImGui::InputText("##passes", g_app.san_passes.data(), g_app.san_passes.size());
    ImGui::SameLine();
    ImGui::Text("Acao NVMe");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    ImGui::InputText("##action", g_app.san_action.data(), g_app.san_action.size());
    browse_row("Pasta de saida", g_app.san_out.data(), g_app.san_out.size(), true);
    ImGui::Checkbox("Confirmo modo sanitizacao / verify / enhanced", &g_app.san_enabled);
    ImGui::Checkbox("Verify no overwrite", &g_app.san_verify);
    ImGui::SameLine();
    ImGui::Checkbox("Enhanced erase", &g_app.san_enhanced);
    ImGui::Spacing();
    action_button("Sanitizar disco", A_SAN_WIPE, 180);
    ImGui::SameLine();
    action_button("NVMe sanitize", A_SAN_NVME, 170);
    ImGui::SameLine();
    action_button("ATA secure erase", A_SAN_ATA, 180);
    ImGui::SameLine();
    action_button("Limpar residuos", A_SAN_CLEAN, 170);
    card_end();
}

void render_audit() {
    section_title("Auditoria", "Exposicao residual: nuvem, backups, snapshots, VSS e roteador");
    card_begin("card_audit", 190);
    browse_row("Pasta de saida", g_app.aud_out.data(), g_app.aud_out.size(), true);
    ImGui::Spacing();
    action_button("Auditar exposicao", A_AUD_AUDIT, 190);
    ImGui::SameLine();
    action_button("Limpar residuos", A_AUD_CLEAN, 190);
    ImGui::Spacing();
    text_muted("A limpeza local nao apaga backup offsite, conta sincronizada, cache de roteador ou log remoto.");
    card_end();
}

void render_wipe() {
    section_title("Wipe", "Limpeza de artefatos, arquivos, hives, journals e VSS");
    card_begin("card_wipe", 260);
    browse_row("Alvo / perfil", g_app.wipe_target.data(), g_app.wipe_target.size(), false);
    browse_row("Pasta de saida", g_app.wipe_out.data(), g_app.wipe_out.size(), true);
    ImGui::Checkbox("Confirmo modo sanitizacao", &g_app.wipe_enabled);
    ImGui::Spacing();
    action_button("Wipe arquivo", A_WIPE_FILE, 170);
    ImGui::SameLine();
    action_button("Wipe espaco livre", A_WIPE_FREE, 190);
    ImGui::SameLine();
    action_button("Wipe browser", A_WIPE_BROWSER, 170);
    ImGui::SameLine();
    action_button("Wipe hive", A_WIPE_HIVE, 150);
    action_button("Wipe journal", A_WIPE_JOURNAL, 170);
    ImGui::SameLine();
    action_button("Wipe artefatos", A_WIPE_ARTIFACTS, 180);
    ImGui::SameLine();
    action_button("Purge USN", A_PURGE_USN, 150);
    ImGui::SameLine();
    action_button("Limpar logs", A_CLEAR_LOGS, 150);
    action_button("Apagar VSS", A_DELETE_VSS, 170);
    card_end();
}

void render_kernel() {
    section_title("Kernel", "Driver read-only: versao e leitura raw por offset");
    card_begin("card_kernel", 230);
    ImGui::Text("Disco");
    ImGui::SameLine(150);
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("##kdisk", g_app.kernel_disk.data(), g_app.kernel_disk.size());
    ImGui::Text("Offset");
    ImGui::SameLine(150);
    ImGui::SetNextItemWidth(220);
    ImGui::InputText("##koff", g_app.kernel_offset.data(), g_app.kernel_offset.size());
    ImGui::Text("Tamanho");
    ImGui::SameLine(150);
    ImGui::SetNextItemWidth(220);
    ImGui::InputText("##klen", g_app.kernel_length.data(), g_app.kernel_length.size());
    browse_row("Arquivo de saida", g_app.kernel_out.data(), g_app.kernel_out.size(), false);
    ImGui::Spacing();
    action_button("Versao do driver", A_KERNEL_VERSION, 190);
    ImGui::SameLine();
    action_button("Ler raw via kernel", A_KERNEL_READ, 190);
    ImGui::SameLine();
    action_button("Ajuda CLI completa", A_KERNEL_HELP, 190);
    card_end();
}

void render_topbar() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 62),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.025f, 0.035f, 0.045f, 1.0f)), 8.0f);
    ImGui::SetCursorPos(ImVec2(20, 12));
    ImGui::PushFont(g_font_title);
    ImGui::TextColored(kText, "OLHO DE DEUS");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(22);
    text_muted("DEEP ENGINE  /  FORENSIC & PRIVACY");
    ImGui::SameLine(ImGui::GetWindowWidth() - 250);
    ImGui::SetCursorPosY(18);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.30f, 0.20f, 1.0f));
    ImGui::Button(g_app.section == 2 || g_app.section == 4 ?
        "SANITIZACAO" : "PERICIA", ImVec2(150, 28));
    ImGui::PopStyleColor();
    ImGui::SetCursorPosY(72);
}

void render_sidebar() {
    ImGui::BeginChild("sidebar", ImVec2(230, 0), false);
    ImGui::Dummy(ImVec2(0, 4));
    const char* items[] = {"Pericia", "Avancado", "Sanitizacao", "Auditoria",
                           "Wipe", "Kernel"};
    const char* subs[] = {"Aquisicao e analise", "VSS, caso, assinatura",
                          "Acoes destrutivas", "Exposicao residual",
                          "Limpeza de rastros", "Leitura raw"};
    for (int i = 0; i < 6; ++i) {
        const bool selected = g_app.section == i;
        ImGui::PushStyleColor(ImGuiCol_Header,
            selected ? ImVec4(0.02f, 0.30f, 0.50f, 1.0f)
                     : ImVec4(0.06f, 0.08f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
            ImVec4(0.04f, 0.22f, 0.36f, 1.0f));
        if (ImGui::Selectable(items[i], selected, 0, ImVec2(0, 44))) {
            g_app.section = i;
        }
        ImGui::PopStyleColor(2);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
        text_muted(subs[i]);
        ImGui::Dummy(ImVec2(0, 4));
    }
    ImGui::Dummy(ImVec2(0, 18));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));
    text_muted(g_app.running ? "Executando..." : "Pronto.");
    ImGui::EndChild();
}

void render_console() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.025f, 0.035f, 0.045f, 1.0f));
    ImGui::BeginChild("console", ImVec2(0, 190), true);
    ImGui::TextColored(kAccent, "CONSOLE");
    ImGui::SameLine();
    text_muted("stdout/stderr do CLI");
    ImGui::Separator();
    ImGui::BeginChild("logscroll", ImVec2(0, 0), false,
        ImGuiWindowFlags_HorizontalScrollbar);
    std::string log;
    {
        std::lock_guard<std::mutex> lock(g_app.log_mutex);
        log = g_app.log;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.85f, 0.90f, 1.0f));
    ImGui::TextUnformatted(log.c_str());
    ImGui::PopStyleColor();
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void render_confirm_modal() {
    if (g_app.pending_action >= 0) ImGui::OpenPopup("Confirmar operacao");
    if (ImGui::BeginPopupModal("Confirmar operacao", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.68f, 0.28f, 1.0f));
        ImGui::TextWrapped("Esta acao e destrutiva e irreversivel.");
        ImGui::PopStyleColor();
        ImGui::TextWrapped("Confirme somente se o alvo correto foi selecionado.");
        ImGui::Spacing();
        if (ImGui::Button("Continuar", ImVec2(150, 34))) {
            confirm_pending();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(150, 34))) {
            g_app.pending_action = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void render_ui() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("##root", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    render_topbar();
    render_sidebar();
    ImGui::SameLine();
    ImGui::BeginChild("main", ImVec2(0, -200), false);
    switch (g_app.section) {
    case 0: render_pericia(); break;
    case 1: render_advanced(); break;
    case 2: render_sanitize(); break;
    case 3: render_audit(); break;
    case 4: render_wipe(); break;
    default: render_kernel(); break;
    }
    ImGui::EndChild();
    render_console();
    render_confirm_modal();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// D3D11

bool create_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level{};
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, levels, 2, D3D11_SDK_VERSION, &desc, &g_swap, &g_device, &level,
        &g_context) != S_OK) {
        return false;
    }
    ID3D11Texture2D* back = nullptr;
    if (g_swap->GetBuffer(0, IID_PPV_ARGS(&back)) != S_OK) return false;
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    return true;
}

void destroy_device() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

void reset_render_target() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    ID3D11Texture2D* back = nullptr;
    if (g_swap && g_swap->GetBuffer(0, IID_PPV_ARGS(&back)) == S_OK) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_swap && wp != SIZE_MINIMIZED) {
            g_swap_ok = false;
            reset_render_target();
            g_swap_ok = true;
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int run_gui(HINSTANCE instance) {
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW cls{sizeof(cls), CS_CLASSDC, wnd_proc, 0, 0, instance, nullptr,
        LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"OlhoDeDeusImGui",
        nullptr};
    RegisterClassExW(&cls);
    HWND hwnd = CreateWindowW(cls.lpszClassName, L"Olho de Deus - Deep Engine",
        WS_OVERLAPPEDWINDOW, 100, 100, 1600, 980, nullptr, nullptr, instance,
        nullptr);
    if (!create_device(hwnd)) return 1;
    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    g_font_body = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    g_font_small = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f);
    g_font_title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", 30.0f);
    if (g_font_body == nullptr) g_font_body = io.Fonts->AddFontDefault();
    if (g_font_small == nullptr) g_font_small = g_font_body;
    if (g_font_title == nullptr) g_font_title = g_font_body;

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(9, 6);
    style.ItemSpacing = ImVec2(10, 9);
    style.Colors[ImGuiCol_Text] = kText;
    style.Colors[ImGuiCol_WindowBg] = kBg;
    style.Colors[ImGuiCol_ChildBg] = kPanel;
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.08f, 0.10f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.14f, 0.18f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.07f, 0.09f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.10f, 0.18f, 0.24f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.24f, 0.32f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.04f, 0.26f, 0.42f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.05f, 0.35f, 0.55f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.09f, 0.13f, 0.17f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.08f, 0.35f, 0.52f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = kAccent;
    style.Colors[ImGuiCol_Separator] = ImVec4(0.16f, 0.21f, 0.25f, 1.0f);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    g_swap_ok = true;

    bool done = false;
    while (!done) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        render_ui();
        ImGui::Render();
        const float clear[4] = {kBg.x, kBg.y, kBg.z, 1.0f};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroy_device();
    DestroyWindow(hwnd);
    UnregisterClassW(cls.lpszClassName, instance);
    return 0;
}

} // namespace deep
