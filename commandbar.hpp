// commandbar.hpp — barra de comandos customizavel (modelo Visual Studio 2026).
// Anatomia: grupos separados por linha, botoes 24x24, texto opcional, split-button
// com chevron, combos/checks embutidos, overflow "..." por grupo, chevron de opcoes
// no fim. Layout e visibilidade persistidos em toolbar.ini ao lado do exe.
// Sem Q_OBJECT: callbacks por std::function.
#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <algorithm>

#include "commands.hpp"
#include "theme_icons.hpp"

namespace ui {

/* Emenda entre duas barras: spacer invisivel de 9px. Precisa suprimir o
   paintEvent, senao a regra global QWidget{background:#1C1C1C} do QSS pinta uma
   tarja de chrome de 1px na altura do meio (medido: linha y=44 da barra 1 toda
   em 0x1C). Quem desenha os contornos e os grips e o CommandBar. */
class SegmentSpacer : public QWidget {
public:
    explicit SegmentSpacer(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName("segmentBreak");
        setFixedSize(9, 1);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
    }

protected:
    void paintEvent(QPaintEvent*) override {}
};

// ---------------------------------------------------------------- botoes
class CommandButton : public QAbstractButton {
public:
    CommandButton(const QString& iconPath, const QString& text, const QString& tip,
                  bool showText, bool mono, QWidget* parent)
        : QAbstractButton(parent), showText_(showText) {
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tip);
        setIconSize(QSize(16, 16));
        if (!iconPath.isEmpty()) {
            const QIcon themed = theme::mono_icon(iconPath);
            if (mono && !themed.isNull()) {
                setIcon(themed);
            } else {
                /* so constroi o QIcon do arquivo quando o tint nao serviu */
                setIcon(QIcon(QDir(QCoreApplication::applicationDirPath()).filePath(iconPath)));
            }
        }
        setText(text);
        if (showText) {
            /* VS medido: o rotulo do botao de comando e mais pesado que o menu */
            label_font_ = font();
            label_font_.setWeight(QFont::DemiBold);
        }
        const int textW = showText ? QFontMetrics(label_font_).horizontalAdvance(text) + 6 : 0;
        setFixedSize(showText ? (16 + 8 + textW + 8) : 24, 24);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF box = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        QColor fill(0, 0, 0, 0);
        if (isEnabled()) {
            if (isDown()) fill = QColor("#2F2F2F");
            else if (underMouse()) fill = QColor("#333333");
            else if (isCheckable() && isChecked()) fill = QColor("#3A3A3A");
        }
        if (fill.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(box, 4, 4);
        }
        p.setOpacity(isEnabled() ? 1.0 : 0.45);
        QIcon::Mode mode = isEnabled() ? (underMouse() ? QIcon::Active : QIcon::Normal) : QIcon::Disabled;
        const QPixmap pm = icon().pixmap(QSize(16, 16), mode);
        p.drawPixmap(showText_ ? 4 : (width() - 16) / 2, (height() - 16) / 2, pm);
        if (showText_) {
            p.setFont(label_font_);
            p.setPen(isEnabled() ? QColor("#FFFFFF") : QColor("#8C8C8C"));
            p.drawText(QRect(26, 0, width() - 30, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
        }
    }

private:
    QFont label_font_;
    bool showText_ = false;
};

// split button: chevron embutido no canto inferior direito (como no VS)
class CommandSplitButton : public QAbstractButton {
public:
    CommandSplitButton(const cmd::Command& c, QWidget* parent) : QAbstractButton(parent), c_(c) {
        setFixedSize(30, 24);
        setFocusPolicy(Qt::NoFocus);
        QString tip = c.tooltip.isEmpty() ? c.title : c.tooltip;
        if (!c.shortcut.isEmpty()) tip += "  (" + c.shortcut + ")";
        setToolTip(tip);
        mono_ = theme::mono_icon(c.iconPath);
        if (!c.menu_items) return;
        menu_ = new QMenu(this);
        const QStringList items = c.menu_items();
        for (int i = 0; i < items.size(); ++i) {
            const QString label = items[i];
            menu_->addAction(label, this, [this, i]() { if (c_.menu_invoke) c_.menu_invoke(i); });
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor fill(0, 0, 0, 0);
        if (isDown()) fill = QColor("#2F2F2F");
        else if (underMouse()) fill = QColor("#333333");
        if (fill.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);
        }
        const QPixmap pm = mono_.pixmap(QSize(16, 16), underMouse() ? QIcon::Active : QIcon::Normal);
        p.drawPixmap(3, 4, pm);
        // chevron pequeno no canto inferior direito
        QPolygon tri;
        tri << QPoint(21, 15) << QPoint(27, 15) << QPoint(24, 19);
        p.setPen(Qt::NoPen);
        p.setBrush(underMouse() ? QColor("#FFFFFF") : QColor("#C8C8C8"));
        p.drawPolygon(tri);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->position().x() >= width() - 11 && menu_) {
            menu_->popup(mapToGlobal(QPoint(0, height())));
            return;
        }
        QAbstractButton::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->position().x() >= width() - 11 && menu_) return;
        QAbstractButton::mouseReleaseEvent(e);
        if (rect().contains(e->position().toPoint()) && c_.invoke) c_.invoke();
    }

private:
    cmd::Command c_;
    QIcon mono_;
    QMenu* menu_ = nullptr;
};

