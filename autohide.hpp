// autohide.hpp — auto-hide de docks no modelo Visual Studio 2026.
// Dock "auto-oculto" vira uma tira (canal) na borda com o nome rotacionado;
// clicar abre um flyout SOBREPOSTO (nao empurra o layout); sai com o mouse
// (com atraso de 320ms) ou Esc. Sem Q_OBJECT.
#pragma once

#include <QAbstractButton>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QMainWindow>
#include <QPainter>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <algorithm>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <vector>

namespace ui {

enum class Edge { Left, Right, Bottom };

class ChannelTab : public QAbstractButton {
public:
    ChannelTab(const QString& text, Edge edge, QWidget* parent)
        : QAbstractButton(parent), text_(text), edge_(edge) {
        setFocusPolicy(Qt::NoFocus);
        setToolTip(text);
        setCheckable(true);
        const int span = fontMetrics().horizontalAdvance(text) + 20;
        if (edge == Edge::Bottom) { setFixedHeight(24); setMinimumWidth(span); }
        else { setFixedWidth(24); setMinimumHeight(span); }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), isChecked() ? QColor("#3A3A3A")
                                       : (underMouse() ? QColor("#333333") : QColor("#282828")));
        p.setPen(QColor("#0E0E0E"));
        if (edge_ == Edge::Left) p.drawLine(width() - 1, 0, width() - 1, height());
        else if (edge_ == Edge::Right) p.drawLine(0, 0, 0, height());
        else p.drawLine(0, 0, width(), 0);

        const QColor color(isChecked() ? "#FFFFFF" : "#D6D6D6");
        /* o rotulo rotacionado era rasterizado (drawText + SmoothTransformation)
           em TODO repaint; agora fica em cache por cor e e invalidado em resize */
        if (cache_key_ != color.rgb() || label_cache_.isNull()) {
            label_cache_ = label_pixmap(color);
            cache_key_ = color.rgb();
        }
        const QPixmap& label = label_cache_;
        if (!label.isNull()) {
            const int x = (width() - label.width()) / 2;
            const int y = (height() - label.height()) / 2;
            p.drawPixmap(x, y, label);
        }
    }


private:
    QPixmap label_pixmap(const QColor& color) const {
        QFont f;
        f.setFamily("Segoe UI");
        f.setPixelSize(12);
        const QFontMetrics fm(f);
        const int w = fm.horizontalAdvance(text_) + 14;
        const int h = 18;
        QPixmap src(w, h);
        src.fill(Qt::transparent);
        QPainter p(&src);
        p.setFont(f);
        p.setPen(color);
        p.drawText(QRect(0, 0, w, h), Qt::AlignCenter, text_);
        p.end();
        if (edge_ == Edge::Bottom) return src;
        QTransform t;
        t.rotate(edge_ == Edge::Left ? -90 : 90);
        return src.transformed(t, Qt::SmoothTransformation);
    }

    void resizeEvent(QResizeEvent* e) override {
        QAbstractButton::resizeEvent(e);
        label_cache_ = QPixmap();   // fonte/tamanho podem ter mudado
    }

    QString text_;
    Edge edge_;
    mutable QPixmap label_cache_;
    mutable QRgb cache_key_ = 0;
};

class ChannelStrip : public QWidget {
public:
    explicit ChannelStrip(QWidget* parent) : QWidget(parent) {}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#282828"));
        p.setPen(QColor("#0E0E0E"));
        if (width() <= 30) p.drawLine(width() - 1, 0, width() - 1, height());
        else p.drawLine(0, 0, width(), 0);
    }
};

class AutoHidePanel : public QWidget {
public:
    explicit AutoHidePanel(QWidget* parent) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        lay_ = new QVBoxLayout(this);
        lay_->setContentsMargins(1, 1, 1, 1);
        hide_timer_ = new QTimer(this);
        hide_timer_->setSingleShot(true);
        hide_timer_->setInterval(320);
        QObject::connect(hide_timer_, &QTimer::timeout, this, [this]() {
            if (!underMouse() && on_hide) on_hide();
        });
    }

    std::function<void()> on_hide;

    void attach(QWidget* content) {
        detach();
        content_ = content;
        content_->setParent(this);
        content_->show();
        lay_->addWidget(content_);
    }
    QWidget* detach() {
        QWidget* out = content_;
        if (!content_) return nullptr;
        lay_->removeWidget(content_);
        content_->setParent(nullptr);
        content_ = nullptr;
        return out;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#282828"));
        p.setPen(QColor("#454545"));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
    void enterEvent(QEnterEvent*) override { hide_timer_->stop(); }
    void leaveEvent(QEvent*) override { hide_timer_->start(); }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape && on_hide) on_hide();
        QWidget::keyPressEvent(e);
    }

