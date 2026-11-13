// language: C++, file: acquireview.cpp, target: Windows 11, Qt 6 Widgets
#include "acquireview.hpp"

#include "android_adb.hpp"
#include "android_backup.hpp"
#include "ios_backup.hpp"
#include "theme_vs2026.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QProcess>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace olho::ui {
namespace {

std::string to_utf8(const QString& s) { return s.toStdString(); }
std::wstring to_wide(const QString& s) { return s.toStdWString(); }
QString from_std(const std::string& s) { return QString::fromStdString(s); }

QString label_style() {
    return QString("color:%1; font-size:12px;").arg(theme::kTextSecondary);
}

}  // namespace

AcquireView::AcquireView(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(8);

    auto* head = new QLabel("Aquisicao", this);
    head->setStyleSheet(QString("color:%1; font-size:13px; font-weight:600;").arg(theme::kTextPrimary));
    root->addWidget(head);

    build_android_section(root);
    build_ios_section(root);

    // A case extracted in an earlier session (or pulled by hand) still has to run
    // through the recovery engine before it can be read.
    auto* recover_row = new QWidget(this);
    auto* recover_layout = new QHBoxLayout(recover_row);
    recover_layout->setContentsMargins(0, 0, 0, 0);
    auto* recover_button = new QPushButton("Recuperar caso ja extraido...", recover_row);
    recover_button->setCursor(Qt::PointingHandCursor);
    auto* report_button = new QPushButton("Gerar laudo PDF", recover_row);
    report_button->setCursor(Qt::PointingHandCursor);
    recover_layout->addStretch(1);
    recover_layout->addWidget(recover_button);
    recover_layout->addWidget(report_button);
    connect(report_button, &QPushButton::clicked, this, [this]() { action_generate_report(); });
    root->addWidget(recover_row);
    connect(recover_button, &QPushButton::clicked, this, [this]() { action_recover_existing(); });

    // Shared log. Acquisition is a long conversation with a device; the operator
    // needs the transcript, not a spinner.
    auto* log_box = new QGroupBox("Registro", this);
    auto* log_layout = new QVBoxLayout(log_box);
    log_layout->setContentsMargins(6, 6, 6, 6);
    log_ = new QPlainTextEdit(log_box);
    log_->setReadOnly(true);
    log_->setStyleSheet(QString("background:%1; color:%2; border:none;"
                                "font-family:'Cascadia Mono','Consolas'; font-size:12px;")
                            .arg(theme::kOutputPane, theme::kTextOnDocument));
    log_layout->addWidget(log_);
    root->addWidget(log_box, 1);

    status_ = new QLabel("Parado.", this);
    status_->setStyleSheet(label_style());
    root->addWidget(status_);

    poll_ = new QTimer(this);
    poll_->setInterval(200);
    connect(poll_, &QTimer::timeout, this, [this]() { poll_job(); });
    poll_->start();
}

AcquireView::~AcquireView() {
    // A detached writer would outlive the widget and touch freed Qt objects.
    if (worker_ && worker_->joinable()) worker_->join();
}

void AcquireView::build_android_section(QVBoxLayout* root) {
    auto* box = new QGroupBox("Android (ADB)", this);
    auto* form = new QFormLayout(box);
    form->setContentsMargins(8, 8, 8, 8);

    auto* device_row = new QWidget(box);
    auto* device_layout = new QHBoxLayout(device_row);
    device_layout->setContentsMargins(0, 0, 0, 0);
    devices_ = new QComboBox(device_row);
    devices_->setMinimumWidth(260);
    detect_ = new QPushButton("Detectar dispositivos", device_row);
    detect_->setCursor(Qt::PointingHandCursor);
    device_layout->addWidget(devices_, 1);
    device_layout->addWidget(detect_);
    form->addRow("Dispositivo", device_row);

    android_password_ = new QLineEdit(box);
    android_password_->setEchoMode(QLineEdit::Password);
    android_password_->setPlaceholderText("senha do .ab (vazio se nao cifrado)");
    form->addRow("Senha do backup", android_password_);

    android_out_ = new QLineEdit(box);
    android_out_->setPlaceholderText("pasta de saida do caso");
    form->addRow("Saida", android_out_);

    auto* row = new QWidget(box);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    collect_android_ = new QPushButton("Coletar WhatsApp", row);
    collect_android_->setCursor(Qt::PointingHandCursor);
    row_layout->addStretch(1);
    row_layout->addWidget(collect_android_);
    form->addRow("", row);

    auto* note = new QLabel(
        "adb backup exige confirmacao no aparelho. Android 12+ pode bloquear o comando;"
        " nesse caso a coleta depende de root ou de imagem fisica.", box);
    note->setWordWrap(true);
    note->setStyleSheet(QString("color:%1; font-size:11px;").arg(theme::kTextMuted));
    form->addRow("", note);

    root->addWidget(box);

    connect(detect_, &QPushButton::clicked, this, [this]() { action_detect_devices(); });
    connect(collect_android_, &QPushButton::clicked, this, [this]() { action_collect_android(); });
}

