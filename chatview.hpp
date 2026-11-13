// language: C++, file: chatview.hpp, target: Windows 11, Qt 6 Widgets
// Conversation viewer for recovered WhatsApp data.
//
// Deliberately free of Q_OBJECT: this project compiles the Qt GUI with a plain
// g++ invocation and no moc step, so a class that needs moc would break the
// build. Everything here is wired with lambdas.
//
// The widget consumes conversations.json produced by wa_recover: it does not
// open the database and knows no SQL.
#pragma once

#include "export_publish.hpp"   // publicacao de export: um caminho so no produto

#include <QString>
#include <QWidget>

class QListWidget;
class QScrollArea;
class QVBoxLayout;
class QLabel;
class QCheckBox;

namespace olho::ui {

class ChatView : public QWidget {
public:
    explicit ChatView(QWidget* parent = nullptr);

    // Reads the export. Returns false and fills *error on a malformed file.
    bool load(const QString& conversations_json, QString* error = nullptr);

    // Looks for conversations.json beside the executable and in ./recovered.
    // Returns true when something was loaded, so a case opened from a script
    // needs no manual dialog step.
    bool try_autoload();

    int live_count() const { return live_count_; }
    int deleted_count() const { return deleted_count_; }

    // Exporta o que o visualizador tem carregado: as conversas e, quando o
    // "mostrar recuperadas" esta ligado, as mensagens apagadas recuperadas.
    // Mesmo contrato de publicacao da Timeline e da Galeria.
    export_result export_messages_csv(const QString& path);

private:
    void clear_bubbles();
    void rebuild();
    void open_dialog();
    QLabel* make_section_label(const QString& text);

    QListWidget* chats_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* bubble_host_ = nullptr;
    QVBoxLayout* bubble_layout_ = nullptr;
    QLabel* header_ = nullptr;
    QCheckBox* show_deleted_ = nullptr;

    int live_count_ = 0;
    int deleted_count_ = 0;

    struct impl;
    impl* data_ = nullptr;
};

}  // namespace olho::ui