private:
    QVBoxLayout* lay_ = nullptr;
    QWidget* content_ = nullptr;
    QTimer* hide_timer_ = nullptr;
};

class AutoHideManager {
public:
    AutoHideManager(QMainWindow* window, const QString& settings_path)
        : window_(window), settings_path_(settings_path) {
        panel_ = new AutoHidePanel(window_);
        panel_->on_hide = [this]() { hide_flyout(); };
        panel_->hide();
    }

    void register_dock(QDockWidget* dock, Edge edge) {
        /* o card do dock tem [barra de titulo, conteudo]; guardamos o conteudo
           para o flyout poder transportar e devolver sem tocar no widget do dock */
        QWidget* content = nullptr;
        QWidget* owner = nullptr;
        QWidget* card = dock->widget();
        if (card && card->layout() && card->layout()->count() > 1) {
            QLayoutItem* item = card->layout()->itemAt(1);
            if (item && item->widget()) { content = item->widget(); owner = card; }
        }
        entries_.push_back(Entry{dock, content, owner, edge, nullptr, QSize(), false});
        ensure_channel(edge);
    }

    bool is_hidden(QDockWidget* dock) const {
        const Entry* e = find(dock);
        return e && e->hidden;
    }

    void toggle(QDockWidget* dock) { set_hidden(dock, !is_hidden(dock)); }

    void set_hidden(QDockWidget* dock, bool hidden) {
        Entry* e = find(dock);
        if (!e || e->hidden == hidden) return;
        if (hidden) {
            e->last_size = dock->size();
            window_->removeDockWidget(dock);
            dock->hide();
            auto* tab = new ChannelTab(dock->windowTitle(), e->edge, channel(e->edge));
            QObject::connect(tab, &QAbstractButton::clicked, tab, [this, dock]() { toggle_flyout(dock); });
            layout_for(e->edge)->insertWidget(0, tab);
            channel(e->edge)->show();
            if (QDockWidget* h = holder(e->edge)) {
                h->show();
                const int k = 24;
                if (e->edge == Edge::Bottom) {
                    window_->resizeDocks({h}, {k}, Qt::Vertical);
                } else {
                    window_->resizeDocks({h}, {k}, Qt::Horizontal);
                }
            }
            e->tab = tab;
            e->hidden = true;
        } else {
            if (e->tab) { e->tab->deleteLater(); e->tab = nullptr; }
            if (current_ == dock) hide_flyout();
            window_->addDockWidget(area_for(e->edge), dock);
            dock->show();
            e->hidden = false;
            if (empty(e->edge)) {
                channel(e->edge)->hide();
                if (QDockWidget* h = holder(e->edge)) h->hide();
            }
        }
        save();
    }

    void toggle_flyout(QDockWidget* dock) {
        if (current_ == dock) { hide_flyout(); return; }
        if (current_) hide_flyout();
        Entry* e = find(dock);
        if (!e) return;
        /* O QDockWidget nunca solta o proprio widget (setWidget apaga o antigo),
           entao o flyout transporta o CONTEUDO do card e devolve depois para o
           layout dele - antes o dock voltava em branco e o card vazava. */
        QWidget* content = e->content ? e->content : dock->widget();
        if (!content) return;
        if (!e->owner) e->owner = content->parentWidget();
        panel_->attach(content);

        QWidget* strip = channel(e->edge);
        const QPoint origin = strip->mapTo(window_, QPoint(0, 0));
        QSize sz = e->last_size.isValid() && !e->last_size.isEmpty() ? e->last_size : QSize(320, 420);
        int x = origin.x(), y = origin.y(), w = sz.width(), h = sz.height();
        if (e->edge == Edge::Left) {
            x = origin.x() + strip->width();
            h = (std::min)(h, window_->height() - y - 32);
        } else if (e->edge == Edge::Right) {
            w = (std::min)(w, 460);
            x = (std::max)(0, origin.x() - w);
            h = (std::min)(h, window_->height() - y - 32);
        } else {
            h = (std::min)(sz.height(), 300);
            y = (std::max)(0, origin.y() - h);
            w = (std::min)(w, window_->width() - x - 24);
        }
        panel_->setGeometry(QRect(x, y, (std::max)(220, w), (std::max)(140, h)));
        panel_->show();
        panel_->raise();
        panel_->setFocus();
        if (e->tab) e->tab->setChecked(true);
        current_ = dock;
    }

    void hide_flyout() {
        if (!current_) return;
        Entry* e = find(current_);
        QWidget* content = panel_->detach();
        if (content && e) {
            if (e->owner && e->owner->layout()) {
                content->setParent(e->owner);
                e->owner->layout()->addWidget(content);
                e->owner->show();
            } else {
                current_->setWidget(content);   // fallback: dock sem card
            }
            current_->hide();
        }
        if (e && e->tab) e->tab->setChecked(false);
        panel_->hide();
        current_ = nullptr;
    }

