// language: C++, file: gui_qt.cpp, target: Windows 10/11
// Qt 6 Widgets shell for the deep engine. Layout inspired by modern IDE and
// forensic suites: activity sidebar, case context, card-based workspace and a
// persistent console. Every action still invokes OlhoDeDeus_deep.exe.

#include "gui.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QUrl>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QAbstractScrollArea>
#include <QCryptographicHash>
#include <QTextCharFormat>
#include <QTextStream>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QVector>
#include <QAction>
#include <QDateTime>
#include <QDockWidget>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QPainter>
#include <QScrollBar>
#include <QMouseEvent>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QStandardItemModel>
#include <QTableView>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTabBar>
#include <QToolBar>
#include <QVariantAnimation>
#include <QToolButton>
#include <QTreeView>
#include "cloud/core/rbac.hpp"
#include "cloud/core/oauth_pkce.hpp"
#include "cloud/core/oauth_callback.hpp"
#include "cloud/core/winhttp_transport.hpp"
#include "cloud/core/oidc.hpp"
#include "cloud/enterprise/identity.hpp"
#include "cloud/service/job_queue.hpp"
#include "cloud/cli/auth_flow.hpp"
#include "cloud/enterprise/secret_store.hpp"
#include "cloud/enterprise/session.hpp"
#include "cloud/enterprise/audit.hpp"
#include "cloud/enterprise/session_vault.hpp"
#include "manifest_sign.hpp"
#include <optional>
#include <memory>
#include <chrono>
#include <shellapi.h>
#include <windows.h>
#include <psapi.h>

#ifndef PROCESS_SUSPEND_RESUME
#define PROCESS_SUSPEND_RESUME 0x0800
#endif
#include <QWidget>

#include <functional>

namespace deep {
namespace {

QString q(const QString& value) { return value; }

class HexView : public QAbstractScrollArea {
public:
    explicit HexView(QWidget* parent = nullptr) : QAbstractScrollArea(parent) {
        setFrameShape(QFrame::NoFrame);
        setFont(QFont("Consolas", 9));
        setMinimumHeight(260);
        data_.resize(4096);
        for (int i = 0; i < data_.size(); ++i) {
            data_[i] = static_cast<char>((i * 37 + 0x1f) & 0xff);
        }
        verticalScrollBar()->setSingleStep(18);
        verticalScrollBar()->setPageStep(12);
    }
    void set_data(const QByteArray& data) {
        data_ = data;
        update_scrollbar();
        verticalScrollBar()->setValue(0);
        viewport()->update();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QAbstractScrollArea::resizeEvent(event);
        update_scrollbar();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), QColor("#121212"));
        painter.setFont(font());
        const int line = 18;
        const int rows = data_.size() / 16;
        const int first = verticalScrollBar()->value();
        const int visible = viewport()->height() / line + 1;
        for (int r = first; r < qMin(rows, first + visible); ++r) {
            const int y = (r - first) * line + 3;
            painter.setPen(QColor("#6E6E6E"));
            painter.drawText(10, y + 12, QString("%1").arg(r * 16, 8, 16, QChar('0')).toUpper());
            painter.setPen(QColor("#D7D7D7"));
            QString hex;
            QString ascii;
            for (int c = 0; c < 16; ++c) {
                const unsigned char value = static_cast<unsigned char>(data_[r * 16 + c]);
                hex += QString("%1 ").arg(value, 2, 16, QChar('0')).toUpper();
                ascii += (value >= 32 && value < 127) ? QChar(value) : QChar('.');
            }
            painter.drawText(108, y + 12, hex);
            painter.setPen(QColor("#9CDCFE"));
            painter.drawText(108 + 16 * 3 * 7 + 24, y + 12, ascii);
        }
    }
    void wheelEvent(QWheelEvent* event) override {
        verticalScrollBar()->setValue(verticalScrollBar()->value() - event->angleDelta().y() / 120 * 3);
        viewport()->update();
    }

private:
    void update_scrollbar() {
        const int rows = data_.size() / 16;
        const int visible = viewport()->height() / 18;
        verticalScrollBar()->setRange(0, qMax(0, rows - visible));
        verticalScrollBar()->setPageStep(qMax(1, visible));
    }
    QByteArray data_;
};

class TimelineView : public QWidget {
public:
    explicit TimelineView(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(280);
    }

public:
    void set_events(const QVector<QDateTime>& times, const QStringList& labels) {
        times_ = times;
        labels_ = labels;
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#121212"));
        const QRect area = rect().adjusted(54, 34, -26, -44);
        painter.setPen(QPen(QColor("#333333"), 1));
        painter.drawLine(area.left(), area.bottom(), area.right(), area.bottom());
        painter.drawLine(area.left(), area.top(), area.left(), area.bottom());
        painter.setFont(QFont("Segoe UI", 8));
        painter.setPen(QColor("#8A8A8A"));
        for (int i = 0; i <= 4; ++i) {
            const int x = area.left() + area.width() * i / 4;
            painter.drawLine(x, area.bottom(), x, area.bottom() + 6);
            QString axis = QString("2026-10-%1").arg(20 + i);
            if (!times_.isEmpty()) {
                const qint64 span = qMax<qint64>(1, times_.first().msecsTo(times_.last()));
                axis = times_.first().addMSecs(span * i / 4).toString("MM-dd HH:mm");
            }
            painter.drawText(x - 30, area.bottom() + 22, axis);
        }
        if (!times_.isEmpty()) {
            const QDateTime first = times_.first();
            const QDateTime last = times_.last();
            const qint64 span = qMax<qint64>(1, first.msecsTo(last));
            for (int i = 0; i < times_.size(); ++i) {
                const double ratio = static_cast<double>(first.msecsTo(times_[i])) / span;
                const QPointF point(area.left() + area.width() * ratio,
                                    area.top() + area.height() * (0.25 + 0.5 * (i % 5) / 4.0));
                painter.setPen(QPen(QColor("#444444"), 1, Qt::DashLine));
                painter.drawLine(QPointF(point.x(), area.bottom()), point);
                painter.setBrush(QColor("#B0B0B0"));
                painter.setPen(QPen(QColor("#E8E8E8"), 1));
                painter.drawEllipse(point, 3, 3);
                if (i % qMax(1, times_.size() / 12) == 0) {
                    painter.setPen(QColor("#D0D0D0"));
                    painter.setFont(QFont("Segoe UI", 8));
                    painter.drawText(point + QPointF(6, -5), labels_.value(i));
                }
            }
        } else {
        struct Event { double x; double y; const char* label; const char* time; };
        const Event events[] = {
            {0.10, 0.30, "Aquisicao iniciada", "08:12"},
            {0.28, 0.62, "$MFT parseado", "08:19"},
            {0.46, 0.40, "USN journal", "08:27"},
            {0.62, 0.72, "Browser artifacts", "08:41"},
            {0.78, 0.28, "EVTX correlacionado", "09:03"},
            {0.91, 0.54, "Relatorio gerado", "09:18"}
        };
        for (const Event& event : events) {
            const QPointF point(area.left() + area.width() * event.x,
                                area.top() + area.height() * (1.0 - event.y));
            painter.setPen(QPen(QColor("#555555"), 1, Qt::DashLine));
            painter.drawLine(QPointF(point.x(), area.bottom()), point);
            painter.setBrush(QColor("#B0B0B0"));
            painter.setPen(QPen(QColor("#E8E8E8"), 1));
            painter.drawEllipse(point, 4, 4);
            painter.setPen(QColor("#D0D0D0"));
            painter.drawText(point + QPointF(8, -7), event.label);
            painter.setPen(QColor("#777777"));
            painter.drawText(point + QPointF(8, 9), event.time);
        }
        }
        painter.setPen(QColor("#B0B0B0"));
        painter.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        painter.drawText(18, 22, "TIMELINE / EVENTOS CORRELACIONADOS");
    }
private:
    QVector<QDateTime> times_;
    QStringList labels_;
};
class ToolTile : public QAbstractButton {
public:
    ToolTile(const QIcon& icon, const QString& text, QWidget* parent = nullptr)
        : QAbstractButton(parent), icon_(icon), text_(text) {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(54);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFocusPolicy(Qt::NoFocus);
        animation_ = new QVariantAnimation(this);
        animation_->setDuration(150);
        animation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                hover_ = value.toReal();
                update();
            });
    }
    void set_active(bool active) { active_ = active; update(); }
    QSize sizeHint() const override { return QSize(128, 54); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = rect().adjusted(1, 1, -1, -1);
        const QColor base("#1A1A1A");
        const QColor hover("#2A2A2A");
        const qreal strength = active_ ? 1.0 : hover_;
        QColor fill(
            base.red() + static_cast<int>((hover.red() - base.red()) * strength),
            base.green() + static_cast<int>((hover.green() - base.green()) * strength),
            base.blue() + static_cast<int>((hover.blue() - base.blue()) * strength));
        painter.setBrush(fill);
        QColor border = active_ ? QColor("#2D2D2D") : QColor("#2D2D2D");
        if (!active_ && hover_ > 0.01) {
            border = QColor(45 + static_cast<int>(47 * hover_),
                            45 + static_cast<int>(8 * hover_),
                            45 + static_cast<int>(100 * hover_));
        }
        painter.setPen(QPen(border, active_ ? 1.5 : 1.0));
        painter.drawRoundedRect(box, 4, 4);
        if (active_) {
            painter.fillRect(QRectF(0, 0, 3, height()), QColor("#7A3FB3"));
        }
        const QPixmap pixmap = icon_.pixmap(24, 24);
        painter.drawPixmap(10, (height() - 24) / 2, pixmap);
        painter.setPen(active_ || hover_ > 0.4 ? QColor("#FFFFFF") : QColor("#B0B0B0"));
        painter.setFont(QFont("Segoe UI", 9.5));
        painter.drawText(QRect(44, 0, width() - 50, height()),
                         Qt::AlignLeft | Qt::AlignVCenter, text_);
    }
    void enterEvent(QEnterEvent*) override {
        animation_->stop();
        animation_->setStartValue(hover_);
        animation_->setEndValue(1.0);
        animation_->start();
    }
    void leaveEvent(QEvent*) override {
        animation_->stop();
        animation_->setStartValue(hover_);
        animation_->setEndValue(0.0);
        animation_->start();
    }

private:
    QIcon icon_;
    QString text_;
    QVariantAnimation* animation_ = nullptr;
    qreal hover_ = 0.0;
    bool active_ = false;
};
class RowHoverDelegate : public QStyledItemDelegate {
public:
    explicit RowHoverDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}
    void set_hover(int row, qreal alpha) { row_ = row; alpha_ = alpha; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const bool selected = (opt.state & QStyle::State_Selected) != 0;
        if (!selected && index.row() == row_ && alpha_ > 0.01) {
            QColor hover(36, 36, 36);
            hover.setAlphaF(static_cast<float>(alpha_));
            painter->fillRect(option.rect, hover);
            opt.backgroundBrush = hover;
        }
        opt.state &= ~QStyle::State_MouseOver;
        QStyledItemDelegate::paint(painter, opt, index);
    }
private:
    int row_ = -1;
    qreal alpha_ = 0.0;
};

class HoverTableView : public QTableView {
public:
    explicit HoverTableView(QWidget* parent = nullptr) : QTableView(parent) {
        setMouseTracking(true);
        animation_ = new QVariantAnimation(this);
        animation_->setDuration(120);
        animation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                if (delegate_) delegate_->set_hover(hover_row_, value.toReal());
                viewport()->update();
            });
        connect(animation_, &QVariantAnimation::finished, this, [this]() {
            if (delegate_) delegate_->set_hover(-1, 0.0);
            hover_row_ = -1;
            viewport()->update();
        });
    }
    void set_hover_delegate(RowHoverDelegate* delegate) { delegate_ = delegate; }

protected:
    void mouseMoveEvent(QMouseEvent* event) override {
        QTableView::mouseMoveEvent(event);
        const int row = indexAt(event->pos()).row();
        if (row == hover_row_) return;
        hover_row_ = row;
        if (delegate_) delegate_->set_hover(hover_row_, 1.0);
        viewport()->update();
    }
    void leaveEvent(QEvent* event) override {
        QTableView::leaveEvent(event);
        animation_->stop();
        animation_->setStartValue(1.0);
        animation_->setEndValue(0.0);
        animation_->start();
    }

private:
    RowHoverDelegate* delegate_ = nullptr;
    QVariantAnimation* animation_ = nullptr;
    int hover_row_ = -1;
};
class MainWindow : public QMainWindow {
public:
    MainWindow() {
        cloud_sessions_ = std::make_unique<deep::cloud::SessionManager>(8 * 60 * 60);
        cloud_session_vault_ = std::make_unique<deep::cloud::SessionVault>(
            session_vault_path().toUtf8().constData());
        setWindowTitle("Olho de Deus - Deep Engine");
        setMinimumSize(1180, 760);
        build_ui();
        restore_persisted_session();
        update_session_label();
        build_process();
        cloud_timer_ = new QTimer(this);
        connect(cloud_timer_, &QTimer::timeout, this, [this]() {
            if (cloud_status_ != nullptr) refresh_cloud_status();
        });
        cloud_timer_->start(1200);
        QTimer::singleShot(0, this, [this]() { load_case(); });
    }

private:
    QVector<QWidget*> tool_pages_;
    QVector<ToolTile*> tool_tiles_;
    QLabel* mode_label_ = nullptr;
    int active_tool_ = -1;
    QLabel* status_label_ = nullptr;
    QPlainTextEdit* console_ = nullptr;
    QProcess* process_ = nullptr;
    QString log_buffer_;

    QLineEdit* image_ = nullptr;
    QLineEdit* out_ = nullptr;
    QLineEdit* evtx_ = nullptr;
    QLineEdit* prefetch_ = nullptr;
    QLineEdit* hive_ = nullptr;
    QLineEdit* srum_ = nullptr;
    QLineEdit* aff4_ = nullptr;
    QLineEdit* e01_ = nullptr;
    QCheckBox* list_ = nullptr;
    QCheckBox* recover_ = nullptr;
    QCheckBox* usn_ = nullptr;
    QCheckBox* carve_ = nullptr;
    QCheckBox* secrets_ = nullptr;

    QLineEdit* adv_image_ = nullptr;
    QLineEdit* adv_out_ = nullptr;
    QLineEdit* adv_e01_ = nullptr;
    QLineEdit* adv_volume_ = nullptr;
    QLineEdit* adv_target_ = nullptr;
    QLineEdit* adv_key_ = nullptr;
    QLineEdit* adv_command_ = nullptr;

    QLineEdit* san_disk_ = nullptr;
    QLineEdit* san_serial_ = nullptr;
    QLineEdit* san_password_ = nullptr;
    QLineEdit* san_passes_ = nullptr;
    QLineEdit* san_action_ = nullptr;
    QLineEdit* san_out_ = nullptr;
    QCheckBox* san_enabled_ = nullptr;
    QCheckBox* san_verify_ = nullptr;
    QCheckBox* san_enhanced_ = nullptr;

    QLineEdit* aud_out_ = nullptr;
    QLineEdit* wipe_target_ = nullptr;
    QLineEdit* wipe_out_ = nullptr;
    QCheckBox* wipe_enabled_ = nullptr;

    QLineEdit* kernel_disk_ = nullptr;
    QLineEdit* kernel_offset_ = nullptr;
    QLineEdit* kernel_length_ = nullptr;
    QLineEdit* kernel_out_ = nullptr;
    QLineEdit* cloud_tenant_ = nullptr;
    QLineEdit* cloud_client_ = nullptr;
    QLineEdit* cloud_account_ = nullptr;
    QLineEdit* cloud_vault_ = nullptr;
    QLineEdit* cloud_scope_ = nullptr;
    QLineEdit* cloud_since_ = nullptr;
    QLineEdit* cloud_until_ = nullptr;
    QLineEdit* cloud_legal_ = nullptr;
    QLineEdit* cloud_job_ = nullptr;
    QLineEdit* cloud_case_ = nullptr;
    QCheckBox* cloud_attachments_ = nullptr;
    QLabel* cloud_status_ = nullptr;
    QLabel* cloud_hint_ = nullptr;
    QLabel* cloud_session_label_ = nullptr;
    QLineEdit* cloud_service_url_ = nullptr;
    QLineEdit* cloud_service_user_ = nullptr;
    QLineEdit* cloud_service_pass_ = nullptr;
    QLabel* cloud_service_status_ = nullptr;
    QString cloud_service_token_;
    QString cloud_service_issuer_;
    QString cloud_service_client_id_;
    QString cloud_vault_ref_;
    QJsonArray cloud_service_jobs_;
    QPushButton* cloud_login_button_ = nullptr;
    QPushButton* cloud_users_button_ = nullptr;
    QPushButton* cloud_audit_button_ = nullptr;
    std::unique_ptr<deep::cloud::SessionManager> cloud_sessions_;
    std::unique_ptr<deep::cloud::SessionVault> cloud_session_vault_;
    std::optional<deep::cloud::Principal> cloud_principal_;
    std::string cloud_session_token_;
    QTimer* cloud_timer_ = nullptr;
    QComboBox* cloud_jobs_ = nullptr;
    QPushButton* cloud_pause_ = nullptr;
    QPushButton* cloud_cancel_ = nullptr;
    bool cloud_paused_ = false;

    QTreeView* tree_ = nullptr;
    QStandardItemModel* tree_model_ = nullptr;
    QSortFilterProxyModel* tree_proxy_ = nullptr;
    QLineEdit* tree_search_ = nullptr;
    QTableView* table_ = nullptr;
    QStandardItemModel* table_model_ = nullptr;
    QSortFilterProxyModel* table_proxy_ = nullptr;
    QLineEdit* table_search_ = nullptr;
    QLabel* table_count_ = nullptr;
    HexView* hex_view_ = nullptr;
    TimelineView* timeline_view_ = nullptr;
    QVector<QByteArray> row_raw_;
    QVector<QDateTime> row_time_;
    QStandardItem* tree_case_item_ = nullptr;
    QStandardItem* tree_device_item_ = nullptr;
    QStandardItem* tree_evidence_ = nullptr;
    QStandardItem* tree_cloud_ = nullptr;
    QString case_dir_;
    QTabWidget* center_tabs_ = nullptr;
    QTabWidget* bottom_tabs_ = nullptr;
    QDockWidget* bottom_dock_ = nullptr;
    QFormLayout* details_form_ = nullptr;
    QLabel* details_name_ = nullptr;
    QLabel* details_path_ = nullptr;
    QLabel* details_size_ = nullptr;
    QLabel* details_hash_ = nullptr;
    QLabel* details_status_ = nullptr;
    QLabel* details_note_ = nullptr;
    QLabel* details_type_ = nullptr;
    QLabel* details_offset_ = nullptr;
    QLabel* details_created_ = nullptr;
    QLabel* details_modified_ = nullptr;
    QLabel* details_confidence_ = nullptr;
    QLabel* memory_label_ = nullptr;
    QLabel* database_label_ = nullptr;
    QString cli() const {
        const QString sibling = QCoreApplication::applicationDirPath() +
            "/OlhoDeDeus_deep.exe";
        return QFileInfo::exists(sibling) ? sibling
            : QCoreApplication::applicationFilePath();
    }