void AcquireView::build_ios_section(QVBoxLayout* root) {
    auto* box = new QGroupBox("iOS (backup do Finder/iTunes)", this);
    auto* form = new QFormLayout(box);
    form->setContentsMargins(8, 8, 8, 8);

    auto* dir_row = new QWidget(box);
    auto* dir_layout = new QHBoxLayout(dir_row);
    dir_layout->setContentsMargins(0, 0, 0, 0);
    backup_dir_ = new QLineEdit(dir_row);
    backup_dir_->setPlaceholderText("pasta do backup (com Manifest.plist e Manifest.db)");
    choose_backup_ = new QPushButton("Escolher...", dir_row);
    choose_backup_->setCursor(Qt::PointingHandCursor);
    dir_layout->addWidget(backup_dir_, 1);
    dir_layout->addWidget(choose_backup_);
    form->addRow("Backup", dir_row);

    backup_password_ = new QLineEdit(box);
    backup_password_->setEchoMode(QLineEdit::Password);
    backup_password_->setPlaceholderText("senha do backup (vazio se nao cifrado)");
    form->addRow("Senha", backup_password_);

    ios_out_ = new QLineEdit(box);
    ios_out_->setPlaceholderText("pasta de saida da extracao");
    form->addRow("Saida", ios_out_);

    auto* row = new QWidget(box);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    extract_ios_ = new QPushButton("Extrair backup", row);
    extract_ios_->setCursor(Qt::PointingHandCursor);
    row_layout->addStretch(1);
    row_layout->addWidget(extract_ios_);
    form->addRow("", row);

    root->addWidget(box);

    connect(choose_backup_, &QPushButton::clicked, this, [this]() { action_choose_backup(); });
    connect(extract_ios_, &QPushButton::clicked, this, [this]() { action_extract_ios(); });
}

QString AcquireView::ensure_out_dir() {
    const QString base = QFileInfo(QCoreApplication::applicationDirPath()).absoluteFilePath();
    const QString dir = base + "/acquisitions/" +
                        QString::number(QDateTime::currentSecsSinceEpoch());
    QDir().mkpath(dir);
    return dir;
}

void AcquireView::append_log(const QString& line) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    pending_.push_back(to_utf8(line));
}

void AcquireView::set_busy(bool busy) {
    busy_ = busy;
    const bool enabled = !busy;
    detect_->setEnabled(enabled);
    collect_android_->setEnabled(enabled);
    choose_backup_->setEnabled(enabled);
    extract_ios_->setEnabled(enabled);
    status_->setText(busy ? "Trabalhando..." : "Parado.");
}

void AcquireView::start_job(const std::function<void()>& body) {
    if (busy_.load()) {
        append_log("[!] ja existe uma aquisicao em andamento");
        return;
    }
    if (worker_ && worker_->joinable()) worker_->join();
    done_ = false;
    set_busy(true);
    worker_.reset(new std::thread([this, body]() {
        try {
            body();
        } catch (const std::exception& e) {
            append_log(QString("[!] excecao: %1").arg(e.what()));
        } catch (...) {
            append_log("[!] excecao desconhecida");
        }
        done_ = true;
    }));
}

void AcquireView::poll_job() {
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        while (!pending_.empty()) {
            log_->appendPlainText(from_std(pending_.front()));
            pending_.pop_front();
        }
    }
    if (done_.load() && busy_.load()) {
        if (worker_ && worker_->joinable()) worker_->join();
        set_busy(false);
    }
}

