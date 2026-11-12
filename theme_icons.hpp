// theme_icons.hpp — monikers monocromaticos com tint por estado (modelo do VS).
// O VS desenha os icones da command bar como glifo de uma cor so (#E6E6E6 normal,
// #FFFFFF no hover, cinza translucido quando desabilitado) sobre #262626.
// Aqui a mesma ideia: preserva o alfa do PNG e troca o RGB pelo tom do estado.
#pragma once

#include <QColor>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QString>

namespace theme {

inline QString icon_root() {
    return QCoreApplication::applicationDirPath() + "/icons/";
}

// tom do glifo por estado (medido na command bar do VS 2026)
inline QColor icon_tint_normal()   { return QColor(0xE6, 0xE6, 0xE6); }
inline QColor icon_tint_active()   { return QColor(0xFF, 0xFF, 0xFF); }
inline QColor icon_tint_disabled() { return QColor(0x8C, 0x8C, 0x8C); }

inline QPixmap tinted_pixmap(const QString& abs_path, int size, const QColor& tint, qreal opacity) {
    QImage src(abs_path);
    if (src.isNull()) return QPixmap();
    src = src.convertToFormat(QImage::Format_ARGB32);
    if (src.width() != size || src.height() != size) {
        src = src.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    // Normaliza o alfa: o pixel mais opaco do icone vira 255.
    // Sem isso o glifo sai lavado (#D4D4D4 em vez de #E6E6E6), porque os PNGs
    // carregam alfa parcial no miolo. Moniker do VS e silhueta cheia.
    // Normaliza pelo percentil 95 do alfa (nao pelo maximo: alguns icones tem
    // uma borda 100% opaca e o miolo em ~91%, o que deixava o glifo lavado).
    QVector<int> alphas;
    alphas.reserve(src.width() * src.height());
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            const int a = qAlpha(line[x]);
            if (a > 0) alphas.push_back(a);
        }
    }
    if (alphas.isEmpty()) return QPixmap();
    std::sort(alphas.begin(), alphas.end());
    const int p95 = alphas[static_cast<int>(alphas.size() * 0.95) >= alphas.size()
                               ? alphas.size() - 1
                               : static_cast<int>(alphas.size() * 0.95)];
    int max_alpha = p95 > 0 ? p95 : alphas.back();

    QImage out(src.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    const int r = tint.red(), g = tint.green(), b = tint.blue();
    const double norm = 255.0 / max_alpha;
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* in_line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* out_line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            const int a = qAlpha(in_line[x]);
            if (a == 0) { out_line[x] = 0; continue; }
            int na = static_cast<int>(a * norm);
            if (na > 255) na = 255;
            // Gamma no alfa: engrossa o traco de 1px. Sem isso o glifo de outline
            // fica em ~#D4D4D4; com 0.72 o miolo do traco chega ao tom cheio,
            // como o moniker do VS (#E6E6E6 com nucleo #FFFFFF).
            na = static_cast<int>(255.0 * std::pow(na / 255.0, 0.72));
            na = static_cast<int>(na * opacity);
            out_line[x] = qRgba(r, g, b, na);
        }
    }
    return QPixmap::fromImage(out);
}

// QIcon com os tres estados; gerado uma vez e cacheado por caminho.
inline QIcon mono_icon(const QString& rel_or_abs_path) {
    static QHash<QString, QIcon> cache;
    // Aceita "icons/x.png", "x.png" ou caminho absoluto.
    QString path = rel_or_abs_path;
    if (!(path.startsWith('/') || path.contains(':'))) {
        QString clean = path;
        if (clean.startsWith("icons/")) clean = clean.mid(6);
        const QString with_root = icon_root() + clean;
        const QString as_is = QCoreApplication::applicationDirPath() + "/" + path;
        path = QFile::exists(with_root) ? with_root : as_is;
    }
    const auto it = cache.constFind(path);
    if (it != cache.constEnd()) return it.value();

    QIcon icon;
    const QList<int> sizes{16, 20, 24, 32};
    for (int s : sizes) {
        const QPixmap normal = tinted_pixmap(path, s, icon_tint_normal(), 1.0);
        if (normal.isNull()) {
            /* cacheia o nulo: senao todo rebuild de barra refazia a resolucao
               de caminho e a tentativa de load do icone que nao existe */
            cache.insert(path, QIcon());
            return QIcon();
        }
        icon.addPixmap(normal, QIcon::Normal, QIcon::Off);
        icon.addPixmap(tinted_pixmap(path, s, icon_tint_active(), 1.0), QIcon::Active, QIcon::Off);
        icon.addPixmap(tinted_pixmap(path, s, icon_tint_active(), 1.0), QIcon::Selected, QIcon::Off);
        icon.addPixmap(tinted_pixmap(path, s, icon_tint_disabled(), 0.55), QIcon::Disabled, QIcon::Off);
    }
    cache.insert(path, icon);
    return icon;
}

}  // namespace theme
