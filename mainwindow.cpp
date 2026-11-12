// language: C++, file: gui_qt.cpp, target: Windows 10/11
// Qt 6 Widgets shell for the deep engine. Layout inspired by modern IDE and
// forensic suites: activity sidebar, case context, card-based workspace and a
// persistent console. Every action still invokes OlhoDeDeus_deep.exe.

#include "gui.hpp"
#include <windowsx.h>
#include "hardware_inventory.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
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
#include <QMenu>
#include <QMenuBar>
#include <QWidgetAction>
#include <QWindow>
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
#include <QTimer>
#include <QLockFile>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QVector>
#include <QAction>
#include <QDateTime>
#include <QCloseEvent>
#include <QDockWidget>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QPainter>
#include <QPainterPath>
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
#include "theme_vs2026.hpp"
#include "commands.hpp"
#include "commandbar.hpp"
#include "docktitle.hpp"
#include "autohide.hpp"
#include "chatview.hpp"
#include "timeline_index.hpp"
#include "acquireview.hpp"
#include "galleryview.hpp"
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
#include "cloud/cli/dropbox_auth.hpp"
#include "cloud/cli/collect_flow.hpp"
#include "social/dyi_import.hpp"
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
#include <atomic>
#include <cmath>
#include <thread>

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
        painter.fillRect(viewport()->rect(), QColor(theme::document()));
        painter.setFont(font());
        const int line = 18;
        const int rows = data_.size() / 16;
        const int first = verticalScrollBar()->value();
        const int visible = viewport()->height() / line + 1;
        for (int r = first; r < qMin(rows, first + visible); ++r) {
            const int y = (r - first) * line + 3;
            painter.setPen(QColor(theme::gutter()));
            painter.drawText(10, y + 12, QString("%1").arg(r * 16, 8, 16, QChar('0')).toUpper());
            painter.setPen(QColor(theme::textOnDocument()));
            QString hex;
            QString ascii;
            for (int c = 0; c < 16; ++c) {
                const unsigned char value = static_cast<unsigned char>(data_[r * 16 + c]);
                hex += QString("%1 ").arg(value, 2, 16, QChar('0')).toUpper();
                ascii += (value >= 32 && value < 127) ? QChar(value) : QChar('.');
            }
            painter.drawText(108, y + 12, hex);
            painter.setPen(QColor(theme::syntaxBlue()));
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
        painter.fillRect(rect(), QColor(theme::document()));
        // Goteira esquerda larga o bastante para o nome da fonte: as linhas da
        // timeline sao POR FONTE, e sem o nome a faixa nao significa nada.
        const QRect area = rect().adjusted(150, 34, -26, -44);
        painter.setPen(QPen(QColor(theme::lineGrid()), 1));
        if (!times_.isEmpty()) {
            painter.drawLine(area.left(), area.bottom(), area.right(), area.bottom());
            painter.drawLine(area.left(), area.top(), area.left(), area.bottom());
        }
        painter.setFont(QFont("Segoe UI", 8));
        painter.setPen(QColor(theme::textMuted()));
        // Eixo so quando existe dado. Antes ele imprimia "2026-10-20 .. 2026-10-24"
        // mesmo sem nenhum evento no caso: data inventada num laudo e inaceitavel.
        if (!times_.isEmpty()) {
            const qint64 span = qMax<qint64>(1, times_.first().msecsTo(times_.last()));
            for (int i = 0; i <= 4; ++i) {
                const int x = area.left() + area.width() * i / 4;
                painter.drawLine(x, area.bottom(), x, area.bottom() + 6);
                const QString axis =
                    times_.first().addMSecs(span * i / 4).toString("MM-dd HH:mm");
                painter.drawText(x - 30, area.bottom() + 22, axis);
            }
        }
        if (!times_.isEmpty()) {
            const QDateTime first = times_.first();
            const QDateTime last = times_.last();
            const qint64 span = qMax<qint64>(1, first.msecsTo(last));

            // ---- faixas por FONTE -------------------------------------------
            // Antes a altura era (i % 5), ou seja: posicao vertical sorteada pelo
            // indice. Agora cada fonte (o prefixo do rotulo) tem a sua faixa, e os
            // eventos ficam na mesma linha do tempo que os irmaos deles.
            QStringList sources;
            QHash<QString, int> source_index;
            for (int i = 0; i < times_.size(); ++i) {
                QString source = labels_.value(i).section(QStringLiteral(" #"), 0, 0).trimmed();
                if (source.isEmpty()) source = QStringLiteral("Evento");
                if (!source_index.contains(source)) {
                    source_index.insert(source, sources.size());
                    sources << source;
                }
            }
            constexpr int kMaxLanes = 8;
            const bool overflow = sources.size() > kMaxLanes;
            const int lane_count = overflow ? kMaxLanes : qMax(1, sources.size());
            const auto lane_for_source = [&](int index) {
                if (!overflow) return index;
                return index < kMaxLanes - 1 ? index : kMaxLanes - 1;
            };
            const double lane_height = area.height() / double(lane_count);
            const QFont lane_font("Segoe UI", 8);
            const QFontMetrics lane_metrics(lane_font);
            painter.setFont(lane_font);

            // Rotulo curto por evento: a hora sempre, o id so quando cabe. O id
            // completo (jid) fica no painel de detalhes; repetir 36 vezes na mesma
            // faixa era o que virava borrao.
            QVector<double> last_label_x(lane_count, -1.0e9);
            QVector<int> lane_of(times_.size(), 0);

            for (int lane = 0; lane < lane_count; ++lane) {
                const QString name = overflow && lane == kMaxLanes - 1
                    ? QStringLiteral("outras fontes (%1)").arg(sources.size() - (kMaxLanes - 1))
                    : sources.value(lane);
                const double y = area.top() + lane_height * (lane + 0.5);
                if (lane > 0) {
                    painter.setPen(QPen(QColor(theme::lineGrid()), 1, Qt::DotLine));
                    painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
                }
                painter.setPen(QColor(theme::textMuted()));
                const QRect caption(area.left() - 144, int(y) - 8, 138, 16);
                painter.drawText(caption, Qt::AlignRight | Qt::AlignVCenter,
                                 lane_metrics.elidedText(name, Qt::ElideMiddle, 134));
            }

            // Posicao de cada evento: primeiro passo, porque as barras precisam dos
            // indices de faixa antes de desenhar qualquer coisa.
            QVector<double> x_of(times_.size(), 0.0);
            QVector<int> count_of(lane_count, 0);
            for (int i = 0; i < times_.size(); ++i) {
                const double ratio = static_cast<double>(first.msecsTo(times_[i])) / span;
                x_of[i] = area.left() + area.width() * ratio;
                QString key = labels_.value(i).section(QStringLiteral(" #"), 0, 0).trimmed();
                if (key.isEmpty()) key = QStringLiteral("Evento");
                const int lane = lane_for_source(source_index.value(key));
                lane_of[i] = lane;
                ++count_of[lane];
            }

            // Barras de densidade em vez de pontos: 243 mensagens em 7 minutos, uma
            // bolinha por evento, viram uma linha continua que nao informa nada. A
            // altura da barra diz QUANTA coisa aconteceu naquela fatia de tempo.
            const int buckets = qBound(8, area.width() / 10, 160);
            QVector<QVector<int>> histogram(lane_count, QVector<int>(buckets, 0));
            for (int i = 0; i < times_.size(); ++i) {
                const double ratio = (x_of.at(i) - area.left()) / double(qMax(1, area.width()));
                const int bucket = qBound(0, int(ratio * (buckets - 1) + 0.5), buckets - 1);
                ++histogram[lane_of.at(i)][bucket];
            }
            const double bar_width = qMax(1.0, area.width() / double(buckets) - 1.0);
            for (int lane = 0; lane < lane_count; ++lane) {
                int peak = 0;
                for (int value : histogram.at(lane)) peak = qMax(peak, value);
                if (peak <= 0) continue;
                const double base = area.top() + lane_height * (lane + 1) - 6.0;
                const double full = qMax(6.0, lane_height - 16.0);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(theme::accent()));
                for (int bucket = 0; bucket < buckets; ++bucket) {
                    const int value = histogram.at(lane).at(bucket);
                    if (value <= 0) continue;
                    // Escala em raiz: um pico de 40 nao pode achatr a rotina de 2.
                    const double height = full * std::sqrt(double(value) / double(peak));
                    const double x = area.left() + area.width() * bucket / double(buckets);
                    painter.drawRect(QRectF(x, base - height, bar_width, height));
                }
            }

            for (int i = 0; i < times_.size(); ++i) {
                const int lane = lane_of.at(i);
                const double y = area.top() + lane_height * (lane + 0.5);
                const QPointF point(x_of.at(i), y);

                // Rotulo: no maximo um a cada 150 px NA MESMA faixa, sempre dentro
                // da area. Sem estas duas regras o texto dos eventos vizinhos se
                // sobrepoe - era o defeito visivel na captura.
                if (point.x() - last_label_x.at(lane) < 150.0) continue;
                const QString stamp = times_.at(i).toString(QStringLiteral("HH:mm:ss"));
                const QString id = labels_.value(i).section(QStringLiteral("#"), -1);
                QString text = stamp;
                if (!id.isEmpty()) text += QStringLiteral("  ") + id;
                const int width = lane_metrics.horizontalAdvance(text);
                double x = point.x() + 6.0;
                if (x + width > area.right()) x = point.x() - 6.0 - width;
                if (x < area.left() + 2.0) continue;   // nao cabe nem recuando
                last_label_x[lane] = point.x();
                painter.setPen(QColor(theme::textDim()));
                painter.drawText(QPointF(x, area.top() + lane_height * lane + 14.0), text);
            }
        } else {
            // ESTADO VAZIO HONESTO.
            //
            // Aqui havia uma timeline de enfeite: seis eventos inventados
            // ("Aquisicao iniciada 08:12", "$MFT parseado 08:19"...) desenhados
            // mesmo com zero eventos no caso. Num produto forense isso e o pior
            // tipo de defeito: o painel AFIRMA atividade que nao existe. Caso sem
            // timeline agora diz que nao tem.
            const QRect card(area.center().x() - 210, area.center().y() - 42, 420, 84);
            painter.setPen(QPen(QColor(theme::selectionBorder()), 1));
            painter.setBrush(QColor(theme::surface()));
            painter.drawRoundedRect(card, 4, 4);
            QFont empty_font("Segoe UI", 9, QFont::DemiBold);
            painter.setFont(empty_font);
            painter.setPen(QColor(theme::textPrimary()));
            painter.drawText(card.adjusted(14, 12, -14, 0), Qt::AlignHCenter | Qt::AlignTop,
                             QStringLiteral("Nenhum evento de timeline neste caso"));
            QFont hint_font("Segoe UI", 8);
            painter.setFont(hint_font);
            painter.setPen(QColor(theme::textSecondary()));
            painter.drawText(card.adjusted(14, 34, -14, -8),
                             Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                             QStringLiteral("A aba Tabela lista os arquivos do caso e o laudo "
                                            "carrega a evidencia. A linha do tempo aparece "
                                            "quando existe timeline.jsonl em qualquer pasta "
                                            "do caso (raiz, recovered/, social/ ou cloud/)."));
        }
        painter.setPen(QColor(theme::textOnDocument()));
        painter.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        painter.drawText(18, 22, "TIMELINE / EVENTOS CORRELACIONADOS");
    }
private:
    QVector<QDateTime> times_;
    QStringList labels_;
};
class ContourTabBar : public QTabBar {
public:
    explicit ContourTabBar(QWidget* parent = nullptr) : QTabBar(parent) {}

protected:
    void paintEvent(QPaintEvent* event) override {
        QTabBar::paintEvent(event);
        /* VS 2026 medido: o destaque do documento ativo nao contorna a aba, ele
           contorna o poco inteiro - linha de 1px accent no topo (y=99 do ref,
           x=318..1584) e nas laterais (x=318 e x=1584 em y=150/300/600).
           O restante do contorno vem do QTabWidget::pane no QSS. */
        QPainter painter(this);
        const QColor accent(theme::accent());
        painter.setPen(QPen(accent, 1.0));
        painter.drawLine(0, 0, width() - 1, 0);
        if (height() > 1) {
            painter.drawLine(0, 0, 0, height() - 1);
            painter.drawLine(width() - 1, 0, width() - 1, height() - 1);
        }
    }
};

class CenterTabs : public QTabWidget {
public:
    explicit CenterTabs(QWidget* parent = nullptr) : QTabWidget(parent) {}
    void useTabBar(QTabBar* bar) { setTabBar(bar); }
};

/* O QToolBarLayout centraliza o widget unico dentro da barra (medido: 11px de
   deslocamento vertical), entao o chrome e posicionado a mao em (0,0,w,h). */
class ChromeToolBar : public QToolBar {
public:
    explicit ChromeToolBar(QWidget* parent = nullptr) : QToolBar(parent) {}
    void attach_chrome(QWidget* chrome) {
        chrome_ = chrome;
        chrome->setParent(this);
        chrome->setGeometry(0, 0, width(), height());
        chrome->show();
        chrome->raise();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QToolBar::resizeEvent(event);
        if (chrome_) chrome_->setGeometry(0, 0, width(), height());
    }
    void showEvent(QShowEvent* event) override {
        QToolBar::showEvent(event);
        if (chrome_) chrome_->setGeometry(0, 0, width(), height());
    }

private:
    QWidget* chrome_ = nullptr;
};

/* Faixas do chrome com geometria explicita: 30 + 4 + 30 + 5 = 69px. */
class ChromeBands : public QWidget {
public:
    explicit ChromeBands(QWidget* parent = nullptr) : QWidget(parent) {}
    QWidget* bar_a = nullptr;
    QWidget* gap = nullptr;
    QWidget* bar_b = nullptr;
    QWidget* band = nullptr;

    /* isHidden() e nao isVisible(): durante build_ui os filhos ainda nao foram
       mostrados e isVisible() responde false, zerando a altura do chrome. */
    static bool band_on(const QWidget* w) { return w && !w->isHidden(); }

    int total_height() const {
        int h = 0;
        if (band_on(bar_a)) h += 30;
        if (band_on(gap)) h += 4;
        if (band_on(bar_b)) h += 30;
        if (band_on(band)) h += 5;
        return h;
    }

    void layout_bands() {
        int y = 0;
        auto place = [&](QWidget* w, int h) {
            if (!band_on(w)) return;
            w->setGeometry(0, y, width(), h);
            y += h;
        };
        place(bar_a, 30);
        place(gap, 4);
        place(bar_b, 30);
        place(band, 5);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#1C1C1C"));
    }
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        layout_bands();
    }
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        layout_bands();
    }
};

/* Card do dock. O fundo e o contorno arredondado vem do QSS; o canto INFERIOR
   pode ser coberto pela view (arvore/tabela) porque o Qt nao recorta filho com
   raio. Tentativas descartadas: overlay pintado por cima (cobria o painel) e
   setMask() no card (colapsava a area dos docks). */
class CardFrame : public QFrame {
public:
    explicit CardFrame(QWidget* parent = nullptr) : QFrame(parent) {}
};

/* Faixa de 5px: 4px #1C1C1C + 1px divisor. */
class ThinBand : public QWidget {
public:
    explicit ThinBand(QWidget* parent = nullptr) : QWidget(parent) {}
    void set_fill(const QColor& fill) { fill_ = fill; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), fill_);
        if (height() > 0) p.fillRect(0, height() - 1, width(), 1, QColor("#0E0E0E"));
    }

private:
    QColor fill_ = QColor("#1C1C1C");
};

class DocumentFrame : public QFrame {
public:
    explicit DocumentFrame(QTabBar* tabs, QWidget* parent = nullptr)
        : QFrame(parent), tabs_(tabs) {
        setAttribute(Qt::WA_StyledBackground, true);
        if (tabs_) {
            connect(tabs_, &QTabBar::currentChanged, this,
                    [this](int) { update(); });
        }
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QFrame::paintEvent(event);
        if (!tabs_ || tabs_->count() == 0) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor accent(theme::accent());
        QPen pen(accent, 1.0);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QRectF box = rect().adjusted(0.5, 0.5, -0.5, -0.5);

        // one continuous path: both bottom corners rounded
        const qreal radius = 12.0;
        QPainterPath path;
        path.moveTo(box.left(), box.top());
        path.lineTo(box.left(), box.bottom() - radius);
        path.quadTo(box.left(), box.bottom(),
                    box.left() + radius, box.bottom());
        path.lineTo(box.right() - radius, box.bottom());
        path.quadTo(box.right(), box.bottom(),
                    box.right(), box.bottom() - radius);
        path.lineTo(box.right(), box.top());
        painter.drawPath(path);

    }

private:
    QTabBar* tabs_ = nullptr;
};

class DocumentTopLine : public QWidget {
public:
    explicit DocumentTopLine(QTabBar* tabs, QWidget* parent = nullptr)
        : QWidget(parent), tabs_(tabs) {
        setFixedHeight(1);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TranslucentBackground, true);
        if (tabs_) {
            connect(tabs_, &QTabBar::currentChanged, this,
                    [this](int) { update(); });
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (!tabs_ || tabs_->count() == 0) return;
        QPainter painter(this);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(theme::accent()));
        const int index = tabs_->currentIndex();
        if (index < 0) return;
        const QRect tab = tabs_->tabRect(index);
        if (!tab.isValid()) return;
        const QPoint tabLeft = mapFromGlobal(tabs_->mapToGlobal(tab.topLeft()));
        const int gapLeft = tabLeft.x();
        const int gapRight = tabLeft.x() + tab.width();
        if (gapLeft > 0) painter.drawRect(0, 0, gapLeft, height());
        if (gapRight < width()) {
            painter.drawRect(gapRight, 0, width() - gapRight, height());
        }
    }

