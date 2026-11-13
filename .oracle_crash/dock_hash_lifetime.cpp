// Isolated reproduction of the teardown crash documented in
// docs/CRASHES-E-BUGS.md #9 (product frame: mainwindow.cpp:5062 -> qhash.h:313).
//
// The product keeps a QHash<QDockWidget*, DockTitleBar*> as a *member* of the
// main window and removes from it inside the dock's QObject::destroyed lambda.
// Member destruction happens in ~MainWindow, but the dock children are only
// deleted later, by QObject::~QObject. So the lambda runs against a QHash whose
// storage is already freed.
//
// Modes:
//   bug        - member QHash, lambda removes from it, no heap churn
//   bug_reused - same, plus heap churn between the two destruction steps (this
//                is what the product does all the time)
//   ok         - same removal, but the QHash outlives the dock
//   control    - dock with no destroyed connection at all
#include <QtWidgets>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <windows.h>

namespace {

char g_report_path[MAX_PATH] = {0};
char g_mode[32] = {0};
const char* g_phase = "construtor";

void append(char* buf, int& used, int cap, const char* text) {
    const int n = static_cast<int>(std::strlen(text));
    if (used + n >= cap) return;
    std::memcpy(buf + used, text, static_cast<size_t>(n));
    used += n;
    buf[used] = 0;
}

void append_hex(char* buf, int& used, int cap, unsigned long long value) {
    char tmp[32];
    std::snprintf(tmp, sizeof(tmp), "0x%llx", value);
    append(buf, used, cap, tmp);
}

LONG WINAPI on_crash(EXCEPTION_POINTERS* info) {
    char text[4096];
    int used = 0;
    text[0] = 0;
    append(text, used, sizeof(text), "oracle dock_hash_lifetime\nmodo=");
    append(text, used, sizeof(text), g_mode);
    append(text, used, sizeof(text), " fase=");
    append(text, used, sizeof(text), g_phase);
    append(text, used, sizeof(text), "\ncodigo=");
    append_hex(text, used, sizeof(text), info->ExceptionRecord->ExceptionCode);
    append(text, used, sizeof(text), " endereco=");
    append_hex(text, used, sizeof(text),
               reinterpret_cast<unsigned long long>(info->ExceptionRecord->ExceptionAddress));
    append(text, used, sizeof(text), "\nexe_base=");
    append_hex(text, used, sizeof(text),
               reinterpret_cast<unsigned long long>(::GetModuleHandleA(nullptr)));
    append(text, used, sizeof(text), "\n");

    void* frames[16] = {0};
    const USHORT got = ::CaptureStackBackTrace(0, 16, frames, nullptr);
    for (USHORT i = 0; i < got; ++i) {
        HMODULE module = nullptr;
        if (!::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  static_cast<LPCSTR>(frames[i]), &module)) {
            continue;
        }
        char name[MAX_PATH] = {0};
        ::GetModuleFileNameA(module, name, MAX_PATH);
        const char* base_name = std::strrchr(name, '\\');
        append(text, used, sizeof(text), "  ");
        append(text, used, sizeof(text), base_name ? base_name + 1 : name);
        append(text, used, sizeof(text), "+");
        append_hex(text, used, sizeof(text),
                   reinterpret_cast<unsigned long long>(frames[i]) -
                       reinterpret_cast<unsigned long long>(module));
        append(text, used, sizeof(text), "\n");
    }

    std::fputs(text, stdout);
    std::fflush(stdout);
    if (g_report_path[0] != 0) {
        if (FILE* f = std::fopen(g_report_path, "wb")) {
            std::fwrite(text, 1, std::strlen(text), f);
            std::fclose(f);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// Runs right after the member QHash is gone and before QObject deletes the dock
// children, which is exactly the window the product fills with widget, pixmap
// and query allocations. Handing the freed block back to the allocator with
// 0xdd in it turns the stale read into a hard fault instead of a silent one.
struct HeapChurn {
    ~HeapChurn() {
        static const size_t kSizes[] = {48, 64, 80, 96, 112, 128, 160, 192, 256, 384, 512};
        for (size_t size : kSizes) {
            for (int i = 0; i < 8; ++i) {
                void* block = std::malloc(size);
                if (block) std::memset(block, 0xdd, size);
            }
        }
    }
};

// The product shape: the map is a member, the dock is a child.
class Host : public QMainWindow {
public:
    explicit Host(const char* mode) {
        auto* dock = new QDockWidget(QStringLiteral("dock"), this);
        auto* label = new QLabel(QStringLiteral("titulo"), dock);
        dock->setWidget(label);

        if (std::strcmp(mode, "ok") == 0) {
            alive_ = std::make_shared<QHash<QWidget*, QLabel*>>();
            alive_->insert(dock, label);
            auto keep = alive_;
            connect(dock, &QObject::destroyed, this, [keep, dock]() {
                g_phase = "lambda_remove_ok";
                keep->remove(dock);
            });
        } else if (std::strcmp(mode, "control") != 0) {
            map_.insert(dock, label);
            connect(dock, &QObject::destroyed, this, [this, dock]() {
                g_phase = "lambda_remove_member";
                map_.remove(dock);
            });
        }
    }

private:
    // Declared before the map on purpose: members are destroyed in reverse, so
    // this destructor runs after the map is gone and before the dock children.
    HeapChurn churn_;
    QHash<QWidget*, QLabel*> map_;                            // bug path
    std::shared_ptr<QHash<QWidget*, QLabel*>> alive_;         // ok path
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const char* mode = argc > 1 ? argv[1] : "bug";
    std::snprintf(g_mode, sizeof(g_mode), "%s", mode);
    if (argc > 2) std::snprintf(g_report_path, sizeof(g_report_path), "%s", argv[2]);

    ::SetUnhandledExceptionFilter(on_crash);
    {
        Host host(mode);
        host.resize(400, 300);
        g_phase = "destruindo";
    }   // member maps die here; the dock children die in ~QObject, after them
    g_phase = "sobreviveu";
    std::printf("sobreviveu modo=%s\n", mode);
    std::fflush(stdout);
    return 0;
}