    QLabel* title(const QString& text, int size, const QString& color = "#e8f1f8") {
        auto* label = new QLabel(text);
        label->setStyleSheet(QString("color:%1;font-size:%2pt;font-weight:600;")
            .arg(color).arg(size));
        return label;
    }

    QLabel* muted(const QString& text) {
        auto* label = new QLabel(text);
        label->setObjectName("muted");
        label->setWordWrap(true);
        return label;
    }

    static QString format_bytes(qint64 bytes) {
        if (bytes < 1024) return QString("%1 B").arg(bytes);
        if (bytes < 1024LL * 1024)
            return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
        if (bytes < 1024LL * 1024 * 1024)
            return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
    }

    static QString compact_timestamp(const QString& raw) {
        QDateTime when = QDateTime::fromString(raw, Qt::ISODateWithMs);
        if (!when.isValid()) when = QDateTime::fromString(raw, Qt::ISODate);
        if (!when.isValid()) return raw.isEmpty() ? QString("-") : raw;
        return when.toUTC().toString("yyyy-MM-dd HH:mm");
    }

    QString cloud_case_base() const {
        QString base = cloud_case_ ? cloud_case_->text().trimmed() : QString();
        if (base.isEmpty()) base = case_dir_;
        return base;
    }

    void set_cloud_hint(const QString& text, bool error = false) {
        if (!cloud_hint_) return;
        cloud_hint_->setText(text);
        cloud_hint_->setProperty("error", error);
        cloud_hint_->setVisible(!text.isEmpty());
        cloud_hint_->style()->unpolish(cloud_hint_);
        cloud_hint_->style()->polish(cloud_hint_);
    }

    bool ensure_cloud_case(const QString& action_text) {
        if (!cloud_case_base().isEmpty()) return true;
        set_cloud_hint("Selecione a pasta do caso antes de " + action_text + ".", true);
        return false;
    }

    void update_cloud_process_buttons() {
        const bool running = process_ != nullptr &&
            process_->state() != QProcess::NotRunning;
        if (cloud_pause_) {
            const bool has_pause_job = cloud_jobs_ &&
                !cloud_jobs_->currentData().toString().isEmpty();
            cloud_pause_->setEnabled(has_pause_job);
            cloud_pause_->setToolTip(has_pause_job
                ? "Pausar ou retomar o job selecionado."
                : "Nenhum job selecionado.");
        }
        if (cloud_cancel_) {
            const bool has_job = cloud_jobs_ && !cloud_jobs_->currentData().toString().isEmpty();
            cloud_cancel_->setEnabled(running || has_job);
            cloud_cancel_->setToolTip(running || has_job
                ? "Cancelar o job preservando o checkpoint."
                : "Nenhum job selecionado.");
        }
    }

    QGroupBox* card(const QString& title_text, QVBoxLayout* parent) {
        auto* group = new QGroupBox(title_text);
        group->setObjectName("card");
        parent->addWidget(group);
        return group;
    }