private:
    QTabBar* tabs_ = nullptr;
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
        const QColor base(theme::commandBar());
        const QColor hover(theme::hoverBar());
        QColor fill(theme::commandBar());
        if (active_) {
            fill = QColor(theme::lineGrid());
        } else if (hover_ > 0.0) {
            fill = QColor(
                base.red() + static_cast<int>((hover.red() - base.red()) * hover_),
                base.green() + static_cast<int>((hover.green() - base.green()) * hover_),
                base.blue() + static_cast<int>((hover.blue() - base.blue()) * hover_));
        }
        painter.setBrush(fill);
        QColor border = active_ ? QColor(theme::selectionBorder()) : QColor(theme::lineGrid());
        if (!active_ && hover_ > 0.01) {
            border = QColor(62 + static_cast<int>(18 * hover_),
                            62 + static_cast<int>(18 * hover_),
                            64 + static_cast<int>(18 * hover_));
        }
        painter.setPen(QPen(border, active_ ? 1.5 : 1.0));
        painter.drawRoundedRect(box, 4, 4);
        if (active_) {
            painter.fillRect(QRectF(0, 0, 2, height()), QColor(theme::accent()));
        }
        const QPixmap pixmap = icon_.pixmap(26, 26);
        painter.drawPixmap(12, (height() - 26) / 2, pixmap);
        painter.setPen(active_ || hover_ > 0.4 ? QColor(theme::textPrimary()) : QColor(theme::textDim()));
        painter.setFont(QFont("Segoe UI", 10));
        painter.drawText(QRect(50, 0, width() - 56, height()),
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
class TitleMenuBar : public QMenuBar {
public:
    explicit TitleMenuBar(QWidget* parent = nullptr) : QMenuBar(parent) {}

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton &&
            actionAt(event->position().toPoint()) == nullptr) {
            if (window() && window()->windowHandle()) {
                window()->windowHandle()->startSystemMove();
            }
            event->accept();
            return;
        }
        QMenuBar::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton &&
            actionAt(event->position().toPoint()) == nullptr) {
            if (window()->isMaximized()) window()->showNormal();
            else window()->showMaximized();
            event->accept();
            return;
        }
        QMenuBar::mouseDoubleClickEvent(event);
    }
};

class TitleBarWidget : public QWidget {
public:
    explicit TitleBarWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName("titleBar");
        setFixedHeight(32);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton &&
            childAt(event->position().toPoint()) == this) {
            if (window() && window()->windowHandle()) {
                window()->windowHandle()->startSystemMove();
            }
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton &&
            childAt(event->position().toPoint()) == this) {
            if (window()->isMaximized()) window()->showNormal();
            else window()->showMaximized();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }
};

class CompactRowDelegate : public QStyledItemDelegate {
public:
    explicit CompactRowDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(22);
        return size;
    }
};

// Ligado pelo run_gui antes de construir a janela. No modo evidencia o
// carregamento automatico do caso mais recente e suprimido: ele e enfileirado no
// construtor e rodava DEPOIS do caso pedido na linha de comando, desfazendo o
// pedido (e disparando duas cargas concorrentes da Timeline).
bool g_gui_evidence_mode = false;

class MainWindow : public QMainWindow {
public:
    // --- modo evidencia (--screenshot) -------------------------------------
    // Carrega um caso e escolhe a aba sem interacao. Existe porque todo recurso
    // desta janela ficou sem prova visual: automacao de janela nesta maquina nao
    // e confiavel, e uma foto tirada pelo proprio app e evidencia melhor que
    // nenhuma.
    void screenshot_setup(const QString& case_dir, const QString& tab_name);
    // A carga da Timeline roda em thread; a foto tem que esperar ela assentar.
    bool timeline_busy() const { return timeline_running_; }

    ~MainWindow() override {
        /* auto_hide_ nao e QObject: sem isso vazava (auditoria, A2) */
        delete auto_hide_;
        // Uma autorizacao ou coleta em andamento terminaria depois do widget
        // destruido. A coleta recebe o pedido de parada e e aguardada.
        if (auth_worker_.joinable()) auth_worker_.join();
        collect_cancel_.store(true);
        if (collect_worker_.joinable()) collect_worker_.join();
        if (social_worker_.joinable()) social_worker_.join();
        // A carga da Timeline escreve em timeline_result_, que morre com a janela.
        if (timeline_worker_.joinable()) timeline_worker_.join();
    }

    MainWindow() {
        cloud_sessions_ = std::make_unique<deep::cloud::SessionManager>(8 * 60 * 60);
        cloud_session_vault_ = std::make_unique<deep::cloud::SessionVault>(
            session_vault_path().toUtf8().constData());
        setWindowTitle("Olho de Deus - Deep Engine");
        setWindowIcon(QIcon(QCoreApplication::applicationDirPath() +
                            "/icons/logo_eye.png"));
        setWindowFlag(Qt::FramelessWindowHint, true);
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
        if (!g_gui_evidence_mode) {
            QTimer::singleShot(0, this, [this]() { load_case(); });
        }
    }

protected:
    bool nativeEvent(const QByteArray& event_type, void* message,
                     qintptr* result) override {
        Q_UNUSED(event_type);
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST && !isMaximized()) {
            const int border = 6;
            const int x = GET_X_LPARAM(msg->lParam);
            const int y = GET_Y_LPARAM(msg->lParam);
            const QRect r = frameGeometry();
            const bool left = x < r.left() + border;
            const bool right = x >= r.right() - border;
            const bool top = y < r.top() + border;
            const bool bottom = y >= r.bottom() - border;
            if (top && left) *result = HTTOPLEFT;
            else if (top && right) *result = HTTOPRIGHT;
            else if (bottom && left) *result = HTBOTTOMLEFT;
            else if (bottom && right) *result = HTBOTTOMRIGHT;
            else if (left) *result = HTLEFT;
            else if (right) *result = HTRIGHT;
            else if (top) *result = HTTOP;
            else if (bottom) *result = HTBOTTOM;
            if (*result) return true;
        }
        return QMainWindow::nativeEvent(event_type, message, result);
    }