void AcquireView::action_detect_devices() {
    start_job([this]() {
        olho::android::Config cfg;
        std::string error;
        if (!olho::android::resolve(cfg, error)) {
            append_log(QString("[!] adb indisponivel: %1").arg(from_std(error)));
            return;
        }
        append_log("[android] consultando dispositivos...");
        auto list = olho::android::list_devices(cfg, 15000);
        for (const auto& notice : list.notices) append_log("[adb] " + from_std(notice));
        if (!list.ok) {
            append_log(QString("[!] %1").arg(from_std(list.error)));
            return;
        }
        if (list.devices.empty()) {
            append_log("[android] nenhum dispositivo listado");
            return;
        }
        for (const auto& d : list.devices) {
            append_log(QString("  %1  estado=%2  modelo=%3")
                           .arg(from_std(d.serial), from_std(olho::android::to_string(d.state)),
                                from_std(d.model)));
        }
        const QString first_usable = [&]() -> QString {
            for (const auto& d : list.devices) {
                if (d.usable()) return from_std(d.serial);
            }
            return QString();
        }();
        if (first_usable.isEmpty()) {
            append_log("[android] nenhum dispositivo em estado 'device' (autorize no aparelho)");
            return;
        }
        QMetaObject::invokeMethod(this, [this, list, first_usable]() {
            devices_->clear();
            for (const auto& d : list.devices) {
                const QString label = from_std(d.serial) + "  [" +
                                      from_std(olho::android::to_string(d.state)) + "]" +
                                      (d.model.empty() ? QString()
                                                       : "  " + from_std(d.model));
                devices_->addItem(label, from_std(d.serial));
            }
            const int index = devices_->findData(first_usable);
            if (index >= 0) devices_->setCurrentIndex(index);
        }, Qt::QueuedConnection);
    });
}

void AcquireView::action_collect_android() {
    const QString serial = devices_->currentData().toString();
    if (serial.isEmpty()) {
        append_log("[!] nenhum dispositivo selecionado; rode a deteccao primeiro");
        return;
    }
    QString out_dir = android_out_->text().trimmed();
    if (out_dir.isEmpty()) {
        out_dir = ensure_out_dir();
        android_out_->setText(out_dir);
    }
    const QString password = android_password_->text();
    const QString ab_path = out_dir + "/whatsapp.ab";
    const QString extract_dir = out_dir + "/extracted";
    const QString case_dir = out_dir + "/case";

    start_job([this, serial, out_dir, password, ab_path, extract_dir, case_dir]() {
        olho::android::Config cfg;
        std::string error;
        if (!olho::android::resolve(cfg, error)) {
            append_log(QString("[!] adb indisponivel: %1").arg(from_std(error)));
            return;
        }

        // 1. Ask the device for its own backup. The user confirms on the handset;
        //    that prompt is the authorisation, so it is not skipped or automated.
        append_log("[android] solicitando backup de com.whatsapp (confirme no aparelho)...");
        const std::wstring cmd = L"\"" + cfg.resolved + L"\" -s " + to_wide(serial) +
                                 L" backup -f \"" + to_wide(ab_path) + L"\" -noapk com.whatsapp";
        auto run = olho::android::run_capture(cmd, 300000);
        if (!run.started) {
            append_log(QString("[!] falha ao executar adb: %1").arg(from_std(run.error)));
            return;
        }
        if (run.timed_out) {
            append_log("[!] adb backup expirou");
            return;
        }
        if (!run.err.empty()) append_log("[adb] " + from_std(run.err));

        const std::wstring wab = to_wide(ab_path);
        if (olho::android::file_size(wab) == 0) {
            append_log("[!] backup vazio: o aparelho recusou o comando (Android 12+ bloqueia)");
            return;
        }
        append_log(QString("[android] backup gravado: %1 bytes")
                       .arg(olho::android::file_size(wab)));

        // 2. Unpack it.
        append_log("[android] extraindo .ab...");
        olho::android::AbStats stats;
        if (!olho::android::extract_ab(wab, to_wide(extract_dir), to_utf8(password), stats)) {
            append_log(QString("[!] extracao falhou: %1").arg(from_std(stats.error)));
            return;
        }
        append_log(QString("  container=%1 cifrado=%2 integridade=%3")
                       .arg(from_std(stats.container))
                       .arg(stats.header.encrypted ? "sim" : "nao")
                       .arg(stats.integrity_ok ? "ok" : "FALHOU"));
        append_log(QString("  arquivos=%1 dirs=%2 bytes=%3")
                       .arg(stats.tar.files).arg(stats.tar.dirs).arg(stats.tar.bytes_written));
        for (const auto& rejected : stats.tar.rejected) {
            append_log("  recusado (tar-slip): " + from_std(rejected));
        }

        // 3. Locate WhatsApp and stage a case ready for wa_recover.
        olho::android::CaseStats case_stats;
        if (!olho::android::build_whatsapp_case(to_wide(extract_dir), to_wide(case_dir),
                                                "whatsapp", case_stats)) {
            append_log(QString("[!] WhatsApp nao localizado: %1")
                           .arg(from_std(case_stats.error)));
            return;
        }
        append_log(QString("  caso montado: %1 arquivos, %2 de midia")
                       .arg(case_stats.files).arg(case_stats.media_files));
        append_log("  comando equivalente: " + from_std(case_stats.command));

        // Close the loop: recover and hand the export to the Chat view.
        last_case_dir_ = case_dir;
        finish_whatsapp_case(case_dir);
        status_->setText("Android: caso montado.");
    });
}

