// language: C++, file: acquireview.hpp, target: Windows 11, Qt 6 Widgets
// Acquisition panel: the door between the operator and the two acquisition
// modules. Android over ADB, iOS over a Finder/iTunes backup.
//
// No Q_OBJECT: this project builds the GUI with a plain g++ invocation and no
// moc step. Work runs on a std::thread and the UI drains its state on a timer,
// which is also the honest model here — an acquisition takes minutes and must
// never freeze the window.
#pragma once

#include <QString>
#include <QWidget>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class QComboBox;
class QVBoxLayout;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QTimer;

namespace olho::ui {

class AcquireView : public QWidget {
public:
    explicit AcquireView(QWidget* parent = nullptr);
    ~AcquireView() override;

    // Called on the UI thread when a recovery produced conversations.json.
    // This is the seam that closes the loop: acquire -> recover -> view, without
    // the operator ever seeing a command line.
    void set_case_handler(std::function<void(const QString&)> handler) {
        case_handler_ = std::move(handler);
    }

    // Argumentos do olho_report para este caso, incluindo --recorte quando existe
    // uma pasta de exports. Publico para o modo evidencia poder imprimir a linha de
    // comando que a GUI usaria: sem isso, "o laudo cita os recortes" seria afirmacao
    // sem prova, porque a acao real abre QFileDialog.
    QStringList report_arguments(const QString& case_dir, const QString& out_path) const;

private:
    void build_android_section(QVBoxLayout* root);
    void build_ios_section(QVBoxLayout* root);

    // Job plumbing. One job at a time: two concurrent acquisitions writing into
    // the same case directory would corrupt each other's manifest.
    void start_job(const std::function<void()>& body);
    void poll_job();
    void append_log(const QString& line);
    void set_busy(bool busy);
    QString ensure_out_dir();

    // Runs wa_recover over a staged <case>/whatsapp and reports the export path.
    void finish_whatsapp_case(const QString& case_dir);

    // Selo do caso ao fim da recuperacao. A cadeia de custodia so vale se a prova
    // for selada antes de alguem ter motivo para mexer nela; deixar isso como um
    // clique no menu significa que, num dia corrido, ninguem clica.
    void seal_case_after_recovery(const QString& case_dir);
    void notify_case(const QString& conversations_json);

    std::function<void(const QString&)> case_handler_;

    // Last case staged by a recovery, so the report action has a target without
    // asking the operator to pick the same folder twice.
    QString last_case_dir_;
    void action_generate_report();
    void report_row(QVBoxLayout* root);

    void action_detect_devices();
    void action_collect_android();
    void action_recover_existing();
    void action_choose_backup();
    void action_extract_ios();

    QComboBox* devices_ = nullptr;
    QPushButton* detect_ = nullptr;
    QPushButton* collect_android_ = nullptr;
    QLineEdit* android_out_ = nullptr;
    QLineEdit* android_password_ = nullptr;

    QLineEdit* backup_dir_ = nullptr;
    QLineEdit* backup_password_ = nullptr;
    QPushButton* choose_backup_ = nullptr;
    QPushButton* extract_ios_ = nullptr;
    QLineEdit* ios_out_ = nullptr;

    QPlainTextEdit* log_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* poll_ = nullptr;

    std::unique_ptr<std::thread> worker_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> done_{false};
    mutable std::mutex log_mutex_;
    std::deque<std::string> pending_;
};

}  // namespace olho::ui