private:
    QVector<QWidget*> tool_pages_;
    QVector<ToolTile*> tool_tiles_;
    QMenu* view_menu_ = nullptr;
    QWidget* bars_chrome_ = nullptr;
    QWidget* bars_gap_ = nullptr;
    QWidget* bar_host_ = nullptr;
    QAction* bar_main_action_ = nullptr;
    QAction* bar_secondary_action_ = nullptr;
    ui::CommandBar* command_bar_main_ = nullptr;
    ui::CommandBar* command_bar_secondary_ = nullptr;
    QLabel* mode_label_ = nullptr;
    int active_tool_ = -1;
    QLabel* status_label_ = nullptr;
    QPlainTextEdit* console_ = nullptr;
    QProcess* process_ = nullptr;
    QString log_buffer_;
    bool dump_pending_ = false;   // OLHO_DUMP=1: dump depois do 1o layout
    bool dumped_ = false;
    QTimer* dump_timer_ = nullptr;

    /* painel Detalhes no formato do painel de Source Control (secoes + filtro) */
    struct DetailsRow {
        QWidget* row = nullptr;
        QLabel* key = nullptr;
        QLabel* value = nullptr;
        int section = 0;
    };
    struct DetailsSection {
        QPushButton* header = nullptr;
        QWidget* body = nullptr;
    };
    QHash<QDockWidget*, ui::DockTitleBar*> dock_title_bars_;
    QVector<DetailsRow> details_rows_;
    QVector<DetailsSection> details_sections_;
    QLabel* details_summary_ = nullptr;
    QLineEdit* details_filter_ = nullptr;
    QCheckBox* details_compact_ = nullptr;
    QComboBox* details_scope_ = nullptr;

    /* Contrato explicito do cabecalho de painel: o builder registra aqui as
       acoes que vao para o menu do titulo e o make_dock consome. Antes era
       findChildren por objectName + click() em botao alheio, que quebrava
       calado ao renomear um botao. */
    struct PanelHeaderActions {
        QMenu* menu = nullptr;
        std::vector<std::pair<QString, std::function<void()>>> items;
    } panel_header_;

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
    QFormLayout* form_conexao_ = nullptr;
    QFormLayout* form_coleta_ = nullptr;
    QComboBox* cloud_provider_ = nullptr;
    QLineEdit* cloud_app_key_ = nullptr;
    QLineEdit* cloud_root_path_ = nullptr;
    QCheckBox* cloud_download_files_ = nullptr;
    QPushButton* auth_m365_ = nullptr;
    QPushButton* auth_dropbox_ = nullptr;
    // A autorizacao espera o navegador por ate 300 s. Roda fora da thread da
    // interface e o resultado volta por um QTimer; travar a janela nesse tempo
    // deixaria o aplicativo sem resposta durante todo o consentimento.
    std::thread auth_worker_;
    std::atomic<bool> auth_done_{false};
    std::atomic<int> auth_code_{-1};
    bool auth_running_ = false;
    QString auth_done_text_;
    QString auth_fail_text_;
    QTimer* auth_timer_ = nullptr;
    // Coleta pedida pela propria interface: roda numa thread de trabalho e
    // devolve o resultado por QTimer, para a janela nunca ficar sem resposta.
    std::thread collect_worker_;
    std::atomic<bool> collect_done_{false};
    std::atomic<bool> collect_cancel_{false};
    bool collect_running_ = false;
    deep::cloud::CollectFlowOutcome collect_outcome_;
    QTimer* collect_timer_ = nullptr;
    QPushButton* collect_now_ = nullptr;
    QPushButton* collect_stop_ = nullptr;
    // Progresso da coleta: o coletor escreve na thread de trabalho, a GUI le no
    // timer. Atomics porque sao dois threads e nenhum lock e necessario.
    std::atomic<int> collect_pages_{0};
    std::atomic<int> collect_objects_{0};
    std::atomic<int> collect_files_{0};
    // Importacao da exportacao de rede social (Facebook/Instagram). Mesmo
    // padrao da coleta: thread de trabalho + QTimer devolvendo o resultado.
    QLineEdit* social_export_ = nullptr;
    QComboBox* social_provider_ = nullptr;
    QPushButton* social_import_ = nullptr;
    QLabel* social_status_ = nullptr;
    std::thread social_worker_;
    std::atomic<bool> social_done_{false};
    bool social_running_ = false;
    olho::social::dyi_report social_report_;
    QTimer* social_timer_ = nullptr;
    QStandardItem* tree_social_ = nullptr;
    QComboBox* cloud_jobs_ = nullptr;
    QPushButton* cloud_pause_ = nullptr;
    QPushButton* cloud_cancel_ = nullptr;
    bool cloud_paused_ = false;

    QTreeView* tree_ = nullptr;
    QStandardItemModel* tree_model_ = nullptr;
    QSortFilterProxyModel* tree_proxy_ = nullptr;
    QFileSystemModel* project_model_ = nullptr;
    QSortFilterProxyModel* project_proxy_ = nullptr;
    QLineEdit* tree_search_ = nullptr;
    QTableView* table_ = nullptr;
    QStandardItemModel* table_model_ = nullptr;
    QSortFilterProxyModel* table_proxy_ = nullptr;
    QLineEdit* table_search_ = nullptr;
    QLabel* table_count_ = nullptr;
    HexView* hex_view_ = nullptr;
    TimelineView* timeline_view_ = nullptr;
    // Paginacao da Timeline: o teto sobe por acao explicita do operador, e o
    // limite vale por caso (abrir outro caso volta ao padrao).
    QAction* timeline_more_ = nullptr;
    QAction* timeline_chrono_ = nullptr;
    int timeline_cap_ = 5000;
    bool timeline_chronological_ = false;
    QString timeline_cap_case_;
    // Carregar a Timeline fora do thread da interface: o corte por data custa
    // ~1 s em 200 mil eventos, e um segundo de janela congelada parece travamento.
    // Mesmo padrao da coleta e do import social: thread de trabalho + QTimer.
    std::thread timeline_worker_;
    std::atomic<bool> timeline_done_{false};
    bool timeline_running_ = false;
    bool timeline_pending_ = false;     // pediram outra carga enquanto rodava
    QString timeline_pending_dir_;
    olho::ui::timeline_load timeline_result_;
    QTimer* timeline_timer_ = nullptr;
    // Progresso da carga: o worker escreve, o timer le. Um atomico por bloco de
    // 5.000 linhas, sem lock e sem tocar em widget fora da thread da interface.
    std::atomic<qint64> timeline_lines_{0};
    // Linha selecionada antes da recarga, para devolver o operador ao lugar dele
    // depois de "carregar mais" (a tabela e reconstruida do zero).
    QByteArray timeline_selected_raw_;
    olho::ui::ChatView* chat_view_ = nullptr;
    olho::ui::AcquireView* acquire_view_ = nullptr;
    QWidget* chat_page_ = nullptr;
    olho::ui::GalleryView* gallery_view_ = nullptr;
    QVector<QByteArray> row_raw_;
    QVector<QDateTime> row_time_;
    QStandardItem* tree_case_item_ = nullptr;
    QStandardItem* tree_device_item_ = nullptr;
    QStandardItem* tree_evidence_ = nullptr;
    QStandardItem* tree_cloud_ = nullptr;
    QString case_dir_;
    CenterTabs* center_tabs_ = nullptr;
    QTabWidget* bottom_tabs_ = nullptr;
    QDockWidget* bottom_dock_ = nullptr;
    QDockWidget* tree_dock_ = nullptr;
    QDockWidget* details_dock_ = nullptr;
    ui::AutoHideManager* auto_hide_ = nullptr;

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

    QLabel* title(const QString& text, int size, const QString& color = theme::kTextOnDocument) {
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

    QString cloud_provider() const {
        return cloud_provider_ ? cloud_provider_->currentData().toString() : QString("m365");
    }

    // Esconde a linha inteira do formulario, nao so o campo: um QLineEdit oculto
    // com o rotulo visivel deixa a coluna torta.
    static void set_form_row_visible(QFormLayout* form, QWidget* field, bool visible) {
        if (form == nullptr || field == nullptr) return;
        field->setVisible(visible);
        if (QWidget* label = form->labelForField(field)) label->setVisible(visible);
    }

    void cloud_apply_provider() {
        const bool dropbox = cloud_provider() == "dropbox";
        set_form_row_visible(form_conexao_, cloud_tenant_, !dropbox);
        set_form_row_visible(form_conexao_, cloud_client_, !dropbox);
        set_form_row_visible(form_conexao_, cloud_scope_, !dropbox);
        set_form_row_visible(form_conexao_, cloud_app_key_, dropbox);
        set_form_row_visible(form_conexao_, cloud_root_path_, dropbox);
        set_form_row_visible(form_coleta_, cloud_since_, !dropbox);
        set_form_row_visible(form_coleta_, cloud_until_, !dropbox);
        set_form_row_visible(form_coleta_, cloud_attachments_, !dropbox);
        set_form_row_visible(form_coleta_, cloud_download_files_, dropbox);
        if (auth_m365_ != nullptr) auth_m365_->setVisible(!dropbox);
        if (auth_dropbox_ != nullptr) auth_dropbox_->setVisible(dropbox);
        if (cloud_account_ != nullptr) {
            cloud_account_->setPlaceholderText(dropbox ? "conta Dropbox" : "conta@dominio");
        }
    }

    void start_cloud_auth(std::function<int()> work, const QString& done_text,
                          const QString& fail_text) {
        if (auth_running_) {
            set_cloud_hint("Ja existe uma autorizacao em andamento.", true);
            return;
        }
        if (auth_worker_.joinable()) auth_worker_.join();
        auth_running_ = true;
        auth_done_.store(false);
        auth_code_.store(-1);
        auth_done_text_ = done_text;
        auth_fail_text_ = fail_text;
        if (auth_m365_ != nullptr) auth_m365_->setEnabled(false);
        if (auth_dropbox_ != nullptr) auth_dropbox_->setEnabled(false);
        auth_worker_ = std::thread([this, work]() {
            const int code = work();
            auth_code_.store(code);
            auth_done_.store(true);
        });
    }

    void poll_cloud_auth() {
        if (!auth_running_ || !auth_done_.load()) return;
        if (auth_worker_.joinable()) auth_worker_.join();
        auth_running_ = false;
        const int code = auth_code_.load();
        if (auth_m365_ != nullptr) auth_m365_->setEnabled(true);
        if (auth_dropbox_ != nullptr) auth_dropbox_->setEnabled(true);
        if (code == 0) {
            append("[cloud] " + auth_done_text_ + "\n");
            set_cloud_hint(auth_done_text_);
        } else {
            set_cloud_hint(auth_fail_text_ + QString(" (codigo %1)").arg(code), true);
            append("[cloud] " + auth_fail_text_ + QString(" (codigo %1)").arg(code) + "\n");
        }
    }

    // Monta as opcoes a partir dos campos e dispara a coleta na thread de
    // trabalho. Recusa antes de gastar rede quando falta o essencial.
    void start_cloud_collect() {
        if (collect_running_) {
            set_cloud_hint("Ja existe uma coleta em andamento.", true);
            return;
        }
        if (!ensure_cloud_case("coletar")) return;
        if (!require_permission(deep::cloud::Permission::RunCollection)) return;

        const QString provider = cloud_provider();
        const bool dropbox = provider == "dropbox";
        const QString account = cloud_account_ ? cloud_account_->text().trimmed() : QString();
        const QString tenant = cloud_tenant_ ? cloud_tenant_->text().trimmed() : QString();
        const QString vault = cloud_vault_ ? cloud_vault_->text().trimmed() : QString();
        const QString legal = cloud_legal_ ? cloud_legal_->text().trimmed() : QString();
        if (account.isEmpty() || vault.isEmpty() || legal.isEmpty() ||
            (!dropbox && tenant.isEmpty())) {
            set_cloud_hint(dropbox ? "Conta, vault key e base legal sao obrigatorios."
                                   : "Conta, tenant, vault key e base legal sao obrigatorios.",
                           true);
            return;
        }

        deep::cloud::CollectFlowOptions options;
        options.provider = provider.toStdString();
        options.case_dir = cloud_case_base().toStdString();
        options.account = account.toStdString();
        options.tenant = tenant.toStdString();
        options.vault_key = vault.toStdString();
        options.legal_basis = legal.toStdString();
        options.job = cloud_job_ && !cloud_job_->text().trimmed().isEmpty()
            ? cloud_job_->text().trimmed().toStdString()
            : ("job_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss").toStdString());
        options.since = cloud_since_ ? cloud_since_->text().trimmed().toStdString() : std::string();
        options.until = cloud_until_ ? cloud_until_->text().trimmed().toStdString() : std::string();
        options.max_pages = 50;
        options.download_attachments = !dropbox && cloud_attachments_ &&
                                       cloud_attachments_->isChecked();
        options.download_files = dropbox && cloud_download_files_ &&
                                 cloud_download_files_->isChecked();
        options.root_path = cloud_root_path_ && !cloud_root_path_->text().trimmed().isEmpty()
            ? cloud_root_path_->text().trimmed().toStdString()
            : std::string("/");
        options.app_key = cloud_app_key_ ? cloud_app_key_->text().trimmed().toStdString()
                                         : std::string();
        if (cloud_job_) cloud_job_->setText(QString::fromStdString(options.job));

        collect_running_ = true;
        collect_done_.store(false);
        collect_cancel_.store(false);
        collect_pages_.store(0);
        collect_objects_.store(0);
        collect_files_.store(0);
        collect_outcome_ = deep::cloud::CollectFlowOutcome{};
        // O coletor chama isto a cada pagina; so numeros atravessam a thread.
        options.progress = [this](int pages, int objects, int files) {
            collect_pages_.store(pages);
            collect_objects_.store(objects);
            collect_files_.store(files);
        };
        if (collect_now_ != nullptr) collect_now_->setEnabled(false);
        if (collect_stop_ != nullptr) collect_stop_->setEnabled(true);
        set_cloud_hint(QString("Coletando %1 em segundo plano...")
                           .arg(dropbox ? "Dropbox" : "Microsoft 365"));
        append("[cloud] coleta iniciada: " + provider + " / " +
               QString::fromStdString(options.job) + "\n");
        write_audit(cloud_principal_ ? QString::fromStdString(cloud_principal_->id) : QString("system"),
                    "cloud.collect.start", QString::fromStdString(options.job), "started",
                    provider);

        // A thread usa um ponteiro para o membro do resultado; o QTimer so o le
        // depois do join, entao nao ha leitura concorrente.
        collect_worker_ = std::thread([this, options]() {
            const std::function<bool()> cancelled = [this]() { return collect_cancel_.load(); };
            deep::cloud::run_collect_flow(options, cancelled, &collect_outcome_);
            collect_done_.store(true);
        });
    }

    void poll_cloud_collect() {
        if (!collect_running_) return;
        if (!collect_done_.load()) {
            // Ainda coletando: mostra o placar ao vivo para o operador saber que
            // o trabalho esta andando, e nao uma janela muda.
            set_cloud_hint(QString("Coletando... %1 paginas, %2 objetos, %3 arquivos")
                               .arg(collect_pages_.load())
                               .arg(collect_objects_.load())
                               .arg(collect_files_.load()));
            return;
        }
        if (collect_worker_.joinable()) collect_worker_.join();
        collect_running_ = false;
        if (collect_now_ != nullptr) collect_now_->setEnabled(true);
        if (collect_stop_ != nullptr) collect_stop_->setEnabled(false);

        const auto& outcome = collect_outcome_;
        const QString estado = outcome.cancelled ? "cancelada"
            : (outcome.ok ? "concluida" : (outcome.partial ? "parcial" : "falhou"));
        append(QString("[cloud] coleta %1: %2 objetos, %3 paginas, %4 arquivos, %5 bytes\n")
                   .arg(estado)
                   .arg(outcome.objects)
                   .arg(outcome.pages)
                   .arg(outcome.files_downloaded)
                   .arg(static_cast<qulonglong>(outcome.bytes_downloaded)));
        if (!outcome.manifest_path.empty()) {
            append("[cloud] manifesto: " + QString::fromStdString(outcome.manifest_path) + "\n");
        }
        if (!outcome.error.empty()) {
            append("[cloud] erro: " + QString::fromStdString(outcome.error) + "\n");
        }
        set_cloud_hint(outcome.error.empty()
                           ? QString("Coleta %1.").arg(estado)
                           : QString("Coleta %1: %2").arg(estado,
                                    QString::fromStdString(outcome.error)),
                       !outcome.ok);
        write_audit(cloud_principal_ ? QString::fromStdString(cloud_principal_->id) : QString("system"),
                    "cloud.collect.finish",
                    cloud_job_ ? cloud_job_->text() : QString(), estado,
                    QString::fromStdString(outcome.error));
        refresh_cloud_jobs();
        refresh_cloud_status();

        // A coleta acabou de escrever cloud/<provedor>/<conta>/ dentro do caso. O
        // selo entra agora, sem depender de alguem lembrar do menu: prova nao
        // selada no minuto seguinte e prova que ninguem pode provar depois.
        if (outcome.ok && !outcome.cancelled && !cloud_case_base().isEmpty()) {
            // Se outro comando estiver rodando, nao abre caixa de dialogo por
            // causa de um selo: avisa no console e deixa o operador decidir.
            if (process_->state() == QProcess::NotRunning) {
                append("[cloud] selando o caso apos a coleta\n");
                run({"--case-seal", "--case", cloud_case_base()}, false);
            } else {
                append("[cloud] selo automatico adiado: outro comando em execucao. "
                       "Rode Projeto > Selar caso quando terminar.\n");
            }
        }
    }

    // --- exportacao de rede social (DYI) -----------------------------------
    void social_pick_zip() {
        if (!social_export_) return;
        const QString path = QFileDialog::getOpenFileName(
            this, "Exportacao (zip)", social_export_->text(),
            "Exportacao (*.zip);;Todos os arquivos (*.*)");
        if (!path.isEmpty()) social_export_->setText(QDir::toNativeSeparators(path));
    }

    void social_pick_folder() {
        if (!social_export_) return;
        const QString path = QFileDialog::getExistingDirectory(
            this, "Exportacao (pasta extraida)", social_export_->text());
        if (!path.isEmpty()) social_export_->setText(QDir::toNativeSeparators(path));
    }

    void start_social_import() {
        if (social_running_) {
            set_cloud_hint("Ja existe uma importacao de rede social em andamento.", true);
            return;
        }
        if (!ensure_cloud_case("importar a exportacao")) return;
        if (!require_permission(deep::cloud::Permission::RunCollection)) return;

        const QString source = social_export_ ? social_export_->text().trimmed() : QString();
        const QString account = cloud_account_ ? cloud_account_->text().trimmed() : QString();
        const QString legal = cloud_legal_ ? cloud_legal_->text().trimmed() : QString();
        if (source.isEmpty() || account.isEmpty() || legal.isEmpty()) {
            set_cloud_hint("Informe o arquivo/pasta da exportacao, a conta e a base legal.", true);
            return;
        }
        QFileInfo info(source);
        if (!info.exists()) {
            set_cloud_hint("A exportacao informada nao existe: " + source, true);
            return;
        }

        olho::social::dyi_config config;
        config.source = source.toStdWString();
        config.case_dir = cloud_case_base().toStdString();
        config.account = account.toStdString();
        config.legal_basis = legal.toStdString();
        config.job_id = cloud_job_ && !cloud_job_->text().trimmed().isEmpty()
            ? cloud_job_->text().trimmed().toStdString()
            : ("social_" + QDateTime::currentDateTimeUtc()
                   .toString("yyyyMMdd_HHmmss").toStdString());
        config.provider = social_provider_ && social_provider_->currentData().isValid()
            ? social_provider_->currentData().toString().toStdString()
            : std::string();
        if (cloud_job_) cloud_job_->setText(QString::fromStdString(config.job_id));

        social_running_ = true;
        social_done_.store(false);
        social_report_ = olho::social::dyi_report{};
        if (social_import_ != nullptr) social_import_->setEnabled(false);
        if (social_status_ != nullptr) social_status_->setText("Importando...");
        set_cloud_hint("Lendo a exportacao em segundo plano...");
        append("[social] importacao iniciada: " + info.fileName() + "\n");
        write_audit(cloud_principal_ ? QString::fromStdString(cloud_principal_->id) : QString("system"),
                    "social.import.start", QString::fromStdString(config.job_id), "started",
                    config.account.c_str());

        // O objeto e membro; o QTimer so le depois do join, sem corrida.
        social_worker_ = std::thread([this, config]() {
            social_report_ = olho::social::import_export(config);
            social_done_.store(true);
        });
    }

    void poll_social_import() {
        if (!social_running_ || !social_done_.load()) return;
        if (social_worker_.joinable()) social_worker_.join();
        social_running_ = false;
        if (social_import_ != nullptr) social_import_->setEnabled(true);

        const auto& report = social_report_;
        const QString estado = report.ok ? "concluida"
            : (report.partial ? "parcial" : "falhou");
        append(QString("[social] %1: %2 conversas, %3 mensagens, %4 com midia, %5 apagadas, "
                       "%6 bytes\n")
                   .arg(estado)
                   .arg(report.threads)
                   .arg(report.messages)
                   .arg(report.media)
                   .arg(report.unsent)
                   .arg(static_cast<qulonglong>(report.bytes)));
        append(QString("[social] midias copiadas: %1 (ausentes no pacote: %2)\n")
                   .arg(report.media_files)
                   .arg(report.media_missing));
        if (!report.source_sha256.empty()) {
            append("[social] sha256 da origem: " + QString::fromStdString(report.source_sha256) +
                   "\n");
        }
        if (!report.manifest_path.empty()) {
            append("[social] manifesto: " + QString::fromStdString(report.manifest_path) + "\n");
        }
        if (!report.error.empty()) {
            append("[social] erro: " + QString::fromStdString(report.error) + "\n");
        }
        const QString resumo = report.ok
            ? QString("%1: %2 conversas, %3 mensagens, %4 midias")
                  .arg(QString::fromStdString(report.provider))
                  .arg(report.threads).arg(report.messages).arg(report.media_files)
            : QString("Importacao %1: %2").arg(estado).arg(QString::fromStdString(report.error));
        if (social_status_ != nullptr) social_status_->setText(resumo);
        set_cloud_hint(resumo, !report.ok);
        write_audit(cloud_principal_ ? QString::fromStdString(cloud_principal_->id) : QString("system"),
                    "social.import.finish",
                    cloud_job_ ? cloud_job_->text() : QString(), estado,
                    QString::fromStdString(report.error));
        // Recarrega o caso para a arvore mostrar a evidencia recem-importada.
        if (report.ok && !case_dir_.isEmpty()) load_case(case_dir_);
        // O export social acabou de escrever social/<provedor>/<conta>/ no caso:
        // sela agora, pelo mesmo motivo da coleta de nuvem.
        if (report.ok && !cloud_case_base().isEmpty()) {
            if (process_->state() == QProcess::NotRunning) {
                append("[social] selando o caso apos a importacao\n");
                run({"--case-seal", "--case", cloud_case_base()}, false);
            } else {
                append("[social] selo automatico adiado: outro comando em execucao. "
                       "Rode Projeto > Selar caso quando terminar.\n");
            }
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
        chain->setStyleSheet(report.ok ? "color:#4CAF50;font-weight:600;"
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
        const QString job_provider = job.value("provider").toString();
        if (cloud_provider_ && !job_provider.isEmpty()) {
            const int index = cloud_provider_->findData(job_provider);
            if (index >= 0 && index != cloud_provider_->currentIndex())
                cloud_provider_->setCurrentIndex(index);
        }
        if (cloud_root_path_) {
            const QString root = job.value("root_path").toString();
            cloud_root_path_->setText(root.isEmpty() ? QString("/") : root);
        }
        if (cloud_download_files_)
            cloud_download_files_->setChecked(job.value("download_files").toBool());
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
            cloud_service_status_->setStyleSheet("color: #4CAF50;");
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
        const QString provider = cloud_provider();
        const bool dropbox = provider == "dropbox";
        if (dropbox) {
            // Um tenant aqui viraria um identificador falso no banco do caso.
            if (account.isEmpty() || vault.isEmpty() || legal.isEmpty()) {
                QMessageBox::warning(this, "Cloud",
                    "Conta, vault key e base legal sao obrigatorios.");
                return;
            }
        } else if (account.isEmpty() || tenant.isEmpty() || vault.isEmpty() || legal.isEmpty()) {
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
        job.provider = provider.toStdString();
        job.account = account.toStdString();
        job.tenant_id = tenant.toStdString();
        job.root_path = cloud_root_path_ && !cloud_root_path_->text().trimmed().isEmpty()
            ? cloud_root_path_->text().trimmed().toStdString()
            : std::string("/");
        job.download_files = dropbox && cloud_download_files_ && cloud_download_files_->isChecked();
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
            body.insert("provider", provider);
            body.insert("account", account);
            body.insert("tenant_id", tenant);
            body.insert("root_path", QString::fromStdString(job.root_path));
            body.insert("download_files", job.download_files);
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
        root->addWidget(muted("Nuvem read-only: Microsoft 365 e Dropbox - consentimento, coleta "
                             "com checkpoint e evidencia com hash."));

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
        form_conexao_ = form;
        cloud_provider_ = new QComboBox;
        cloud_provider_->addItem("Microsoft 365", "m365");
        cloud_provider_->addItem("Dropbox", "dropbox");
        cloud_provider_->setToolTip("O provedor decide quais campos valem e qual token e usado.");
        connect(cloud_provider_, &QComboBox::currentIndexChanged, this, [this](int) {
            cloud_apply_provider();
            // Guarda a escolha: reabrir o app no mesmo provedor evita erro de
            // operacao (autorizar num provedor e coletar em outro).
            QSettings settings(layout_settings_path(), QSettings::IniFormat);
            settings.setValue("ui/provider", cloud_provider());
        });
        {
            QSettings settings(layout_settings_path(), QSettings::IniFormat);
            const QString saved = settings.value("ui/provider", "m365").toString();
            const int found = cloud_provider_->findData(saved);
            if (found >= 0) cloud_provider_->setCurrentIndex(found);
        }
        form->addRow("Provedor", cloud_provider_);
        cloud_tenant_ = plain_field(form, "Tenant ID");
        cloud_client_ = plain_field(form, "Client ID (app publico)");
        cloud_account_ = plain_field(form, "Conta (UPN)");
        cloud_vault_ = plain_field(form, "Vault key", "tenant/conta");
        cloud_scope_ = plain_field(form, "Escopos", "User.Read Mail.Read Files.Read.All");
        cloud_scope_->setText("User.Read Mail.Read Files.Read.All");
        cloud_app_key_ = plain_field(form, "App key (Dropbox)");
        cloud_root_path_ = plain_field(form, "Pasta raiz (Dropbox)", "/");
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
        auth_m365_ = action("Autenticar M365", [this]() {
            if (!require_permission(deep::cloud::Permission::RunCollection)) return;
            const QString tenant = cloud_tenant_ ? cloud_tenant_->text().trimmed() : QString();
            const QString client = cloud_client_ ? cloud_client_->text().trimmed() : QString();
            if (tenant.isEmpty() || client.isEmpty()) {
                set_cloud_hint("Informe Tenant ID e Client ID para autenticar.", true);
                return;
            }
            deep::cloud::AuthFlowOptions options;
            options.tenant = tenant.toStdString();
            options.client_id = client.toStdString();
            options.scope = cloud_scope_ ? cloud_scope_->text().trimmed().toStdString()
                                         : std::string();
            options.vault_key = cloud_vault_ ? cloud_vault_->text().trimmed().toStdString()
                                             : std::string();
            set_cloud_hint("Aguardando a autenticacao no navegador...");
            start_cloud_auth([options]() { return deep::cloud::run_auth_flow(options); },
                             "Autenticacao M365 concluida; token salvo no DPAPI.",
                             "Autenticacao M365 falhou. Verifique tenant/client.");
        }, false, "cloud.png");
        auth_dropbox_ = action("Autenticar Dropbox", [this]() {
            if (!require_permission(deep::cloud::Permission::RunCollection)) return;
            const QString app_key = cloud_app_key_ ? cloud_app_key_->text().trimmed() : QString();
            if (app_key.isEmpty()) {
                set_cloud_hint("Informe a app key do aplicativo Dropbox para autenticar.", true);
                return;
            }
            deep::cloud::DropboxAuthOptions options;
            options.app_key = app_key.toStdString();
            options.vault_key = cloud_vault_ ? cloud_vault_->text().trimmed().toStdString()
                                             : std::string();
            if (options.vault_key.empty()) options.vault_key = "dropbox/default";
            set_cloud_hint("Abrindo o navegador para o consentimento do Dropbox...");
            start_cloud_auth([options]() { return deep::cloud::run_dropbox_auth(options); },
                             "Autenticacao Dropbox concluida; token salvo no DPAPI.",
                             "Autenticacao Dropbox falhou. Veja o registro.");
        }, false, "cloud.png");
        auth_dropbox_->setVisible(false);
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
        // Coleta na propria interface: o mesmo fluxo que o CLI e o worker usam,
        // rodando na thread de trabalho e com pedido de parada.
        collect_now_ = action("Coletar agora", [this]() { start_cloud_collect(); },
                              false, "cloud.png");
        collect_stop_ = action("Cancelar coleta", [this]() {
            if (!collect_running_) {
                set_cloud_hint("Nenhuma coleta em andamento.", true);
                return;
            }
            collect_cancel_.store(true);
            set_cloud_hint("Parada pedida; a coleta para no proximo ponto seguro.");
            append("[cloud] parada pedida pelo operador\n");
        }, false, "tab_table.png");
        collect_stop_->setEnabled(false);
        action_row1->addWidget(auth_m365_);
        action_row1->addWidget(auth_dropbox_);
        action_row1->addWidget(collect_now_);
        action_row1->addWidget(collect_stop_);
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
        form_coleta_ = form2;
        cloud_since_ = plain_field(form2, "Desde");
        cloud_since_->setPlaceholderText("AAAA-MM-DD");
        cloud_until_ = plain_field(form2, "Ate");
        cloud_until_->setPlaceholderText("AAAA-MM-DD");
        cloud_legal_ = plain_field(form2, "Base legal");
        cloud_job_ = plain_field(form2, "Job ID");
        cloud_attachments_ = new QCheckBox("Anexos inline (<= 50 MB)");
        form2->addRow("Anexos", cloud_attachments_);
        cloud_download_files_ = new QCheckBox("Baixar conteudo dos arquivos (Dropbox)");
        cloud_download_files_->setToolTip(
            "Sem isto a coleta guarda nome, tamanho e hash do servidor, mas nao o conteudo.");
        form2->addRow("Conteudo", cloud_download_files_);
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
        auto* social_card = card("REDES SOCIAIS (EXPORTACAO)", right_layout);
        auto* social_layout = new QVBoxLayout(social_card);
        social_layout->setSpacing(8);
        auto* social_form = new QFormLayout;
        social_form->setHorizontalSpacing(10);
        social_form->setVerticalSpacing(6);
        social_provider_ = new QComboBox;
        social_provider_->addItem("Detectar", QString());
        social_provider_->addItem("Facebook", "facebook");
        social_provider_->addItem("Instagram", "instagram");
        social_provider_->addItem("Google (Takeout)", "google");
        social_provider_->addItem("Telegram (export)", "telegram");
        social_provider_->setToolTip(
            "A exportacao oficial (Baixar suas informacoes) diz de qual rede ela e; "
            "Detectar le isso do proprio arquivo.");
        social_form->addRow("Provedor", social_provider_);
        connect(social_provider_, &QComboBox::currentIndexChanged, this, [this](int) {
            QSettings settings(layout_settings_path(), QSettings::IniFormat);
            settings.setValue("social/provider", social_provider_->currentData().toString());
        });
        {
            QSettings settings(layout_settings_path(), QSettings::IniFormat);
            const QString saved = settings.value("social/provider", "").toString();
            const int found = social_provider_->findData(saved);
            if (found >= 0) social_provider_->setCurrentIndex(found);
        }

        auto* export_row = new QWidget;
        auto* export_layout = new QHBoxLayout(export_row);
        export_layout->setContentsMargins(0, 0, 0, 0);
        export_layout->setSpacing(6);
        social_export_ = new QLineEdit;
        social_export_->setPlaceholderText("zip ou pasta da exportacao");
        auto* pick_zip = new QPushButton("Zip...");
        auto* pick_folder = new QPushButton("Pasta...");
        pick_zip->setCursor(Qt::PointingHandCursor);
        pick_folder->setCursor(Qt::PointingHandCursor);
        export_layout->addWidget(social_export_, 1);
        export_layout->addWidget(pick_zip);
        export_layout->addWidget(pick_folder);
        social_form->addRow("Export", export_row);
        social_layout->addLayout(social_form);
        connect(pick_zip, &QPushButton::clicked, this, [this]() { social_pick_zip(); });
        connect(pick_folder, &QPushButton::clicked, this, [this]() { social_pick_folder(); });

        social_import_ = action("Importar export", [this]() { start_social_import(); },
                                false, "cloud.png");
        social_layout->addWidget(social_import_);
        social_status_ = new QLabel("Nenhuma exportacao importada.");
        social_status_->setWordWrap(true);
        social_status_->setObjectName("muted");
        social_layout->addWidget(social_status_);

        right_layout->addStretch(1);

        columns->addWidget(left, 1);
        columns->addWidget(right, 0);
        root->addLayout(columns, 1);
        scroll->setWidget(body);
        update_session_label();
        update_cloud_process_buttons();
        // O combo precisa existir antes do primeiro ajuste de visibilidade.
        cloud_apply_provider();
        auth_timer_ = new QTimer(this);
        auth_timer_->setInterval(250);
        connect(auth_timer_, &QTimer::timeout, this, [this]() { poll_cloud_auth(); });
        auth_timer_->start();
        collect_timer_ = new QTimer(this);
        collect_timer_->setInterval(250);
        connect(collect_timer_, &QTimer::timeout, this, [this]() { poll_cloud_collect(); });
        collect_timer_->start();
        social_timer_ = new QTimer(this);
        social_timer_->setInterval(250);
        connect(social_timer_, &QTimer::timeout, this, [this]() { poll_social_import(); });
        social_timer_->start();

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
    QWidget* page_hardware() {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto* body = new QWidget;
        auto* root = new QVBoxLayout(body);
        root->setContentsMargins(18, 16, 18, 18);
        root->setSpacing(12);
        root->addWidget(title("Hardware", 18));
        root->addWidget(muted(
            "Inventario read-only do host: sistema, firmware, CPU, memoria, discos, "
            "volumes, Secure Boot e TPM. Nada e alterado."));

        const deep::forensic::HardwareInventory inventory =
            deep::forensic::collect_hardware_inventory();

        auto* system = card("SISTEMA", root);
        auto* form = new QFormLayout(system);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(8);
        const auto add = [&](const QString& label, const QString& value) {
            auto* text = new QLabel(value.isEmpty() ? "nao disponivel" : value);
            text->setObjectName(value.isEmpty() ? "muted" : "monoValue");
            text->setTextInteractionFlags(Qt::TextSelectableByMouse);
            text->setWordWrap(true);
            form->addRow(label, text);
        };
        add("Host", QString::fromStdString(inventory.hostname));
        add("Sistema", QString::fromStdString(
            inventory.system_manufacturer + " " + inventory.system_model).trimmed());
        add("Serial", QString::fromStdString(inventory.system_serial));
        add("BIOS", QString::fromStdString(
            inventory.bios_vendor + " " + inventory.bios_version).trimmed());
        add("Placa-mae", QString::fromStdString(
            inventory.board_manufacturer + " " + inventory.board_product).trimmed());
        add("CPU", QString::fromStdString(inventory.cpu));
        add("RAM", format_bytes(static_cast<qint64>(inventory.ram_bytes)));
        add("OS", QString("%1 %2 (%3)").arg(
            QString::fromStdString(inventory.os_name),
            QString::fromStdString(inventory.os_version),
            QString::fromStdString(inventory.os_build)).trimmed());
        add("Secure Boot", inventory.secure_boot
            ? (*inventory.secure_boot ? "habilitado" : "desabilitado") : QString());
        add("TPM", QString::fromStdString(inventory.tpm.value_or("")));

        auto* disks_card = card("DISCOS FISICOS", root);
        auto* disks = new QTableWidget(0, 7);
        disks->setObjectName("dataGrid");
        disks->setHorizontalHeaderLabels(
            {"Disco", "Modelo", "Serial", "Barramento", "Tipo", "Tamanho", "SMART"});
        disks->horizontalHeader()->setStretchLastSection(false);
        disks->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        disks->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        disks->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        disks->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        disks->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        disks->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        disks->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
        disks->verticalHeader()->setVisible(false);
        disks->setEditTriggers(QAbstractItemView::NoEditTriggers);
        disks->setSelectionBehavior(QAbstractItemView::SelectRows);
        disks->setAlternatingRowColors(true);
        disks->setMinimumHeight(150);
        for (const auto& disk : inventory.disks) {
            const int row = disks->rowCount();
            disks->insertRow(row);
            auto set = [&](int column, const QString& value) {
                auto* item = new QTableWidgetItem(value);
                disks->setItem(row, column, item);
            };
            set(0, QString::fromStdString(disk.device));
            set(1, QString::fromStdString(disk.model));
            set(2, QString::fromStdString(disk.serial));
            set(3, QString::fromStdString(disk.bus));
            set(4, disk.ssd ? "SSD" : "HDD");
            set(5, format_bytes(static_cast<qint64>(disk.bytes)));
            set(6, disk.smart ? QString::fromStdString(*disk.smart) : "nao consultado");
        }
        auto* disks_layout = new QVBoxLayout(disks_card);
        disks_layout->addWidget(disks);
        auto* volumes_card = card("VOLUMES", root);
        auto* volumes = new QTableWidget(0, 5);
        volumes->setObjectName("dataGrid");
        volumes->setHorizontalHeaderLabels({"Letra", "Label", "Filesystem", "Total", "Livre"});
        volumes->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        volumes->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        volumes->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        volumes->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        volumes->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        volumes->verticalHeader()->setVisible(false);
        volumes->setEditTriggers(QAbstractItemView::NoEditTriggers);
        volumes->setSelectionBehavior(QAbstractItemView::SelectRows);
        volumes->setAlternatingRowColors(true);
        volumes->setMinimumHeight(130);
        for (const auto& volume : inventory.volumes) {
            const int row = volumes->rowCount();
            volumes->insertRow(row);
            auto set = [&](int column, const QString& value) {
                volumes->setItem(row, column, new QTableWidgetItem(value));
            };
            set(0, QString::fromStdString(volume.root));
            set(1, QString::fromStdString(volume.label));
            set(2, QString::fromStdString(volume.filesystem));
            set(3, format_bytes(static_cast<qint64>(volume.total_bytes)));
            set(4, format_bytes(static_cast<qint64>(volume.free_bytes)));
        }
        auto* volumes_layout = new QVBoxLayout(volumes_card);
        volumes_layout->addWidget(volumes);

        auto* footer = new QHBoxLayout;
        footer->addStretch(1);
        footer->addWidget(action("Exportar JSON", [this, inventory]() {
            const QString path = QFileDialog::getSaveFileName(
                this, "Exportar inventario de hardware",
                "hardware_inventory.json", "JSON (*.json)");
            if (path.isEmpty()) return;
            std::string error;
            if (!deep::forensic::write_hardware_inventory_json(
                    inventory, path.toStdString(), error)) {
                QMessageBox::warning(this, "Hardware",
                    QString::fromStdString(error));
                return;
            }
            append("[hardware] inventario exportado: " + path + "\n");
            status_label_->setText("Inventario de hardware exportado.");
        }, false, "hardware.png"));
        root->addLayout(footer);
        root->addStretch(1);
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

    QWidget* document_bar(const QString& title) {
        auto* bar = new QWidget;
        bar->setObjectName("docBar");
        bar->setFixedHeight(32);
        auto* layout = new QHBoxLayout(bar);
        layout->setContentsMargins(12, 0, 12, 0);
        layout->setSpacing(8);
        auto* breadcrumb = new QLabel("DEEP ENGINE   /   WORKSPACE");
        breadcrumb->setObjectName("docBreadcrumb");
        auto* scope = new QLabel("ESCOPO GLOBAL");
        scope->setObjectName("docScope");
        layout->addWidget(breadcrumb);
        layout->addStretch(1);
        layout->addWidget(scope);
        return bar;
    }

    QWidget* wrap_document(QWidget* page, const QString& title) {
        auto* wrapper = new DocumentFrame(center_tabs_ ? center_tabs_->tabBar() : nullptr);
        wrapper->setObjectName("documentFrame");
        auto* layout = new QVBoxLayout(wrapper);
        layout->setContentsMargins(1, 1, 1, 13);
        layout->setSpacing(0);
        layout->addWidget(document_bar(title));
        auto* body = new QFrame;
        body->setObjectName("documentBody");
        body->setAttribute(Qt::WA_StyledBackground, true);
        auto* body_layout = new QVBoxLayout(body);
        body_layout->setContentsMargins(0, 0, 0, 0);
        body_layout->setSpacing(0);
        body_layout->addWidget(page);
        layout->addWidget(body, 1);
        return wrapper;
    }

    void attach_tab_close(int tab, QWidget* page) {
        auto* close = new QToolButton(center_tabs_);
        close->setObjectName("tabClose");
        close->setText(QString(QChar(0x00D7)));
        close->setFixedSize(16, 16);
        close->setCursor(Qt::PointingHandCursor);
        close->setToolTip("Fechar aba");
        connect(close, &QToolButton::clicked, this, [this, page]() {
            const int index = center_tabs_->indexOf(page);
            if (index >= 0) close_tool_tab(index);
        });
        center_tabs_->tabBar()->setTabButton(tab, QTabBar::RightSide, close);
    }

    QWidget* add_fixed_document(QWidget* page, const QString& icon_name,
                                const QString& title) {
        QWidget* wrapper = wrap_document(page, title);
        const int tab = center_tabs_->addTab(wrapper, icon(icon_name), title);
        attach_tab_close(tab, wrapper);
        return wrapper;
    }

    void open_tool(int index) {
        if (index < 0 || index >= 8) return;
        if (tool_pages_.size() < 8) tool_pages_.resize(8);
        if (tool_pages_[index] == nullptr) {
            QWidget* page = nullptr;
            switch (index) {
            case 0: page = page_pericia(); break;
            case 1: page = page_advanced(); break;
            case 2: page = page_sanitize(); break;
            case 3: page = page_audit(); break;
            case 4: page = page_wipe(); break;
            case 5: page = page_kernel(); break;
            case 6: page = page_cloud(); break;
            case 7: page = page_hardware(); break;
            default: break;
            }
            const QStringList titles = {"Pericia", "Avancado", "Sanitizacao",
                                        "Auditoria", "Wipe", "Kernel", "Cloud", "Hardware"};
            const QStringList files = {"pericia.png", "avancado.png", "sanitizacao.png",
                                       "auditoria.png", "wipe.png", "kernel.png", "cloud.png",
                                       "hardware.png"};
            QWidget* frame = wrap_document(page, titles[index]);
            tool_pages_[index] = frame;
            const int tab = center_tabs_->addTab(frame, QIcon(QCoreApplication::applicationDirPath() +
                "/icons/" + files[index]), titles[index]);
            center_tabs_->setTabToolTip(tab, "Ferramenta " + titles[index]);
            attach_tab_close(tab, frame);
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
        if (tab_index < 0) return;
        if (tab_index < 3) {
            center_tabs_->setTabVisible(tab_index, false);
            status_label_->setText("Aba fechada. Use Views para reabrir.");
            return;
        }
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
        auto* outer = new QWidget;
        outer->setObjectName("treePanelOuter");
        auto* outer_layout = new QVBoxLayout(outer);
        outer_layout->setContentsMargins(6, 0, 2, 0);
        outer_layout->setSpacing(0);
        auto* panel = new QFrame;
        panel->setObjectName("treePanel");
        panel->setAttribute(Qt::WA_StyledBackground, true);
        outer_layout->addWidget(panel);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(8, 8, 8, 6);
        layout->setSpacing(4);

        auto* title_row = new QHBoxLayout;
        auto* heading = new QLabel("Gerenciador do Caso");
        heading->setObjectName("explorerTitle");
        heading->setFixedHeight(24);
        title_row->addWidget(heading);
        title_row->addStretch(1);
        auto* more_menu = new QMenu(this);
        more_menu->addAction("Recarregar caso", this, [this]() {
            if (!case_dir_.isEmpty()) load_case(case_dir_);
        });
        more_menu->addAction("Expandir tudo", this, [this]() { tree_->expandAll(); });
        more_menu->addAction("Recolher tudo", this, [this]() { tree_->collapseAll(); });
        panel_header_.menu = more_menu;
        layout->addLayout(title_row);

        auto add_explorer_action = [this](const QString& /*icon_name*/,
                                          const QString& tip,
                                          std::function<void()> fn) {
            panel_header_.items.emplace_back(tip, std::move(fn));
        };
        add_explorer_action("explorer_back.png", "Item anterior", [this]() {
            const QModelIndex current = tree_->currentIndex();
            const QModelIndex previous = tree_->indexAbove(current);
            if (previous.isValid()) tree_->setCurrentIndex(previous);
        });
        add_explorer_action("explorer_forward.png", QString::fromUtf8("Pr\xC3\xB3ximo item"), [this]() {
            const QModelIndex current = tree_->currentIndex();
            const QModelIndex next = tree_->indexBelow(current);
            if (next.isValid()) tree_->setCurrentIndex(next);
        });
        add_explorer_action("explorer_home.png", QString::fromUtf8("In\xC3\xAD" "cio"), [this]() {
            if (tree_->model()->rowCount() > 0) {
                tree_->setCurrentIndex(tree_->model()->index(0, 0));
                tree_->scrollToTop();
            }
        });
        /* "Recarregar caso" ja vem do menu do painel; nao duplicar */
        add_explorer_action("search.png", "Pesquisar no caso",
                            [this]() { tree_search_->setFocus(); });

        tree_search_ = new QLineEdit;
        tree_search_->setObjectName("treeSearch");
        tree_search_->setPlaceholderText("Pesquisar no caso...");
        tree_search_->setClearButtonEnabled(true);
        tree_search_->setFixedHeight(24);
        layout->addWidget(tree_search_);

        tree_model_ = new QStandardItemModel(this);
        auto* root = tree_model_->invisibleRootItem();
        tree_case_item_ = tree_node(root, "CASO OLHO-2026-001", "folder.png");
        tree_device_item_ = tree_node(tree_case_item_, "DISCO-0  /  Windows 11", "folder.png");
        auto* device = tree_device_item_;
        auto* ntfs = tree_node(device, "NTFS  /  volume C:", "folder.png");
        tree_node(ntfs, "$MFT", "file.png");
        tree_node(ntfs, "$UsnJrnl", "file.png");
        auto* users = tree_node(ntfs, "Users", "folder.png");
        tree_node(users, "pedri", "folder.png");
        auto* windows = tree_node(ntfs, "Windows", "folder.png");
        tree_node(windows, "System32", "folder.png");
        tree_evidence_ = tree_node(root, "EVIDENCIAS", "folder.png");
        auto* evidence = tree_evidence_;
        tree_cloud_ = tree_node(root, "CLOUD", "folder.png");
        tree_social_ = tree_node(root, "REDES SOCIAIS", "folder.png");
        tree_node(evidence, "imagem.raw", "file.png");
        tree_node(evidence, "case.db", "file.png");
        tree_node(evidence, "exposure_report.md", "file.png");

        tree_proxy_ = new QSortFilterProxyModel(this);
        tree_proxy_->setSourceModel(tree_model_);
        tree_proxy_->setRecursiveFilteringEnabled(true);
        tree_proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);

        tree_ = new QTreeView;
        tree_->setObjectName("workTree");
        tree_->setModel(tree_proxy_);
        tree_->setHeaderHidden(true);
        tree_->setUniformRowHeights(true);
        tree_->setItemDelegate(new CompactRowDelegate(tree_));
        tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
        tree_->setIconSize(QSize(16, 16));
        tree_->setAnimated(true);
        tree_->setIndentation(14);
        tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tree_->expandAll();
        layout->addWidget(tree_, 1);
        connect(tree_search_, &QLineEdit::textChanged, this, [this](const QString& text) {
            tree_proxy_->setFilterFixedString(text);
            tree_->expandAll();
        });
        return outer;
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
        table_->horizontalHeader()->resizeSection(2, 92);
        table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
        table_->horizontalHeader()->resizeSection(3, 145);
        table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
        table_->horizontalHeader()->resizeSection(4, 92);
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
        refresh_details_summary();
        apply_details_filter();
    }
    QWidget* build_details_panel() {
        auto* shell = new QWidget;
        shell->setObjectName("detailsOuter");
        auto* shell_layout = new QVBoxLayout(shell);
        /* respiro de 4px: e o que deixa o canto arredondado do card visivel */
        shell_layout->setContentsMargins(4, 4, 4, 4);
        shell_layout->setSpacing(0);
        auto* panel = new QFrame;
        panel->setObjectName("detailsPanel");
        panel->setAttribute(Qt::WA_StyledBackground, true);
        panel->setAttribute(Qt::WA_StyledBackground, true);
        panel->setMinimumWidth(330);
        panel->setMaximumWidth(420);
        shell_layout->addWidget(panel);
        auto* outer = new QVBoxLayout(panel);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        QMenu* more_menu = new QMenu(this);
        panel_header_.menu = more_menu;
        more_menu->addAction("Copiar detalhes", this, [this]() { copy_details(); });
        more_menu->addSeparator();
        more_menu->addAction("Expandir tudo", this, [this]() { set_details_sections_expanded(true); });
        more_menu->addAction("Recolher tudo", this, [this]() { set_details_sections_expanded(false); });

        /* Anatomia copiada do painel de Source Control do editor de referencia
           (medido no print): barra de ferramentas #353535 em 26px, linha de
           resumo, campo de filtro, linha de acao com split button + check, e o
           corpo em secoes recolhiveis com contagem. */
        auto* toolbar = new QWidget;
        toolbar->setObjectName("detailsToolbar");
        toolbar->setFixedHeight(26);
        auto* tb = new QHBoxLayout(toolbar);
        tb->setContentsMargins(8, 0, 6, 0);
        tb->setSpacing(2);
        details_scope_ = new QComboBox(toolbar);
        details_scope_->setObjectName("detailsScope");
        details_scope_->addItems({"Todos os campos", "Somente preenchidos", "Somente vazios"});
        details_scope_->setFixedHeight(20);
        tb->addWidget(details_scope_);
        tb->addStretch(1);
        auto glyph = [this, toolbar](const QString& text, const QString& tip, std::function<void()> fn) {
            auto* b = new QToolButton(toolbar);
            b->setObjectName("detailsGlyph");
            b->setText(text);
            b->setToolTip(tip);
            b->setFixedSize(20, 20);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, [fn]() { fn(); });
            return b;
        };
        tb->addWidget(glyph(QString::fromUtf8("\u21D5"), "Expandir tudo",
                            [this]() { set_details_sections_expanded(true); }));
        tb->addWidget(glyph(QString::fromUtf8("\u21D1"), "Recolher tudo",
                            [this]() { set_details_sections_expanded(false); }));
        tb->addWidget(glyph(QString::fromUtf8("\u2398"), "Copiar detalhes",
                            [this]() { copy_details(); }));
        outer->addWidget(toolbar);

        details_summary_ = new QLabel("Nenhum item selecionado");
        details_summary_->setObjectName("detailsSummary");
        outer->addWidget(details_summary_);

        details_filter_ = new QLineEdit;
        details_filter_->setObjectName("detailsFilter");
        details_filter_->setPlaceholderText("Filtrar propriedades...");
        details_filter_->setClearButtonEnabled(true);
        details_filter_->setFixedHeight(24);
        auto* filter_row = new QHBoxLayout;
        filter_row->setContentsMargins(8, 4, 8, 6);
        filter_row->addWidget(details_filter_);
        outer->addLayout(filter_row);

        auto* action_row = new QHBoxLayout;
        action_row->setContentsMargins(8, 0, 8, 8);
        action_row->setSpacing(8);
        auto* copy = new QToolButton;
        copy->setObjectName("detailsPrimary");
        copy->setText("Copiar Detalhes");
        copy->setToolButtonStyle(Qt::ToolButtonTextOnly);
        copy->setPopupMode(QToolButton::InstantPopup);
        copy->setFixedHeight(24);
        auto* copy_menu = new QMenu(copy);
        copy_menu->addAction("Copiar tudo", this, [this]() { copy_details(); });
        copy_menu->addAction("Copiar SHA-256", this, [this]() {
            if (details_hash_) QApplication::clipboard()->setText(details_hash_->toolTip());
        });
        copy_menu->addAction("Copiar caminho", this, [this]() {
            if (details_path_) QApplication::clipboard()->setText(details_path_->text());
        });
        copy->setMenu(copy_menu);
        action_row->addWidget(copy, 1);
        details_compact_ = new QCheckBox("Compacto");
        details_compact_->setObjectName("detailsCompact");
        action_row->addWidget(details_compact_);
        outer->addLayout(action_row);

        auto* sep = new QFrame;
        sep->setObjectName("divider");
        sep->setFixedHeight(1);
        outer->addWidget(sep);

        auto* scroll = new QScrollArea;
        scroll->setObjectName("detailsScroll");
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* body = new QWidget;
        body->setObjectName("detailsBody");
        auto* body_layout = new QVBoxLayout(body);
        body_layout->setContentsMargins(0, 4, 0, 8);
        body_layout->setSpacing(0);
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
            /* Ignored colapsava a largura para 0 quando o painel ficava
               estreito (flyout do auto-hide) e o valor sumia da tela */
            value->setMinimumWidth(90);
            value->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        }
        details_path_->setObjectName("monoValue");
        details_hash_->setObjectName("monoValue");
        details_status_->setObjectName("detailsBadge");
        details_confidence_->setObjectName("detailsBadge");
        const QFont mono("Consolas", 9);
        details_path_->setFont(mono);
        details_hash_->setFont(mono);
        add_details_section(body_layout, QString::fromUtf8("Identifica\xC3\xA7\xC3\xA3o"), {
            {"Nome", details_name_}, {"Caminho", details_path_},
            {"Tamanho", details_size_}, {"Tipo", details_type_}}, true);
        add_details_section(body_layout, "Integridade", {
            {"SHA-256", details_hash_}, {"Status", details_status_}}, true);
        add_details_section(body_layout, "Coleta", {
            {"Offset", details_offset_}, {"Criado", details_created_},
            {"Modificado", details_modified_},
            {QString::fromUtf8("Confian\xC3\xA7" "a"), details_confidence_}}, true);
        add_details_section(body_layout, QString::fromUtf8("Hist\xC3\xB3rico"), {}, false);
        body_layout->addStretch(1);
        scroll->setWidget(body);
        outer->addWidget(scroll, 1);

        details_note_ = new QLabel("Selecione uma linha da tabela para atualizar os metadados.");
        details_note_->setObjectName("detailsNote");
        details_note_->setWordWrap(true);
        outer->addWidget(details_note_);

        connect(details_filter_, &QLineEdit::textChanged, this, [this](const QString&) {
            apply_details_filter();
        });
        connect(details_compact_, &QCheckBox::toggled, this, [this](bool) {
            apply_details_filter();
        });
        connect(details_scope_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { apply_details_filter(); });
        return shell;
    }

    /* ---- secoes recolhiveis do painel Detalhes (modelo do painel git) ---- */
    void add_details_section(QVBoxLayout* into, const QString& title,
                             const std::vector<std::pair<QString, QLabel*>>& rows, bool expanded) {
        auto* header = new QPushButton;
        header->setObjectName("detailsSection");
        header->setCheckable(true);
        header->setChecked(expanded);
        header->setFocusPolicy(Qt::NoFocus);
        header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        header->setFixedHeight(24);
        header->setText(QString("%1 %2  (%3)")
                            .arg(expanded ? QString::fromUtf8("\u25BE") : QString::fromUtf8("\u25B8"),
                                 title)
                            .arg(rows.size()));
        into->addWidget(header);

        auto* body = new QWidget;
        body->setObjectName("detailsSectionBody");
        auto* bl = new QVBoxLayout(body);
        bl->setContentsMargins(12, 0, 8, 6);
        bl->setSpacing(1);
        const int section_index = details_sections_.size();
        for (const auto& pair : rows) {
            auto* row = new QWidget(body);
            row->setObjectName("detailsRow");
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 2, 0, 2);
            rl->setSpacing(10);
            auto* key = new QLabel(pair.first, row);
            key->setObjectName("detailsKey");
            key->setFixedWidth(86);
            key->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            rl->addWidget(key);
            /* valor embrulhado com stretch para o badge nao esticar na linha */
            auto* value_wrap = new QWidget(row);
            auto* vl = new QHBoxLayout(value_wrap);
            vl->setContentsMargins(0, 0, 0, 0);
            vl->setSpacing(6);
            if (pair.second->objectName() == "detailsBadge") {
                /* badge nao estica: encosta a esquerda */
                vl->addWidget(pair.second, 0);
                vl->addStretch(1);
            } else {
                /* valor ocupa o resto da linha (com stretch(1) no lugar do
                   addStretch, senao o texto era espremido a ~120px) */
                vl->addWidget(pair.second, 1);
            }
            rl->addWidget(value_wrap, 1);
            bl->addWidget(row);
            details_rows_.append(DetailsRow{row, key, pair.second, section_index});
        }
        into->addWidget(body);
        body->setVisible(expanded);
        connect(header, &QPushButton::toggled, this, [header, body, title, rows](bool on) {
            body->setVisible(on);
            header->setText(QString("%1 %2  (%3)")
                                .arg(on ? QString::fromUtf8("\u25BE") : QString::fromUtf8("\u25B8"),
                                     title)
                                .arg(rows.size()));
        });
        details_sections_.append(DetailsSection{header, body});
    }

    void set_details_sections_expanded(bool on) {
        for (const DetailsSection& s : details_sections_) {
            if (s.header) s.header->setChecked(on);
        }
    }

    void copy_details() {
        QStringList lines;
        for (const DetailsRow& r : details_rows_) {
            if (!r.key || !r.value) continue;
            lines << r.key->text() + ": " + static_cast<QLabel*>(r.value)->text();
        }
        if (lines.isEmpty() && details_note_) lines << details_note_->text();
        QApplication::clipboard()->setText(lines.join('\n'));
        if (status_label_) status_label_->setText("Detalhes copiados para a area de transferencia.");
    }

    void refresh_details_summary() {
        if (!details_summary_) return;
        int filled = 0;
        for (const DetailsRow& r : details_rows_) {
            const QString v = static_cast<QLabel*>(r.value)->text();
            if (!v.isEmpty() && v != "-") ++filled;
        }
        const int total = details_rows_.size();
        details_summary_->setText(QString("%1 de %2 campos preenchidos").arg(filled).arg(total));
    }

    void apply_details_filter() {
        const QString needle = details_filter_ ? details_filter_->text().trimmed() : QString();
        const bool compact = details_compact_ && details_compact_->isChecked();
        const int scope = details_scope_ ? details_scope_->currentIndex() : 0;
        int shown = 0;
        for (const DetailsRow& r : details_rows_) {
            const QString value = static_cast<QLabel*>(r.value)->text();
            const bool empty = value.isEmpty() || value == "-";
            bool show = true;
            if (!needle.isEmpty()) {
                show = r.key->text().contains(needle, Qt::CaseInsensitive) ||
                       value.contains(needle, Qt::CaseInsensitive);
            }
            if (show && (compact || scope == 1) && empty) show = false;
            if (show && scope == 2 && !empty) show = false;
            r.row->setVisible(show);
            if (show) ++shown;
        }
        if (details_summary_ && (!needle.isEmpty() || compact || scope != 0)) {
            details_summary_->setText(QString("%1 de %2 campos visiveis")
                                          .arg(shown).arg(details_rows_.size()));
        } else {
            refresh_details_summary();
        }
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

    void open_center_tab(int index) {
        if (!center_tabs_) return;
        center_tabs_->setTabVisible(index, true);
        center_tabs_->setCurrentIndex(index);
    }

    // Diagnostico de geometria (OLHO_DUMP=1). Escreve _layout_dump.txt ao lado do exe.
    void dump_layout_tree() {
        QFile f(QCoreApplication::applicationDirPath() + "/_layout_dump.txt");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        QTextStream s(&f);
        s << "window " << width() << "x" << height()
          << " dpr=" << devicePixelRatioF() << "\n";
        if (QWidget* mw = menuWidget()) {
            const QPoint p = mw->mapTo(this, QPoint(0, 0));
            s << "menuWidget y=" << p.y() << " h=" << mw->height()
              << " w=" << mw->width() << "\n";
        }
        for (QToolBar* tb : findChildren<QToolBar*>()) {
            const QPoint p = tb->mapTo(this, QPoint(0, 0));
            s << "QToolBar#" << tb->objectName() << " y=" << p.y()
              << " h=" << tb->height() << " w=" << tb->width()
              << " contentsMargins=" << tb->contentsMargins().top() << "/"
              << tb->contentsMargins().bottom() << "\n";
            if (QLayout* lay = tb->layout()) s << "  layout margins="
                << lay->contentsMargins().top() << "/" << lay->contentsMargins().bottom()
                << " spacing=" << lay->spacing() << "\n";
        }
        const QStringList names = {"titleBar", "titleLogo", "titleMenuButton", "titleSearch",
                                   "commandBar", "barBottomGap", "centerTabs", "dockTitleBar",
                                   "dockCard", "dockDragStrip", "detailsToolbar",
                                   "statusBarHost", "consolePanel"};
        for (const QString& n : names) {
            const QList<QWidget*> hits = findChildren<QWidget*>(n);
            if (hits.isEmpty()) { s << n << " <none>\n"; continue; }
            int i = 0;
            for (QWidget* w : hits) {
                const QPoint p = w->mapTo(this, QPoint(0, 0));
                s << n << "[" << i++ << "] y=" << p.y() << " x=" << p.x()
                  << " w=" << w->width() << " h=" << w->height()
                  << (w->isVisible() ? "" : " hidden")
                  << " minH=" << w->minimumHeight() << " maxH=" << w->maximumHeight()
                  << "\n";
                /* conteudo do menu "..." do titulo do dock (o que o clique abre) */
                if (n == "dockTitleBar") {
                    if (auto* tb = dynamic_cast<ui::DockTitleBar*>(w)) {
                        if (QMenu* m = tb->menu()) {
                            for (QAction* a : m->actions()) {
                                s << "    . " << (a->isSeparator() ? QString("---")
                                                                  : a->text()) << "\n";
                            }
                        } else {
                            s << "    . <sem menu>\n";
                        }
                    }
                }
            }
        }
        s.flush();
    }

    void build_menu_bar() {
        auto* title = new TitleBarWidget(this);
        setMenuWidget(title);
        auto* title_layout = new QHBoxLayout(title);
        title_layout->setContentsMargins(6, 0, 0, 0);
        title_layout->setSpacing(4);

        auto* mark = new QLabel(title);
        mark->setObjectName("titleLogo");
        mark->setFixedSize(20, 20);
        /* VS: a marca da barra de titulo e monoline clara e ocupa 20px (ink
           medido em x=7..26); o olho colorido destoava do resto do chrome */
        {
            const QIcon mono_mark = theme::mono_icon("icons/logo_eye_square.png");
            if (!mono_mark.isNull()) {
                mark->setPixmap(mono_mark.pixmap(16, 16));
            } else {
                mark->setPixmap(QPixmap(QCoreApplication::applicationDirPath() +
                    "/icons/logo_eye_square.png").scaled(16, 16, Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation));
            }
        }
        title_layout->addWidget(mark);

        auto* bar = new QMenuBar(title);
        bar->setObjectName("titleMenuBar");
        bar->setNativeMenuBar(false);
        bar->setFixedHeight(30);
        // QMenuBar dentro de QHBoxLayout encolhe ate 0 se ficar com politica expansivel
        bar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        bar->setMinimumWidth(520);
        title_layout->addWidget(bar);

        auto* file = bar->addMenu("Arquivo");
        file->addAction("Abrir caso...", this, [this]() {
            const QString dir = QFileDialog::getExistingDirectory(
                this, "Abrir caso", case_dir_);
            if (!dir.isEmpty()) load_case(dir);
        });
        file->addAction("Atualizar caso", this, [this]() { load_case(case_dir_); });
        file->addSeparator();
        file->addAction("Sair", this, [this]() { close(); });

        auto* edit = bar->addMenu("Editar");
        edit->addAction("Copiar console", this, [this]() {
            if (console_) QApplication::clipboard()->setText(console_->toPlainText());
        });
        edit->addAction("Limpar console", this, [this]() {
            if (console_) console_->clear();
        });

        auto* view = bar->addMenu("Exibir");
        view_menu_ = view;
        const QStringList view_names = {"Tabela", "Hex View", "Timeline"};
        for (int i = 0; i < view_names.size(); ++i) {
            view->addAction(view_names[i], this, [this, i]() {
                center_tabs_->setTabVisible(i, true);
                center_tabs_->setCurrentIndex(i);
            });
        }
        view->addSeparator();
        view->addAction("Console", this, [this]() {
            bottom_dock_->show();
            bottom_dock_->raise();
        });
        // Um caso de 200 mil eventos nao cabe numa tabela de itens, mas tambem nao
        // pode ser escondido atras de um teto fixo: o teto dobra por decisao do
        // operador e a proxima carga ja vem maior.
        timeline_more_ = view->addAction("Timeline: carregar mais eventos", this, [this]() {
            if (case_dir_.isEmpty()) return;
            const int before = timeline_cap_;
            timeline_cap_ = qMin(200000, timeline_cap_ <= 0 ? 5000 : timeline_cap_ * 5);
            append(QString("[case] timeline: limite de exibicao %1 -> %2\n")
                       .arg(before).arg(timeline_cap_));
            // So a Timeline: o resto do caso nao mudou.
            refresh_timeline(case_dir_);
        });
        timeline_more_->setEnabled(false);
        // Corte por data custa uma leitura completa do caso (1,14 s em 200 mil
        // eventos), por isso e opcao e nao padrao: a janela passa a ser o comeco da
        // historia, em vez de "os primeiros arquivos que apareceram".
        timeline_chrono_ = view->addAction("Timeline: cortar por data (mais antigos)", this,
                                           [this]() {
            if (case_dir_.isEmpty()) return;
            timeline_chronological_ = !timeline_chronological_;
            append(timeline_chronological_
                       ? "[case] timeline: corte por data (mais antigos)\n"
                       : "[case] timeline: corte pela ordem dos arquivos\n");
            refresh_timeline(case_dir_);
        });
        timeline_chrono_->setCheckable(true);
        timeline_chrono_->setEnabled(false);

        auto* tools = bar->addMenu("Ferramentas");
        const QStringList tool_names = {"Pericia", "Avancado", "Sanitizacao",
                                        "Auditoria", "Wipe", "Kernel", "Cloud", "Hardware"};
        for (int i = 0; i < tool_names.size(); ++i) {
            tools->addAction(tool_names[i], this, [this, i]() { open_tool(i); });
        }

        auto* project = bar->addMenu("Projeto");
        project->addAction("Inventario de hardware", this, [this]() { open_tool(7); });
        project->addAction("Consultar caso", this, [this]() { case_query(); });
        project->addAction("Resumo do caso", this, [this]() { case_summary(); });
        // Integridade do caso: sela (ou verifica) db, journal, WAL e a midia
        // extraida que estiver em recovered/ - o selo da midia e uma lista de
        // digests com HMAC, e o resultado cai no painel Console.
        project->addAction("Selar caso (integridade)", this, [this]() {
            if (!ensure_cloud_case("selar o caso")) return;
            set_cloud_hint(QString());
            run({"--case-seal", "--case", cloud_case_base()}, false);
        });
        project->addAction("Verificar selo do caso", this, [this]() {
            if (!ensure_cloud_case("verificar o selo")) return;
            set_cloud_hint(QString());
            run({"--case-check", "--case", cloud_case_base()}, false);
        });

        auto* window = bar->addMenu("Janela");
        window->addAction("Console", this, [this]() {
            bottom_dock_->show();
            bottom_dock_->raise();
        });
        window->addAction("Tabela", this, [this]() { open_center_tab(0); });
        window->addAction("Hex View", this, [this]() { open_center_tab(1); });
        window->addAction("Timeline", this, [this]() { open_center_tab(2); });
        auto* git = bar->addMenu("Git");
        git->addAction("Atualizar caso", this, [this]() { load_case(case_dir_); });
        git->addAction("Historico do caso", this, [this]() { case_summary(); });

        auto* debug = bar->addMenu("Depurar");
        debug->addAction("Rodar engine no caso", this, [this]() { case_query(); });
        debug->addAction("Console", this, [this]() { bottom_dock_->show(); bottom_dock_->raise(); });

        auto* test = bar->addMenu("Teste");
        test->addAction("Resumo do caso", this, [this]() { case_summary(); });

        auto* ext = bar->addMenu(QString::fromUtf8("Extens\xC3\xB5""es"));
        QAction* soon = ext->addAction("Nenhuma extensao instalada");
        soon->setEnabled(false);

        auto* help_menu = bar->addMenu("Ajuda");
        help_menu->addAction("Ajuda CLI", this, [this]() { help(); });
        help_menu->addAction("Sobre", this, [this]() {
            QMessageBox::about(this, "Olho de Deus",
                "Olho de Deus / Eye of God\n"
                "Deep Engine Forensic Workstation\n\n"
                "Pericia, sanitizacao e analise read-only.");
        });

        bar->adjustSize();
        // QMenuBar sob QSS pinta o fundo mas nao desenha os itens.
        // O VS usa botoes de menu na linha de titulo; fazemos igual e deixamos
        // o QMenuBar apenas como dono dos QMenu (invisivel).
        {
            auto* menu_row = new QWidget(title);
            menu_row->setObjectName("titleMenuRow");
            auto* menu_row_lay = new QHBoxLayout(menu_row);
            menu_row_lay->setContentsMargins(0, 0, 0, 0);
            menu_row_lay->setSpacing(0);
            QHash<QString, QAction*> by_title;
            for (QAction* a : bar->actions()) {
                if (a->menu()) by_title.insert(a->text(), a);
            }
            // ordem do Visual Studio 2026
            const QStringList order = {"Arquivo", "Editar", "Exibir", "Git", "Projeto",
                                       "Depurar", "Teste", "Ferramentas",
                                       QString::fromUtf8("Extens\xC3\xB5""es"),
                                       "Janela", "Ajuda"};
            QStringList titles = order;
            for (const QString& t : by_title.keys()) {
                if (!titles.contains(t)) titles << t;
            }
            for (const QString& t : titles) {
                QAction* a = by_title.value(t, nullptr);
                if (!a) continue;
                auto* b = new QToolButton(menu_row);
                b->setObjectName("titleMenuButton");
                b->setText(a->text());
                b->setMenu(a->menu());
                b->setPopupMode(QToolButton::InstantPopup);
                b->setFixedHeight(30);
                b->setFocusPolicy(Qt::NoFocus);
                menu_row_lay->addWidget(b);
            }
            menu_row_lay->addStretch(1);
            title_layout->insertWidget(1, menu_row);
            bar->hide();
        }
        title_layout->addSpacing(2);
        auto* search = new QLineEdit(title);
        search->setObjectName("titleSearch");
        search->setPlaceholderText("Pesquisar");
        // VS medido: campo de busca ocupa x=612..718 (106px) na linha de titulo
        search->setFixedSize(100, 24);
        search->setClearButtonEnabled(true);
        const QIcon mag = theme::mono_icon("icons/search.png");
        if (!mag.isNull()) search->addAction(mag, QLineEdit::LeadingPosition);
        connect(search, &QLineEdit::textChanged, this, [this](const QString& text) {
            if (tree_search_) tree_search_->setText(text);
        });
        title_layout->addWidget(search);

        auto* search_menu = new QToolButton(title);
        search_menu->setObjectName("titleSearchMenu");
        search_menu->setText(QString::fromUtf8("\u25BE"));
        search_menu->setFixedSize(18, 24);
        search_menu->setPopupMode(QToolButton::InstantPopup);
        search_menu->setParent(search);
        search_menu->move(100 - 20, 0);
        search_menu->raise();
        auto* search_actions = new QMenu(search_menu);
        search_actions->addAction("Pesquisar no caso", this, [this]() {
            if (tree_search_) { tree_search_->setFocus(); tree_search_->selectAll(); }
        });
        search_actions->addAction("Limpar busca", this, [this]() {
            if (tree_search_) tree_search_->clear();
        });
        search_menu->setMenu(search_actions);

        title_layout->addSpacing(8);
        auto* project_chip = new QLabel("Eye-of-God-main", title);
        project_chip->setObjectName("projectName");
        project_chip->setFixedHeight(22);
        project_chip->setContentsMargins(8, 0, 8, 0);
        title_layout->addWidget(project_chip);
        title_layout->addStretch(1);

        auto* avatar = new QLabel("OP", title);
        avatar->setObjectName("userAvatar");
        avatar->setFixedSize(24, 24);
        avatar->setAlignment(Qt::AlignCenter);
        title_layout->addWidget(avatar);
        title_layout->addSpacing(8);

        auto add_control = [this, title](const QString& glyph,
                                         const QString& name, bool close,
                                         std::function<void()> fn) {
            auto* button = new QToolButton(title);
            button->setObjectName(name);
            button->setProperty("windowClose", close);
            button->setText(glyph);
            button->setFixedSize(46, 30);
            button->setFocusPolicy(Qt::NoFocus);
            connect(button, &QToolButton::clicked, this, fn);
            title->layout()->addWidget(button);
        };
        add_control(QString(QChar(0x2013)), "titleMin", false,
                    [this]() { showMinimized(); });
        add_control(QString(QChar(0x25A1)), "titleMax", false, [this]() {
            if (isMaximized()) showNormal(); else showMaximized();
        });
        add_control(QString(QChar(0x00D7)), "titleClose", true,
                    [this]() { close(); });
    }

    void register_commands() {
        using cmd::Command;
        using cmd::Kind;
        auto& reg = cmd::Registry::instance();

        auto simple = [&reg](const QString& id, const QString& title, const QString& cat,
                             const QString& icon_path, const QString& shortcut,
                             const QString& bars, std::function<void()> fn) {
            Command c;
            c.id = id; c.title = title; c.category = cat; c.iconPath = icon_path;
            c.shortcut = shortcut; c.defaultBars = bars; c.invoke = std::move(fn);
            reg.add(c);
        };
        auto view_fn = [this](int i) {
            return [this, i]() {
                if (!center_tabs_) return;
                center_tabs_->setTabVisible(i, true);
                center_tabs_->setCurrentIndex(i);
            };
        };

        // Caso
        simple("case.open", "Abrir caso", "Caso", "icons/folder.png", "Ctrl+O", "secondary",
               [this]() {
                   const QString dir = QFileDialog::getExistingDirectory(this, "Abrir caso", case_dir_);
                   if (!dir.isEmpty()) load_case(dir);
               });
        simple("case.refresh", "Atualizar caso", "Caso", "icons/refresh.png", "F5", "secondary",
               [this]() { load_case(case_dir_); });
        simple("case.summary", "Resumo do caso", "Caso", "icons/action_read.png", "", "secondary",
               [this]() { case_summary(); });
        simple("case.query", "Consultar caso", "Caso", "icons/search.png", "", "secondary",
               [this]() { case_query(); });

        // Aquisicao
        simple("tool.acquisition", "Aquisicao", "Aquisicao", "icons/acquisition.png", "", "main",
               [this]() { open_tool(0); });
        simple("tool.artifacts", "Artefatos", "Aquisicao", "icons/artifacts.png", "", "main",
               [this]() { open_tool(0); });
        simple("tool.e01", "E01", "Aquisicao", "icons/e01.png", "", "main",
               [this]() { open_tool(0); });
        simple("tool.pericia", "Pericia", "Aquisicao", "icons/pericia.png", "", "main",
               [this]() { open_tool(0); });
        simple("tool.recovery", "Recuperacao", "Aquisicao", "icons/recovery.png", "", "main",
               [this]() { open_tool(1); });

        // Ferramentas
        simple("tool.sanitize", "Sanitizacao", "Ferramentas", "icons/sanitizacao.png", "", "main",
               [this]() { open_tool(2); });
        simple("tool.auditoria", "Auditoria", "Ferramentas", "icons/auditoria.png", "", "main",
               [this]() { open_tool(3); });
        simple("tool.wipe", "Wipe", "Ferramentas", "icons/wipe.png", "", "main",
               [this]() { open_tool(4); });
        simple("tool.kernel", "Kernel", "Ferramentas", "icons/kernel.png", "", "main",
               [this]() { open_tool(5); });
        simple("tool.cloud", "Cloud", "Ferramentas", "icons/cloud.png", "", "main",
               [this]() { open_tool(6); });
        simple("tool.hardware", "Hardware", "Ferramentas", "icons/hardware.png", "", "main",
               [this]() { open_tool(7); });
        simple("tool.avancado", "Avancado", "Ferramentas", "icons/avancado.png", "", "main",
               [this]() { open_tool(1); });

        // Visualizar
        simple("view.table", "Tabela", "Visualizar", "icons/tab_table.png", "", "secondary", view_fn(0));
        simple("view.hex", "Hex View", "Visualizar", "icons/tab_hex.png", "", "secondary", view_fn(1));
        simple("view.timeline", "Timeline", "Visualizar", "icons/tab_timeline.png", "", "secondary", view_fn(2));
        simple("dock.console", "Console", "Visualizar", "icons/terminal.png", "", "secondary",
               [this]() { if (bottom_dock_) { bottom_dock_->show(); bottom_dock_->raise(); } });

        // Arvore
        simple("tree.expand", "Expandir tudo", "Arvore", "icons/expand_all.png", "", "secondary",
               [this]() { if (tree_) tree_->expandAll(); });
        simple("tree.collapse", "Recolher tudo", "Arvore", "icons/collapse_all.png", "", "secondary",
               [this]() { if (tree_) tree_->collapseAll(); });
        simple("tree.refresh", "Recarregar arvore", "Arvore", "icons/explorer_refresh.png", "", "secondary",
               [this]() { load_case(case_dir_); });

        // Console
        simple("console.clear", "Limpar console", "Console", "icons/terminal.png", "", "secondary",
               [this]() { if (console_) console_->clear(); });
        simple("console.copy", "Copiar console", "Console", "icons/action_read.png", "", "secondary",
               [this]() { if (console_) QApplication::clipboard()->setText(console_->toPlainText()); });
        simple("help.about", "Sobre", "Console", "icons/action_help.png", "", "secondary",
               [this]() { help(); });

        {
            Command c;
            c.id = "engine.run"; c.title = "Rodar engine"; c.category = "Executar";
            c.iconPath = "icons/play.png"; c.defaultBars = "main";
            c.kind = Kind::TextButton; c.mono = false;
            c.tooltip = "Executar a engine no caso carregado";
            c.invoke = [this]() { case_query(); };
            reg.add(c);
        }
        {
            Command c;
            c.id = "case.combo"; c.title = "Caso"; c.category = "Caso";
            c.defaultBars = "secondary"; c.kind = Kind::Combo;
            c.widget_factory = [this](QWidget* parent) {
                auto* combo = new QComboBox(parent);
                combo->setObjectName("commandCombo");
                combo->setMinimumWidth(170);
                combo->addItem(icon("tab_table.png"), "Caso carregado");
                combo->setToolTip("Caso ativo");
                return combo;
            };
            reg.add(c);
        }
        {
            Command c;
            c.id = "views.split"; c.title = "Views"; c.category = "Visualizar";
            c.iconPath = "icons/tab_table.png"; c.defaultBars = "secondary";
            c.kind = Kind::SplitButton;
            c.invoke = view_fn(0);
            c.menu_items = []() { return QStringList{"Tabela", "Hex View", "Timeline"}; };
            /* mesma rotina de view_fn: um unico ponto com guard de nullptr */
            c.menu_invoke = [this](int i) {
                if (!center_tabs_) return;
                center_tabs_->setTabVisible(i, true);
                center_tabs_->setCurrentIndex(i);
            };
            reg.add(c);
        }
    }

    QString layout_settings_path() const {
        return QCoreApplication::applicationDirPath() + "/layout.ini";
    }

    // versionar o esquema evita aplicar um estado de outra versao do app
    static constexpr int kLayoutVersion = 2;
    void save_layout() {
        QSettings s(layout_settings_path(), QSettings::IniFormat);
        s.setValue("layout/version", kLayoutVersion);
        s.setValue("layout/windowState", saveState());
        /* a geometria da janela nao e restaurada: o app abre maximizado,
           entao gravar saveGeometry() era chave morta */
    }

    void restore_layout() {
        QSettings s(layout_settings_path(), QSettings::IniFormat);
        if (s.status() != QSettings::NoError) {
            qWarning("layout.ini ilegivel; usando o layout padrao");
            return;
        }
        const int version = s.value("layout/version", 0).toInt();
        const QByteArray st = s.value("layout/windowState").toByteArray();
        if (version != kLayoutVersion || st.isEmpty()) return;
        if (!restoreState(st)) qWarning("restoreState falhou; layout padrao mantido");
    }

    /* barra de titulo embutida no card de cada dock */
    ui::DockTitleBar* dock_title_bar(QDockWidget* dock) const {
        return dock_title_bars_.value(dock, nullptr);
    }

    QDockWidget* make_dock(const QString& title, const QString& object_name, QWidget* content) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(object_name);
        dock->setFeatures(QDockWidget::DockWidgetMovable |
                          QDockWidget::DockWidgetFloatable |
                          QDockWidget::DockWidgetClosable);
        /* Card: o titulo agora fica DENTRO do painel arredondado (como no painel
           de referencia). A barra nativa do dock vira uma tira transparente de
           6px, que e o respiro do card e continua servindo para arrastar. */
        auto* card = new CardFrame;
        card->setObjectName("dockCard");
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto* card_layout = new QVBoxLayout(card);
        card_layout->setContentsMargins(0, 0, 0, 0);
        card_layout->setSpacing(0);
        auto* title_bar = new ui::DockTitleBar(title, dock);
        title_bar->set_embedded(true);
        card_layout->addWidget(title_bar);
        card_layout->addWidget(content, 1);

        dock->setWidget(card);
        auto* drag_strip = new QWidget(dock);
        drag_strip->setObjectName("dockDragStrip");
        drag_strip->setFixedHeight(6);
        drag_strip->setStyleSheet("background: transparent;");
        dock->setTitleBarWidget(drag_strip);
        dock_title_bars_.insert(dock, title_bar);
        connect(dock, &QObject::destroyed, this, [this, dock]() { dock_title_bars_.remove(dock); });
        /* VS (Solution Explorer): a linha do titulo tem o titulo e os glifos
           (pin, opcoes, fechar) - nao uma fileira de icones. As acoes extras do
           painel passam para o menu de opcoes, sem perder funcao. */
        auto* title_menu = new QMenu(title_bar);
        if (panel_header_.menu) {
            for (QAction* action : panel_header_.menu->actions()) title_menu->addAction(action);
            /* separador so quando existem acoes extras (senao sobrava um
               traco solto no fim do menu) */
            if (!title_menu->actions().isEmpty() && !panel_header_.items.empty()) {
                title_menu->addSeparator();
            }
        }
        for (const auto& item : panel_header_.items) {
            title_menu->addAction(item.first, this, item.second);
        }
        if (!title_menu->actions().isEmpty()) title_bar->set_menu(title_menu);
        panel_header_ = PanelHeaderActions();   // limpa para o proximo painel
        for (QLabel* lbl : content->findChildren<QLabel*>()) {
            const QString n = lbl->objectName();
            if (n == "explorerTitle" || n == "panelTitle" || n == "brandTitle") lbl->hide();
        }
        return dock;
    }

    void closeEvent(QCloseEvent* event) override {
        save_layout();
        QMainWindow::closeEvent(event);
    }

    void build_ui() {
        build_menu_bar();
        // ---- command bars customizaveis (modelo VS: 28 + 6 + 25 px) ----
        register_commands();
        auto* host = new ChromeToolBar(this);
        host->setObjectName("commandBarHost");
        addToolBar(Qt::TopToolBarArea, host);
        host->setWindowTitle("Principal");
        host->setMovable(false);
        host->setFloatable(false);
        host->setFixedHeight(69);
        host->setStyleSheet("QToolBar#commandBarHost{background:#1C1C1C;border:none;"
                            "padding:0px;margin:0px;spacing:0px;}"
                            "QToolBar::separator{width:0px;height:0px;}");
        host->setContentsMargins(0, 0, 0, 0);
        if (QLayout* host_layout = host->layout()) {
            host_layout->setContentsMargins(0, 0, 0, 0);
            host_layout->setSpacing(0);
        }
        // QToolBar sem item nenhum e recolhido pelo QMainWindow (medido: y=-500,
        // w=100), entao um preenchimento ocupa o item e o chrome fica por cima
        // como filho direto, com geometria explicita.
        auto* filler = new QWidget(host);
        filler->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        filler->setMinimumSize(1, 1);
        host->addWidget(filler);
        auto* chrome = new ChromeBands(host);
        chrome->setObjectName("chromeBands");
        command_bar_main_ = new ui::CommandBar("main", 30, chrome);
        auto* bar_gap = new QWidget(chrome);
        bar_gap->setObjectName("barsGap");
        auto* bar_bottom_gap = new ThinBand(chrome);
        bar_bottom_gap->setObjectName("barBottomGap");
        command_bar_secondary_ = new ui::CommandBar("secondary", 30, chrome);
        chrome->bar_a = command_bar_main_;
        chrome->gap = bar_gap;
        chrome->bar_b = command_bar_secondary_;
        chrome->band = bar_bottom_gap;
        host->attach_chrome(chrome);
        chrome->layout_bands();
        command_bar_main_->set_on_layout_changed([this]() {
            if (command_bar_secondary_) command_bar_secondary_->refresh();
        });
        command_bar_secondary_->set_on_layout_changed([this]() {
            if (command_bar_main_) command_bar_main_->refresh();
        });
        bars_chrome_ = chrome;
        bars_gap_ = bar_gap;
        bar_host_ = host;
        if (view_menu_) {
            QMenu* tb = view_menu_->addMenu("Barras de Ferramentas");
            bar_main_action_ = tb->addAction("Principal");
            bar_main_action_->setCheckable(true);
            bar_main_action_->setChecked(true);
            bar_secondary_action_ = tb->addAction("Secundaria");
            bar_secondary_action_->setCheckable(true);
            bar_secondary_action_->setChecked(true);
            /* modelo VS: uma entrada por toolbar/classe, ligavel e desligavel */
            tb->addSeparator();
            if (command_bar_main_) {
                const QStringList segs = command_bar_main_->segments();
                for (const QString& seg : segs) {
                    QAction* a = tb->addAction(ui::CommandBar::segment_label(seg));
                    a->setCheckable(true);
                    a->setChecked(command_bar_main_->segment_visible(seg));
                    const QString key = seg;
                    connect(a, &QAction::toggled, this, [this, key](bool on) {
                        if (command_bar_main_) command_bar_main_->set_segment_visible(key, on);
                    });
                }
            }
            tb->addSeparator();
            tb->addAction("Personalizar...", this, [this]() {
                if (command_bar_main_) command_bar_main_->popup_customize_menu(QCursor::pos());
            });
            connect(bar_main_action_, &QAction::toggled, this, [this](bool on) {
                if (!command_bar_main_) return;
                command_bar_main_->set_bar_visible(on);
            });
            connect(bar_secondary_action_, &QAction::toggled, this, [this](bool on) {
                if (!command_bar_secondary_) return;
                command_bar_secondary_->set_source("view-menu");
                command_bar_secondary_->set_bar_visible(on);
            });
            auto sync_visibility = [this]() {
                const bool a = command_bar_main_ && command_bar_main_->bar_visible();
                const bool b = command_bar_secondary_ && command_bar_secondary_->bar_visible();
                if (command_bar_main_) command_bar_main_->setVisible(a);
                if (command_bar_secondary_) command_bar_secondary_->setVisible(b);
                if (bars_gap_) bars_gap_->setVisible(a && b);
                if (bars_chrome_) bars_chrome_->setVisible(a || b);
                auto* bands = dynamic_cast<ChromeBands*>(bars_chrome_);
                if (bands) bands->layout_bands();
                if (auto* tb = dynamic_cast<ChromeToolBar*>(bar_host_)) {
                    const int total = bands ? bands->total_height() : 69;
                    tb->setFixedHeight((std::max)(total, 0));
                }
                if (bar_main_action_) { QSignalBlocker blk(bar_main_action_); bar_main_action_->setChecked(a); }
                if (bar_secondary_action_) { QSignalBlocker blk(bar_secondary_action_); bar_secondary_action_->setChecked(b); }
            };
            command_bar_main_->set_on_visibility_changed([sync_visibility](bool) { sync_visibility(); });
            command_bar_secondary_->set_on_visibility_changed([sync_visibility](bool) { sync_visibility(); });
            sync_visibility();
        }

        tool_pages_.resize(8);
        // Docking real (modelo VS): grupos aninhados, abas, flutuante e animado.
        setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks |
                       QMainWindow::AnimatedDocks | QMainWindow::GroupedDragging);
        setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

        tree_dock_ = make_dock("Gerenciador do Caso", "treeDock", build_tree_panel());
        addDockWidget(Qt::LeftDockWidgetArea, tree_dock_);

        center_tabs_ = new CenterTabs;
        center_tabs_->setObjectName("centerTabs");
        center_tabs_->useTabBar(new ContourTabBar(center_tabs_));
        center_tabs_->setDocumentMode(true);
        center_tabs_->setIconSize(QSize(16, 16));
        add_fixed_document(build_table_tab(), "tab_table.png", "Tabela");
        add_fixed_document(build_hex_tab(), "tab_hex.png", "Hex View");
        timeline_view_ = new TimelineView;
        add_fixed_document(timeline_view_, "tab_timeline.png", "Timeline");
        // Conversas recuperadas. O widget le o export do wa_recover e nao abre
        // banco nenhum: a GUI nunca precisa saber SQL.
        chat_view_ = new olho::ui::ChatView;
        /* quando existe um export de recuperacao ao lado do exe, o caso aberto e
           o de conversas: mostrar a aba em vez de obrigar um clique */
        const bool has_conversations = chat_view_->try_autoload();
        QWidget* chat_page = add_fixed_document(chat_view_, "artifacts.png", "Chat");
        gallery_view_ = new olho::ui::GalleryView;
        gallery_view_->try_autoload();
        add_fixed_document(gallery_view_, "artifacts.png", "Galeria");
        acquire_view_ = new olho::ui::AcquireView;
        add_fixed_document(acquire_view_, "acquisition.png", "Aquisicao");
        chat_page_ = chat_page;
        /* ciclo fechado: o painel adquire, chama o wa_recover e entrega o export
           aqui, sem o operador tocar em linha de comando */
        acquire_view_->set_case_handler([this](const QString& conversations_json) {
            if (chat_view_) chat_view_->load(conversations_json);
            /* a mesma recuperacao produz a pasta de midia; a galeria aponta para
               ela sem o operador escolher caminho de novo */
            if (gallery_view_) {
                const QString media_dir = QFileInfo(conversations_json).absolutePath() + "/media";
                gallery_view_->load_directory(media_dir);
            }
            if (chat_page_) center_tabs_->setCurrentWidget(chat_page_);
        });
        // A Timeline carrega em thread propria; o timer que colhe o resultado tem
        // que existir na janela e nao no construtor de uma aba, senao quem nunca
        // abre a aba Cloud fica com a tabela presa em "carregando".
        timeline_timer_ = new QTimer(this);
        timeline_timer_->setInterval(120);
        connect(timeline_timer_, &QTimer::timeout, this, [this]() { poll_timeline(); });
        timeline_timer_->start();
        if (has_conversations) center_tabs_->setCurrentWidget(chat_page);
        center_tabs_->tabBar()->setExpanding(false);
        setCentralWidget(center_tabs_);

        details_dock_ = make_dock("Detalhes", "detailsDock", build_details_panel());
        addDockWidget(Qt::RightDockWidgetArea, details_dock_);
        resizeDocks({tree_dock_, details_dock_}, {300, 380}, Qt::Horizontal);

        // auto-hide (modelo VS): canal lateral + flyout sobreposto
        auto_hide_ = new ui::AutoHideManager(this, layout_settings_path());
        auto wire_pin = [this](QDockWidget* dock, ui::Edge edge) {
            auto_hide_->register_dock(dock, edge);
            if (auto* tb = dock_title_bar(dock)) {
                tb->set_pin_handler([this, dock]() { auto_hide_->toggle(dock); });
            }
        };
        wire_pin(tree_dock_, ui::Edge::Left);
        wire_pin(details_dock_, ui::Edge::Right);
        update_details(0);

        bottom_tabs_ = new QTabWidget;
        bottom_tabs_->setObjectName("bottomTabs");
        bottom_tabs_->setDocumentMode(true);
        bottom_tabs_->addTab(build_console_tab(), "Console");
        bottom_tabs_->tabBar()->hide();
        /* o "..." do dock Console abria um menu vazio; agora tem as acoes */
        auto* console_menu = new QMenu(this);
        console_menu->addAction("Limpar console", this, [this]() {
            if (console_) console_->clear();
        });
        console_menu->addAction("Copiar console", this, [this]() {
            if (console_) QApplication::clipboard()->setText(console_->toPlainText());
        });
        panel_header_.menu = console_menu;
        bottom_dock_ = make_dock("Console", "bottomDock", bottom_tabs_);
        addDockWidget(Qt::BottomDockWidgetArea, bottom_dock_);
        resizeDocks({bottom_dock_}, {150}, Qt::Vertical);
        if (auto_hide_) {
            auto_hide_->register_dock(bottom_dock_, ui::Edge::Bottom);
            if (auto* tb = dock_title_bar(bottom_dock_)) {
                tb->set_pin_handler([this]() { auto_hide_->toggle(bottom_dock_); });
            }
        }
        /* ordem correta (auditoria): TODOS os docks criados e registrados
           primeiro; restoreState() so restaura objectNames existentes e o
           auto-hide casa por nome (load antes do register apagava a entrada
           do dock Console do ini a cada boot). */
        restore_layout();
        if (auto_hide_) auto_hide_->load();

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
        QTimer::singleShot(0, this, [this]() {
            if (tree_dock_) tree_dock_->setFocus();
        });
        if (qEnvironmentVariableIsSet("OLHO_DUMP")) {
            dump_pending_ = true;
            dump_timer_ = new QTimer(this);
            dump_timer_->setSingleShot(true);
            /* folga curta: o maximize do WM chega como evento nativo depois do
               show, entao esperar ~150ms desde o ultimo resize mede a janela
               final sem voltar a depender de tempo de parede arbitrario */
            dump_timer_->setInterval(150);
            connect(dump_timer_, &QTimer::timeout, this, [this]() {
                if (dumped_) return;
                dumped_ = true;
                dump_pending_ = false;
                dump_layout_tree();
            });
        }
    }

    /* Dump de geometria: nao por tempo de parede (o load_case enfileirado no
       construtor podia segurar o event loop e medir geometria velha) nem no
       primeiro show (o maximize chega depois). Re-arma um timer 0ms a cada
       show/resize, entao mede quando a rajada de eventos assentou. */
    void arm_dump() {
        if (!dump_pending_ || dumped_ || !dump_timer_) return;
        dump_timer_->start();
    }

    void showEvent(QShowEvent* event) override {
        QMainWindow::showEvent(event);
        arm_dump();
    }

    void resizeEvent(QResizeEvent* event) override {
        QMainWindow::resizeEvent(event);
        arm_dump();
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
                if (!name.isEmpty()) tree_node(tree_evidence_, name, "file.png");
            }
        }
        if (tree_case_item_) tree_case_item_->setText("CASO " + case_id);
        if (tree_device_item_) tree_device_item_->setText(computer + "  /  " + os);

        append("[case] " + QDir::toNativeSeparators(case_dir) + "\n");
        // Tabela, contador, grafico e mensagens: em um lugar so, para "carregar
        // mais" e o corte por data atualizarem a Timeline sem recarregar o caso.
        refresh_timeline(case_dir);

        if (tree_cloud_) {
            tree_cloud_->removeRows(0, tree_cloud_->rowCount());
            // Um no por provedor, um filho por conta: M365 e Dropbox convivem no
            // mesmo caso e a arvore precisa mostrar de qual veio cada evidencia.
            QDir providers_root(case_dir + "/cloud");
            for (const QString& provider_name : providers_root.entryList(
                     QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (provider_name.compare("jobs", Qt::CaseInsensitive) == 0) continue;
                QDir provider_dir(providers_root.filePath(provider_name));
                const QStringList accounts = provider_dir.entryList(
                    QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                if (accounts.isEmpty()) continue;
                auto* provider_item = tree_node(tree_cloud_, provider_name, "cloud.png");
                for (const QString& account : accounts) {
                    auto* account_item = tree_node(provider_item, account, "folder.png");
                    const QString manifest_path = provider_dir.filePath(
                        account + "/manifest.json");
                    if (QFileInfo::exists(manifest_path))
                        tree_node(account_item, "manifest.json", "tab_table.png");
                    QDir raw_dir(provider_dir.filePath(account + "/raw"));
                    for (const QString& raw_name : raw_dir.entryList(
                            QDir::Files, QDir::Name))
                        tree_node(account_item, raw_name, "tab_hex.png");
                }
            }
        }
        if (tree_social_) {
            tree_social_->removeRows(0, tree_social_->rowCount());
            // <caso>/social/<provedor>/<conta>/raw/*.json, a mesma forma da nuvem.
            QDir social_root(case_dir + "/social");
            for (const QString& provider_name : social_root.entryList(
                     QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                QDir provider_dir(social_root.filePath(provider_name));
                const QStringList accounts = provider_dir.entryList(
                    QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                if (accounts.isEmpty()) continue;
                auto* provider_item = tree_node(tree_social_, provider_name, "cloud.png");
                for (const QString& account : accounts) {
                    auto* account_item = tree_node(provider_item, account, "folder.png");
                    const QString manifest_path = provider_dir.filePath(
                        account + "/manifest.json");
                    if (QFileInfo::exists(manifest_path))
                        tree_node(account_item, "manifest.json", "tab_table.png");
                    QDir raw_dir(provider_dir.filePath(account + "/raw"));
                    const QStringList raws = raw_dir.entryList(QDir::Files, QDir::Name);
                    for (const QString& raw_name : raws)
                        tree_node(account_item, raw_name, "tab_hex.png");
                    if (!raws.isEmpty())
                        tree_node(account_item, QString("%1 conversa(s)").arg(raws.size()),
                                  "tab_table.png");
                    QDir media_dir(provider_dir.filePath(account + "/media"));
                    const QStringList medias = media_dir.entryList(QDir::Files, QDir::Name);
                    if (!medias.isEmpty()) {
                        auto* media_item = tree_node(account_item,
                                                     QString("midia (%1)").arg(medias.size()),
                                                     "tab_hex.png");
                        for (const QString& media_name : medias)
                            tree_node(media_item, media_name, "file.png");
                    }
                }
            }
        }
        // Selecao, contagem e barra de status sao preenchidas por apply_timeline(),
        // quando a carga assincrona termina - aqui a tabela ainda esta vazia.
    }

    // Pede a carga da Timeline. Roda numa thread de trabalho: o corte por data
    // custa ~1 s em 200 mil eventos e a janela nao pode congelar por isso.
    // Se ja houver uma carga rodando, marca pendente em vez de bloquear ou perder
    // o pedido.
    void refresh_timeline(const QString& case_dir) {
        if (!table_model_) return;
        if (timeline_running_) {
            timeline_pending_ = true;
            timeline_pending_dir_ = case_dir;
            return;
        }
        // O teto e o modo valem por caso: abrir outro caso volta ao padrao, senao um
        // caso gigante deixaria o proximo lento por causa de uma sessao anterior.
        if (timeline_cap_case_ != case_dir) {
            timeline_cap_case_ = case_dir;
            timeline_cap_ = olho::ui::kTimelineDisplayCap;
            timeline_chronological_ = false;
        }
        const olho::ui::timeline_cap_mode cap_mode =
            timeline_chronological_ ? olho::ui::timeline_cap_mode::oldest_by_time
                                    : olho::ui::timeline_cap_mode::first_in_file_order;
        const int cap = timeline_cap_;

        // Guarda a linha que o operador estava olhando: "carregar mais" nao pode
        // jogar a selecao de volta para o topo.
        timeline_selected_raw_.clear();
        if (table_ != nullptr && table_proxy_ != nullptr && table_->currentIndex().isValid()) {
            const int source_row = table_proxy_->mapToSource(table_->currentIndex()).row();
            if (source_row >= 0 && source_row < row_raw_.size()) {
                timeline_selected_raw_ = row_raw_.at(source_row);
            }
        }

        // Some com a tabela antiga e diz que esta trabalhando: uma tabela velha na
        // tela enquanto a nova carrega e pior que uma tabela vazia.
        table_model_->removeRows(0, table_model_->rowCount());
        row_raw_.clear();
        row_time_.clear();
        if (table_count_) table_count_->setText(QStringLiteral("carregando timeline..."));
        if (timeline_view_) timeline_view_->set_events({}, QStringList());
        timeline_result_ = olho::ui::timeline_load{};

        timeline_done_.store(false);
        timeline_lines_.store(0);
        timeline_running_ = true;
        if (timeline_worker_.joinable()) timeline_worker_.join();
        timeline_worker_ = std::thread([this, case_dir, cap, cap_mode]() {
            const olho::ui::timeline_progress_fn progress =
                [this](qint64 lines, qint64) { timeline_lines_.store(lines); };
            timeline_result_ =
                olho::ui::load_case_timeline(case_dir, cap, cap_mode, progress);
            timeline_done_.store(true);
        });
    }

    void poll_timeline() {
        if (!timeline_running_) return;
        if (!timeline_done_.load()) {
            // Rotulo que anda: "carregando" parado por segundos parece travamento.
            if (table_count_ != nullptr) {
                const qint64 lines = timeline_lines_.load();
                table_count_->setText(lines > 0
                    ? QStringLiteral("carregando timeline... %1 linhas").arg(lines)
                    : QStringLiteral("carregando timeline..."));
            }
            return;
        }
        if (timeline_worker_.joinable()) timeline_worker_.join();
        timeline_running_ = false;
        apply_timeline(QFileInfo(timeline_cap_case_).fileName());
        if (timeline_pending_) {
            timeline_pending_ = false;
            const QString next = timeline_pending_dir_;
            QTimer::singleShot(0, this, [this, next]() {
                if (!next.isEmpty()) refresh_timeline(next);
            });
        }
    }

    // Preenche tabela, contador, grafico e mensagens com o resultado da thread.
    // Roda no thread da interface, depois do join.
    void apply_timeline(const QString& case_id) {
        if (!table_model_) return;
        const olho::ui::timeline_load& timeline = timeline_result_;
        QVector<QDateTime> times;
        QStringList labels;
        const int cloud_files = timeline.cloud_files;
        for (const olho::ui::timeline_event& event : timeline.events) {
            QList<QStandardItem*> items;
            auto* name_item = new QStandardItem(event.provider + " / " + event.event_id);
            name_item->setIcon(icon(event.from_cloud ? "cloud.png" : "auditoria.png"));
            items << name_item;
            items << new QStandardItem(event.channel + " / record " + event.record_id);
            items << new QStandardItem(QString("%1 B").arg(event.raw.size()));
            items << new QStandardItem(event.when.isValid()
                                           ? event.when.toString("yyyy-MM-dd HH:mm:ss")
                                           : QStringLiteral("-"));
            items << new QStandardItem(event.from_cloud ? QStringLiteral("Cloud")
                                                        : QStringLiteral("Coleta"));
            table_model_->appendRow(items);
            row_raw_.append(event.raw.toUtf8());
            row_time_.append(event.when);
            times.append(event.when);
            labels.append(event.provider.left(22) + " #" + event.event_id);
        }

        if (table_count_) {
            // Numero de linhas, nao de linhas "que couberam": um caso de 200 mil
            // eventos aparece como "5000 de 200000", nunca como se fossem 5000.
            table_count_->setText(timeline.truncated
                ? QString("%1 de %2 eventos (limite de exibicao, %3)  /  %4 cloud")
                      .arg(timeline.events.size())
                      .arg(timeline.total)
                      .arg(timeline.chronological ? QStringLiteral("mais antigos")
                                                  : QStringLiteral("ordem de arquivo"))
                      .arg(cloud_files)
                : QString("%1 eventos  /  %2 cloud")
                      .arg(timeline.events.size())
                      .arg(cloud_files));
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

        append(QString("[case] timeline: %1 de %2 evento(s) em %3 arquivo(s), %4 cloud\n")
                   .arg(timeline.events.size())
                   .arg(timeline.total)
                   .arg(timeline.files)
                   .arg(cloud_files));
        if (timeline.invalid > 0) {
            append(QString("[case] %1 linha(s) de timeline nao parsearam\n").arg(timeline.invalid));
        }
        if (timeline.truncated) {
            append(timeline.chronological
                       ? "[case] corte por data: a janela mostra os eventos mais antigos\n"
                       : "[case] corte na ordem dos arquivos; use Exibir > Timeline: cortar por "
                         "data para ver os mais antigos\n");
        }
        if (timeline_more_) {
            timeline_more_->setEnabled(timeline.truncated);
            timeline_more_->setText(
                timeline.truncated
                    ? QString("Timeline: carregar mais (%1 de %2 eventos)")
                          .arg(timeline.events.size()).arg(timeline.total)
                    : QString("Timeline: carregar mais eventos"));
        }
        if (timeline_chrono_) {
            timeline_chrono_->setChecked(timeline_chronological_);
            timeline_chrono_->setText(
                timeline_chronological_ ? "Timeline: cortar pela ordem dos arquivos"
                                        : "Timeline: cortar por data (mais antigos)");
            timeline_chrono_->setEnabled(true);
        }
        if (!timeline.first_stamp.isEmpty()) {
            append("[case] periodo: " + timeline.first_stamp.left(19) + " -> " +
                   timeline.last_stamp.left(19) + "\n");
        }
        // Selecao e barra de status dependem da tabela ja preenchida, entao moram
        // aqui e nao em load_case (que agora retorna antes da carga terminar).
        // Guarda de ponteiro porque isto roda de um timer, nao do carregamento.
        int restore_row = -1;
        if (!timeline_selected_raw_.isEmpty()) {
            for (int i = 0; i < row_raw_.size(); ++i) {
                if (row_raw_.at(i) == timeline_selected_raw_) {
                    restore_row = i;
                    break;
                }
            }
        }
        if (restore_row >= 0 && table_ != nullptr && table_proxy_ != nullptr) {
            const QModelIndex proxy = table_proxy_->mapFromSource(
                table_model_->index(restore_row, 0));
            if (proxy.isValid()) {
                table_->setCurrentIndex(proxy);
                update_details(restore_row);
                if (hex_view_ && restore_row < row_raw_.size()) {
                    hex_view_->set_data(row_raw_.at(restore_row));
                }
            } else {
                restore_row = -1;
            }
        }
        timeline_selected_raw_.clear();
        if (restore_row < 0 && table_ != nullptr && table_model_->rowCount() > 0) {
            table_->selectRow(0);
            update_details(0);
            if (hex_view_ && !row_raw_.isEmpty()) hex_view_->set_data(row_raw_.first());
        }
        if (status_label_ && !case_id.isEmpty()) {
            status_label_->setText(QString("Caso %1 carregado: %2 eventos.")
                .arg(case_id).arg(table_model_->rowCount()));
        }
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
            QColor color(theme::semantic::normal());
            if (line.contains("[exit 0]") || line.contains("sucesso", Qt::CaseInsensitive))
                color = QColor(theme::semantic::ok());
            else if (line.contains("erro", Qt::CaseInsensitive) ||
                     line.contains("error", Qt::CaseInsensitive) ||
                     line.contains("falha", Qt::CaseInsensitive) ||
                     line.contains("exit code", Qt::CaseInsensitive) ||
                     (line.contains("[exit ") && !line.contains("[exit 0]")))
                color = QColor(theme::semantic::error());
            else if (line.startsWith(">"))
                color = QColor(theme::semantic::echo());
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
        QString qss = QString::fromUtf8(file.readAll());
        /* url() em QSS carregado por string resolve contra o CWD, nao contra o
           arquivo; o marcador vira caminho absoluto do exe na hora de carregar. */
        /* url() do QSS: caminho entre aspas e com barras normais, senao
           espaco/#/% no diretorio invalidam o url e o icone some calado */
        qss.replace("@APP@", QDir::fromNativeSeparators(QCoreApplication::applicationDirPath()));
        return qss;
    }
    /* falha silenciosa aqui deixava o app inteiro sem QSS e sem aviso */
    qWarning("nao foi possivel abrir %s; seguindo sem folha de estilo",
             qUtf8Printable(path));
    return style_sheet();
}
} // namespace
void MainWindow::screenshot_setup(const QString& case_dir, const QString& tab_name) {
    if (!case_dir.isEmpty()) load_case(case_dir);
    if (tab_name.isEmpty() || center_tabs_ == nullptr) return;
    for (int i = 0; i < center_tabs_->count(); ++i) {
        const QString text = center_tabs_->tabText(i);
        if (text.compare(tab_name, Qt::CaseInsensitive) == 0 ||
            text.contains(tab_name, Qt::CaseInsensitive)) {
            center_tabs_->setCurrentIndex(i);
            return;
        }
    }
    std::printf("screenshot: aba nao encontrada: %s\n", qUtf8Printable(tab_name));
}

int run_gui(HINSTANCE) {
    int argc = 1;
    char app_name[] = "OlhoDeDeus_gui";
    char* argv[] = {app_name, nullptr};
    QApplication app(argc, argv);

    // Etapa 1: modo evidencia. Abre a janela, carrega um caso, escolhe a aba,
    // espera a Timeline assentar, salva o PNG e sai. Sem isso, nenhum recurso de
    // GUI deste repositorio tem prova visual.
    QString shot_path;
    QString shot_case;
    QString shot_tab;
    int shot_delay_ms = 800;
    {
        int count = 0;
        LPWSTR* list = ::CommandLineToArgvW(::GetCommandLineW(), &count);
        if (list != nullptr) {
            for (int i = 1; i < count; ++i) {
                const QString arg = QString::fromWCharArray(list[i]);
                const bool has_value = i + 1 < count;
                if (arg == "--screenshot" && has_value) {
                    shot_path = QString::fromWCharArray(list[++i]);
                } else if (arg == "--case" && has_value) {
                    shot_case = QString::fromWCharArray(list[++i]);
                } else if (arg == "--tab" && has_value) {
                    shot_tab = QString::fromWCharArray(list[++i]);
                } else if (arg == "--delay" && has_value) {
                    shot_delay_ms = QString::fromWCharArray(list[++i]).toInt();
                }
            }
            ::LocalFree(list);
        }
    }
    const bool screenshot_mode = !shot_path.isEmpty();
    g_gui_evidence_mode = screenshot_mode;

    /* instancia unica: duas copias do app escrevem no mesmo toolbar.ini e
       layout.ini (QSettings em cache por processo) e uma desfaz a outra.
       No modo evidencia o cadeado e ignorado de proposito: fotografar uma janela
       que ja esta aberta e um uso legitimo. */
    QLockFile instance_lock(QDir::tempPath() + "/olho-de-deus-gui.lock");
    instance_lock.setStaleLockTime(30000);
    if (!screenshot_mode && !instance_lock.tryLock(300)) {
        /* ja existe uma janela: traz ela para a frente e sai (sem dialogo,
           que ficaria preso se ninguem clicasse) */
        if (HWND existing = ::FindWindowW(nullptr, L"Olho de Deus - Deep Engine")) {
            ::ShowWindow(existing, SW_RESTORE);
            ::SetForegroundWindow(existing);
        }
        return 0;
    }
    QApplication::setStyle("Fusion");
    app.setStyleSheet(load_style_sheet());
    QFont font("Segoe UI", 10);
    app.setFont(font);
    MainWindow window;
    if (!screenshot_mode) {
        window.showMaximized();
        return app.exec();
    }

    window.resize(1440, 900);
    window.show();
    // ADIADO de proposito: o construtor da janela enfileira um load_case() sem
    // argumentos (o caso mais recente de cases/) e ele roda no primeiro giro do
    // event loop. Chamar o setup agora faria o caso pedido ser sobrescrito pelo
    // automatico - foi exatamente o que aconteceu no primeiro teste.
    QTimer::singleShot(0, &window, [&window, shot_case, shot_tab]() {
        window.screenshot_setup(shot_case, shot_tab);
    });

    // Espera em passos de 100 ms: primeiro a Timeline (thread propria), depois o
    // pintor do WM. Teto de 20 s para nao pendurar um CI.
    auto* waiter = new QTimer(&window);
    auto elapsed = std::make_shared<int>(0);
    QObject::connect(waiter, &QTimer::timeout, &window,
                     [&window, waiter, elapsed, shot_path, shot_delay_ms]() {
        *elapsed += 100;
        if (window.timeline_busy() && *elapsed < 20000) return;
        // O setup entra no primeiro giro do event loop; a foto espera pelo menos
        // dois tiques depois disso.
        if (*elapsed < shot_delay_ms + 200) return;
        waiter->stop();
        const QPixmap pixmap = window.grab();
        const bool saved = !pixmap.isNull() && pixmap.save(shot_path, "PNG");
        std::printf("screenshot: %s salvo=%d %dx%d\n", qUtf8Printable(shot_path),
                    saved ? 1 : 0, pixmap.width(), pixmap.height());
        std::fflush(stdout);
        QCoreApplication::exit(saved ? 0 : 2);
    });
    waiter->start(100);
    return app.exec();
}

} // namespace deep
