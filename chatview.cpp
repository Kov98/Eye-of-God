// language: C++, file: chatview.cpp, target: Windows 11, Qt 6 Widgets
#include "chatview.hpp"

#include "theme_vs2026.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QVBoxLayout>

namespace olho::ui {
namespace {

QString style_bubble_incoming() {
    return QString("background:%1; color:%2; border:1px solid %3;"
                   " border-radius:8px; padding:6px 9px; font-size:12px;")
        .arg(theme::kSurface, theme::kTextPrimary, theme::kStrokeCard);
}

QString style_bubble_deleted() {
    return QString("background:%1; color:%2; border:1px dashed %3;"
                   " border-radius:8px; padding:6px 9px; font-size:12px;")
        .arg(theme::kOutputPane, theme::kTextMuted, theme::kSelectionBorder);
}

QString format_stamp(qint64 ts) {
    if (ts <= 0) return QString();
    // WhatsApp keeps seconds; anything larger is already milliseconds.
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(ts > 100000000000ll ? ts / 1000 : ts);
    return dt.toString("dd/MM/yyyy HH:mm");
}

}  // namespace

struct ChatView::impl {
    struct message {
        QString text;
        qint64 ts = 0;
        bool deleted = false;
        QString origin;
    };
    struct chat {
        QString jid;
        QVector<message> messages;
    };
    QVector<chat> chats;
    QVector<message> deleted;
};

ChatView::ChatView(QWidget* parent) : QWidget(parent), data_(new impl) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Toolbar
    auto* bar = new QWidget(this);
    bar->setObjectName("detailsToolbar");
    auto* bar_layout = new QHBoxLayout(bar);
    bar_layout->setContentsMargins(8, 4, 8, 4);
    bar_layout->setSpacing(8);

    header_ = new QLabel("Nenhuma conversa carregada", bar);
    header_->setStyleSheet(QString("color:%1; font-size:12px;").arg(theme::kTextSecondary));

    show_deleted_ = new QCheckBox("Mostrar recuperadas", bar);
    show_deleted_->setChecked(true);
    show_deleted_->setStyleSheet(QString("color:%1; font-size:12px;").arg(theme::kTextPrimary));

    auto* open_button = new QPushButton("Abrir conversas...", bar);
    open_button->setCursor(Qt::PointingHandCursor);

    bar_layout->addWidget(header_, 1);
    bar_layout->addWidget(show_deleted_);
    bar_layout->addWidget(open_button);
    root->addWidget(bar);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    chats_ = new QListWidget(splitter);
    chats_->setObjectName("chatList");
    chats_->setMinimumWidth(220);
    chats_->setStyleSheet(QString("QListWidget{background:%1; color:%2; border:none; font-size:12px;}"
                                  "QListWidget::item{padding:5px 8px; border-radius:4px;}"
                                  "QListWidget::item:selected{background:%3; border:1px solid %4;}")
                              .arg(theme::kSurface, theme::kTextPrimary,
                                   theme::kHoverRow, theme::kSelectionBorder));

    scroll_ = new QScrollArea(splitter);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setStyleSheet(QString("QScrollArea{background:%1; border:none;}").arg(theme::kDocument));

    bubble_host_ = new QWidget(scroll_);
    bubble_host_->setStyleSheet(QString("background:%1;").arg(theme::kDocument));
    bubble_layout_ = new QVBoxLayout(bubble_host_);
    bubble_layout_->setContentsMargins(10, 10, 10, 10);
    bubble_layout_->setSpacing(4);
    bubble_layout_->addStretch(1);
    scroll_->setWidget(bubble_host_);

    splitter->addWidget(chats_);
    splitter->addWidget(scroll_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    // setStretchFactor alone leaves the list eating half the pane; explicit
    // sizes are what actually bound it.
    splitter->setSizes({260, 900});
    root->addWidget(splitter, 1);

    connect(open_button, &QPushButton::clicked, this, [this]() { open_dialog(); });
    connect(chats_, &QListWidget::currentRowChanged, this, [this](int) { rebuild(); });
    connect(show_deleted_, &QCheckBox::toggled, this, [this](bool) { rebuild(); });
}

bool ChatView::try_autoload() {
    const QString base = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        base + "/conversations.json",
        base + "/recovered/conversations.json",
        QDir::currentPath() + "/conversations.json",
    };
    for (const QString& path : candidates) {
        if (!QFile::exists(path)) continue;
        if (load(path, nullptr)) return true;
    }
    return false;
}

void ChatView::open_dialog() {
    QString start = QCoreApplication::applicationDirPath();
    const QString path = QFileDialog::getOpenFileName(this, "conversations.json", start,
                                                      "Conversas (*.json)");
    if (path.isEmpty()) return;
    QString error;
    if (!load(path, &error)) {
        header_->setText("Falha ao carregar: " + error);
    }
}