void AcquireView::notify_case(const QString& conversations_json) {
    if (!case_handler_) return;
    const std::function<void(const QString&)> handler = case_handler_;
    QMetaObject::invokeMethod(this, [handler, conversations_json]() {
        handler(conversations_json);
    }, Qt::QueuedConnection);
}

void AcquireView::finish_whatsapp_case(const QString& case_dir) {
    const QString wa = QCoreApplication::applicationDirPath() + "/wa_recover.exe";
    if (!QFile::exists(wa)) {
        append_log("[!] wa_recover.exe nao esta ao lado do executavel; pule a recuperacao");
        return;
    }
    const QString db = case_dir + "/whatsapp/msgstore.db";
    const QString key = case_dir + "/whatsapp/key";
    const QString out = case_dir + "/recovered";
    if (!QFile::exists(db) || !QFile::exists(key)) {
        append_log("[!] caso incompleto: falta msgstore.db ou a chave");
        return;
    }

    append_log("[recuperacao] executando wa_recover...");
    QProcess proc;
    proc.setProgram(wa);
    proc.setArguments({db, "--key", key, "--out", out, "--media", case_dir + "/whatsapp"});
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    if (!proc.waitForStarted(10000)) {
        append_log("[!] nao iniciou wa_recover: " + proc.errorString());
        return;
    }
    proc.waitForFinished(-1);
    const QStringList lines = QString::fromUtf8(proc.readAllStandardOutput())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& text : lines) {
        if (!text.trimmed().isEmpty()) append_log("  " + text.trimmed());
    }
    if (proc.exitCode() != 0) {
        append_log(QString("[!] wa_recover saiu com codigo %1").arg(proc.exitCode()));
        return;
    }

    const QString conversations = out + "/conversations.json";
    if (!QFile::exists(conversations)) {
        append_log("[!] recuperacao terminou sem conversations.json");
        return;
    }
    append_log("[recuperacao] export pronto: " + conversations);
    notify_case(conversations);

    // Fecha o ciclo: recuperou, selou. O selo cobre banco, pastas e indice, e o
    // laudo passa a recusar o caso se algo mudar depois deste ponto.
    seal_case_after_recovery(case_dir);
}