// ---------------------------------------------------------------- barra
class CommandBar : public QWidget {
public:
    CommandBar(const QString& layoutId, int fixedHeight, QWidget* parent = nullptr)
        : QWidget(parent), layoutId_(layoutId) {
        setObjectName("commandBar");
        setFixedHeight(fixedHeight);
        row_ = new QHBoxLayout(this);
        /* 15px a esquerda (era 10): no VS o primeiro glifo comeca em x=18, com
           o bloco em 6 e o grip em 7..9 - ou seja ~12px de area de pega no grip.
           Com 10px de margem a faixa agarravel ficava com 4px e ninguem acertava. */
        row_->setContentsMargins(15, 1, 6, 1);
        row_->setSpacing(0);
        setMouseTracking(true);   // cursor de "pega" no grip, sem precisar clicar
        load();
        rebuild();
        /* auto-teste do reorder sem mouse: OLHO_REORDER="from,to" */
        if (qEnvironmentVariableIsSet("OLHO_REORDER")) {
            const QStringList p = qEnvironmentVariable("OLHO_REORDER").split(',');
            if (p.size() == 2 && !p[0].isEmpty() && !p[1].isEmpty()) {
                QTimer::singleShot(0, this, [this, p]() { move_segment(p[0].toInt(), p[1].toInt()); });
            }
        }
        /* auto-teste do GESTO (press/move/release sinteticos em Qt, sem mouse do
           sistema): OLHO_DRAGSIM="from,to" prova o hit-test do grip, o limiar de
           3px e o mapeamento do vao de insercao. */
        if (qEnvironmentVariableIsSet("OLHO_DRAGSIM")) {
            const QStringList p = qEnvironmentVariable("OLHO_DRAGSIM").split(',');
            if (p.size() == 2) {
                QTimer::singleShot(500, this, [this, p]() {
                    const int from = p[0].toInt();
                    const int to = p[1].toInt();
                    const QVector<QPair<qreal, qreal>> segs = segment_bounds();
                    if (from < 0 || from >= segs.size()) return;
                    const qreal sy = height() / 2.0;
                    const QPointF src(segs[from].first + 2.0, sy);
                    qreal dstx = 0.0;
                    if (to < segs.size()) dstx = (segs[to].first + segs[to].second) / 2.0;
                    else dstx = segs.last().second + 3.0;
                    const QPointF mid((src.x() + dstx) / 2.0, sy);
                    const QPointF dst(dstx, sy);
                    QMouseEvent pr(QEvent::MouseButtonPress, src, mapToGlobal(src.toPoint()),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent mv1(QEvent::MouseMove, mid, mapToGlobal(mid.toPoint()),
                                    Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent mv2(QEvent::MouseMove, dst, mapToGlobal(dst.toPoint()),
                                    Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent rl(QEvent::MouseButtonRelease, dst, mapToGlobal(dst.toPoint()),
                                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    auto log = [this](const char* tag) {
                        QFile f(QCoreApplication::applicationDirPath() + "/drag.log");
                        if (!f.open(QIODevice::Append | QIODevice::Text)) return;
                        QTextStream ts(&f);
                        ts << tag << " drag_from=" << drag_from_ << " dragging=" << int(dragging_)
                           << " gap=" << drop_gap_ << " segs=";
                        const QVector<QPair<qreal, qreal>> b = segment_bounds();
                        for (int i = 0; i < b.size(); ++i) ts << b[i].first << "/" << b[i].second << " ";
                        ts << " first=" << (order_.isEmpty() ? QString() : order_.first()) << "\n";
                    };
                    log("pre ");
                    QCoreApplication::sendEvent(this, &pr);
                    log("press");
                    QCoreApplication::sendEvent(this, &mv1);
                    QCoreApplication::sendEvent(this, &mv2);
                    log("move");
                    QCoreApplication::sendEvent(this, &rl);
                    log("rel ");
                });
            }
        }
        /* auto-teste do visual do arrasto: OLHO_DRAG="from,gap" (grip roxo + caret) */
        if (qEnvironmentVariableIsSet("OLHO_DRAG")) {
            const QStringList p = qEnvironmentVariable("OLHO_DRAG").split(',');
            if (p.size() == 2 && !p[0].isEmpty() && !p[1].isEmpty()) {
                QTimer::singleShot(600, this, [this, p]() {
                    drag_from_ = p[0].toInt();
                    drop_gap_ = p[1].toInt();
                    dragging_ = true;
                    update();
                });
            }
        }
    }

    void refresh() { rebuild(); }

    void schedule_rebuild() {
        if (rebuild_queued_) return;
        rebuild_queued_ = true;
        QTimer::singleShot(0, this, [this]() {
            rebuild_queued_ = false;
            rebuild();
        });
    }

    void set_on_layout_changed(std::function<void()> fn) { onLayoutChanged_ = std::move(fn); }
    void set_on_visibility_changed(std::function<void(bool)> fn) { onVisibilityChanged_ = std::move(fn); }

    bool bar_visible() const { return visible_; }
    void set_source(const QString& s) { source_ = s; }

    /* sem boolean trap: o "persistir" e um enum nomeado (ENM-9) */
    enum class Persist { No, Yes };

    void set_bar_visible(bool on, Persist persist = Persist::Yes) {
        /* log de auditoria: descobre quem desliga a barra sem clique */
        {
            QFile log(QCoreApplication::applicationDirPath() + "/toolbar.log");
            if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                QTextStream ts(&log);
                ts << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "Z id=" << layoutId_
                   << " visible=" << (on ? 1 : 0) << " src=" << (source_.isEmpty() ? "?" : source_)
                   << "\n";
            }
        }
        visible_ = on;
        if (persist == Persist::Yes) save();
        if (onVisibilityChanged_) onVisibilityChanged_(on);
        rebuild();
    }

    /* --- barras (segmentos) ------------------------------------------------
       Modelo VS: cada toolbar da linha pode ser ligada/desligada, e a escolha
       fica no botao direito da barra. Persistido em toolbar.ini. */
    static QString segment_label(const QString& seg) {
        if (seg == "Aquisicao") return QString::fromUtf8("Aquisi\xC3\xA7\xC3\xA3o");
        if (seg == "Exibicao") return QString::fromUtf8("Exibi\xC3\xA7\xC3\xA3o");
        return seg;
    }

    QStringList segments() const {
        QStringList out;
        const cmd::Registry& reg = cmd::Registry::instance();
        for (const QString& id : order_) {
            if (id == "|" || id == "||") continue;
            const cmd::Command* c = reg.get(id);
            if (!c) continue;
            const QString seg = segment_of(c->id, c->category);
            if (!out.contains(seg)) out.push_back(seg);
        }
        return out;
    }

    bool segment_visible(const QString& seg) const { return !hidden_segments_.contains(seg); }

    void set_segment_visible(const QString& seg, bool on, Persist persist = Persist::Yes) {
        if (on) hidden_segments_.removeAll(seg);
        else if (!hidden_segments_.contains(seg)) hidden_segments_.push_back(seg);
        if (persist == Persist::Yes) save();
        rebuild();
        if (onLayoutChanged_) onLayoutChanged_();
    }

    // usado pelo menu Exibir > Barras de Ferramentas > Personalizar
    void popup_customize_menu(const QPoint& global_pos) {
        QMenu* m = build_customize_menu(this);
        m->exec(global_pos);
        m->deleteLater();
    }

protected:
    /* Uma barra do VS nao e um bloco unico: sao varias toolbars encostadas, cada
       uma com o SEU bloco arredondado e o SEU grip. Medido no recorte em
       x=462..470: preenchimento termina em 461, contorno em 462, 4px de chrome
       (463..466), contorno em 467, grip 468..470 (3px) e o preenchimento da
       proxima barra a partir de 471. As curvas internas sao as mesmas (raio ~4,
       rampa de antialias em 2-3px). */
    void paintEvent(QPaintEvent*) override {
        painting_ = true;   // nao reativar layout de dentro do paint
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor chrome("#1C1C1C");
        const QColor fill("#262626");
        const QColor outline("#0E0E0E");
        const QColor grip("#131313");
        p.fillRect(rect(), chrome);   // respiro do chrome dos dois lados

        const QVector<QPair<qreal, qreal>> segs = segment_bounds();
        const qreal top = 0.5;
        const qreal bottom = height() - 0.5;
        for (int s = 0; s < segs.size(); ++s) {
            const qreal left = segs[s].first;
            const qreal right = segs[s].second;
            if (right - left > 2.0) {
                const QRectF seg(left, top, right - left, bottom - top);
                QPainterPath path;
                path.addRoundedRect(seg, 4.0, 4.0);   // VS: curva de ~4px no canto
                p.fillPath(path, fill);
                p.setPen(QPen(outline, 1.0));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);

                /* grip: 3px #131313 encostados na borda esquerda. Tem de comecar
                   em .0 e nao em .5: com left+1 o antialias deixava meio pixel de
                   chrome em x=7 e x=10 (medido: 0x1C nos dois) e o grip saia com
                   2px cheios em vez de 3. VS: x=6 contorno, x=7..9 grip, x=10
                   em diante preenchimento. */
                p.setClipPath(path);
                p.fillRect(QRectF(seg.left() + 0.5, seg.top(), 3.0, seg.height()), grip);
                p.setClipping(false);
            }
        }

        /* arrastando uma barra: o grip dela fica roxo (#9184EE) e aparece o caret
           de insercao no ponto onde ela cai (modelo VS) */
        if (dragging_ && drag_from_ >= 0 && drag_from_ < segs.size()) {
            const QColor accent("#9184EE");
            const QPair<qreal, qreal>& d = segs[drag_from_];
            QPainterPath dpath;
            dpath.addRoundedRect(QRectF(d.first, top, d.second - d.first, bottom - top), 4.0, 4.0);
            p.setClipPath(dpath);
            p.fillRect(QRectF(d.first + 0.5, top, 3.0, bottom - top), accent);
            p.setClipping(false);

            const int gap = drop_gap_ < 0 ? 0 : drop_gap_;
            qreal cx = 0.0;
            if (gap <= 0) cx = segs.first().first - 3.0;
            else if (gap >= segs.size()) cx = segs.last().second + 1.0;
            else cx = (segs[gap - 1].second + segs[gap].first) / 2.0 - 1.0;
            p.fillRect(QRectF(cx, top, 2.0, bottom - top), accent);
        }
        painting_ = false;
    }

    /* Rebuild a cada resize custava dezenas de QWidget/QMenu por segundo ao
       arrastar a janela. Agora: so quando a largura util muda e no maximo um
       rebuild por ciclo de eventos. */
    void resizeEvent(QResizeEvent* e) override {
        QWidget::resizeEvent(e);
        if (e->size().width() == last_width_) return;
        last_width_ = e->size().width();
        schedule_rebuild();
    }

private:
    static QString settings_path() {
        return QCoreApplication::applicationDirPath() + "/toolbar.ini";
    }

    void load() {
        QSettings s(settings_path(), QSettings::IniFormat);
        s.beginGroup(layoutId_);
        order_ = s.value("order").toStringList();
        closeChevron_ = s.value("chevron", true).toBool();
        hidden_segments_ = s.value("hiddenBars").toStringList();
        const int cfg = s.value("cfg", 1).toInt();
        s.endGroup();
        s.beginGroup("bars");
        visible_ = s.value(layoutId_, true).toBool();
        s.endGroup();
        if (order_.isEmpty()) {
            order_ = default_order();
        } else if (cfg < 3) {
            /* cfg 2 injetou separadores "|" automaticos; agora as barras vem do
               segmento (categoria), entao marcador magro sobrando e ruido */
            order_.removeAll("|");
            save();
        }
    }

    void save() {
        QSettings s(settings_path(), QSettings::IniFormat);
        s.beginGroup(layoutId_);
        s.setValue("order", order_);
        s.setValue("chevron", closeChevron_);
        if (!hidden_segments_.isEmpty()) s.setValue("hiddenBars", hidden_segments_);
        else s.remove("hiddenBars");
        s.setValue("cfg", 3);
        s.endGroup();
        s.beginGroup("bars");
        s.setValue(layoutId_, visible_);
        s.endGroup();
    }

    bool enabled_for(const cmd::Command& c) const { return c.isEnabled ? c.isEnabled() : true; }

    static QString tooltip_for(const cmd::Command& c) {
        QString tip = c.tooltip.isEmpty() ? c.title : c.tooltip;
        if (!c.shortcut.isEmpty()) tip += "  (" + c.shortcut + ")";
        return tip;
    }

    int command_width(const cmd::Command& c) const {
        switch (c.kind) {
            case cmd::Kind::TextButton: {
                /* tem de casar com CommandButton::paintEvent, que desenha em
                   DemiBold; medir em Regular cortava o ultimo botao do grupo */
                QFont f = font();
                f.setWeight(QFont::DemiBold);
                const int tw = QFontMetrics(f).horizontalAdvance(c.title) + 6;
                return 16 + 8 + tw + 8;
            }
            case cmd::Kind::SplitButton: return 30;
            case cmd::Kind::Combo: return 180;
            case cmd::Kind::Check: return 22 + QFontMetrics(font()).horizontalAdvance(c.title) + 8;
            default: return 26;
        }
    }

    QWidget* make_widget(const cmd::Command& c) {
        switch (c.kind) {
            case cmd::Kind::TextButton: {
                auto* b = new CommandButton(c.iconPath, c.title, tooltip_for(c), true, c.mono, this);
                b->setCheckable(c.checkable);
                if (c.checkable && c.isChecked) b->setChecked(c.isChecked());
                b->setEnabled(enabled_for(c));
                /* guardar &c seria UAF: o Command vive num QVector que realoca
                   quando outra ferramenta e registrada. Resolve o id no clique. */
                const QString id = c.id;
                QObject::connect(b, &QAbstractButton::clicked, this, [id]() {
                    if (const cmd::Command* cmd = cmd::Registry::instance().get(id)) {
                        if (cmd->invoke) cmd->invoke();
                    }
                });
                return b;
            }
            case cmd::Kind::SplitButton:
                return new CommandSplitButton(c, this);
            case cmd::Kind::Combo:
            case cmd::Kind::Check: {
                QWidget* w = c.widget_factory ? c.widget_factory(this) : nullptr;
                if (w) {
                    w->setParent(this);
                    if (c.kind == cmd::Kind::Combo) w->setFixedHeight(24);
                }
                return w;
            }
            default: {
                auto* b = new CommandButton(c.iconPath, c.title, tooltip_for(c), false, c.mono, this);
                b->setCheckable(c.checkable);
                if (c.checkable && c.isChecked) b->setChecked(c.isChecked());
                b->setEnabled(enabled_for(c));
                const QString id = c.id;
                QObject::connect(b, &QAbstractButton::clicked, this, [id]() {
                    if (const cmd::Command* cmd = cmd::Registry::instance().get(id)) {
                        if (cmd->invoke) cmd->invoke();
                    }
                });
                return b;
            }
        }
    }

    /* Icone desenhado, nao glifo: U+2691/U+22EF/U+25BE caem na fonte de emoji
       colorida do Windows e saem fora de escala. 16px logicos com DPR 2 para
       nao borrar em tela HiDPI sem escalonamento do Qt. */
    static QIcon chevron_icon(const QColor& c) {
        const qreal s = 16.0;
        QPixmap pm(int(s * 2), int(s * 2));
        pm.setDevicePixelRatio(2.0);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(c, 1.4);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        QPainterPath path;
        path.moveTo(3.5, 6.0);
        path.lineTo(s / 2.0, 10.5);
        path.lineTo(s - 3.5, 6.0);
        p.drawPath(path);
        return QIcon(pm);
    }

    QToolButton* make_icon_button(const QIcon& icon, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(icon);
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setFixedSize(22, 24);
        b->setFocusPolicy(Qt::NoFocus);
        /* menu-indicator zerado: o Qt desenha uma setinha no canto inferior
           direito de todo QToolButton com menu, que sobreposta ao chevron
           virava um rabisco (parecia bug). */
        b->setStyleSheet("QToolButton{background:transparent;border:none;}"
                         "QToolButton::menu-indicator{image:none;width:0;height:0;}"
                         "QToolButton:hover{background:#333333;border-radius:4px;}"
                         "QToolButton:pressed{background:#2F2F2F;}");
        return b;
    }

    /* Qual BARRA (bloco separado, modelo VS) recebe o comando. O VS separa por
       FUNCAO (Standard | Build | Debug | Layout), nao por registro; aqui cada
       classe de ferramenta forense vira uma barra. Visualizar + Arvore dividem a
       mesma barra de exibicao e "Sobre" sai da barra de console. */
    static QString segment_of(const QString& id, const QString& category) {
        if (id == "help.about") return "Ajuda";
        /* bloco de UM comando so nao existe no VS: o "Rodar engine" entra em
           Ferramentas */
        if (category == "Executar") return "Ferramentas";
        if (category == "Visualizar" || category == "Arvore") return "Exibicao";
        return category;
    }


    /* Emenda entre duas barras: spacer transparente de 9px entre 5px e 4px de
       respiro (18px no total). Quem desenha os dois contornos e os dois grips e
       o paintEvent, que precisa saber onde cada barra comeca e termina. */
    void add_segment_break() {
        row_->addSpacing(5);
        row_->addWidget(new SegmentSpacer(this));
        row_->addSpacing(4);
    }

    QStringList default_order() const {
        const cmd::Registry& reg = cmd::Registry::instance();
        QStringList base = reg.defaultOrderFor(layoutId_);
        if (layoutId_ == "main") {
            /* a barra de cima e a principal: num perfil novo ela leva todas as
               ferramentas, nao so as marcadas com "main" no registro (era o que
               deixava a barra com 12 comandos e tres blocos vazios) */
            for (const cmd::Command& c : reg.all()) {
                if (!base.contains(c.id)) base.push_back(c.id);
            }
        }
        return base;
    }

    QToolButton* make_chevron(const QString& text, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setFixedSize(18, 24);
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet("QToolButton{background:transparent;border:none;color:#D6D6D6;font-size:12px;}"
                         "QToolButton::menu-indicator{image:none;width:0;height:0;}"
                         "QToolButton:hover{background:#333333;border-radius:4px;color:#FFFFFF;}"
                         "QToolButton:pressed{background:#2F2F2F;}");
        return b;
    }

    void add_separator() {
        /* VS medido: entre grupos da MESMA faixa a separacao e uma linha de
           1px x 16px em #383838 (colunas x=82/223/312/408 do recorte), com ~4px
           de respiro de cada lado. O grip escuro (GroupDivider) so aparece na
           fronteira entre faixas arrastaveis da toolbar. */
        auto* line = new QWidget(this);
        line->setFixedSize(1, 16);
        line->setStyleSheet("background:#383838;");
        row_->addSpacing(4);
        row_->addWidget(line);
        row_->addSpacing(4);
    }

    void rebuild() {
        if (rebuilding_) return;
        rebuilding_ = true;
        while (QLayoutItem* item = row_->takeAt(0)) {
            /* sem setParent(nullptr): o widget ja saiu do layout e
               deleteLater basta; reparentar o promote a top-level */
            if (QWidget* w = item->widget()) { w->hide(); w->deleteLater(); }
            delete item;
        }

        const cmd::Registry& reg = cmd::Registry::instance();
        /* order_ aceita marcadores: "|" = separador de grupo (1px x 16px em
           #383838) e "||" = quebra de segmento (9px com grip). Sem eles o
           agrupamento ficava preso as categorias. */
        QStringList seq;
        for (const QString& id : order_) {
            if (id == "|" || id == "||") seq.push_back(id);
            else if (reg.get(id)) seq.push_back(id);
        }

        const int margins = row_->contentsMargins().left() + row_->contentsMargins().right();
        const int reserved = (closeChevron_ ? 24 : 2);
        int avail = width() - margins - reserved;
        int used = 0;

        QMenu* group_overflow = nullptr;

        QString current;

        auto flush_group = [&]() {
            if (current.isEmpty()) return;
            /* Modelo VS: o "..." do grupo so existe quando o grupo transbordou.
               Com tudo visivel o VS nao mostra chevron nenhum por grupo; a
               personalizacao fica no "Opcoes da barra" da ponta direita. */
            if (group_overflow) {
                auto* dots = make_chevron(QString::fromUtf8("\u22EF"), "Opcoes deste grupo");
                group_overflow->setParent(dots);
                QMenu* merged = new QMenu(dots);
                /* reaproveita as QAction originais (ja conectadas); recriar por
                   texto perdia o handler do comando */
                for (QAction* a : group_overflow->actions()) merged->addAction(a);
                if (!merged->actions().isEmpty()) merged->addSeparator();
                for (QAction* a : build_group_menu(current, dots)->actions()) merged->addAction(a);
                dots->setMenu(merged);
                dots->setPopupMode(QToolButton::InstantPopup);
                row_->addWidget(dots);
                used += 20;
            }
            group_overflow = nullptr;

        };

        bool emitted_any = false;
        bool pending_sep = false;
        bool pending_heavy = false;
        QString current_seg;
        for (const QString& entry : seq) {
            if (entry == "|" || entry == "||") {
                pending_sep = true;
                pending_heavy = (entry == "||");
                continue;
            }
            const cmd::Command* c = reg.get(entry);
            if (!c) continue;
            const QString seg = segment_of(c->id, c->category);
            if (hidden_segments_.contains(seg)) continue;   // toolbar desligada
            const bool cat_changed = (c->category != current);
            const bool seg_changed = (seg != current_seg);
            if (cat_changed) {
                flush_group();
                current = c->category;
            }
            if (emitted_any) {
                if (seg_changed || pending_heavy) {
                    /* barra nova: bloco proprio, com o seu contorno e o seu grip */
                    add_segment_break();
                    used += 18;   // 5 + 9 + 4
                } else if (pending_sep) {
                    add_separator();
                    used += 9;    // 4 + 1 + 4
                }
            }
            current_seg = seg;
            pending_sep = false;
            pending_heavy = false;
            const int w = command_width(*c) + 2;
            if (used + w > avail) {
                if (!group_overflow) group_overflow = new QMenu(this);
                const cmd::Command copy = *c;
                group_overflow->addAction(c->title, this, [copy]() { if (copy.invoke) copy.invoke(); });
                continue;
            }
            used += w;
            emitted_any = true;
            if (QWidget* wid = make_widget(*c)) row_->addWidget(wid);
        }
        flush_group();
        row_->addStretch(1);

        if (closeChevron_) {
            /* VS: no canto direito existe UM botao so, o chevron de opcoes.
               Antes eram dois QToolButton com glifo de texto - o "pin" usava
               U+2691, que no Windows cai na fonte de emoji colorida e saia como
               uma bandeirinha verde/branca de 12px (parecia bug), e o chevron
               U+25BE ficava pequeno demais. Agora: icone desenhado em QPixmap
               com DPR 2, traco 1.4 arredondado, 16x16 num botao 22x24. As duas
               acoes do pin (ocultar/redefinir) ja vivem no menu de opcoes. */
            auto* opts = make_icon_button(chevron_icon(QColor("#C8C8C8")), "Opcoes da barra");
            opts->setPopupMode(QToolButton::InstantPopup);
            opts->setMenu(build_customize_menu(opts));
            row_->addWidget(opts);
        }
        rebuilding_ = false;
        /* O paint desenha um bloco por segmento usando a geometria dos spacers.
           Sem ativar o layout aqui, o paint roda antes dos filhos ganharem
           posicao e as emendas do meio da barra nao aparecem (medido: 2 de 6). */
        row_->activate();
        update();
    }

    /* geometria de filho mudando nao repinta o pai; o paint depende dela */
    bool event(QEvent* e) override {
        if (e->type() == QEvent::LayoutRequest) {
            QTimer::singleShot(0, this, [this]() { update(); });
        }
        return QWidget::event(e);
    }

    /* Botao direito na barra = menu de toolbars, igual ao VS: lista das classes
       com check, personalizacao e ocultar. Desligar uma classe nao cria buraco:
       o rebuild pula os itens e as emendas se reorganizam. */
    void contextMenuEvent(QContextMenuEvent* e) override {
        QMenu m(this);
        m.addSection("Barras nesta linha");
        const QStringList segs = segments();
        for (const QString& seg : segs) {
            QAction* a = m.addAction(segment_label(seg));
            a->setCheckable(true);
            a->setChecked(segment_visible(seg));
            const QString key = seg;
            QObject::connect(a, &QAction::toggled, this, [this, key](bool on) {
                set_segment_visible(key, on, Persist::No);
            });
        }
        m.addSeparator();
        m.addMenu(build_customize_menu(&m));
        QAction* hide = m.addAction("Ocultar esta barra");
        QObject::connect(hide, &QAction::triggered, this, [this]() {
            source_ = "context-menu";
            set_bar_visible(false);
        });
        m.exec(e->globalPos());
        save();   // os toggles acima rodaram com Persist::No
    }

    /* ---------------------------------------------------------- arrastar barra
       O "pega" e o grip preto de 3px na borda esquerda de cada bloco (o VS faz
       igual). Durante o arrasto o grip fica roxo e um caret marca onde a barra
       cai; soltar remonta order_ e persiste. */
    QVector<qreal> break_positions() {
        QVector<qreal> breaks;
        if (!row_) return breaks;
        auto grab = [this, &breaks]() {
            breaks.clear();
            for (int i = 0; i < row_->count(); ++i) {
                QLayoutItem* it = row_->itemAt(i);
                QWidget* w = it ? it->widget() : nullptr;
                if (w && w->objectName() == "segmentBreak") breaks.push_back(w->geometry().left());
            }
            std::sort(breaks.begin(), breaks.end());
        };
        grab();
        /* acabou de ser remontada: os spacers ainda estao em x=0. Ativar o
           layout aqui evita que o hit-test do grip erre / o caret caia no lugar
           errado no primeiro frame (medido: bounds viravam 5.5/0.5). */
        if (!painting_ && !breaks.isEmpty() && breaks.first() <= 0.5 && width() > 50) {
            row_->activate();
            grab();
        }
        return breaks;
    }

    QVector<QPair<qreal, qreal>> segment_bounds() {
        const QVector<qreal> breaks = break_positions();
        QVector<QPair<qreal, qreal>> out;
        qreal left = 6.5;
        for (int s = 0; s <= breaks.size(); ++s) {
            const bool last = (s == breaks.size());
            const qreal right = last ? (width() - 6.5) : (breaks[s] + 0.5);
            out.push_back(qMakePair(left, right));
            if (!last) left = breaks[s] + 5.5;
        }
        return out;
    }

    /* indice do bloco cujo grip esta em x, ou -1 */
    int segment_at_grip(qreal x) {
        const QVector<QPair<qreal, qreal>> segs = segment_bounds();
        for (int s = 0; s < segs.size(); ++s) {
            if (x >= segs[s].first - 1.0 && x < segs[s].first + 8.0) return s;
        }
        return -1;
    }

    /* em qual "vao" entre blocos o cursor esta: 0 = antes do primeiro */
    int gap_at(qreal x) {
        const QVector<QPair<qreal, qreal>> segs = segment_bounds();
        int gap = 0;
        for (int s = 0; s < segs.size(); ++s) {
            if (x > (segs[s].first + segs[s].second) / 2.0) gap = s + 1;
        }
        return gap;
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            const int seg = segment_at_grip(e->position().x());
            if (seg >= 0) {
                drag_from_ = seg;
                drag_origin_ = e->position().toPoint();
                drop_gap_ = seg;
                e->accept();
                return;
            }
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (drag_from_ >= 0) {
            if (!dragging_ && (e->position().toPoint() - drag_origin_).manhattanLength() >= 3) {
                dragging_ = true;
                setCursor(Qt::ClosedHandCursor);
            }
            if (dragging_) {
                drop_gap_ = gap_at(e->position().x());
                update();
            }
            return;
        }
        setCursor(segment_at_grip(e->position().x()) >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (drag_from_ >= 0 && e->button() == Qt::LeftButton) {
            const int from = drag_from_;
            const bool was_dragging = dragging_;
            const int gap = gap_at(e->position().x());
            drag_from_ = -1;
            dragging_ = false;
            drop_gap_ = -1;
            setCursor(Qt::OpenHandCursor);
            update();
            if (was_dragging) {
                const int to = (gap > from) ? gap - 1 : gap;
                if (to != from) move_segment(from, to);
            }
            return;
        }
        QWidget::mouseReleaseEvent(e);
    }

    void leaveEvent(QEvent* e) override {
        if (drag_from_ < 0) unsetCursor();
        QWidget::leaveEvent(e);
    }

    /* move o bloco `from` para a posicao `to`, remontando order_ por classe */
    void move_segment(int from, int to) {
        const cmd::Registry& reg = cmd::Registry::instance();
        QStringList keys;
        QVector<QStringList> items;
        for (const QString& id : order_) {
            if (id == "|" || id == "||") continue;   // marcador solto nao sobrevive
            const cmd::Command* c = reg.get(id);
            if (!c) continue;
            const QString seg = segment_of(c->id, c->category);
            int k = keys.indexOf(seg);
            if (k < 0) {
                keys.push_back(seg);
                items.push_back(QStringList());
                k = keys.size() - 1;
            }
            items[k].push_back(id);
        }
        if (from < 0 || to < 0 || from >= keys.size() || to >= keys.size() || from == to) {
            update();
            return;
        }
        keys.move(from, to);
        items.move(from, to);
        QStringList out;
        for (int k = 0; k < keys.size(); ++k) out += items[k];
        if (out == order_) {
            update();
            return;
        }
        order_ = out;
        save();
        rebuild();
        if (onLayoutChanged_) onLayoutChanged_();
    }

    QMenu* build_group_menu(const QString& category, QWidget* parent) {
        auto* menu = new QMenu(parent);
        const cmd::Registry& reg = cmd::Registry::instance();
        for (const cmd::Command& c : reg.all()) {
            if (c.category != category) continue;
            QAction* a = menu->addAction(c.title);
            a->setCheckable(true);
            a->setChecked(order_.contains(c.id));
            const QString id = c.id;
            QObject::connect(a, &QAction::toggled, this, [this, id](bool on) {
                if (on) { if (!order_.contains(id)) order_.push_back(id); }
                else { order_.removeAll(id); }
                save();
                rebuild();
                if (onLayoutChanged_) onLayoutChanged_();
            });
        }
        menu->addSeparator();
        menu->addAction("Redefinir barra", this, [this]() {
            order_ = default_order();
            save();
            rebuild();
        });
        return menu;
    }

    QMenu* build_customize_menu(QWidget* parent) {
        auto* menu = new QMenu(parent);
        const cmd::Registry& reg = cmd::Registry::instance();
        QMenu* add = menu->addMenu("Adicionar ou remover botoes");
        for (const QString& category : reg.categories()) {
            add->addSection(category);
            for (const cmd::Command& c : reg.all()) {
                if (c.category != category) continue;
                QAction* a = add->addAction(c.title);
                a->setCheckable(true);
                a->setChecked(order_.contains(c.id));
                const QString id = c.id;
                QObject::connect(a, &QAction::toggled, this, [this, id](bool on) {
                    if (on) { if (!order_.contains(id)) order_.push_back(id); }
                    else { order_.removeAll(id); }
                    save();
                    rebuild();
                    if (onLayoutChanged_) onLayoutChanged_();
                });
            }
        }
        menu->addSeparator();
        menu->addAction("Redefinir barra", this, [this]() {
            order_ = default_order();
            save();
            rebuild();
            if (onLayoutChanged_) onLayoutChanged_();
        });
        QAction* hide = menu->addAction("Ocultar barra");
        QObject::connect(hide, &QAction::triggered, this, [this]() {
            source_ = "customize-menu";
            set_bar_visible(false);
        });
        return menu;
    }

    QString layoutId_;
    QStringList order_;
    bool closeChevron_ = true;
    bool visible_ = true;
    QStringList hidden_segments_;   // toolbars desligadas (modelo VS)
    bool painting_ = false;
    int drag_from_ = -1;            // bloco sendo arrastado (-1 = nenhum)
    int drop_gap_ = -1;             // vao de insercao sob o cursor
    QPoint drag_origin_;
    bool dragging_ = false;
    bool rebuilding_ = false;
    bool rebuild_queued_ = false;
    int last_width_ = -1;
    QString source_;   // quem pediu a mudanca (auditoria do toolbar.log)
    QHBoxLayout* row_ = nullptr;
    std::function<void()> onLayoutChanged_;
    std::function<void(bool)> onVisibilityChanged_;
};

}  // namespace ui