bool ChatView::load(const QString& conversations_json, QString* error) {
    QFile file(conversations_json);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = "nao abriu " + conversations_json;
        return false;
    }
    QJsonParseError parse_error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = parse_error.errorString();
        return false;
    }

    data_->chats.clear();
    data_->deleted.clear();
    live_count_ = 0;
    deleted_count_ = 0;

    const QJsonObject root = doc.object();
    for (const QJsonValue& value : root.value("conversas").toArray()) {
        const QJsonObject chat_obj = value.toObject();
        impl::chat chat;
        chat.jid = chat_obj.value("jid").toString();
        for (const QJsonValue& m : chat_obj.value("mensagens").toArray()) {
            const QJsonObject mo = m.toObject();
            impl::message msg;
            msg.text = mo.value("texto").toString();
            msg.ts = static_cast<qint64>(mo.value("ts").toDouble());
            msg.deleted = mo.value("apagada").toBool();
            if (msg.text.isEmpty()) continue;
            chat.messages.push_back(msg);
            ++live_count_;
        }
        data_->chats.push_back(chat);
    }

    for (const QJsonValue& value : root.value("apagadas").toArray()) {
        const QJsonObject mo = value.toObject();
        impl::message msg;
        msg.deleted = true;
        msg.origin = mo.value("origem").toString();
        const QJsonArray texts = mo.value("textos").toArray();
        // The last text column is the message body; the earlier ones are jids.
        for (const QJsonValue& t : texts) {
            const QString s = t.toString();
            if (!s.isEmpty()) msg.text = s;
        }
        if (msg.text.isEmpty()) continue;
        data_->deleted.push_back(msg);
        ++deleted_count_;
    }

    header_->setText(QString("%1 conversas | %2 mensagens | %3 recuperadas")
                         .arg(data_->chats.size())
                         .arg(live_count_)
                         .arg(deleted_count_));

    chats_->clear();
    for (const auto& chat : data_->chats) {
        chats_->addItem(QString("%1  (%2)").arg(chat.jid).arg(chat.messages.size()));
    }
    if (show_deleted_->isChecked() && !data_->deleted.isEmpty()) {
        chats_->addItem(QString("--- recuperadas (%1) ---").arg(data_->deleted.size()));
    }
    if (chats_->count() > 0) chats_->setCurrentRow(0);
    rebuild();
    return true;
}

void ChatView::clear_bubbles() {
    while (bubble_layout_->count() > 0) {
        QLayoutItem* item = bubble_layout_->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
}

void ChatView::rebuild() {
    clear_bubbles();

    const int row = chats_->currentRow();
    const bool deleted_channel = show_deleted_->isChecked() &&
                                 !data_->deleted.isEmpty() &&
                                 row == chats_->count() - 1;

    auto add_bubble = [this](const QString& text, qint64 ts, bool deleted, const QString& origin) {
        auto* holder = new QWidget(bubble_host_);
        auto* row_layout = new QHBoxLayout(holder);
        row_layout->setContentsMargins(0, 0, 0, 0);

        auto* bubble = new QLabel(holder);
        bubble->setWordWrap(true);
        bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bubble->setMaximumWidth(620);
        bubble->setStyleSheet(deleted ? style_bubble_deleted() : style_bubble_incoming());

        QString body = text;
        if (deleted) {
            body.prepend(QString("[recuperada/%1] ").arg(origin.isEmpty() ? "?" : origin));
        }
        const QString stamp = format_stamp(ts);
        if (!stamp.isEmpty()) body += QString("\n%1").arg(stamp);
        bubble->setText(body);

        row_layout->addWidget(bubble);
        row_layout->addStretch(1);   // incoming messages sit on the left
        bubble_layout_->addWidget(holder);
    };

    if (deleted_channel) {
        bubble_layout_->addWidget(make_section_label("Mensagens apagadas recuperadas"));
        for (const auto& msg : data_->deleted) {
            add_bubble(msg.text, msg.ts, true, msg.origin);
        }
    } else if (row >= 0 && row < data_->chats.size()) {
        const auto& chat = data_->chats[row];
        for (const auto& msg : chat.messages) {
            add_bubble(msg.text, msg.ts, msg.deleted, msg.origin);
        }
    }

    bubble_layout_->addStretch(1);
    scroll_->verticalScrollBar()->setValue(0);
}

QLabel* ChatView::make_section_label(const QString& text) {
    auto* label = new QLabel(text, bubble_host_);
    label->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600; padding:2px 2px 6px 2px;")
                             .arg(theme::kTextSecondary));
    return label;
}

}  // namespace olho::ui