void AcquireView::seal_case_after_recovery(const QString& case_dir) {
    const QString deep = QCoreApplication::applicationDirPath() + "/OlhoDeDeus_deep.exe";
    if (!QFile::exists(deep)) {
        append_log("[selo] OlhoDeDeus_deep.exe nao esta ao lado do executavel; "
                   "sele manualmente em Projeto > Selar caso");
        return;
    }
    if (case_dir.isEmpty() || !QDir(case_dir).exists()) {
        append_log("[selo] pasta do caso invalida; selo nao aplicado");
        return;
    }
    append_log("[selo] selando o caso (banco + pastas + indice)...");
    QProcess proc;
    proc.setProgram(deep);
    proc.setArguments({"--case-seal", "--case", case_dir});
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    if (!proc.waitForStarted(10000)) {
        append_log("[!] nao iniciou o selo: " + proc.errorString() +
                   " - sele manualmente em Projeto > Selar caso");
        return;
    }
    // Hashear o export inteiro leva tempo num caso grande; por isso isto roda no
    // job da recuperacao, nao na thread da interface. A leitura e incremental:
    // esperar o fim para so entao ler transformaria o progresso do filho em
    // silencio - o operador veria uma janela parada e concluiria "travou".
    QString pending;
    const auto drain = [&pending, this](bool flush_all) {
        int cut = pending.indexOf(QChar(10));
        while (cut >= 0) {
            const QString line = pending.left(cut).trimmed();
            pending.remove(0, cut + 1);
            if (!line.isEmpty()) append_log("  " + line);
            cut = pending.indexOf(QChar(10));
        }
        if (flush_all && !pending.trimmed().isEmpty()) {
            append_log("  " + pending.trimmed());
            pending.clear();
        }
    };
    while (proc.state() != QProcess::NotRunning) {
        proc.waitForReadyRead(500);
        pending += QString::fromUtf8(proc.readAllStandardOutput());
        drain(false);
    }
    pending += QString::fromUtf8(proc.readAllStandardOutput());
    drain(true);
    if (proc.exitCode() != 0) {
        append_log(QString("[!] selo falhou (codigo %1): o caso NAO esta selado - "
                           "rode Projeto > Selar caso e verifique o motivo")
                       .arg(proc.exitCode()));
        return;
    }
    append_log("[selo] caso selado e indexado; alteracao posterior reprova o laudo");
}

QStringList AcquireView::report_arguments(const QString& case_dir, const QString& out_path) const {
    QStringList args;
    args << case_dir + "/recovered" << "--out" << out_path;
    // O laudo cita o que o perito exportou. A pasta e convencional em dois lugares
    // porque a GUI exporta para <caso>/exports e a recuperacao vive em
    // <caso>/recovered: checar os dois evita "exportei e o laudo nao viu".
    const QStringList candidates = {
        case_dir + "/exports",
        case_dir + "/recovered/exports",
    };
    for (const QString& dir : candidates) {
        QDir folder(dir);
        if (!folder.exists()) continue;
        const QStringList entries = folder.entryList({"*.csv", "*.jsonl"}, QDir::Files,
                                                     QDir::Name);
        if (entries.isEmpty()) continue;
        args << "--recorte" << dir;
        break;   // uma pasta basta: varias so duplicariam os anexos
    }
    return args;
}

void AcquireView::action_generate_report() {
    if (last_case_dir_.isEmpty() || !QDir(last_case_dir_ + "/recovered").exists()) {
        append_log("[!] nenhum caso recuperado nesta sessao; rode uma recuperacao primeiro");
        return;
    }
    const QString suggested = last_case_dir_ + "/laudo.pdf";
    const QString out = QFileDialog::getSaveFileName(this, "Salvar laudo", suggested,
                                                     "PDF (*.pdf)");
    if (out.isEmpty()) return;

    const QString tool = QCoreApplication::applicationDirPath() + "/olho_report.exe";
    if (!QFile::exists(tool)) {
        append_log("[!] olho_report.exe nao esta ao lado do executavel");
        return;
    }
    // Os argumentos saem de report_arguments(): a mesma funcao que o modo evidencia
    // imprime, entao o que o teste prova e o que o botao faz.
    const QStringList arguments = report_arguments(last_case_dir_, out);
    const bool with_recortes = arguments.contains(QStringLiteral("--recorte"));
    start_job([this, tool, out, arguments, with_recortes]() {
        append_log("[laudo] gerando PDF do caso " + last_case_dir_);
        if (with_recortes) {
            append_log("[laudo] os recortes exportados do caso vao ser citados e verificados");
        }
        QProcess proc;
        proc.setProgram(tool);
        proc.setArguments(arguments);
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start();
        if (!proc.waitForStarted(10000)) {
            append_log("[!] nao iniciou olho_report: " + proc.errorString());
            return;
        }
        proc.waitForFinished(-1);
        const QStringList lines = QString::fromUtf8(proc.readAllStandardOutput())
                                      .split(QChar(10), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            if (!line.trimmed().isEmpty()) append_log("  " + line.trimmed());
        }
        if (proc.exitCode() != 0) {
            // O codigo 4 e o gate do selo: o laudo foi RECUSADO porque a prova
            // nao confere. Dizer so "saiu com 4" esconde o unico fato que importa.
            if (proc.exitCode() == 4) {
                append_log("[!] laudo RECUSADO: o selo de integridade do caso nao confere");
                append_log("    nada foi escrito. Use Projeto > Verificar selo do caso para o diagnostico;");
                append_log("    para emitir o laudo documentando a violacao: --allow-broken-seal");
            } else {
                append_log(QString("[!] olho_report saiu com codigo %1").arg(proc.exitCode()));
            }
            return;
        }
        append_log("[laudo] pronto: " + out);
    });
}