    ~AutoHideManager() {
        /* o painel guarda callbacks com "this" cru: zerar antes de morrer */
        if (panel_) panel_->on_hide = nullptr;
    }

    void save() {
        /* durante load() nao gravar: o load aplica set_hidden() e o save()
           reescreveria o ini so com os docks ja registrados (apagando os
           outros, ex. o dock Console, que ainda nao foi registrado) */
        if (loading_) return;
        QSettings s(settings_path_, QSettings::IniFormat);
        QStringList hidden;
        for (const Entry& e : entries_) {
            if (e.hidden && e.dock) hidden << e.dock->objectName();
        }
        s.beginGroup("autohide");
        s.setValue("docks", hidden);
        s.endGroup();
    }

    void load() {
        QSettings s(settings_path_, QSettings::IniFormat);
        s.beginGroup("autohide");
        const QStringList hidden = s.value("docks").toStringList();
        s.endGroup();
        loading_ = true;
        for (const QString& name : hidden) {
            for (Entry& e : entries_) {
                if (e.dock && e.dock->objectName() == name) set_hidden(e.dock, true);
            }
        }
        loading_ = false;
    }

private:
    bool loading_ = false;

    struct Entry {
        QDockWidget* dock = nullptr;
        QWidget* content = nullptr;   // conteudo transportavel (dentro do card)
        QWidget* owner = nullptr;     // quem devolve o conteudo no layout
        Edge edge = Edge::Left;
        ChannelTab* tab = nullptr;
        QSize last_size;
        bool hidden = false;
    };

    Entry* find(QDockWidget* dock) {
        for (Entry& e : entries_) if (e.dock == dock) return &e;
        return nullptr;
    }
    const Entry* find(QDockWidget* dock) const {
        for (const Entry& e : entries_) if (e.dock == dock) return &e;
        return nullptr;
    }
    bool empty(Edge edge) const {
        for (const Entry& e : entries_) if (e.hidden && e.edge == edge) return false;
        return true;
    }
    static Qt::DockWidgetArea area_for(Edge e) {
        return e == Edge::Left ? Qt::LeftDockWidgetArea
                               : (e == Edge::Right ? Qt::RightDockWidgetArea : Qt::BottomDockWidgetArea);
    }
    QWidget* channel(Edge e) {
        return e == Edge::Left ? left_ : (e == Edge::Right ? right_ : bottom_);
    }
    QDockWidget* holder(Edge e) {
        return e == Edge::Left ? holder_left_ : (e == Edge::Right ? holder_right_ : holder_bottom_);
    }
    QBoxLayout* layout_for(Edge e) {
        QWidget* c = channel(e);
        return c ? qobject_cast<QBoxLayout*>(c->layout()) : nullptr;
    }
    void ensure_channel(Edge e) {
        if (channel(e)) return;
        auto* strip = new ChannelStrip(window_);
        strip->setObjectName("autoHideChannel");
        QBoxLayout* lay = (e == Edge::Bottom)
                              ? static_cast<QBoxLayout*>(new QHBoxLayout(strip))
                              : static_cast<QBoxLayout*>(new QVBoxLayout(strip));
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        lay->addStretch(1);
        if (e == Edge::Bottom) { strip->setFixedHeight(24); bottom_ = strip; }
        else if (e == Edge::Left) { strip->setFixedWidth(24); left_ = strip; }
        else { strip->setFixedWidth(24); right_ = strip; }

        auto* holder = new QDockWidget(window_);
        holder->setObjectName(QString("autoHideHolder_") + QString::number(static_cast<int>(e)));
        holder->setFeatures(QDockWidget::NoDockWidgetFeatures);
        auto* empty_title = new QWidget(holder);
        empty_title->setFixedHeight(0);
        holder->setTitleBarWidget(empty_title);
        holder->setStyleSheet("QDockWidget{border:none;margin:0px;padding:0px;}");
        holder->setContentsMargins(0, 0, 0, 0);
        holder->setWidget(strip);
        window_->addDockWidget(area_for(e), holder);
        if (e == Edge::Left) holder_left_ = holder;
        else if (e == Edge::Right) holder_right_ = holder;
        else holder_bottom_ = holder;
        holder->hide();
    }

    QMainWindow* window_ = nullptr;
    QString settings_path_;
    AutoHidePanel* panel_ = nullptr;
    QWidget* left_ = nullptr;
    QWidget* right_ = nullptr;
    QWidget* bottom_ = nullptr;
    QDockWidget* holder_left_ = nullptr;
    QDockWidget* holder_right_ = nullptr;
    QDockWidget* holder_bottom_ = nullptr;
    QDockWidget* current_ = nullptr;
    std::vector<Entry> entries_;
};

}  // namespace ui
