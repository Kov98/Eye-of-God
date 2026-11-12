// docktitle.hpp — barra de titulo de dock no estilo Visual Studio 2026.
// 24px de altura, fundo #282828, texto 12px, glifos de pin/opcoes/fechar a direita.
// Substitui o QDockWidget::title nativo (que nao aceita botoes nem densidade do VS).
#pragma once

#include <QDockWidget>
#include <QMenu>
#include <functional>
#include <utility>
#include <QLabel>
#include <QHBoxLayout>
#include <QPainter>
#include <QToolButton>
#include <QWidget>

namespace ui {

class DockTitleBar : public QWidget {
public:
    DockTitleBar(const QString& title, QDockWidget* dock)
        : QWidget(dock), dock_(dock) {
        setObjectName("dockTitleBar");
        setFixedHeight(24);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(8, 0, 4, 0);
        row->setSpacing(2);

        auto* label = new QLabel(title, this);
        label->setObjectName("dockTitleText");
        label->setStyleSheet("color:#E6E6E6;font-size:12px;background:transparent;");
        row->addWidget(label, 1);

        // glifos: pin, opcoes, fechar (mesma ordem do VS)
        pin_ = glyph(QString::fromUtf8("\u2191"), "Fixar / auto-ocultar", [this, dock]() {
            if (pin_handler_) { pin_handler_(); return; }
            dock->setFloating(!dock->isFloating());
            if (dock->isFloating()) dock->resize(dock->width() < 320 ? 360 : dock->width(), dock->height());
        });
        row->addWidget(pin_);
        row->addWidget(glyph(QString::fromUtf8("\u22EF"), "Opcoes do dock", [this]() {
            if (menu_) menu_->popup(mapToGlobal(QPoint(width() - 8, height())));
        }));
        row->addWidget(glyph(QString::fromUtf8("\u2715"), "Fechar", [dock]() { dock->hide(); }));
    }

    void set_menu(QMenu* menu) { menu_ = menu; }
    /* embutido no card do painel: sem fundo proprio nem divisor inferior, para o
       canto arredondado do card aparecer atras do titulo */
    void set_embedded(bool on) { embedded_ = on; update(); }
    QMenu* menu() const { return menu_; }   // usado pelo diagnostico (OLHO_DUMP)
    void set_pin_handler(std::function<void()> fn) { pin_handler_ = std::move(fn); }

protected:
    void paintEvent(QPaintEvent*) override {
        if (embedded_) return;   // fundo/canto vem do card
        QPainter p(this);
        p.fillRect(rect(), QColor("#282828"));
        p.setPen(QColor("#0E0E0E"));
        p.drawLine(0, height() - 1, width(), height() - 1);
    }

private:
    QToolButton* glyph(const QString& text, const QString& tip, std::function<void()> fn) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setFixedSize(20, 20);
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet("QToolButton{background:transparent;border:none;color:#CECECE;font-size:11px;}"
                         "QToolButton:hover{background:#333333;border-radius:2px;color:#FFFFFF;}");
        connect(b, &QToolButton::clicked, this, [fn = std::move(fn)]() { if (fn) fn(); });
        return b;
    }

    QDockWidget* dock_ = nullptr;
    QMenu* menu_ = nullptr;
    QToolButton* pin_ = nullptr;
    bool embedded_ = false;
    std::function<void()> pin_handler_;
};

}  // namespace ui