void AcquireView::action_recover_existing() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Pasta que contem whatsapp/msgstore.db");
    if (dir.isEmpty()) return;
    const QString parent = QFileInfo(dir).absolutePath();
    const QString candidate = (QFileInfo(dir).fileName() == "whatsapp") ? parent : dir;
    start_job([this, candidate]() {
        append_log("[caso] recuperando de " + candidate);
        finish_whatsapp_case(candidate);
    });
}

void AcquireView::action_choose_backup() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Pasta do backup iOS");
    if (!dir.isEmpty()) backup_dir_->setText(dir);
}

void AcquireView::action_extract_ios() {
    const QString dir = backup_dir_->text().trimmed();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        append_log("[!] selecione uma pasta de backup valida");
        return;
    }
    QString out_dir = ios_out_->text().trimmed();
    if (out_dir.isEmpty()) {
        out_dir = ensure_out_dir();
        ios_out_->setText(out_dir);
    }
    const QString password = backup_password_->text();

    start_job([this, dir, out_dir, password]() {
        olho::ios::Backup backup;
        std::string error;
        append_log("[ios] abrindo backup...");
        if (!olho::ios::backup_open(to_utf8(dir), backup, error)) {
            append_log(QString("[!] %1").arg(from_std(error)));
            return;
        }
        append_log(QString("  dispositivo=%1 iOS=%2 entradas=%3 cifrado=%4")
                       .arg(from_std(backup.device_name))
                       .arg(from_std(backup.product_version))
                       .arg(backup.entry_count)
                       .arg(backup.is_encrypted ? "sim" : "nao"));

        if (backup.is_encrypted) {
            if (password.isEmpty()) {
                append_log("[!] backup cifrado: informe a senha");
                return;
            }
            append_log("[ios] derivando chave (PBKDF2 + AES-KW)...");
            if (!olho::ios::backup_unlock(backup, to_utf8(password), error)) {
                append_log(QString("[!] senha recusada ou keybag invalido: %1")
                               .arg(from_std(error)));
                return;
            }
            append_log("  keybag desembrulhado");
        }

        append_log("[ios] extraindo...");
        olho::ios::ExtractResult result;
        if (!olho::ios::backup_extract(backup, to_utf8(out_dir), result, error)) {
            append_log(QString("[!] extracao falhou: %1").arg(from_std(error)));
            return;
        }
        append_log(QString("  arquivos=%1 dirs=%2 bytes=%3 falhas=%4")
                       .arg(result.files_written).arg(result.directories_created)
                       .arg(result.bytes_written).arg(result.files_failed));
        for (const auto& failure : result.failures) {
            append_log("  falha: " + from_std(failure));
        }

        const std::string manifest = to_utf8(out_dir) + "/manifest.json";
        if (olho::ios::backup_write_manifest_json(backup, result, manifest, error)) {
            append_log("  manifesto: " + from_std(manifest));
        }

        // A backup routinely contains WhatsApp's own database. Stage it the same
        // way as the Android path so both ends feed one recovery engine.
        olho::android::WaCase wa_case;
        std::string wa_error;
        if (olho::android::locate_whatsapp(to_wide(out_dir), wa_case, wa_error) && wa_case.ok) {
            append_log("[ios] WhatsApp encontrado no backup: " + from_std(wa_case.detail));
            const QString case_dir = out_dir + "/case";
            olho::android::CaseStats case_stats;
            if (olho::android::build_whatsapp_case(to_wide(out_dir), to_wide(case_dir),
                                                   "whatsapp", case_stats)) {
                append_log(QString("  caso montado: %1 arquivos").arg(case_stats.files));
                finish_whatsapp_case(case_dir);
            } else {
                append_log(QString("[!] falha ao montar caso: %1").arg(from_std(case_stats.error)));
            }
        } else {
            append_log("[ios] sem msgstore do WhatsApp neste backup (ou sem chave)");
        }
        status_->setText("iOS: backup extraido.");
    });
}

}  // namespace olho::ui