    QLineEdit* path_field(QFormLayout* form, const QString& label,
                          bool folder, const QString& filter = QString()) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto* edit = new QLineEdit;
        edit->setPlaceholderText(label);
        layout->addWidget(edit, 1);
        auto* browse = new QPushButton("...");
        browse->setFixedWidth(42);
        layout->addWidget(browse);
        form->addRow(label, row);
        connect(browse, &QPushButton::clicked, this, [this, edit, folder, filter]() {
            QString value;
            if (folder) value = QFileDialog::getExistingDirectory(this, "Selecionar pasta");
            else value = QFileDialog::getOpenFileName(this, "Selecionar arquivo",
                QString(), filter.isEmpty() ? "Todos (*.*)" : filter);
            if (!value.isEmpty()) edit->setText(QDir::toNativeSeparators(value));
        });
        return edit;
    }

    QLineEdit* plain_field(QFormLayout* form, const QString& label,
                           const QString& placeholder = QString()) {
        auto* edit = new QLineEdit;
        edit->setPlaceholderText(placeholder.isEmpty() ? label : placeholder);
        form->addRow(label, edit);
        return edit;
    }
    QPushButton* action(const QString& text, std::function<void()> fn,
                        bool danger = false, const QString& icon_name = QString()) {
        auto* button = new QPushButton(text);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(34);
        if (!icon_name.isEmpty()) {
            button->setIcon(icon(icon_name));
            button->setIconSize(QSize(16, 16));
        }
        if (danger) button->setProperty("danger", true);
        connect(button, &QPushButton::clicked, this, fn);
        return button;
    }

    void add_actions(QGridLayout* grid, const QList<QPair<QString,
        std::function<void()>>>& actions, int columns = 4) {
        int row = 0;
        int col = 0;
        for (const auto& entry : actions) {
            grid->addWidget(action(entry.first, entry.second), row, col);
            if (++col == columns) { col = 0; ++row; }
        }
    }

    void check_row(QHBoxLayout* layout, QCheckBox*& box, const QString& text,
                   bool checked = false) {
        box = new QCheckBox(text);
        box->setChecked(checked);
        layout->addWidget(box);
    }

    QWidget* page_pericia() {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto* body = new QWidget;
        auto* root = new QVBoxLayout(body);
        root->setContentsMargins(18, 16, 18, 18);
        root->setSpacing(12);
        root->addWidget(title("Pericia", 18));
        root->addWidget(muted("Aquisicao, analise NTFS, artefatos e empacotamento do caso."));
        auto* paths = card("FONTES E SAIDA", root);
        auto* form = new QFormLayout(paths);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        image_ = path_field(form, "Imagem / disco", false,
            "Imagens (*.raw *.img *.dd *.E01);;Todos (*.*)");
        out_ = path_field(form, "Pasta de saida", true);
        evtx_ = path_field(form, "EVTX", false, "EVTX (*.evtx);;Todos (*.*)");
        prefetch_ = path_field(form, "Prefetch dir", true);
        hive_ = path_field(form, "Hive", false, "Hives (*.*);;Todos (*.*)");
        srum_ = path_field(form, "SRUM", false, "SRUM (SRUDB.dat);;Todos (*.*)");
        aff4_ = path_field(form, "AFF4", false, "AFF4 (*.aff4);;Todos (*.*)");
        e01_ = path_field(form, "E01", false, "E01 (*.E01);;Todos (*.*)");
        auto* options = card("OPCOES DE ANALISE", root);
        auto* options_layout = new QHBoxLayout(options);
        check_row(options_layout, list_, "Listar MFT", true);
        check_row(options_layout, recover_, "Recuperar", true);
        check_row(options_layout, usn_, "USN");
        check_row(options_layout, carve_, "Carving");
        check_row(options_layout, secrets_, "Browser secrets");
        auto* actions = card("ACOES", root);
        auto* grid = new QGridLayout(actions);
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(8);
        add_actions(grid, {
            {"Escanear imagem", [this]() { scan(); }},
            {"Coletar artefatos", [this]() { artifacts(); }},
            {"Criar raw", [this]() { raw_image(); }},
            {"Criar AFF4", [this]() { aff4_create(); }},
            {"Verificar AFF4", [this]() { aff4_verify(); }},
            {"Criar E01", [this]() { e01_create(); }},
            {"Escanear E01", [this]() { e01_scan(); }},
            {"Limpar console", [this]() { console_->clear(); }}
        });
        scroll->setWidget(body);
        return scroll;
    }

    QWidget* page_advanced() {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto* body = new QWidget;
        auto* root = new QVBoxLayout(body);
        root->setContentsMargins(18, 16, 18, 18);
        root->setSpacing(12);
        root->addWidget(title("Avancado", 18));
        root->addWidget(muted("VSS, imagem, caso, assinatura, recycle e ferramentas de baixo nivel."));
        auto* paths = card("FERRAMENTAS", root);
        auto* form = new QFormLayout(paths);
        adv_image_ = path_field(form, "Imagem", false, "Imagens (*.raw *.img *.dd);;Todos (*.*)");
        adv_out_ = path_field(form, "Pasta de saida", true);
        adv_e01_ = path_field(form, "E01", false, "E01 (*.E01);;Todos (*.*)");
        adv_volume_ = path_field(form, "Volume VSS", false);
        adv_target_ = path_field(form, "Alvo / caso", false);
        adv_key_ = path_field(form, "Chave .priv/.pub", false);
        auto* actions = card("ACOES AVANCADAS", root);
        auto* grid = new QGridLayout(actions);
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(8);
        add_actions(grid, {
            {"Verificar imagem", [this]() { image_verify(); }},
            {"VSS list", [this]() { vss_list(); }},
            {"VSS read", [this]() { vss_read(); }},
            {"EVTX da imagem", [this]() { image_evtx(); }},
            {"Prefetch da imagem", [this]() { image_prefetch(); }},
            {"Recover recycle", [this]() { recycle(); }},
            {"Case query", [this]() { case_query(); }},
            {"Case summary", [this]() { case_summary(); }},
            {"Keygen", [this]() { keygen(); }},
            {"Assinar manifesto", [this]() { sign_manifest(); }},
            {"Verificar assinatura", [this]() { verify_manifest(); }},
            {"Deep USN file", [this]() { deep_usn(); }},
            {"Listar discos", [this]() { list_disks(); }}
        });
        auto* raw = card("COMANDO CLI LIVRE", root);
        auto* raw_layout = new QHBoxLayout(raw);
        adv_command_ = new QLineEdit;
        adv_command_->setPlaceholderText("Ex.: --deep-help   ou   --image-verify disco.raw");
        raw_layout->addWidget(adv_command_, 1);
        raw_layout->addWidget(action("Executar", [this]() { run_raw_command(); }));
        scroll->setWidget(body);
        return scroll;
    }

    QWidget* page_sanitize() {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto* body = new QWidget;
        auto* root = new QVBoxLayout(body);
        root->setContentsMargins(18, 16, 18, 18);
        root->setSpacing(12);
        root->addWidget(title("Sanitizacao", 18, "#ffb36b"));
        root->addWidget(muted("Acoes destrutivas. O modo sanitizacao e obrigatorio."));
        auto* warning = new QLabel(
            "ATENCAO: sanitizacao e irreversivel. Nuvem, backup offsite, roteador e logs remotos NAO sao apagados.");
        warning->setWordWrap(true);
        warning->setObjectName("warning");
        root->addWidget(warning);
        auto* data = card("ALVO E PARAMETROS", root);
        auto* form = new QFormLayout(data);
        san_disk_ = plain_field(form, "Disco (numero)", "Ex.: 1");
        san_disk_->setPlaceholderText("Ex.: 1");
        san_serial_ = plain_field(form, "Serial exato");
        san_password_ = plain_field(form, "Senha ATA");
        san_password_->setEchoMode(QLineEdit::Password);
        san_passes_ = plain_field(form, "Passes (1-7)", "1");
        san_passes_->setPlaceholderText("1");
        san_action_ = plain_field(form, "Acao NVMe", "block ou crypto");
        san_action_->setPlaceholderText("block ou crypto");
        san_out_ = path_field(form, "Pasta de saida", true);
        auto* mode = card("CONFIRMACAO", root);
        auto* mode_layout = new QVBoxLayout(mode);
        san_enabled_ = new QCheckBox("Confirmo modo sanitizacao");
        san_verify_ = new QCheckBox("Verificar overwrite (readback)");
        san_enhanced_ = new QCheckBox("Enhanced erase (ATA)");
        mode_layout->addWidget(san_enabled_);
        mode_layout->addWidget(san_verify_);
        mode_layout->addWidget(san_enhanced_);
        auto* actions = card("ACOES", root);
        auto* grid = new QGridLayout(actions);
        add_actions(grid, {
            {"Listar discos", [this]() { list_disks(); }},
            {"Sanitizar disco", [this]() { sanitize_disk(); }},
            {"NVMe sanitize", [this]() { nvme_sanitize(); }},
            {"ATA secure erase", [this]() { ata_erase(); }},
            {"Limpar residuos", [this]() { residue_clean(); }}
        }, 3);
        scroll->setWidget(body);
        return scroll;
    }

    QWidget* page_audit() {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto* body = new QWidget;
        auto* root = new QVBoxLayout(body);
        root->setContentsMargins(18, 16, 18, 18);
        root->setSpacing(12);
        root->addWidget(title("Auditoria", 18));
        root->addWidget(muted("Exposicao residual: nuvem, backups, snapshots, VSS e roteador."));

        auto* content = new QHBoxLayout;
        content->setSpacing(14);
        auto* left = new QWidget;
        auto* left_layout = new QVBoxLayout(left);
        left_layout->setContentsMargins(0, 0, 0, 0);
        left_layout->setSpacing(12);

        auto* params = new QGroupBox("Parâmetros de Saída");
        params->setObjectName("card");
        auto* params_layout = new QVBoxLayout(params);
        params_layout->setSpacing(12);
        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(12);
        aud_out_ = path_field(form, "Pasta de saída", true);
        params_layout->addLayout(form);
        auto* action_row = new QHBoxLayout;
        action_row->setSpacing(8);
        action_row->addWidget(action("Auditar exposição", [this]() { exposure_audit(); }, false, "action_audit.png"));
        action_row->addWidget(action("Limpar resíduos", [this]() { residue_clean(); }, true, "wipe.png"));
        action_row->addStretch(1);
        params_layout->addLayout(action_row);
        left_layout->addWidget(params);

        auto* result = card("RESULTADO", left_layout);
        auto* result_layout = new QVBoxLayout(result);
        result_layout->setSpacing(8);
        auto* result_note = new QLabel(
            "Execute a auditoria para gerar exposure_report.md e exposure_audit.jsonl. "
            "O console registra o caminho de saída e o exit code.");
        result_note->setWordWrap(true);
        result_note->setObjectName("muted");
        result_layout->addWidget(result_note);
        left_layout->addStretch(1);

        auto* right = new QWidget;
        right->setFixedWidth(310);
        auto* right_layout = new QVBoxLayout(right);
        right_layout->setContentsMargins(0, 0, 0, 0);
        right_layout->setSpacing(12);
        auto* scope = card("ESCOPO", right_layout);
        auto* scope_layout = new QVBoxLayout(scope);
        auto* local = new QLabel(
            "LOCAL  /  VSS, Windows.old, FileHistory, lixeira, pagefile, hiberfil e snapshots de VM.");
        local->setWordWrap(true);
        local->setObjectName("muted");
        auto* remote = new QLabel(
            "REMOTO  /  nuvem, backup offsite, roteador e logs remotos exigem checklist manual.");
        remote->setWordWrap(true);
        remote->setObjectName("muted");
        scope_layout->addWidget(local);
        scope_layout->addWidget(remote);
        auto* checklist = card("CHECKLIST", right_layout);
        auto* checks = new QVBoxLayout(checklist);
        checks->setSpacing(9);
        for (const QString& item : QStringList{
                 "Escopo local documentado",
                 "Contas sincronizadas mapeadas",
                 "Backup offsite verificado",
                 "Logs remotos preservados"})
            checks->addWidget(new QCheckBox(item));
        right_layout->addStretch(1);

        auto* note = new QLabel(
            "⚠️  A limpeza local não apaga backup offsite, conta sincronizada, cache de roteador ou log remoto.");
        note->setWordWrap(true);
        note->setObjectName("warning");
        right_layout->addWidget(note);

        content->addWidget(left, 1);
        content->addWidget(right, 0);
        root->addLayout(content, 1);
        scroll->setWidget(body);
        return scroll;
    }
    QWidget* page_wipe() {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto* body = new QWidget;
        auto* root = new QVBoxLayout(body);
        root->setContentsMargins(18, 16, 18, 18);
        root->setSpacing(12);
        root->addWidget(title("Wipe", 18, "#E8C49A"));
        root->addWidget(muted("Limpeza de arquivos, espaco livre, browser, hives, journals e VSS."));

        auto* content = new QHBoxLayout;
        content->setSpacing(14);
        auto* left = new QWidget;
        auto* left_layout = new QVBoxLayout(left);
        left_layout->setContentsMargins(0, 0, 0, 0);
        left_layout->setSpacing(12);
        auto* data = card("ALVO", left_layout);
        auto* form = new QFormLayout(data);
        wipe_target_ = path_field(form, "Alvo / perfil", false);
        wipe_out_ = path_field(form, "Pasta de saida", true);
        wipe_enabled_ = new QCheckBox("Confirmo modo sanitizacao");
        form->addRow(QString(), wipe_enabled_);
        auto* actions = card("ACOES", left_layout);
        auto* grid = new QGridLayout(actions);
        add_actions(grid, {
            {"Wipe arquivo", [this]() { wipe_file(); }},
            {"Wipe espaco livre", [this]() { wipe_free(); }},
            {"Wipe browser", [this]() { wipe_browser(); }},
            {"Wipe hive", [this]() { wipe_hive(); }},
            {"Wipe journal", [this]() { wipe_journal(); }},
            {"Wipe artefatos", [this]() { wipe_artifacts(); }},
            {"Purge USN", [this]() { purge_usn(); }},
            {"Limpar logs", [this]() { clear_logs(); }},
            {"Apagar VSS", [this]() { delete_vss(); }}
        }, 3);
        left_layout->addStretch(1);

        auto* right = new QWidget;
        right->setFixedWidth(310);
        auto* right_layout = new QVBoxLayout(right);
        right_layout->setContentsMargins(0, 0, 0, 0);
        right_layout->setSpacing(12);
        auto* checklist = card("CHECKLIST", right_layout);
        auto* checks = new QVBoxLayout(checklist);
        checks->setSpacing(9);
        const QStringList items = {
            "Alvo e serial conferidos",
            "Backup ou imagem preservada",
            "Modo sanitizacao confirmado",
            "Escopo remoto documentado",
            "Responsavel ciente do risco"
        };
        for (const QString& item : items) checks->addWidget(new QCheckBox(item));
        auto* scope = card("ESCOPO", right_layout);
        auto* scope_text = new QVBoxLayout(scope);
        auto* local = new QLabel("LOCAL\nArquivos, espaco livre, browser, hives, journals e VSS.");
        local->setWordWrap(true);
        local->setObjectName("muted");
        auto* remote = new QLabel("REMOTO\nNuvem, backup offsite, roteador e logs remotos NAO sao apagados.");
        remote->setWordWrap(true);
        remote->setObjectName("warning");
        scope_text->addWidget(local);
        scope_text->addWidget(remote);
        auto* hint = new QLabel("Acoes destrutivas exigem confirmacao e modo sanitizacao. O console registra cada comando e exit code.");
        hint->setWordWrap(true);
        hint->setObjectName("muted");
        right_layout->addWidget(hint);
        right_layout->addStretch(1);

        content->addWidget(left, 1);
        content->addWidget(right, 0);
        root->addLayout(content, 1);
        scroll->setWidget(body);
        return scroll;
    }
    QString identity_path() const {
        const QString local = qEnvironmentVariable("LOCALAPPDATA");
        return local + "/OlhoDeDeus/identity.json";
    }

    QString audit_path() const {
        const QString local = qEnvironmentVariable("LOCALAPPDATA");
        return local + "/OlhoDeDeus/audit.jsonl";
    }

    void write_audit(const QString& actor, const QString& action,
                     const QString& target, const QString& result,
                     const QString& detail) {
        deep::cloud::AuditLog log(audit_path().toUtf8().constData());
        std::string error;
        if (!log.append(actor.toStdString(), action.toStdString(),
                        target.toStdString(), result.toStdString(),
                        detail.toStdString(), error)) {
            append("[audit] " + QString::fromStdString(error) + "\n");
        }
    }

    void cloud_show_audit() {
        if (!require_permission(deep::cloud::Permission::ViewAudit)) return;
        deep::cloud::AuditLog log(audit_path().toUtf8().constData());
        const auto report = log.verify();
        const auto entries = log.tail(200);

        QDialog dialog(this);
        dialog.setWindowTitle("Trilha de auditoria - Cloud Enterprise");
        dialog.resize(940, 520);
        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(20, 18, 20, 18);
        layout->setSpacing(12);
        layout->addWidget(title("Trilha de auditoria", 18));
        layout->addWidget(muted(
            "Eventos encadeados por SHA-256. A verificacao detecta edicao ou "
            "reordenacao dentro do arquivo local."));
        auto* chain = new QLabel(report.ok
            ? QString("Cadeia integra: %1 eventos.").arg(report.entries)
            : QString("Falha na cadeia: %1").arg(
                  QString::fromStdString(report.errors.empty() ? "erro" : report.errors.front())));
        chain->setStyleSheet(report.ok ? "color:#8BC34A;font-weight:600;"
                                       : "color:#FF8A65;font-weight:600;");
        layout->addWidget(chain);

        auto* table = new QTableWidget(0, 5);
        table->setHorizontalHeaderLabels({"Data UTC", "Ator", "Acao", "Alvo", "Resultado"});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->verticalHeader()->setVisible(false);
        table->setAlternatingRowColors(true);
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            const int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(it->timestamp_utc)));
            table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(it->actor)));
            table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(it->action)));
            table->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(it->target)));
            table->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(it->result)));
            table->setRowHeight(row, 28);
        }
        layout->addWidget(table, 1);
        auto* footer = new QHBoxLayout;
        QString last_export;
        auto* export_audit = new QPushButton("Exportar auditoria");
        auto* sign_audit = new QPushButton("Assinar exportacao");
        auto* verify_audit = new QPushButton("Verificar assinatura");
        footer->addWidget(export_audit);
        footer->addWidget(sign_audit);
        footer->addWidget(verify_audit);
        footer->addStretch(1);
        auto* close = new QPushButton("Fechar");
        footer->addWidget(close);
        layout->addLayout(footer);
        connect(sign_audit, &QPushButton::clicked, &dialog, [&]() {
            if (last_export.isEmpty()) {
                QMessageBox::information(&dialog, "Auditoria", "Exporte a auditoria primeiro.");
                return;
            }
            const QString key = QFileDialog::getOpenFileName(&dialog,
                "Chave privada ECDSA", QString(), "Chaves (*.priv);;Todos (*.*)");
            if (key.isEmpty()) return;
            const QString signature = last_export + ".sig";
            std::wstring error;
            if (deep::sign_manifest(last_export.toStdWString(), key.toStdWString(),
                                    signature.toStdWString(), error) != 0) {
                QMessageBox::warning(&dialog, "Auditoria",
                    QString::fromStdWString(error.empty() ? L"Falha ao assinar." : error));
                return;
            }
            QMessageBox::information(&dialog, "Auditoria",
                "Assinatura criada:\n" + signature);
        });
        connect(verify_audit, &QPushButton::clicked, &dialog, [&]() {
            if (last_export.isEmpty()) {
                QMessageBox::information(&dialog, "Auditoria", "Exporte a auditoria primeiro.");
                return;
            }
            const QString key = QFileDialog::getOpenFileName(&dialog,
                "Chave publica ECDSA", QString(), "Chaves (*.pub);;Todos (*.*)");
            if (key.isEmpty()) return;
            std::wstring error;
            const QString signature = last_export + ".sig";
            if (deep::verify_manifest(last_export.toStdWString(), key.toStdWString(),
                                      signature.toStdWString(), error) != 0) {
                QMessageBox::warning(&dialog, "Auditoria",
                    QString::fromStdWString(error.empty() ? L"Assinatura invalida." : error));
                return;
            }
            QMessageBox::information(&dialog, "Auditoria", "Assinatura valida.");
        });        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    }
    QString session_vault_path() const {
        const QString local = qEnvironmentVariable("LOCALAPPDATA");
        return local + "/OlhoDeDeus/session.bin";
    }

    void persist_session() {
        if (!cloud_session_vault_ || !cloud_principal_ || cloud_session_token_.empty())
            return;
        deep::cloud::PersistedSession snapshot;
        snapshot.token = cloud_session_token_;
        snapshot.principal = *cloud_principal_;
        snapshot.expires_at_epoch = std::chrono::duration_cast<std::chrono::seconds>(
            (std::chrono::system_clock::now() + std::chrono::hours(8)).time_since_epoch()).count();
        std::string error;
        if (!cloud_session_vault_->save(snapshot, error))
            append("[cloud] sessao nao persistida: " + QString::fromStdString(error) + "\n");
    }

    bool local_principal_active(const deep::cloud::Principal& principal, bool& known) const {
        known = false;
        deep::cloud::LocalIdentityStore store(identity_path().toUtf8().constData());
        std::string error;
        if (!store.load(error)) return false;
        for (const auto& user : store.users()) {
            if (user.id == principal.id) {
                known = true;
                return user.active && user.role == principal.role;
            }
        }
        return false;
    }
    void restore_persisted_session() {
        if (!cloud_session_vault_ || !cloud_sessions_) return;
        bool found = false;
        std::string error;
        const auto snapshot = cloud_session_vault_->load(found, error);
        if (!found) return;
        if (!snapshot) {
            append("[cloud] sessao persistida invalida: " + QString::fromStdString(error) + "\n");
            cloud_session_vault_->erase(error);
            return;
        }
        const auto now = std::chrono::system_clock::now();
        if (snapshot->expires_at_epoch <= std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count()) {
            cloud_session_vault_->erase(error);
            return;
        }
        bool local_known = false;
        if (local_principal_active(snapshot->principal, local_known) == false &&
            local_known) {
            cloud_session_vault_->erase(error);
            return;
        }
        if (!cloud_sessions_->restore(snapshot->token, snapshot->principal,
                std::chrono::system_clock::time_point(std::chrono::seconds(
                    snapshot->expires_at_epoch)), error)) {
            cloud_session_vault_->erase(error);
            return;
        }
        cloud_principal_ = snapshot->principal;
        cloud_session_token_ = snapshot->token;
        append("[cloud] sessao restaurada: " +
               QString::fromStdString(snapshot->principal.id) + "\n");
    }
    void update_session_label() {
        if (!cloud_session_label_) return;
        if (cloud_principal_) {
            cloud_session_label_->setText(QString("Sessao: %1 (%2)")
                .arg(QString::fromStdString(cloud_principal_->display_name),
                     QString::fromStdString(deep::cloud::role_name(cloud_principal_->role))));
            if (cloud_login_button_) cloud_login_button_->setText("Sair");
        } else {
            cloud_session_label_->setText("Nao autenticado.");
            if (cloud_login_button_) cloud_login_button_->setText("Entrar");
        }
    }

    bool prompt_new_admin(deep::cloud::LocalIdentityStore& store) {
        QDialog dialog(this);
        dialog.setWindowTitle("Primeiro acesso - Cloud Enterprise");
        dialog.setMinimumWidth(480);
        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(22, 20, 22, 20);
        layout->setSpacing(12);
        layout->addWidget(title("Primeiro acesso", 18));
        layout->addWidget(muted(
            "Crie o administrador local deste computador. A senha exige pelo menos "
            "10 caracteres e fica somente como PBKDF2; ela nunca e exibida depois."));
        auto* card = new QGroupBox("ADMINISTRADOR INICIAL");
        auto* form = new QFormLayout(card);
        form->setContentsMargins(14, 18, 14, 14);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(10);
        auto* user = new QLineEdit("admin");
        auto* display = new QLineEdit("Administrador");
        auto* password = new QLineEdit;
        password->setEchoMode(QLineEdit::Password);
        auto* confirm = new QLineEdit;
        confirm->setEchoMode(QLineEdit::Password);
        form->addRow("Usuario", user);
        form->addRow("Nome", display);
        form->addRow("Senha", password);
        form->addRow("Confirmar", confirm);
        layout->addWidget(card);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        password->setFocus();
        if (dialog.exec() != QDialog::Accepted) return false;
        if (password->text() != confirm->text()) {
            QMessageBox::warning(this, "Cloud", "As senhas nao coincidem.");
            return false;
        }
        std::string error;
        if (!store.create_user(user->text().toStdString(), display->text().toStdString(),
                               deep::cloud::Role::Admin,
                               password->text().toStdString(), error)) {
            QMessageBox::warning(this, "Cloud",
                QString::fromStdString("Nao foi possivel criar o administrador: " + error));
            return false;
        }
        return true;
    }

    void cloud_oidc_login() {
        QDialog setup(this);
        setup.setWindowTitle("Entrar com OIDC - Cloud Enterprise");
        setup.setMinimumWidth(500);
        auto* layout = new QVBoxLayout(&setup);
        layout->setContentsMargins(22, 20, 22, 20);
        layout->setSpacing(12);
        layout->addWidget(title("Entrar com OIDC", 18));
        layout->addWidget(muted(
            "O navegador sera aberto no provedor. O ID token e validado por "
            "assinatura RS256, issuer, audience, exp e nbf antes de abrir a sessao."));
        auto* card = new QGroupBox("PROVEDOR");
        auto* form = new QFormLayout(card);
        form->setContentsMargins(14, 18, 14, 14);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(10);
        auto* issuer = new QLineEdit;
        issuer->setText(cloud_service_issuer_);
        issuer->setPlaceholderText("https://login.microsoftonline.com/<tenant>/v2.0");
        auto* client_id = new QLineEdit;
        client_id->setText(cloud_service_client_id_);
        client_id->setPlaceholderText("Client ID publico");
        form->addRow("Issuer", issuer);
        form->addRow("Client ID", client_id);
        layout->addWidget(card);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &setup, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &setup, &QDialog::reject);
        issuer->setFocus();
        if (setup.exec() != QDialog::Accepted) return;
        if (issuer->text().trimmed().isEmpty() || client_id->text().trimmed().isEmpty()) return;

        deep::cloud::WinHttpTransport transport;
        deep::cloud::RetryPolicy policy;
        policy.max_attempts = 2;
        deep::cloud::HttpClient http(transport, policy);
        const auto discovery = deep::cloud::discover_oidc(
            http, issuer->text().trimmed().toStdString());
        if (!discovery.ok) {
            QMessageBox::warning(this, "OIDC",
                QString::fromStdString("Discovery falhou: " + discovery.error));
            return;
        }
        deep::cloud::OAuthCallbackServer callback;
        std::string callback_error;
        if (!callback.start(callback_error)) {
            QMessageBox::warning(this, "OIDC",
                QString::fromStdString("Callback local falhou: " + callback_error));
            return;
        }
        deep::cloud::OAuthConfig config;
        config.authorization_endpoint = discovery.configuration.authorization_endpoint;
        config.token_endpoint = discovery.configuration.token_endpoint;
        config.client_id = client_id->text().trimmed().toStdString();
        config.redirect_uri = callback.redirect_uri();
        config.scope = "openid profile email";
        const auto pkce = deep::cloud::generate_pkce();
        const std::string state = deep::cloud::random_urlsafe(24);
        const std::string url = deep::cloud::build_authorize_url(config, state, pkce.challenge);
        if (url.empty()) {
            QMessageBox::warning(this, "OIDC", "Nao foi possivel montar a URL de login.");
            return;
        }
        const std::wstring wide_url(url.begin(), url.end());
        const HINSTANCE opened = ShellExecuteW(nullptr, L"open", wide_url.c_str(),
                                                nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(opened) <= 32) {
            QMessageBox::warning(this, "OIDC", "Nao foi possivel abrir o navegador.");
            return;
        }
        QProgressDialog waiting("Conclua o login no navegador...", QString(), 0, 0, this);
        waiting.setWindowTitle("OIDC");
        waiting.setWindowModality(Qt::WindowModal);
        waiting.setMinimumDuration(0);
        waiting.show();
        QApplication::processEvents();
        const auto callback_result = callback.wait(std::chrono::seconds(300));
        waiting.close();
        if (!callback_result.ok()) {
            QMessageBox::warning(this, "OIDC",
                QString::fromStdString("Login nao concluido: " + callback_result.error));
            return;
        }
        if (callback_result.state != state) {
            QMessageBox::warning(this, "OIDC", "State OIDC invalido.");
            return;
        }
        const auto exchanged = deep::cloud::exchange_authorization_code(
            transport, config, callback_result.code, pkce.verifier);
        if (!exchanged.token || exchanged.token->id_token.empty()) {
            QMessageBox::warning(this, "OIDC",
                QString::fromStdString("Token exchange falhou: " + exchanged.error +
                                       " (id_token ausente)"));
            return;
        }
        const auto verified = deep::cloud::verify_oidc_token(
            http, discovery.configuration, exchanged.token->id_token,
            config.client_id);
        if (!verified.ok) {
            QMessageBox::warning(this, "OIDC",
                QString::fromStdString("ID token rejeitado: " + verified.error));
            return;
        }
        const auto role = deep::cloud::oidc_role(verified.claims, deep::cloud::Role::Reviewer);
        const auto principal = deep::cloud::principal_from_oidc(verified.claims, role);
        if (cloud_service_url_ && cloud_service_url_->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "OIDC", "Conecte o servico antes do login OIDC.");
            return;
        }
        QJsonObject service_body;
        service_body.insert("issuer", issuer->text().trimmed());
        service_body.insert("client_id", client_id->text().trimmed());
        service_body.insert("id_token", QString::fromStdString(exchanged.token->id_token));
        QByteArray service_response;
        int service_status = 0;
        QString service_error;
        if (!cloud_service_http("POST", "/v1/session/oidc",
                QJsonDocument(service_body), service_response, service_status, service_error)) {
            QMessageBox::warning(this, "OIDC",
                "Servico indisponivel para OIDC: " + service_error);
            return;
        }
        if (service_status != 200) {
            QMessageBox::warning(this, "OIDC",
                QString("Servico recusou o ID token (HTTP %1).").arg(service_status));
            return;
        }
        const QJsonObject service_reply = parse_json_object(service_response);
        cloud_service_token_ = service_reply.value("token").toString();
        if (cloud_service_token_.isEmpty()) {
            QMessageBox::warning(this, "OIDC", "Servico nao retornou sessao.");
            return;
        }
        if (cloud_service_status_) cloud_service_status_->setText("Servico: OIDC conectado");
        std::string session_error;
        const auto token = cloud_sessions_->create(principal, session_error);
        if (!token) {
            QMessageBox::warning(this, "OIDC",
                QString::fromStdString("Falha ao abrir sessao: " + session_error));
            return;
        }
        cloud_principal_ = principal;
        cloud_session_token_ = *token;
        persist_session();
        write_audit(QString::fromStdString(principal.id), "login.oidc",
                    QString::fromStdString(discovery.configuration.issuer), "allowed",
                    QString::fromStdString(deep::cloud::role_name(principal.role)));
        update_session_label();
        append("[cloud] sessao OIDC iniciada: " +
               QString::fromStdString(principal.id) + "\n");
    }
    void cloud_login() {
        if (!cloud_sessions_) return;
        deep::cloud::LocalIdentityStore store(identity_path().toUtf8().constData());
        std::string error;
        if (!store.load(error)) {
            QMessageBox::warning(this, "Cloud",
                QString::fromStdString("Falha ao ler identidade: " + error));
            return;
        }
        if (store.users().empty() && !prompt_new_admin(store)) return;

        QDialog dialog(this);
        dialog.setWindowTitle("Entrar - Cloud Enterprise");
        dialog.setMinimumWidth(440);
        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(22, 20, 22, 20);
        layout->setSpacing(12);
        layout->addWidget(title("Entrar", 18));
        layout->addWidget(muted("Sessao local para o modulo Cloud. O token de sessao "
                                "fica somente em memoria e expira em 8 horas."));
        auto* card = new QGroupBox("CREDENCIAIS");
        auto* form = new QFormLayout(card);
        form->setContentsMargins(14, 18, 14, 14);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(10);
        auto* user = new QLineEdit;
        auto* password = new QLineEdit;
        password->setEchoMode(QLineEdit::Password);
        form->addRow("Usuario", user);
        form->addRow("Senha", password);
        layout->addWidget(card);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        user->setFocus();
        if (dialog.exec() != QDialog::Accepted) return;
        if (user->text().isEmpty() || password->text().isEmpty()) return;
        const auto principal = store.authenticate(user->text().toStdString(),
                                                  password->text().toStdString());
        if (!principal) {
            write_audit(user->text(), "login", "cloud", "denied",
                        "invalid credentials or inactive user");
            QMessageBox::warning(this, "Cloud", "Usuario, senha ou estado invalido.");
            return;
        }
        const auto token = cloud_sessions_->create(*principal, error);
        if (!token) {
            QMessageBox::warning(this, "Cloud",
                QString::fromStdString("Falha ao abrir sessao: " + error));
            return;
        }
        cloud_principal_ = *principal;
        cloud_session_token_ = *token;
        persist_session();
        write_audit(QString::fromStdString(principal->id), "login", "cloud", "allowed",
                    QString::fromStdString(deep::cloud::role_name(principal->role)));
        update_session_label();
        append("[cloud] sessao iniciada: " + QString::fromStdString(principal->id) + "\n");
    }
    void cloud_logout() {
        if (cloud_principal_) {
            write_audit(QString::fromStdString(cloud_principal_->id), "logout", "cloud",
                        "allowed", "session revoked");
        }
        if (cloud_sessions_ && !cloud_session_token_.empty())
            cloud_sessions_->revoke(cloud_session_token_);
        cloud_session_token_.clear();
        cloud_principal_.reset();
        if (cloud_session_vault_) {
            std::string vault_error;
            cloud_session_vault_->erase(vault_error);
        }
        update_session_label();
        append("[cloud] sessao encerrada\n");
    }

    void toggle_cloud_session() {
        if (cloud_principal_) cloud_logout();
        else cloud_login();
    }

    bool require_permission(deep::cloud::Permission permission) {
        if (!cloud_sessions_) return false;
        if (!cloud_principal_) cloud_login();
        if (!cloud_principal_) return false;
        if (!cloud_session_token_.empty()) {
            const auto current = cloud_sessions_->validate(cloud_session_token_);
            if (!current) {
                cloud_principal_.reset();
                cloud_session_token_.clear();
                update_session_label();
                QMessageBox::warning(this, "Cloud", "Sessao expirada. Entre novamente.");
                return false;
            }
            cloud_principal_ = *current;
        }
        bool local_known = false;
        if (local_principal_active(*cloud_principal_, local_known) == false &&
            local_known) {
            cloud_logout();
            QMessageBox::warning(this, "Cloud", "Usuario desativado ou papel alterado. Entre novamente.");
            return false;
        }
        const deep::cloud::Decision decision =
            deep::cloud::authorize(*cloud_principal_, permission);
        if (!decision.allowed) {
            QMessageBox::warning(this, "Cloud",
                QString::fromStdString("Acesso negado: " + decision.reason));
            return false;
        }
        return true;
    }
    QJsonObject parse_json_object(const QByteArray& data) {
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return {};
        return document.object();
    }
    void set_cloud_status(const QString& text, bool failed) {
        if (!cloud_status_) return;
        if (cloud_status_->text() == text &&
            cloud_status_->property("failed").toBool() == failed) return;
        cloud_status_->setText(text);
        cloud_status_->setProperty("failed", failed);
        cloud_status_->style()->unpolish(cloud_status_);
        cloud_status_->style()->polish(cloud_status_);
    }
    void refresh_cloud_status() {
        if (!cloud_status_) return;
        const QString base = cloud_case_base();
        if (base.isEmpty()) {
            set_cloud_status("Nenhum caso selecionado.", false);
            return;
        }
        if (cloud_jobs_ && cloud_jobs_->currentData().toString().startsWith("service:")) {
            const QString job_id = cloud_jobs_->currentData().toString().mid(8);
            for (const QJsonValue& value : cloud_service_jobs_) {
                const QJsonObject job = value.toObject();
                if (job.value("job_id").toString() != job_id) continue;
                const QString status = job.value("status").toString("unknown");
                const bool failed = status.compare("failed", Qt::CaseInsensitive) == 0 ||
                                    status.compare("partial", Qt::CaseInsensitive) == 0;
                set_cloud_status(QString(
                    "Job: %1\nStatus: %2\nConta: %3\nTenant: %4\nTentativas: %5")
                    .arg(job_id, status, job.value("account").toString("-"),
                         job.value("tenant_id").toString("-"),
                         QString::number(job.value("attempts").toInt())), failed);
                return;
            }
            set_cloud_status("Job do servico nao encontrado na fila.", true);
            return;
        }
        const QDir jobs(base + "/cloud/jobs");
        const QStringList files = jobs.entryList({"*.json"}, QDir::Files, QDir::Time);

        // A deliberate selection in the history combo wins over the newest file;
        // the polling timer must not overwrite what the analyst is inspecting.
        QString job_path;
        if (cloud_jobs_ != nullptr && cloud_jobs_->currentIndex() >= 0)
            job_path = cloud_jobs_->currentData().toString();
        if (job_path.isEmpty() || !QFileInfo::exists(job_path)) {
            if (!jobs.exists() || files.isEmpty()) {
                set_cloud_status("Nenhum job cloud neste caso.", false);
                return;
            }
            job_path = jobs.filePath(files.first());
        }
        QFile file(job_path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            set_cloud_status("Nao foi possivel ler o job.", true);
            return;
        }
        const QJsonObject job = parse_json_object(file.readAll());
        const QString status = job.value("status").toString("unknown");
        const QString source = job.value("source").toString("-");
        QStringList source_parts;
        const QJsonObject sources = job.value("sources").toObject();
        for (const QString& name : {"profile", "messages", "drive"}) {
            const QJsonObject entry = sources.value(name).toObject();
            if (entry.isEmpty()) continue;
            source_parts << QString("%1 %2").arg(name)
                .arg(entry.value("status").toString("-"));
        }
        const QString sources_line = source_parts.isEmpty()
            ? QString("-") : source_parts.join("  ·  ");
        const bool failed = status.compare("failed", Qt::CaseInsensitive) == 0 ||
                            status.compare("partial", Qt::CaseInsensitive) == 0;
        set_cloud_status(QString(
            "Job: %1\n"
            "Status: %2\n"
            "Origem: %3  ·  pagina %4\n"
            "Progresso: %5 paginas  ·  %6 objetos\n"
            "Retomado: %7\n"
            "Fontes: %8\n"
            "Atualizado: %9")
            .arg(job.value("job_id").toString("-"))
            .arg(status)
            .arg(source)
            .arg(job.value("checkpoint_page").toInt())
            .arg(job.value("pages").toInt())
            .arg(job.value("objects").toInt())
            .arg(job.value("resumed").toBool() ? "sim" : "nao")
            .arg(sources_line)
            .arg(job.value("updated_at_utc").toString("-")), failed);
        cloud_status_->setToolTip(QDir::toNativeSeparators(job_path));
    }
    void load_cloud_job_file(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        const QJsonObject job = parse_json_object(file.readAll());
        if (cloud_tenant_) cloud_tenant_->setText(job.value("tenant_id").toString());
        if (cloud_account_) cloud_account_->setText(job.value("account").toString());
        if (cloud_vault_) cloud_vault_->setText(job.value("vault_key").toString());
        if (cloud_legal_) cloud_legal_->setText(job.value("legal_basis").toString());
        if (cloud_job_) cloud_job_->setText(job.value("job_id").toString());
        append("[cloud] job carregado: " + job.value("job_id").toString() +
               " (" + job.value("status").toString() + ")\n");
        refresh_cloud_status();
    }

    void refresh_service_jobs() {
        if (cloud_service_token_.isEmpty()) return;
        QByteArray response;
        int status = 0;
        QString error;
        if (!cloud_service_http("GET", "/v1/queue", QJsonDocument(),
                response, status, error)) {
            set_cloud_hint("Fila do servico indisponivel: " + error, true);
            return;
        }
        if (status != 200) {
            if (status == 401) cloud_service_token_.clear();
            set_cloud_hint(QString("Fila do servico recusada (HTTP %1).").arg(status), true);
            return;
        }
        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(response, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isArray()) {
            set_cloud_hint("Fila do servico retornou JSON invalido.", true);
            return;
        }
        cloud_service_jobs_ = document.array();
        refresh_cloud_jobs();
    }
    void refresh_cloud_jobs() {
        if (!cloud_jobs_) return;
        const QString previous = cloud_jobs_->currentData().toString();
        const QString base = cloud_case_base();
        cloud_jobs_->blockSignals(true);
        cloud_jobs_->clear();
        QSet<QString> seen;
        if (!base.isEmpty()) {
            const QDir jobs(base + "/cloud/jobs");
            const QStringList files = jobs.entryList({"*.json"}, QDir::Files, QDir::Time);
            for (const QString& name : files) {
                const QString path = jobs.filePath(name);
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
                const QJsonObject job = parse_json_object(file.readAll());
                const QString job_id = job.value("job_id").toString(name);
                const QString status = job.value("status").toString("?");
                cloud_jobs_->addItem(QString("%1  /  %2  /  %3")
                    .arg(job_id, status, job.value("account").toString("-")), path);
                seen.insert(job_id);
            }
        }
        for (const QJsonValue& value : cloud_service_jobs_) {
            const QJsonObject job = value.toObject();
            const QString job_id = job.value("job_id").toString();
            if (job_id.isEmpty() || seen.contains(job_id)) continue;
            cloud_jobs_->addItem(QString("%1  /  %2  /  %3")
                .arg(job_id, job.value("status").toString("?"),
                     job.value("account").toString("-")),
                "service:" + job_id);
            seen.insert(job_id);
        }
        if (cloud_jobs_->count() == 0) {
            // Keep a visible empty state; the placeholder cannot be selected.
            cloud_jobs_->addItem("Nenhum job", QString());
            if (auto* model = qobject_cast<QStandardItemModel*>(cloud_jobs_->model())) {
                if (auto* item = model->item(cloud_jobs_->count() - 1))
                    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            }
            cloud_jobs_->setToolTip("Nenhum job cloud encontrado neste caso.");
        } else {
            cloud_jobs_->setToolTip("Selecione um job para inspecionar o status.");
            const int found = previous.isEmpty() ? -1 : cloud_jobs_->findData(previous);
            if (found >= 0) cloud_jobs_->setCurrentIndex(found);
        }
        cloud_jobs_->blockSignals(false);
        update_cloud_process_buttons();
    }

    void load_latest_cloud_job() {
        const QString base = cloud_case_base();
        if (cloud_jobs_ && cloud_jobs_->currentData().toString().startsWith("service:")) {
            const QString job_id = cloud_jobs_->currentData().toString().mid(8);
            for (const QJsonValue& value : cloud_service_jobs_) {
                const QJsonObject job = value.toObject();
                if (job.value("job_id").toString() != job_id) continue;
                const QString status = job.value("status").toString("unknown");
                const bool failed = status.compare("failed", Qt::CaseInsensitive) == 0 ||
                                    status.compare("partial", Qt::CaseInsensitive) == 0;
                set_cloud_status(QString(
                    "Job: %1\nStatus: %2\nConta: %3\nTenant: %4\nTentativas: %5")
                    .arg(job_id, status, job.value("account").toString("-"),
                         job.value("tenant_id").toString("-"),
                         QString::number(job.value("attempts").toInt())), failed);
                return;
            }
            set_cloud_status("Job do servico nao encontrado na fila.", true);
            return;
        }
        const QDir jobs(base + "/cloud/jobs");
        const QStringList files = jobs.entryList({"*.json"}, QDir::Files, QDir::Time);
        if (files.isEmpty()) {
            refresh_cloud_jobs();
            refresh_cloud_status();
            append("[cloud] nenhum job encontrado no caso\n");
            return;
        }
        if (cloud_case_ && cloud_case_->text().trimmed().isEmpty())
            cloud_case_->setText(base);
        refresh_cloud_jobs();
        if (cloud_jobs_) {
            const int found = cloud_jobs_->findData(jobs.filePath(files.first()));
            if (found >= 0) cloud_jobs_->setCurrentIndex(found);
        }
        load_cloud_job_file(jobs.filePath(files.first()));
    }

    void toggle_cloud_pause() {
        if (!process_ || process_->state() == QProcess::NotRunning) {
            set_cloud_hint("Nenhum job em execucao para pausar ou retomar.", true);
            update_cloud_process_buttons();
            return;
        }
        using NtProcessFn = LONG(WINAPI*)(HANDLE);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        auto suspend = ntdll ? reinterpret_cast<NtProcessFn>(
            GetProcAddress(ntdll, "NtSuspendProcess")) : nullptr;
        auto resume = ntdll ? reinterpret_cast<NtProcessFn>(
            GetProcAddress(ntdll, "NtResumeProcess")) : nullptr;
        if (!suspend || !resume) {
            set_cloud_hint("Pausa indisponivel neste Windows.", true);
            append("[cloud] pausa indisponivel neste Windows\n");
            return;
        }
        HANDLE process = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE,
            static_cast<DWORD>(process_->processId()));
        if (!process) {
            set_cloud_hint("Nao foi possivel abrir o processo para pausa.", true);
            append("[cloud] nao foi possivel abrir o processo para pausa\n");
            return;
        }
        const LONG result = cloud_paused_ ? resume(process) : suspend(process);
        CloseHandle(process);
        if (result == 0) {
            cloud_paused_ = !cloud_paused_;
            if (cloud_pause_)
                cloud_pause_->setText(cloud_paused_ ? "Retomar job" : "Pausar job");
            set_cloud_hint(cloud_paused_ ? "Job pausado; use Retomar para continuar."
                                         : "Job retomado.");
            append(cloud_paused_ ? "[cloud] job pausado; retome para continuar\n"
                                 : "[cloud] job retomado\n");
        } else {
            set_cloud_hint("Falha ao pausar ou retomar o job.", true);
            append("[cloud] falha ao pausar ou retomar o job\n");
        }
    }
    void cloud_manage_users() {
        if (!require_permission(deep::cloud::Permission::ManageUsers)) return;
        deep::cloud::LocalIdentityStore store(identity_path().toUtf8().constData());
        std::string error;
        if (!store.load(error)) {
            QMessageBox::warning(this, "Cloud",
                QString::fromStdString("Falha ao ler identidade: " + error));
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle("Usuarios - Cloud Enterprise");
        dialog.resize(940, 580);
        auto* outer = new QVBoxLayout(&dialog);
        outer->setContentsMargins(20, 18, 20, 18);
        outer->setSpacing(12);
        outer->addWidget(title("Usuarios locais", 18));
        outer->addWidget(muted("Contas, papeis e acesso ao modulo Cloud neste computador."));

        auto* columns = new QHBoxLayout;
        columns->setSpacing(14);
        auto* list_card = new QGroupBox("USUARIOS");
        list_card->setObjectName("card");
        auto* list_layout = new QVBoxLayout(list_card);
        auto* table = new QTableWidget(0, 4);
        table->setHorizontalHeaderLabels({"Usuario", "Nome", "Papel", "Ativo"});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setAlternatingRowColors(true);
        table->verticalHeader()->setVisible(false);
        list_layout->addWidget(table);
        columns->addWidget(list_card, 1);

        auto* form_card = new QGroupBox("NOVO USUARIO");
        form_card->setObjectName("card");
        form_card->setFixedWidth(340);
        auto* form_layout = new QVBoxLayout(form_card);
        auto* form = new QFormLayout;
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(8);
        auto* user_id = new QLineEdit;
        auto* display_name = new QLineEdit;
        auto* password = new QLineEdit;
        password->setEchoMode(QLineEdit::Password);
        auto* role_combo = new QComboBox;
        for (const auto role : {deep::cloud::Role::Admin, deep::cloud::Role::Examiner,
                                deep::cloud::Role::Reviewer, deep::cloud::Role::Auditor}) {
            role_combo->addItem(QString::fromStdString(deep::cloud::role_name(role)),
                                static_cast<int>(role));
        }
        form->addRow("Usuario", user_id);
        form->addRow("Nome", display_name);
        form->addRow("Senha", password);
        form->addRow("Papel", role_combo);
        form_layout->addLayout(form);
        auto* create = new QPushButton("Criar usuario");
        form_layout->addWidget(create);
        form_layout->addStretch(1);
        columns->addWidget(form_card, 0);
        outer->addLayout(columns, 1);

        auto refresh = [&]() {
            table->setRowCount(0);
            for (const auto& user : store.users()) {
                const int row = table->rowCount();
                table->insertRow(row);
                table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(user.id)));
                table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(user.display_name)));
                table->setItem(row, 2, new QTableWidgetItem(
                    QString::fromStdString(deep::cloud::role_name(user.role))));
                table->setItem(row, 3, new QTableWidgetItem(user.active ? "sim" : "nao"));
                table->setRowHeight(row, 30);
            }
        };
        refresh();

        auto selected_id = [&]() -> QString {
            const int row = table->currentRow();
            if (row < 0 || table->item(row, 0) == nullptr) return {};
            return table->item(row, 0)->text();
        };

        auto* footer = new QHBoxLayout;
        auto* toggle = new QPushButton("Ativar / Desativar");
        auto* apply_role = new QPushButton("Aplicar papel");
        auto* close = new QPushButton("Fechar");
        footer->addWidget(toggle);
        footer->addWidget(apply_role);
        footer->addStretch(1);
        footer->addWidget(close);
        outer->addLayout(footer);

        connect(create, &QPushButton::clicked, &dialog, [&]() {
            std::string create_error;
            const auto role = static_cast<deep::cloud::Role>(role_combo->currentData().toInt());
            if (!store.create_user(user_id->text().toStdString(),
                                   display_name->text().toStdString(), role,
                                   password->text().toStdString(), create_error)) {
                QMessageBox::warning(&dialog, "Cloud",
                    QString::fromStdString("Nao foi possivel criar: " + create_error));
                return;
            }
            write_audit(cloud_principal_ ? QString::fromStdString(cloud_principal_->id) : "system",
                        "user.create", user_id->text(), "allowed",
                        QString::fromStdString(deep::cloud::role_name(role)));
            password->clear();
            refresh();
        });
        connect(toggle, &QPushButton::clicked, &dialog, [&]() {
            const QString id = selected_id();
            if (id.isEmpty()) return;
            bool active = false;
            for (const auto& user : store.users()) {
                if (QString::fromStdString(user.id) == id) active = user.active;
            }
            std::string toggle_error;
            if (!store.set_active(id.toStdString(), !active, toggle_error)) {
                QMessageBox::warning(&dialog, "Cloud",
                    QString::fromStdString("Falha ao alterar estado: " + toggle_error));
                return;
            }
            if (active && cloud_sessions_) cloud_sessions_->revoke_user(id.toStdString());
            write_audit(cloud_principal_ ? QString::fromStdString(cloud_principal_->id) : "system",
                        "user.active", id, "allowed", active ? "disabled" : "enabled");
            refresh();
        });
        connect(apply_role, &QPushButton::clicked, &dialog, [&]() {
            const QString id = selected_id();
            if (id.isEmpty()) return;
            const auto role = static_cast<deep::cloud::Role>(role_combo->currentData().toInt());
            std::string role_error;
            if (!store.set_role(id.toStdString(), role, role_error)) {
                QMessageBox::warning(&dialog, "Cloud",
                    QString::fromStdString("Falha ao alterar papel: " + role_error));
                return;
            }
            write_audit(cloud_principal_ ? QString::fromStdString(cloud_principal_->id) : "system",
                        "user.role", id, "allowed",
                        QString::fromStdString(deep::cloud::role_name(role)));
            refresh();
        });
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    }
    bool cloud_service_http(const QString& method, const QString& path,
                            const QJsonDocument& body, QByteArray& response,
                            int& status, QString& error) {
        if (!cloud_service_url_) {
            error = "URL do servico ausente";
            return false;
        }
        QString base = cloud_service_url_->text().trimmed();
        while (base.endsWith('/')) base.chop(1);
        if (base.isEmpty()) {
            error = "URL do servico ausente";
            return false;
        }
        QNetworkAccessManager manager;
        QNetworkRequest request(QUrl(base + path));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        if (!cloud_service_token_.isEmpty())
            request.setRawHeader("Authorization",
                ("Bearer " + cloud_service_token_).toUtf8());
        QNetworkReply* reply = manager.sendCustomRequest(
            request, method.toUtf8(), body.toJson(QJsonDocument::Compact));
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(10000);
        loop.exec();
        if (!reply->isFinished()) {
            reply->abort();
            error = "tempo esgotado ao falar com o servico";
            reply->deleteLater();
            return false;
        }
        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        response = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && status == 0)
            error = reply->errorString();
        reply->deleteLater();
        return status > 0;
    }

    void cloud_connect_service() {
        if (!require_permission(deep::cloud::Permission::ViewCase)) return;
        if (!cloud_service_url_ || !cloud_service_user_ || !cloud_service_pass_) return;
        const QString user = cloud_service_user_->text().trimmed();
        const QString password = cloud_service_pass_->text();
        if (user.isEmpty() || password.isEmpty()) {
            set_cloud_hint("Informe usuario e senha do servico.", true);
            return;
        }
        QJsonObject body;
        body.insert("username", user);
        body.insert("password", password);
        QByteArray response;
        int status = 0;
        QString error;
        if (!cloud_service_http("POST", "/v1/session",
                QJsonDocument(body), response, status, error)) {
            if (cloud_service_status_) cloud_service_status_->setText("Servico: offline");
            set_cloud_hint("Servico indisponivel: " + error, true);
            return;
        }
        const QJsonObject reply = parse_json_object(response);
        const QString token = reply.value("token").toString();
        if (status != 200 || token.isEmpty()) {
            if (cloud_service_status_) cloud_service_status_->setText("Servico: login recusado");
            set_cloud_hint("Login no servico recusado.", true);
            return;
        }
        cloud_service_token_ = token;
        if (cloud_service_status_) {
            cloud_service_status_->setText("Servico: conectado");
            cloud_service_status_->setStyleSheet("color: #8bc34a;");
        }
        set_cloud_hint(QString());
        append("[cloud] servico conectado\n");
        QByteArray config_response;
        int config_status = 0;
        QString config_error;
        if (cloud_service_http("GET", "/v1/config", QJsonDocument(),
                config_response, config_status, config_error) && config_status == 200) {
            const QJsonObject config = parse_json_object(config_response);
            cloud_service_issuer_ = config.value("issuer").toString();
            cloud_service_client_id_ = config.value("client_id").toString();
            if (!cloud_service_issuer_.isEmpty()) {
                const QUrl issuer_url(cloud_service_issuer_);
                const QStringList segments = issuer_url.path().split('/', Qt::SkipEmptyParts);
                if (!segments.isEmpty() && cloud_tenant_ &&
                    cloud_tenant_->text().trimmed().isEmpty())
                    cloud_tenant_->setText(segments.first());
                if (cloud_client_ && cloud_client_->text().trimmed().isEmpty())
                    cloud_client_->setText(cloud_service_client_id_);
            }
        }
    }
    void cloud_toggle_pause_job() {
        if (!require_permission(deep::cloud::Permission::CancelJob)) return;
        const QString base = cloud_case_base();
        if (base.isEmpty() || !cloud_jobs_) return;
        const QString data = cloud_jobs_->currentData().toString();
        if (data.isEmpty()) {
            set_cloud_hint("Selecione um job para pausar ou retomar.", true);
            return;
        }
        const QString job_id = data.startsWith("service:")
            ? data.mid(8) : QFileInfo(data).completeBaseName();
        QString status;
        if (data.startsWith("service:")) {
            for (const QJsonValue& value : cloud_service_jobs_) {
                const QJsonObject job = value.toObject();
                if (job.value("job_id").toString() == job_id) {
                    status = job.value("status").toString();
                    break;
                }
            }
        } else {
            QFile file(data);
            if (file.open(QIODevice::ReadOnly))
                status = parse_json_object(file.readAll()).value("status").toString();
        }
        const bool resume = status.compare("paused", Qt::CaseInsensitive) == 0;
        if (!cloud_service_token_.isEmpty()) {
            QByteArray response;
            int http_status = 0;
            QString error;
            const QString endpoint = "/v1/queue/" +
                QUrl::toPercentEncoding(job_id) + (resume ? "/resume" : "/pause");
            if (!cloud_service_http("POST", endpoint, QJsonDocument(),
                    response, http_status, error)) {
                set_cloud_hint("Falha ao falar com o servico: " + error, true);
                return;
            }
            if (http_status != 200) {
                set_cloud_hint(QString("Servico recusou (HTTP %1).").arg(http_status), true);
                return;
            }
        } else {
            deep::cloud::JobQueue queue((base + "/cloud/queue").toStdString());
            std::string error;
            const bool ok = resume
                ? queue.resume(job_id.toStdString(), error)
                : queue.pause(job_id.toStdString(), error);
            if (!ok) {
                set_cloud_hint(QString::fromStdString(error), true);
                return;
            }
        }
        append(QString("[cloud] job %1: %2\n")
            .arg(job_id, resume ? "retomado" : "pausado"));
        set_cloud_hint(resume ? "Job retomado." : "Job pausado; checkpoint preservado.");
        refresh_service_jobs();
        refresh_cloud_jobs();
        refresh_cloud_status();
    }
    void cloud_cancel_job() {
        if (!require_permission(deep::cloud::Permission::CancelJob)) return;
        const QString base = cloud_case_base();
        if (base.isEmpty() || !cloud_jobs_) {
            set_cloud_hint("Selecione um job para cancelar.", true);
            return;
        }
        const QString path = cloud_jobs_->currentData().toString();
        if (path.isEmpty()) {
            set_cloud_hint("Selecione um job para cancelar.", true);
            return;
        }
        const QString job_id = path.startsWith("service:")
            ? path.mid(8) : QFileInfo(path).completeBaseName();
        if (QMessageBox::question(this, "Cancelar job",
                "Cancelar o job " + job_id + "? O checkpoint sera preservado.") !=
            QMessageBox::Yes) return;
        if (!cloud_service_token_.isEmpty()) {
            QByteArray response;
            int status = 0;
            QString error;
            const QString endpoint = "/v1/queue/" + QUrl::toPercentEncoding(job_id);
            if (!cloud_service_http("DELETE", endpoint, QJsonDocument(),
                    response, status, error)) {
                set_cloud_hint("Falha ao cancelar no servico: " + error, true);
                return;
            }
            if (status != 200 && status != 204) {
                set_cloud_hint(QString("Servico recusou o cancelamento (HTTP %1).").arg(status), true);
                return;
            }
        } else {
            deep::cloud::JobQueue queue((base + "/cloud/queue").toStdString());
            std::string error;
            if (!queue.cancel(job_id.toStdString(), error)) {
                set_cloud_hint(QString::fromStdString(error), true);
                return;
            }
        }
        append("[cloud] cancelamento solicitado: " + job_id + "\n");
        set_cloud_hint("Cancelamento solicitado; checkpoint preservado.");
        refresh_cloud_jobs();
        refresh_cloud_status();
    }
    void cloud_enqueue_collection() {
        if (!require_permission(deep::cloud::Permission::RunCollection)) return;
        QString base = cloud_case_ ? cloud_case_->text().trimmed() : QString();
        if (base.isEmpty()) base = case_dir_;
        if (base.isEmpty()) {
            QMessageBox::information(this, "Cloud", "Selecione um caso.");
            return;
        }
        const QString account = cloud_account_ ? cloud_account_->text().trimmed() : QString();
        const QString tenant = cloud_tenant_ ? cloud_tenant_->text().trimmed() : QString();
        const QString vault = cloud_vault_ ? cloud_vault_->text().trimmed() : QString();
        const QString legal = cloud_legal_ ? cloud_legal_->text().trimmed() : QString();
        if (account.isEmpty() || tenant.isEmpty() || vault.isEmpty() || legal.isEmpty()) {
            QMessageBox::warning(this, "Cloud",
                "Conta, tenant, vault key e base legal sao obrigatorios.");
            return;
        }
        deep::cloud::QueuedJob job;
        job.job_id = cloud_job_ ? cloud_job_->text().trimmed().toStdString() : std::string();
        if (job.job_id.empty()) {
            job.job_id = "job_" + QDateTime::currentDateTimeUtc()
                .toString("yyyyMMdd_HHmmss").toStdString();
        }
        job.account = account.toStdString();
        job.tenant_id = tenant.toStdString();
        if (cloud_vault_ref_.isEmpty() && !vault.isEmpty()) {
            deep::cloud::SecretStore store;
            std::string ref;
            std::string ref_error;
            if (!store.create(vault.toStdString(), ref, ref_error)) {
                QMessageBox::warning(this, "Cloud", QString::fromStdString(ref_error));
                return;
            }
            cloud_vault_ref_ = QString::fromStdString(ref);
        }
        if (cloud_vault_ref_.isEmpty()) {
            QMessageBox::warning(this, "Cloud", "Nao foi possivel criar a referencia do vault.");
            return;
        }
        job.vault_ref = cloud_vault_ref_.toStdString();
        job.legal_basis = legal.toStdString();
        job.since = cloud_since_ ? cloud_since_->text().trimmed().toStdString() : std::string();
        job.until = cloud_until_ ? cloud_until_->text().trimmed().toStdString() : std::string();
        job.download_attachments = cloud_attachments_ && cloud_attachments_->isChecked();
        job.max_pages = 50;
        if (!cloud_service_token_.isEmpty()) {
            QJsonObject body;
            body.insert("job_id", QString::fromStdString(job.job_id));
            body.insert("account", account);
            body.insert("tenant_id", tenant);
            body.insert("legal_basis", legal);
            body.insert("vault_ref", cloud_vault_ref_);
            body.insert("since", QString::fromStdString(job.since));
            body.insert("until", QString::fromStdString(job.until));
            body.insert("max_pages", 50);
            body.insert("download_attachments", job.download_attachments);
            QByteArray response;
            int status = 0;
            QString service_error;
            if (!cloud_service_http("POST", "/v1/queue", QJsonDocument(body),
                    response, status, service_error)) {
                set_cloud_hint("Falha ao enfileirar no servico: " + service_error, true);
                return;
            }
            if (status != 201 && status != 200) {
                if (status == 401) cloud_service_token_.clear();
                set_cloud_hint(QString("Servico recusou o job (HTTP %1).").arg(status), true);
                return;
            }
            if (cloud_job_) cloud_job_->setText(QString::fromStdString(job.job_id));
            refresh_cloud_jobs();
            append("[cloud] job enfileirado no servico: " +
                   QString::fromStdString(job.job_id) + "\n");
            return;
        }
        const std::string queue_dir = (base + "/cloud/queue").toStdString();
        deep::cloud::JobQueue queue(queue_dir);
        std::string error;
        if (!queue.enqueue(job, error)) {
            QMessageBox::warning(this, "Cloud", QString::fromStdString(error));
            return;
        }
        if (cloud_job_) cloud_job_->setText(QString::fromStdString(job.job_id));
        refresh_cloud_jobs();
        append("[cloud] job enfileirado: " + QString::fromStdString(job.job_id) + "\n");
    }
    void cloud_export_report() {
        const QString base = cloud_case_base();
        if (base.isEmpty()) {
            set_cloud_hint("Selecione a pasta do caso antes de gerar o relatorio.", true);
            return;
        }
        if (!require_permission(deep::cloud::Permission::VerifyEvidence)) return;
        set_cloud_hint(QString());
        const QString path = QFileDialog::getSaveFileName(this, "Relatorio cloud",
            base + "/cloud_report.html", "HTML (*.html);;JSON (*.json)");
        if (path.isEmpty()) return;
        QProcess process;
        const QString format = path.endsWith(".json", Qt::CaseInsensitive) ? "json" : "html";
        process.start(cli(), {"--cloud-report", "--case", base, "--out", path,
                              "--format", format});
        if (!process.waitForFinished(30000) || process.exitCode() != 0) {
            QMessageBox::warning(this, "Relatorio cloud",
                QString::fromUtf8(process.readAllStandardError()));
            return;
        }
        append("[cloud] relatorio: " + QDir::toNativeSeparators(path) + "\n");
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
    void cloud_show_objects() {
        const QString base = cloud_case_base();
        if (base.isEmpty()) {
            set_cloud_hint("Selecione a pasta do caso antes de consultar os objetos.", true);
            return;
        }
        if (!require_permission(deep::cloud::Permission::ViewCase)) return;
        set_cloud_hint(QString());
        const QString db = base + "/case.db";
        if (!QFileInfo::exists(db)) {
            QMessageBox::information(this, "Cloud DB",
                "Importe o caso para o DB primeiro.");
            return;
        }
        QProcess query;
        query.start(cli(), {"--case-query", db,
            "SELECT object_type, external_id, name, path, size_bytes, collected_at_utc "
            "FROM cloud_object ORDER BY collected_at_utc DESC LIMIT 1000"});
        if (!query.waitForFinished(15000) || query.exitCode() != 0) {
            QMessageBox::warning(this, "Cloud DB",
                QString::fromUtf8(query.readAllStandardError()));
            return;
        }
        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(
            query.readAllStandardOutput(), &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isArray()) {
            QMessageBox::warning(this, "Cloud DB", "Resposta SQL invalida.");
            return;
        }
        const QJsonArray rows = document.array();

        QDialog dialog(this);
        dialog.setWindowTitle("Objetos cloud - Cloud Enterprise");
        dialog.resize(980, 560);
        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(20, 18, 20, 18);
        layout->setSpacing(12);
        layout->addWidget(title("Objetos cloud", 18));
        layout->addWidget(muted(
            "Indice SQL do caso. O manifesto e os hashes originais continuam "
            "sendo a fonte de verdade."));
        auto* filters = new QHBoxLayout;
        filters->addWidget(new QLabel("Tipo:"));
        auto* type_filter = new QComboBox;
        type_filter->addItem("Todos", QString());
        for (const QString& type : {"message", "file", "attachment", "raw_page", "event"})
            type_filter->addItem(type, type);
        filters->addWidget(type_filter);
        auto* count = new QLabel;
        count->setObjectName("muted");
        filters->addWidget(count);
        filters->addStretch(1);
        layout->addLayout(filters);

        auto* table = new QTableWidget(0, 6);
        table->setObjectName("dataGrid");
        table->setHorizontalHeaderLabels({"Tipo", "ID externo", "Nome", "Caminho",
                                          "Tamanho", "Coletado UTC"});
        auto* header = table->horizontalHeader();
        header->setStretchLastSection(false);
        header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::Interactive);
        header->resizeSection(1, 170);
        header->setSectionResizeMode(2, QHeaderView::Interactive);
        header->resizeSection(2, 220);
        header->setSectionResizeMode(3, QHeaderView::Stretch);
        header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(5, QHeaderView::Fixed);
        header->resizeSection(5, 132);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->verticalHeader()->setVisible(false);
        table->setAlternatingRowColors(true);
        table->setWordWrap(false);
        table->setShowGrid(false);
        table->verticalHeader()->setDefaultSectionSize(28);

        auto* empty = new QLabel;
        empty->setObjectName("emptyState");
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        auto* stack = new QStackedWidget;
        stack->addWidget(table);
        stack->addWidget(empty);
        layout->addWidget(stack, 1);

        auto refresh = [&]() {
            const QString wanted = type_filter->currentData().toString();
            table->setUpdatesEnabled(false);
            table->setRowCount(0);
            for (const auto& value : rows) {
                const QJsonObject object = value.toObject();
                if (!wanted.isEmpty() && object.value("object_type").toString() != wanted)
                    continue;
                const int row = table->rowCount();
                table->insertRow(row);
                const QString external_id = object.value("external_id").toString();
                const QString name = object.value("name").toString();
                const QString path = object.value("path").toString();
                const QString collected = object.value("collected_at_utc").toString();
                const qint64 bytes = object.value("size_bytes").toVariant().toLongLong();
                table->setItem(row, 0, new QTableWidgetItem(
                    object.value("object_type").toString()));
                table->setItem(row, 1, new QTableWidgetItem(external_id));
                table->setItem(row, 2, new QTableWidgetItem(name));
                auto* path_item = new QTableWidgetItem(path);
                path_item->setToolTip(path);
                table->setItem(row, 3, path_item);
                auto* size_item = new QTableWidgetItem(format_bytes(bytes));
                size_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                size_item->setToolTip(QString("%1 bytes").arg(bytes));
                table->setItem(row, 4, size_item);
                auto* time_item = new QTableWidgetItem(compact_timestamp(collected));
                time_item->setToolTip(collected);
                table->setItem(row, 5, time_item);
                table->setRowHeight(row, 28);
            }
            table->setUpdatesEnabled(true);
            const int shown = table->rowCount();
            if (shown == 0) {
                empty->setText(wanted.isEmpty()
                    ? "Nenhum objeto importado no case.db."
                    : QString("Nenhum objeto do tipo \"%1\".").arg(wanted));
                stack->setCurrentIndex(1);
            } else {
                stack->setCurrentIndex(0);
            }
            count->setText(QString("%1 objetos").arg(shown));
        };
        connect(type_filter, &QComboBox::currentIndexChanged, &dialog,
                [&](int) { refresh(); });
        refresh();
        auto* footer = new QHBoxLayout;
        footer->addStretch(1);
        auto* close = new QPushButton("Fechar");
        footer->addWidget(close);
        layout->addLayout(footer);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    }
    QWidget* page_cloud() {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto* body = new QWidget;
        auto* root = new QVBoxLayout(body);
        root->setContentsMargins(18, 16, 18, 18);
        root->setSpacing(12);
        root->addWidget(title("Cloud", 18));
        root->addWidget(muted("Microsoft 365 read-only: consentimento, coleta e evidencia com hash."));

        auto* columns = new QHBoxLayout;
        columns->setSpacing(14);
        auto* left = new QWidget;
        auto* left_layout = new QVBoxLayout(left);
        left_layout->setContentsMargins(0, 0, 0, 0);
        left_layout->setSpacing(12);
        auto* connection = card("CONEXAO", left_layout);
        auto* form = new QFormLayout(connection);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(10);
        cloud_tenant_ = plain_field(form, "Tenant ID");
        cloud_client_ = plain_field(form, "Client ID (app publico)");
        cloud_account_ = plain_field(form, "Conta (UPN)");
        cloud_vault_ = plain_field(form, "Vault key", "tenant/conta");
        cloud_scope_ = plain_field(form, "Escopos", "User.Read Mail.Read Files.Read.All");
        cloud_scope_->setText("User.Read Mail.Read Files.Read.All");
        cloud_service_url_ = plain_field(form, "Servico URL", "http://127.0.0.1:8766");
        cloud_service_user_ = plain_field(form, "Usuario do servico");
        cloud_service_pass_ = plain_field(form, "Senha do servico");
        if (cloud_service_pass_) cloud_service_pass_->setEchoMode(QLineEdit::Password);
        cloud_service_status_ = new QLabel("Servico: desconectado");
        cloud_service_status_->setObjectName("muted");
        form->addRow("", cloud_service_status_);

        auto* actions = card("ACOES", left_layout);
        auto* action_layout = new QVBoxLayout(actions);
        action_layout->setSpacing(8);
        auto* action_row1 = new QHBoxLayout;
        action_row1->setSpacing(8);
        auto* action_row2 = new QHBoxLayout;
        action_row2->setSpacing(8);
        auto* action_row3 = new QHBoxLayout;
        action_row3->setSpacing(8);
        action_layout->addLayout(action_row1);
        action_layout->addLayout(action_row2);
        action_layout->addLayout(action_row3);
        auto* auth = action("Autenticar M365", [this]() {
            if (cloud_tenant_->text().trimmed().isEmpty() ||
                cloud_client_->text().trimmed().isEmpty()) {
                set_cloud_hint("Informe Tenant ID e Client ID para autenticar.", true);
                return;
            }
            if (!require_permission(deep::cloud::Permission::RunCollection)) return;
            deep::cloud::AuthFlowOptions options;
            options.tenant = cloud_tenant_->text().trimmed().toStdString();
            options.client_id = cloud_client_->text().trimmed().toStdString();
            options.scope = cloud_scope_->text().trimmed().toStdString();
            options.vault_key = cloud_vault_->text().trimmed().toStdString();
            set_cloud_hint("Aguardando autenticacao no navegador...");
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const int code = deep::cloud::run_auth_flow(options);
            QApplication::restoreOverrideCursor();
            if (code == 0) {
                deep::cloud::SecretStore store;
                std::string ref;
                std::string ref_error;
                if (!options.vault_key.empty() &&
                    store.create(options.vault_key, ref, ref_error)) {
                    cloud_vault_ref_ = QString::fromStdString(ref);
                }
                append("[cloud] autenticacao M365 concluida\n");
                set_cloud_hint("Autenticacao M365 concluida; token salvo no DPAPI.");
            } else {
                set_cloud_hint("Autenticacao M365 falhou. Verifique tenant/client.", true);
            }
        }, false, "cloud.png");
        auto* collect = action("Enfileirar coleta", [this]() { cloud_enqueue_collection(); },
                                 false, "tab_table.png");
        auto* status = action("Atualizar status", [this]() {
            if (!ensure_cloud_case("atualizar o status")) return;
            if (!require_permission(deep::cloud::Permission::ViewCase)) return;
            refresh_service_jobs();
            refresh_cloud_jobs();
            refresh_cloud_status();
            set_cloud_hint(QString());
        }, false, "action_audit.png");

        auto* verify = action("Verificar evidencia", [this]() {
            if (!ensure_cloud_case("verificar a evidencia")) return;
            if (!require_permission(deep::cloud::Permission::VerifyEvidence)) return;
            const QString base = cloud_case_base();
            set_cloud_hint(QString());
            QStringList args{"--cloud-verify", "--case", base};
            if (cloud_account_ && !cloud_account_->text().isEmpty())
                args << "--account" << cloud_account_->text();
            run(args, false);
        }, false, "tab_table.png");
        action_row1->addWidget(auth);
        action_row1->addWidget(collect);
        action_row1->addWidget(status);
        action_row1->addWidget(verify);
        auto* import_db = action("Importar DB", [this]() {
            if (!ensure_cloud_case("importar o DB")) return;
            if (!require_permission(deep::cloud::Permission::VerifyEvidence)) return;
            set_cloud_hint(QString());
            run({"--cloud-db-import", "--case", cloud_case_base()}, false);
        }, false, "tab_table.png");
        action_row2->addWidget(import_db);
        auto* query_db = action("Consultar DB", [this]() {
            if (!ensure_cloud_case("consultar o DB")) return;
            if (!require_permission(deep::cloud::Permission::ViewCase)) return;
            set_cloud_hint(QString());
            const QString sql =
                "SELECT object_type, COUNT(*) AS total FROM cloud_object "
                "GROUP BY object_type ORDER BY total DESC";
            run({"--case-query", cloud_case_base() + "/case.db", sql}, false);
        }, false, "tab_table.png");
        action_row2->addWidget(query_db);
        auto* objects_db = action("Objetos DB", [this]() { cloud_show_objects(); },
                                  false, "tab_table.png");
        action_row2->addWidget(objects_db);
        auto* report_db = action("Relatorio", [this]() { cloud_export_report(); },
                                 false, "action_audit.png");
        action_row2->addWidget(report_db);
        auto* load_job = action("Carregar job", [this]() {
            if (!ensure_cloud_case("carregar o job")) return;
            if (!require_permission(deep::cloud::Permission::ViewCase)) return;
            set_cloud_hint(QString());
            load_latest_cloud_job();
        }, false, "action_audit.png");
        auto* service_connect = action("Conectar servico", [this]() { cloud_connect_service(); },
                                       false, "cloud.png");
        action_row3->addWidget(service_connect);
        action_row3->addWidget(load_job);
        cloud_pause_ = action("Pausar/Retomar job", [this]() { cloud_toggle_pause_job(); },
                                       false, "kernel.png");
        cloud_pause_->setEnabled(false);
        action_row3->addWidget(cloud_pause_);
        cloud_cancel_ = action("Cancelar job", [this]() { cloud_cancel_job(); },
                                 true, "wipe.png");
        cloud_cancel_->setEnabled(false);
        action_row3->addWidget(cloud_cancel_);
        action_row3->addStretch(1);
        cloud_hint_ = new QLabel;
        cloud_hint_->setObjectName("formHint");
        cloud_hint_->setWordWrap(true);
        cloud_hint_->setVisible(false);
        action_layout->addWidget(cloud_hint_);
        auto* flow = card("FLUXO DE TRABALHO", left_layout);
        auto* flow_layout = new QVBoxLayout(flow);
        auto* flow_text = new QLabel(
            "1. Entre com uma conta autorizada.\n"
            "2. Autentique o tenant Microsoft 365 em modo leitura.\n"
            "3. Informe caso, base legal e job; colete ou retome.\n"
            "4. Verifique o manifesto antes de exportar.");
        flow_text->setWordWrap(true);
        flow_text->setObjectName("muted");
        flow_layout->addWidget(flow_text);
        left_layout->addStretch(1);

        auto* right = new QWidget;
        right->setFixedWidth(330);
        auto* right_layout = new QVBoxLayout(right);
        right_layout->setContentsMargins(0, 0, 0, 0);
        right_layout->setSpacing(12);
        auto* scope = card("ESCOPO E AUTORIZACAO", right_layout);
        auto* form2 = new QFormLayout(scope);
        form2->setHorizontalSpacing(14);
        form2->setVerticalSpacing(10);
        cloud_case_ = path_field(form2, "Caso (pasta)", true);
        cloud_since_ = plain_field(form2, "Desde");
        cloud_since_->setPlaceholderText("AAAA-MM-DD");
        cloud_until_ = plain_field(form2, "Ate");
        cloud_until_->setPlaceholderText("AAAA-MM-DD");
        cloud_legal_ = plain_field(form2, "Base legal");
        cloud_job_ = plain_field(form2, "Job ID");
        cloud_attachments_ = new QCheckBox("Anexos inline (<= 50 MB)");
        form2->addRow("Anexos", cloud_attachments_);
        auto* warning = new QLabel(
            "⚠️  A coleta exige autorizacao registrada e consentimento do tenant. "
            "Tokens ficam no DPAPI; nada e escrito no tenant.");
        warning->setWordWrap(true);
        warning->setObjectName("warning");
        right_layout->addWidget(warning);
        auto* session_card = card("SESSAO", right_layout);
        auto* session_layout = new QVBoxLayout(session_card);
        cloud_session_label_ = new QLabel("Nao autenticado.");
        cloud_session_label_->setWordWrap(true);
        cloud_session_label_->setObjectName("muted");
        session_layout->addWidget(cloud_session_label_);
        cloud_login_button_ = action("Entrar", [this]() { toggle_cloud_session(); },
                                    false, "pericia.png");
        session_layout->addWidget(cloud_login_button_);
        auto* oidc_login = action("Entrar com OIDC", [this]() { cloud_oidc_login(); },
                                   false, "cloud.png");
        session_layout->addWidget(oidc_login);
        cloud_users_button_ = action("Usuarios", [this]() { cloud_manage_users(); },
                                     false, "action_audit.png");
        session_layout->addWidget(cloud_users_button_);
        cloud_audit_button_ = action("Trilha de auditoria", [this]() { cloud_show_audit(); },
                                     false, "action_audit.png");
        session_layout->addWidget(cloud_audit_button_);
        auto* status_card = card("STATUS DO JOB", right_layout);
        auto* status_layout = new QVBoxLayout(status_card);
        cloud_status_ = new QLabel("Nenhum job carregado.");
        cloud_status_->setWordWrap(true);
        cloud_status_->setObjectName("cloudStatus");
        cloud_status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        status_layout->addWidget(cloud_status_);
        auto* history_caption = new QLabel("Historico de jobs");
        history_caption->setObjectName("panelTitle");
        status_layout->addWidget(history_caption);
        cloud_jobs_ = new QComboBox;
        cloud_jobs_->setObjectName("cloudJobHistory");
        cloud_jobs_->setMaxVisibleItems(12);
        status_layout->addWidget(cloud_jobs_);
        connect(cloud_jobs_, &QComboBox::currentIndexChanged, this, [this](int) {
            if (!cloud_jobs_) return;
            const QString path = cloud_jobs_->currentData().toString();
            if (!path.isEmpty()) load_cloud_job_file(path);
            update_cloud_process_buttons();
        });
        right_layout->addStretch(1);

        columns->addWidget(left, 1);
        columns->addWidget(right, 0);
        root->addLayout(columns, 1);
        scroll->setWidget(body);
        update_session_label();
        update_cloud_process_buttons();
        QTimer::singleShot(0, this, [this]() { load_latest_cloud_job(); });
        return scroll;
    }
    QWidget* page_kernel() {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto* body = new QWidget;
        auto* root = new QVBoxLayout(body);
        root->setContentsMargins(18, 16, 18, 18);
        root->setSpacing(12);
        root->addWidget(title("Kernel", 18));
        root->addWidget(muted("Driver read-only: versao e leitura raw por disco/offset."));

        auto* content = new QHBoxLayout;
        content->setSpacing(14);
        auto* left = new QWidget;
        auto* left_layout = new QVBoxLayout(left);
        left_layout->setContentsMargins(0, 0, 0, 0);
        left_layout->setSpacing(12);
        auto* data = card("LEITURA RAW", left_layout);
        auto* form = new QFormLayout(data);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(10);
        kernel_disk_ = plain_field(form, "Disco (numero)");
        kernel_offset_ = plain_field(form, "Offset", "0");
        kernel_length_ = plain_field(form, "Tamanho", "4096");
        kernel_out_ = path_field(form, "Arquivo de saida", false);
        auto* actions = card("ACOES", left_layout);
        auto* action_layout = new QVBoxLayout(actions);
        action_layout->setSpacing(8);
        auto* action_row1 = new QHBoxLayout;
        action_row1->setSpacing(8);
        auto* action_row2 = new QHBoxLayout;
        action_row2->setSpacing(8);
        auto* action_row3 = new QHBoxLayout;
        action_row3->setSpacing(8);
        action_layout->addLayout(action_row1);
        action_layout->addLayout(action_row2);
        action_layout->addLayout(action_row3);
        action_layout->addWidget(action("Versao do driver", [this]() { kernel_version(); }, false, "action_driver.png"));
        action_layout->addWidget(action("Ler raw via kernel", [this]() { kernel_read(); }, false, "action_read.png"));
        action_layout->addWidget(action("Ajuda CLI completa", [this]() { help(); }, false, "action_help.png"));
        action_layout->addStretch(1);
        auto* checklist = card("CHECKLIST", left_layout);
        auto* checks = new QVBoxLayout(checklist);
        checks->setSpacing(9);
        for (const QString& item : QStringList{
                 "Driver carregado no host",
                 "Disco e offset conferidos",
                 "Tamanho menor que o volume",
                 "Arquivo de saida definido"})
            checks->addWidget(new QCheckBox(item));
        left_layout->addStretch(1);

        auto* right = new QWidget;
        right->setFixedWidth(310);
        auto* right_layout = new QVBoxLayout(right);
        right_layout->setContentsMargins(0, 0, 0, 0);
        right_layout->setSpacing(12);
        auto* status = card("STATUS DO DRIVER", right_layout);
        auto* status_form = new QFormLayout(status);
        status_form->setHorizontalSpacing(12);
        status_form->setVerticalSpacing(9);
        auto* driver = new QLabel("Nao carregado");
        driver->setObjectName("monoValue");
        auto* device = new QLabel("\\\\.\\OlhoDeDeus");
        device->setObjectName("monoValue");
        auto* access = new QLabel("read-only / offset");
        access->setObjectName("monoValue");
        status_form->addRow("Driver", driver);
        status_form->addRow("Device", device);
        status_form->addRow("Acesso", access);
        auto* security = card("SEGURANCA", right_layout);
        auto* security_layout = new QVBoxLayout(security);
        auto* note = new QLabel(
            "A leitura usa o driver somente leitura. O disco de origem nao e alterado; "
            "valide o offset e o tamanho antes de executar.");
        note->setWordWrap(true);
        note->setObjectName("muted");
        security_layout->addWidget(note);
        auto* warning = new QLabel("⚠️  O driver precisa estar carregado no host. Em VM, use snapshot antes de testar.");
        warning->setWordWrap(true);
        warning->setObjectName("warning");
        right_layout->addWidget(warning);
        auto* example = card("EXEMPLO", right_layout);
        auto* example_layout = new QVBoxLayout(example);
        auto* example_text = new QLabel(
            "--kernel-version\n"
            "--kernel-read 0 --offset 0 --length 4096 --out raw.bin");
        example_text->setObjectName("monoValue");
        example_text->setWordWrap(true);
        example_layout->addWidget(example_text);
        right_layout->addStretch(1);

        content->addWidget(left, 1);
        content->addWidget(right, 0);
        root->addLayout(content, 1);
        scroll->setWidget(body);
        return scroll;
    }
    QIcon icon(const QString& name) const {
        QIcon value(QCoreApplication::applicationDirPath() + "/icons/" + name);
        return value.isNull() ? style()->standardIcon(QStyle::SP_FileIcon) : value;
    }

    QStandardItem* tree_node(QStandardItem* parent, const QString& text,
                             const QString& icon_name = QString()) {
        auto* item = new QStandardItem(text);
        if (!icon_name.isEmpty()) item->setIcon(icon(icon_name));
        item->setEditable(false);
        if (parent) parent->appendRow(item);
        return item;
    }

    void open_tool(int index) {
        if (index < 0 || index >= 7) return;
        if (tool_pages_.size() < 7) tool_pages_.resize(7);
        if (tool_pages_[index] == nullptr) {
            QWidget* page = nullptr;
            switch (index) {
            case 0: page = page_pericia(); break;
            case 1: page = page_advanced(); break;
            case 2: page = page_sanitize(); break;
            case 3: page = page_audit(); break;
            case 4: page = page_wipe(); break;
            case 5: page = page_kernel(); break;
            default: page = page_cloud(); break;
            }
            tool_pages_[index] = page;
            const QStringList titles = {"Pericia", "Avancado", "Sanitizacao",
                                        "Auditoria", "Wipe", "Kernel", "Cloud"};
            const QStringList files = {"pericia.png", "avancado.png", "sanitizacao.png",
                                       "auditoria.png", "wipe.png", "kernel.png", "cloud.png"};
            const int tab = center_tabs_->addTab(page, QIcon(QCoreApplication::applicationDirPath() +
                "/icons/" + files[index]), titles[index]);
            center_tabs_->setTabToolTip(tab, "Ferramenta " + titles[index]);
            auto* close = new QToolButton(center_tabs_);
            close->setObjectName("tabClose");
            close->setText("×");
            close->setFixedSize(16, 16);
            close->setCursor(Qt::PointingHandCursor);
            close->setToolTip("Fechar aba");
            connect(close, &QToolButton::clicked, this, [this, page]() {
                close_tool_tab(center_tabs_->indexOf(page));
            });
            center_tabs_->tabBar()->setTabButton(tab, QTabBar::RightSide, close);
        }
        center_tabs_->setCurrentWidget(tool_pages_[index]);
        active_tool_ = index;
        for (int i = 0; i < tool_tiles_.size(); ++i)
            tool_tiles_[i]->set_active(i == index);
        const bool sanitize = index == 2 || index == 4;
        if (mode_label_) {
            mode_label_->setText(sanitize ? "MODO / SANITIZACAO" : "MODO / PERICIA");
            mode_label_->setProperty("sanitize", sanitize);
            mode_label_->style()->unpolish(mode_label_);
            mode_label_->style()->polish(mode_label_);
        }
    }

    void close_tool_tab(int tab_index) {
        if (tab_index < 3) return;
        QWidget* page = center_tabs_->widget(tab_index);
        for (int i = 0; i < tool_pages_.size(); ++i) {
            if (tool_pages_[i] == page) {
                tool_pages_[i] = nullptr;
                if (active_tool_ == i) active_tool_ = -1;
                break;
            }
        }
        center_tabs_->removeTab(tab_index);
        if (page) page->deleteLater();
    }
    QWidget* build_tree_panel() {
        auto* panel = new QWidget;
        panel->setObjectName("treePanel");
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);
        auto* tool_header = new QHBoxLayout;
        auto* tool_title = new QLabel("FERRAMENTAS");
        tool_title->setObjectName("panelTitle");
        tool_header->addWidget(tool_title);
        tool_header->addStretch(1);
        mode_label_ = new QLabel("MODO / PERICIA");
        mode_label_->setObjectName("modeInline");
        tool_header->addWidget(mode_label_);
        layout->addLayout(tool_header);

        const QStringList tool_names = {"Pericia", "Avancado", "Sanitizacao",
                                        "Auditoria", "Wipe", "Kernel", "Cloud"};
        const QStringList tool_files = {"pericia.png", "avancado.png", "sanitizacao.png",
                                        "auditoria.png", "wipe.png", "kernel.png", "cloud.png"};
        auto* tools = new QGridLayout;
        tools->setHorizontalSpacing(6);
        tools->setVerticalSpacing(6);
        for (int i = 0; i < tool_names.size(); ++i) {
            auto* tile = new ToolTile(icon(tool_files[i]), tool_names[i]);
            if (i == 0) tile->set_active(true);
            tool_tiles_.append(tile);
            tools->addWidget(tile, i / 2, i % 2);
            connect(tile, &ToolTile::clicked, this, [this, i]() { open_tool(i); });
        }
        layout->addLayout(tools);
        auto* divider = new QFrame;
        divider->setFrameShape(QFrame::HLine);
        divider->setObjectName("divider");
        layout->addWidget(divider);

        auto* heading = new QLabel("NAVEGACAO");
        heading->setObjectName("panelTitle");
        layout->addWidget(heading);
        tree_search_ = new QLineEdit;
        tree_search_->setPlaceholderText("Pesquisar no caso...");
        tree_search_->setClearButtonEnabled(true);
        layout->addWidget(tree_search_);

        tree_model_ = new QStandardItemModel(this);
        auto* root = tree_model_->invisibleRootItem();
        tree_case_item_ = tree_node(root, "CASO OLHO-2026-001", "pericia.png");
        tree_device_item_ = tree_node(tree_case_item_, "DISCO-0  /  Windows 11", "pericia.png");
        auto* device = tree_device_item_;
        auto* ntfs = tree_node(device, "NTFS  /  volume C:", "sanitizacao.png");
        tree_node(ntfs, "$MFT", "auditoria.png");
        tree_node(ntfs, "$UsnJrnl", "auditoria.png");
        auto* users = tree_node(ntfs, "Users", "auditoria.png");
        tree_node(users, "pedri", "auditoria.png");
        auto* windows = tree_node(ntfs, "Windows", "auditoria.png");
        tree_node(windows, "System32", "auditoria.png");
        tree_evidence_ = tree_node(root, "EVIDENCIAS", "avancado.png");
        auto* evidence = tree_evidence_;
        tree_cloud_ = tree_node(root, "CLOUD / M365", "cloud.png");
        tree_node(evidence, "imagem.raw", "sanitizacao.png");
        tree_node(evidence, "case.db", "avancado.png");
        tree_node(evidence, "exposure_report.md", "auditoria.png");

        tree_proxy_ = new QSortFilterProxyModel(this);
        tree_proxy_->setSourceModel(tree_model_);
        tree_proxy_->setRecursiveFilteringEnabled(true);
        tree_proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);

        tree_ = new QTreeView;
        tree_->setObjectName("workTree");
        tree_->setModel(tree_proxy_);
        tree_->setHeaderHidden(true);
        tree_->setUniformRowHeights(true);
        tree_->setIconSize(QSize(18, 18));
        tree_->setAnimated(true);
        tree_->setIndentation(16);
        tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tree_->expandAll();
        layout->addWidget(tree_, 1);
        connect(tree_search_, &QLineEdit::textChanged, this, [this](const QString& text) {
            tree_proxy_->setFilterFixedString(text);
            tree_->expandAll();
        });
        return panel;
    }

    QWidget* build_table_tab() {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(8);
        auto* header = new QHBoxLayout;
        auto* heading = new QLabel("TABELA / EVIDENCIAS");
        heading->setObjectName("panelTitle");
        header->addWidget(heading);
        header->addStretch(1);
        table_count_ = new QLabel("4 itens  /  NTFS");
        table_count_->setObjectName("muted");
        header->addWidget(table_count_);
        table_search_ = new QLineEdit;
        table_search_->setPlaceholderText("Filtrar tabela...");
        table_search_->setClearButtonEnabled(true);
        table_search_->setMaximumWidth(220);
        header->addWidget(table_search_);
        layout->addLayout(header);

        table_model_ = new QStandardItemModel(0, 5, this);
        table_model_->setHorizontalHeaderLabels(
            {"NOME", "CAMINHO", "TAMANHO", "DATA", "STATUS"});
        const struct Row { const char* name; const char* path; const char* size;
                           const char* date; const char* status; } rows[] = {
            {"$MFT", "C:\\$MFT", "268 MB", "2026-10-28 08:19", "Analisado"},
            {"$UsnJrnl", "C:\\$Extend\\$UsnJrnl", "64 MB", "2026-10-28 08:27", "Analisado"},
            {"Users\\pedri\\NTUSER.DAT", "C:\\Users\\pedri\\NTUSER.DAT", "12 MB", "2026-10-28 08:41", "Extraido"},
            {"Windows\\System32\\config\\SYSTEM", "C:\\Windows\\System32\\config\\SYSTEM", "18 MB", "2026-10-28 09:03", "Pendente"}
        };
        for (const Row& row : rows) {
            QList<QStandardItem*> items;
            auto* name = new QStandardItem(QString::fromUtf8(row.name));
            name->setIcon(icon("auditoria.png"));
            items << name;
            items << new QStandardItem(QString::fromUtf8(row.path));
            items << new QStandardItem(QString::fromUtf8(row.size));
            items << new QStandardItem(QString::fromUtf8(row.date));
            items << new QStandardItem(QString::fromUtf8(row.status));
            table_model_->appendRow(items);
        }
        table_proxy_ = new QSortFilterProxyModel(this);
        table_proxy_->setSourceModel(table_model_);
        table_proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
        table_proxy_->setFilterKeyColumn(-1);
        auto* forensic_table = new HoverTableView;
        table_ = forensic_table;
        table_->setObjectName("dataGrid");
        table_->setModel(table_proxy_);
        auto* row_hover = new RowHoverDelegate(table_);
        table_->setItemDelegate(row_hover);
        forensic_table->set_hover_delegate(row_hover);
        table_->setSortingEnabled(false);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setAlternatingRowColors(true);
        table_->setShowGrid(false);
        table_->setWordWrap(false);
        table_->verticalHeader()->setVisible(false);
        table_->verticalHeader()->setDefaultSectionSize(30);
        table_->horizontalHeader()->setStretchLastSection(false);
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
        table_->horizontalHeader()->resizeSection(0, 350);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        table_->horizontalHeader()->resizeSection(2, 70);
        table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
        table_->horizontalHeader()->resizeSection(3, 145);
        table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
        table_->horizontalHeader()->resizeSection(4, 80);
        table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        table_->setMinimumHeight(260);
        layout->addWidget(table_, 1);
        connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                if (!current.isValid()) return;
                const QModelIndex source = table_proxy_->mapToSource(current);
                update_details(source.row());
                if (hex_view_ && source.row() < row_raw_.size())
                    hex_view_->set_data(row_raw_.at(source.row()));
            });
        connect(table_search_, &QLineEdit::textChanged, this, [this](const QString& text) {
            table_proxy_->setFilterFixedString(text);
        });
        table_->selectRow(0);
        return page;
    }

    void update_details(int row) {
        if (details_name_ == nullptr) return;
        if (row < 0 || row >= table_model_->rowCount()) return;
        const QByteArray raw = row_raw_.value(row);
        const QDateTime when = row_time_.value(row);
        details_name_->setText(table_model_->item(row, 0)->text());
        details_path_->setText(table_model_->item(row, 1)->text());
        details_size_->setText(QString("%1 bytes").arg(raw.size()));
        details_status_->setText(table_model_->item(row, 4)->text());
        details_type_->setText(raw.size() > 0 ? "Evento EVTX" : "Artefato");
        details_offset_->setText(QString("linha %1").arg(row + 1));
        details_created_->setText(when.isValid() ? when.toString("yyyy-MM-dd HH:mm:ss") : "-");
        details_modified_->setText(when.isValid() ? when.toString("yyyy-MM-dd HH:mm:ss") : "-");
        details_confidence_->setText(raw.size() > 0 ? "Alta (coleta)" : "Media");
        if (details_note_) details_note_->setVisible(false);
        const QByteArray digest = QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex();
        const QString hash = QString::fromLatin1(digest);
        QString visible_hash = hash;
        if (hash.size() == 64) {
            visible_hash = hash.left(16) + "\n" + hash.mid(16, 16) + "\n" +
                           hash.mid(32, 16) + "\n" + hash.mid(48, 16);
        }
        details_hash_->setText(hash.isEmpty() ? "-" : visible_hash);
        details_hash_->setToolTip(hash);
    }
    QWidget* build_details_panel() {
        auto* panel = new QWidget;
        panel->setObjectName("detailsPanel");
        panel->setMinimumWidth(360);
        panel->setMaximumWidth(440);
        auto* outer = new QVBoxLayout(panel);
        outer->setContentsMargins(14, 12, 14, 12);
        outer->setSpacing(8);
        auto* heading = new QLabel("DETALHES");
        heading->setObjectName("panelTitle");
        outer->addWidget(heading);
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* body = new QWidget;
        details_form_ = new QFormLayout(body);
        details_form_->setContentsMargins(0, 0, 0, 0);
        details_form_->setHorizontalSpacing(14);
        details_form_->setVerticalSpacing(12);
        details_form_->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
        details_form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        details_name_ = new QLabel("-");
        details_path_ = new QLabel("-");
        details_size_ = new QLabel("-");
        details_hash_ = new QLabel("-");
        details_status_ = new QLabel("-");
        details_type_ = new QLabel("-");
        details_offset_ = new QLabel("-");
        details_created_ = new QLabel("-");
        details_modified_ = new QLabel("-");
        details_confidence_ = new QLabel("-");
        for (QLabel* value : {details_name_, details_path_, details_size_, details_hash_, details_status_,
                              details_type_, details_offset_, details_created_, details_modified_, details_confidence_}) {
            value->setWordWrap(true);
            value->setMinimumWidth(0);
            value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        }
        details_path_->setObjectName("monoValue");
        details_hash_->setObjectName("monoValue");
        const QFont mono("Consolas", 9);
        details_path_->setFont(mono);
        details_hash_->setFont(mono);
        details_form_->addRow("Nome", details_name_);
        details_form_->addRow("Caminho", details_path_);
        details_form_->addRow("Tamanho", details_size_);
        details_form_->addRow("SHA-256", details_hash_);
        details_form_->addRow("Status", details_status_);
        details_form_->addRow("Tipo", details_type_);
        details_form_->addRow("Offset", details_offset_);
        details_form_->addRow("Criado", details_created_);
        details_form_->addRow("Modificado", details_modified_);
        details_form_->addRow("Confianca", details_confidence_);
        scroll->setWidget(body);
        outer->addWidget(scroll, 1);
        details_note_ = new QLabel("Selecione uma linha da tabela para atualizar os metadados.");
        details_note_->setObjectName("muted");
        details_note_->setWordWrap(true);
        outer->addWidget(details_note_);
        return panel;
    }

    QWidget* build_hex_tab() {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(8);
        auto* heading = new QLabel("HEX VIEW  /  OFFSET 0x00000000");
        heading->setObjectName("panelTitle");
        layout->addWidget(heading);
        hex_view_ = new HexView;
        layout->addWidget(hex_view_, 1);
        return page;
    }

    QWidget* build_console_tab() {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(12, 8, 12, 8);
        layout->setSpacing(6);
        auto* header = new QHBoxLayout;
        auto* heading = new QLabel("CONSOLE");
        heading->setObjectName("panelTitle");
        header->addWidget(heading);
        header->addStretch(1);
        auto* clear = new QPushButton("Limpar");
        clear->setFixedWidth(82);
        header->addWidget(clear);
        layout->addLayout(header);
        console_ = new QPlainTextEdit;
        console_->setReadOnly(true);
        console_->setMaximumBlockCount(4000);
        console_->setFont(QFont("Consolas", 9));
        layout->addWidget(console_, 1);
        connect(clear, &QPushButton::clicked, console_, &QPlainTextEdit::clear);
        return page;
    }

    void build_ui() {
        auto* toolbar = addToolBar("Principal");
        toolbar->setObjectName("mainToolbar");
        toolbar->setMovable(false);
        toolbar->setIconSize(QSize(20, 20));
        toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        auto* brand = new QWidget;
        auto* brand_layout = new QVBoxLayout(brand);
        brand_layout->setContentsMargins(10, 2, 14, 2);
        brand_layout->setSpacing(0);
        auto* brand_title = new QLabel("OLHO DE DEUS");
        brand_title->setObjectName("brandTitle");
        auto* brand_sub = new QLabel("DEEP ENGINE  /  FORENSIC WORKSTATION");
        brand_sub->setObjectName("brandSub");
        brand_layout->addWidget(brand_title);
        brand_layout->addWidget(brand_sub);
        toolbar->addWidget(brand);
        toolbar->addSeparator();

        auto add_tool = [this, toolbar](const QString& text, const QString& icon_name,
                                        std::function<void()> fn) {
            auto* action = toolbar->addAction(icon(icon_name), text);
            action->setToolTip(text);
            connect(action, &QAction::triggered, this, fn);
            return action;
        };
        add_tool("Aquisicao", "pericia.png", [this]() { open_tool(0); });
        add_tool("Artefatos", "auditoria.png", [this]() { open_tool(0); });
        add_tool("E01", "sanitizacao.png", [this]() { open_tool(0); });
        toolbar->addSeparator();
        add_tool("Sanitizacao", "sanitizacao.png", [this]() { open_tool(2); });
        add_tool("Wipe", "wipe.png", [this]() { open_tool(4); });
        add_tool("Kernel", "kernel.png", [this]() { open_tool(5); });
        toolbar->addSeparator();
        add_tool("Atualizar", "avancado.png", [this]() {
            status_label_->setText("Dados atualizados.");
            append("[ui] case refresh requested\n");
        });
        add_tool("Console", "kernel.png", [this]() {
            bottom_dock_->show();
            bottom_dock_->raise();
        });

        tool_pages_.resize(7);
        auto* columns = new QSplitter(Qt::Horizontal);
        columns->setObjectName("mainSplitter");
        columns->setChildrenCollapsible(false);
        columns->setHandleWidth(1);
        columns->addWidget(build_tree_panel());
        center_tabs_ = new QTabWidget;
        center_tabs_->setObjectName("centerTabs");
        center_tabs_->setDocumentMode(true);
        center_tabs_->setIconSize(QSize(16, 16));
        center_tabs_->addTab(build_table_tab(), icon("tab_table.png"), "Tabela");
        center_tabs_->addTab(build_hex_tab(), icon("tab_hex.png"), "Hex View");
        timeline_view_ = new TimelineView;
        center_tabs_->addTab(timeline_view_, icon("tab_timeline.png"), "Timeline");
        center_tabs_->tabBar()->setExpanding(false);
        columns->addWidget(center_tabs_);
        columns->addWidget(build_details_panel());
        columns->setStretchFactor(0, 0);
        columns->setStretchFactor(1, 1);
        columns->setStretchFactor(2, 0);
        columns->setSizes({340, 780, 380});
        setCentralWidget(columns);
        update_details(0);

        bottom_tabs_ = new QTabWidget;
        bottom_tabs_->setObjectName("bottomTabs");
        bottom_tabs_->setDocumentMode(true);
        bottom_tabs_->addTab(build_console_tab(), "Console");
        bottom_tabs_->tabBar()->hide();
        bottom_dock_ = new QDockWidget("Console", this);
        bottom_dock_->setObjectName("bottomDock");
        bottom_dock_->setWidget(bottom_tabs_);
        bottom_dock_->setFeatures(QDockWidget::DockWidgetMovable |
                                 QDockWidget::DockWidgetFloatable |
                                 QDockWidget::DockWidgetClosable);
        addDockWidget(Qt::BottomDockWidgetArea, bottom_dock_);
        resizeDocks({bottom_dock_}, {150}, Qt::Vertical);

        status_label_ = new QLabel("Pronto.");
        statusBar()->addWidget(status_label_, 1);
        memory_label_ = new QLabel;
        database_label_ = new QLabel("DB: " + QDir::toNativeSeparators(
            QCoreApplication::applicationDirPath() + "/case.db"));
        database_label_->setObjectName("statusMono");
        statusBar()->addPermanentWidget(memory_label_);
        statusBar()->addPermanentWidget(database_label_);
        auto timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() {
            PROCESS_MEMORY_COUNTERS_EX counters{};
            if (GetProcessMemoryInfo(GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
                memory_label_->setText(QString("Mem: %1 MB")
                    .arg(counters.WorkingSetSize / (1024 * 1024)));
            }
        });
        timer->start(2000);
        memory_label_->setText("Mem: -- MB");
        append("[ui] forensic workstation ready\n");
        append("[ui] case OLHO-2026-001 / NTFS / 4 items\n");
    }
    void load_case(const QString& preferred = QString()) {
        const QString app = QCoreApplication::applicationDirPath();
        const QStringList roots = {app + "/cases", app + "/../cases",
                                   QDir::currentPath() + "/cases"};
        QString case_dir = preferred;
        for (const QString& root : roots) {
            if (!case_dir.isEmpty()) break;
            QDir dir(root);
            if (!dir.exists()) continue;
            const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                      QDir::Time);
            for (const QString& entry : entries) {
                const QString candidate = dir.filePath(entry);
                if (QFileInfo::exists(candidate + "/timeline.jsonl") ||
                    QFileInfo::exists(candidate + "/manifest.json")) {
                    case_dir = candidate;
                    break;
                }
            }
            if (!case_dir.isEmpty()) break;
        }
        if (case_dir.isEmpty()) {
            status_label_->setText("Nenhum caso encontrado em cases/.");
            return;
        }
        case_dir_ = case_dir;
        QString case_id = QFileInfo(case_dir).fileName();
        QString computer = "LOCAL";
        QString os = "Windows";

        QFile manifest(case_dir + "/manifest.json");
        if (manifest.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QJsonDocument doc = QJsonDocument::fromJson(manifest.readAll());
            const QJsonObject obj = doc.object();
            case_id = obj.value("case_id").toString(case_id);
            computer = obj.value("computer").toString(computer);
            os = obj.value("windows_product_name").toString(os);
            if (!obj.value("windows_build").toString().isEmpty())
                os += " build " + obj.value("windows_build").toString();
        }
        QJsonArray files;
        {
            QFile manifest_files(case_dir + "/manifest.json");
            if (manifest_files.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QJsonObject obj = QJsonDocument::fromJson(manifest_files.readAll()).object();
                files = obj.value("files").toArray();
            }
        }
        if (tree_evidence_) {
            tree_evidence_->removeRows(0, tree_evidence_->rowCount());
            for (const QJsonValue& value : files) {
                const QString name = value.toObject().value("name").toString();
                if (!name.isEmpty()) tree_node(tree_evidence_, name, "sanitizacao.png");
            }
        }
        if (tree_case_item_) tree_case_item_->setText("CASO " + case_id);
        if (tree_device_item_) tree_device_item_->setText(computer + "  /  " + os);

        table_model_->removeRows(0, table_model_->rowCount());
        row_raw_.clear();
        row_time_.clear();
        QVector<QDateTime> times;
        QStringList labels;

        int loaded = 0;
        int cloud_files = 0;
        const auto load_timeline_file = [&](const QString& path, bool cloud) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
            QTextStream stream(&file);
            while (!stream.atEnd() && loaded < 5000) {
                const QByteArray line = stream.readLine().toUtf8();
                if (line.trimmed().isEmpty()) continue;
                QJsonParseError error{};
                const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
                if (error.error != QJsonParseError::NoError || !doc.isObject()) continue;
                const QJsonObject obj = doc.object();
                const QString stamp = obj.value("timestamp_utc").toString();
                QDateTime when = QDateTime::fromString(stamp, Qt::ISODateWithMs);
                if (!when.isValid()) when = QDateTime::fromString(stamp, Qt::ISODate);
                if (when.isValid()) when = when.toLocalTime();
                const QString provider = obj.value("provider").toString("Evento");
                const QString channel = obj.value("channel").toString("-");
                const QString event_id = obj.value("event_id").toString("-");
                const QString record = obj.value("record_id").toString("-");
                const QString name = provider + " / " + event_id;
                const QString row_path = channel + " / record " + record;
                QList<QStandardItem*> items;
                auto* name_item = new QStandardItem(name);
                name_item->setIcon(icon(cloud ? "cloud.png" : "auditoria.png"));
                items << name_item;
                items << new QStandardItem(row_path);
                items << new QStandardItem(QString("%1 B").arg(line.size()));
                items << new QStandardItem(when.isValid()
                    ? when.toString("yyyy-MM-dd HH:mm:ss") : "-");
                items << new QStandardItem(cloud ? "Cloud" : "Coleta");
                table_model_->appendRow(items);
                row_raw_.append(line);
                row_time_.append(when);
                times.append(when);
                labels.append(provider.left(22) + " #" + event_id);
                ++loaded;
            }
        };

        load_timeline_file(case_dir + "/timeline.jsonl", false);
        QDirIterator cloud_it(case_dir + "/cloud", QStringList{"timeline.jsonl"},
                              QDir::Files, QDirIterator::Subdirectories);
        while (cloud_it.hasNext()) {
            const QString path = cloud_it.next();
            if (QFileInfo(path).size() == 0) continue;
            load_timeline_file(path, true);
            ++cloud_files;
        }

        if (tree_cloud_) {
            tree_cloud_->removeRows(0, tree_cloud_->rowCount());
            QDir cloud_root(case_dir + "/cloud/m365");
            if (cloud_root.exists()) {
                for (const QString& account : cloud_root.entryList(
                        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                    auto* account_item = tree_node(tree_cloud_, account, "cloud.png");
                    const QString manifest_path = cloud_root.filePath(account + "/manifest.json");
                    if (QFileInfo::exists(manifest_path))
                        tree_node(account_item, "manifest.json", "tab_table.png");
                    QDir raw_dir(cloud_root.filePath(account + "/raw"));
                    for (const QString& raw_name : raw_dir.entryList(
                            QDir::Files, QDir::Name))
                        tree_node(account_item, raw_name, "tab_hex.png");
                }
            }
        }
        if (table_count_) {
            table_count_->setText(QString("%1 eventos  /  %2 cloud")
                .arg(table_model_->rowCount()).arg(cloud_files));
        }
        if (timeline_view_) {
            QVector<QDateTime> sampled_times;
            QStringList sampled_labels;
            const int stride = qMax(1, times.size() / 80);
            for (int i = 0; i < times.size(); i += stride) {
                sampled_times.append(times[i]);
                sampled_labels.append(labels.value(i));
            }
            timeline_view_->set_events(sampled_times, sampled_labels);
        }
        if (table_model_->rowCount() > 0) {
            table_->selectRow(0);
            update_details(0);
            if (hex_view_ && !row_raw_.isEmpty()) hex_view_->set_data(row_raw_.first());
        }
        status_label_->setText(QString("Caso %1 carregado: %2 eventos.")
            .arg(case_id).arg(table_model_->rowCount()));
        append("[case] " + QDir::toNativeSeparators(case_dir) + "\n");
        append(QString("[case] %1 eventos carregados do timeline.jsonl\n")
            .arg(table_model_->rowCount()));
    }

    QString channel_label(const QString& dir) const {
        Q_UNUSED(dir);
        return "EVTX";
    }
    void build_process() {
        process_ = new QProcess(this);
        process_->setProcessChannelMode(QProcess::MergedChannels);
        connect(process_, &QProcess::readyReadStandardOutput, this, [this]() {
            append(QString::fromLocal8Bit(process_->readAllStandardOutput()));
        });
        connect(process_, &QProcess::readyReadStandardError, this, [this]() {
            append(QString::fromLocal8Bit(process_->readAllStandardError()));
        });
        connect(process_, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
                append(QString("\n[exit %1%2]\n").arg(code)
                    .arg(status == QProcess::CrashExit ? " / crash" : ""));
                if (cloud_pause_) cloud_pause_->setText("Pausar job");
                cloud_paused_ = false;
                update_cloud_process_buttons();
                set_cloud_hint(QString());
                const QStringList finished_args = process_->arguments();
                if (finished_args.contains("--cloud-m365-collect") ||
                    finished_args.contains("--cloud-db-import")) {
                    const QString target = cloud_case_ ? cloud_case_->text().trimmed() : case_dir_;
                    QTimer::singleShot(0, this, [this, target]() {
                        load_case(target);
                        refresh_cloud_jobs();
                        refresh_cloud_status();
                    });
                }
                status_label_->setText("Pronto.");
            });
        connect(process_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                append(QString("\n[process error %1]\n").arg(static_cast<int>(error)));
                status_label_->setText("Erro ao executar.");
                update_cloud_process_buttons();
            });
        connect(process_, &QProcess::stateChanged, this,
            [this](QProcess::ProcessState) { update_cloud_process_buttons(); });
        update_cloud_process_buttons();
    }

    void append(const QString& text) {
        if (text.isEmpty()) return;
        QTextCursor cursor(console_->document());
        cursor.movePosition(QTextCursor::End);
        const QStringList lines = text.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            const QString line = lines[i];
            QColor color("#B0B0B0");
            if (line.contains("[exit 0]") || line.contains("sucesso", Qt::CaseInsensitive))
                color = QColor("#4CAF50");
            else if (line.contains("erro", Qt::CaseInsensitive) ||
                     line.contains("error", Qt::CaseInsensitive) ||
                     line.contains("falha", Qt::CaseInsensitive) ||
                     line.contains("exit code", Qt::CaseInsensitive) ||
                     (line.contains("[exit ") && !line.contains("[exit 0]")))
                color = QColor("#F44336");
            else if (line.startsWith(">"))
                color = QColor("#8A8A8A");
            QTextCharFormat format;
            format.setForeground(color);
            cursor.insertText(line, format);
            if (i + 1 < lines.size()) cursor.insertText("\n", format);
        }
        console_->setTextCursor(cursor);
    }

    bool run(const QStringList& args, bool destructive,
             bool require_sanitize = false, bool sanitize_ok = true) {
        if (process_->state() != QProcess::NotRunning) {
            QMessageBox::information(this, "Olho de Deus",
                "Ja existe um comando em execucao.");
            return false;
        }
        if (require_sanitize && !sanitize_ok) {
            QMessageBox::warning(this, "Modo sanitizacao",
                "Marque a confirmacao de modo sanitizacao antes de continuar.");
            return false;
        }
        if (destructive) {
            const auto answer = QMessageBox::warning(this,
                "Confirmar operacao destrutiva",
                "Esta acao e irreversivel.\n\n"
                "Nuvem, backup offsite, roteador e logs remotos NAO sao apagados.\n\n"
                "Continuar?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) return false;
        }
        append("> " + cli() + " " + args.join(" ") + "\n");
        status_label_->setText("Executando...");
        process_->setProgram(cli());
        process_->setArguments(args);
        process_->start();
        update_cloud_process_buttons();
        return true;
    }

    // --- actions -----------------------------------------------------------
    void scan() {
        QStringList a{"--deep-scan", image_->text(), "--out", out_->text()};
        if (list_->isChecked()) a << "--list";
        if (recover_->isChecked()) a << "--recover";
        if (usn_->isChecked()) a << "--usn";
        if (carve_->isChecked()) a << "--carve";
        if (image_->text().isEmpty() || out_->text().isEmpty()) return;
        run(a, false);
    }
    void artifacts() {
        QStringList a{"--artifacts", "--out", out_->text()};
        if (!evtx_->text().isEmpty()) a << "--evtx-file" << evtx_->text();
        if (!prefetch_->text().isEmpty()) a << "--prefetch-dir" << prefetch_->text();
        if (!hive_->text().isEmpty()) a << "--hive-file" << hive_->text();
        if (!srum_->text().isEmpty()) a << "--srum-db" << srum_->text();
        if (secrets_->isChecked()) a << "--browser-secrets";
        run(a, false);
    }
    void raw_image() {
        run({"--image-create", image_->text(), "--out", out_->text() + "/image.raw"}, false);
    }
    void aff4_create() {
        run({"--aff4-zip", image_->text(), "--out", out_->text() + "/image.aff4"}, false);
    }
    void aff4_verify() { run({"--aff4-zip-verify", aff4_->text()}, false); }
    void e01_create() {
        run({"--e01-create", image_->text(), "--out", out_->text() + "/image"}, false);
    }
    void e01_scan() { run({"--e01-scan", e01_->text(), "--out", out_->text()}, false); }

    void image_verify() { run({"--image-verify", adv_image_->text()}, false); }
    void vss_list() { run({"--vss-list"}, false); }
    void vss_read() {
        run({"--vss-read", adv_volume_->text(), "--out", adv_out_->text() + "/vss"}, false);
    }
    void image_evtx() {
        run({"--image-evtx", adv_image_->text(), "--out", adv_out_->text()}, false);
    }
    void image_prefetch() {
        run({"--image-prefetch", adv_image_->text(), "--out", adv_out_->text()}, false);
    }
    void recycle() {
        run({"--recycle-recover", adv_target_->text(), "--out", adv_out_->text()}, false);
    }
    void case_query() { run({"--case-query", adv_target_->text()}, false); }
    void case_summary() { run({"--case-summary", adv_target_->text()}, false); }
    void keygen() { run({"--keygen", adv_key_->text()}, false); }
    void sign_manifest() {
        run({"--sign-manifest", adv_target_->text(), "--key", adv_key_->text()}, false);
    }
    void verify_manifest() {
        run({"--verify-manifest", adv_target_->text(), "--key", adv_key_->text()}, false);
    }
    void deep_usn() {
        run({"--deep-usn-file", adv_target_->text(), "--out", adv_out_->text()}, false);
    }
    void list_disks() { run({"--list-disks"}, false); }
    void exposure_audit() { run({"--exposure-audit", "--out", aud_out_->text()}, false); }
    void residue_clean() {
        const bool from_sanitize = active_tool_ == 2;
        const QString target = from_sanitize ? san_out_->text() : aud_out_->text();
        const bool enabled = from_sanitize ? san_enabled_->isChecked() : true;
        run({"--mode", "sanitize", "--clean-local-residue", "--out", target,
             "--confirm", "CLEAN-RESIDUE"}, true, from_sanitize, enabled);
    }
    void kernel_version() { run({"--kernel-version"}, false); }
    void kernel_read() {
        run({"--kernel-read", kernel_disk_->text(), "--offset",
             kernel_offset_->text().isEmpty() ? "0" : kernel_offset_->text(),
             "--length", kernel_length_->text(), "--out", kernel_out_->text()}, false);
    }
    void help() { run({"--deep-help"}, false); }

    void sanitize_disk() {
        QStringList a{"--mode", "sanitize", "--sanitize-disk", san_disk_->text(),
                      "--serial", san_serial_->text(), "--out", san_out_->text()};
        if (!san_passes_->text().isEmpty()) a << "--passes" << san_passes_->text();
        if (san_verify_->isChecked()) a << "--verify";
        a << "--confirm" << "WIPE-DISK";
        run(a, true, true, san_enabled_->isChecked());
    }
    void nvme_sanitize() {
        run({"--mode", "sanitize", "--nvme-sanitize", san_disk_->text(),
             "--serial", san_serial_->text(), "--action",
             san_action_->text().isEmpty() ? "block" : san_action_->text(),
             "--confirm", "NVME-SANITIZE"}, true, true, san_enabled_->isChecked());
    }
    void ata_erase() {
        QStringList a{"--mode", "sanitize", "--ata-erase", san_disk_->text(),
                      "--serial", san_serial_->text()};
        if (!san_password_->text().isEmpty()) a << "--password" << san_password_->text();
        if (san_enhanced_->isChecked()) a << "--enhanced";
        a << "--confirm" << "ATA-ERASE";
        run(a, true, true, san_enabled_->isChecked());
    }
    void wipe_file() {
        run({"--mode", "sanitize", "--wipe-file", wipe_target_->text(),
             "--out", wipe_out_->text(), "--confirm", "WIPE"}, true, true,
            wipe_enabled_->isChecked());
    }
    void wipe_free() {
        run({"--mode", "sanitize", "--wipe-free", wipe_target_->text(),
             "--out", wipe_out_->text(), "--confirm", "WIPE"}, true, true,
            wipe_enabled_->isChecked());
    }
    void wipe_browser() {
        run({"--mode", "sanitize", "--wipe-browser", "--browser-profile",
             wipe_target_->text(), "--out", wipe_out_->text(),
             "--confirm", "WIPE"}, true, true, wipe_enabled_->isChecked());
    }
    void wipe_hive() {
        run({"--mode", "sanitize", "--wipe-hive", wipe_target_->text(),
             "--out", wipe_out_->text(), "--confirm", "WIPE"}, true, true,
            wipe_enabled_->isChecked());
    }
    void wipe_journal() {
        run({"--mode", "sanitize", "--wipe-journal", wipe_target_->text(),
             "--out", wipe_out_->text(), "--confirm", "WIPE"}, true, true,
            wipe_enabled_->isChecked());
    }
    void wipe_artifacts() {
        run({"--mode", "sanitize", "--wipe-artifacts", "--confirm", "WIPE"},
            true, true, wipe_enabled_->isChecked());
    }
    void purge_usn() {
        run({"--mode", "sanitize", "--purge-usn", wipe_target_->text(),
             "--confirm", "WIPE"}, true, true, wipe_enabled_->isChecked());
    }
    void clear_logs() {
        run({"--mode", "sanitize", "--clear-logs", "--confirm", "WIPE"},
            true, true, wipe_enabled_->isChecked());
    }
    void delete_vss() {
        run({"--mode", "sanitize", "--delete-vss", "--confirm", "WIPE"},
            true, true, wipe_enabled_->isChecked());
    }

    void run_raw_command() {
        const QString raw = adv_command_->text().trimmed();
        if (raw.isEmpty()) return;
        const QStringList a = QProcess::splitCommand(raw);
        const bool destructive = raw.contains("--wipe") ||
            raw.contains("--sanitize-disk") || raw.contains("--nvme-sanitize") ||
            raw.contains("--ata-erase") || raw.contains("--clean-local-residue") ||
            raw.contains("--purge-usn") || raw.contains("--clear-logs") ||
            raw.contains("--delete-vss");
        run(a, destructive);
    }
};

QString style_sheet() { return {}; }

QString load_style_sheet() {
    const QString path = QCoreApplication::applicationDirPath() +
        "/resources/forensic.qss";
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(file.readAll());
    }
    return style_sheet();
}
} // namespace
int run_gui(HINSTANCE) {
    int argc = 1;
    char app_name[] = "OlhoDeDeus_gui";
    char* argv[] = {app_name, nullptr};
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
    app.setStyleSheet(load_style_sheet());
    QFont font("Segoe UI", 10);
    app.setFont(font);
    MainWindow window;
    window.showMaximized();
    return app.exec();
}

} // namespace deep
