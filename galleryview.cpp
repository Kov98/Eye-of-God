// language: C++, file: galleryview.cpp, target: Windows 11, Qt 6 Widgets
#include "galleryview.hpp"

#include "theme_vs2026.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace olho::ui {

// ---------------------------------------------------------------------------
// Media item
// ---------------------------------------------------------------------------
enum class MediaKind { Image = 0, Video = 1, Audio = 2, Other = 3 };

struct MediaItem {
    QString path;      // absolute
    QString rel;       // relative to the scanned root, for stable sorting
    QString name;      // file name only
    QString suffix;    // lower case, no dot
    MediaKind kind = MediaKind::Other;
    qint64 bytes = 0;
    QDateTime modified;

    // Filled lazily: a folder of 5.000 photos must not decode 5.000 headers on
    // the clock the user is waiting on.
    bool probed = false;
    QSize image_size;
    ExifData exif;
    QString sha256;

    // Midia efemera ("ver uma vez"): o aplicativo apaga o arquivo depois da
    // primeira visualizacao, entao ver isto na galeria e a diferenca entre
    // "uma foto qualquer" e "a prova de que essa foto existiu".
    bool view_once = false;

    // Extra fields lifted from report.json when the recovery tool wrote one.
    QHash<QString, QString> report;
};

namespace {

// ---- geometry -------------------------------------------------------------
constexpr int kThumbW = 160;
constexpr int kThumbH = 120;
constexpr int kTilePad = 6;
constexpr int kTileLabelH = 18;
constexpr int kTileW = kThumbW + kTilePad * 2;
constexpr int kTileH = kThumbH + kTilePad * 2 + kTileLabelH;
constexpr int kTileGap = 10;
constexpr int kGridPad = 14;

// ---- budgets --------------------------------------------------------------
constexpr int kThumbCacheMax = 512;          // ~39 MB of 160x120 ARGB32 worst case
constexpr int kScanBatch = 128;              // directory entries per timer tick
constexpr int kMaxIfdEntries = 256;          // hostile-input guard
constexpr qint64 kHashSlice = 4 * 1024 * 1024;
constexpr int kMaxPreviewSide = 12000;       // refuse to decode past this

// ---- formatting -----------------------------------------------------------
QString format_bytes(qint64 n) {
    if (n < 0) return QStringLiteral("-");
    if (n < 1024) return QStringLiteral("%1 B").arg(n);
    const double kb = double(n) / 1024.0;
    if (kb < 1024.0) return QStringLiteral("%1 KB").arg(QString::number(kb, 'f', 1));
    const double mb = kb / 1024.0;
    if (mb < 1024.0) return QStringLiteral("%1 MB").arg(QString::number(mb, 'f', 1));
    return QStringLiteral("%1 GB").arg(QString::number(mb / 1024.0, 'f', 2));
}

QString format_dimensions(const QSize& size) {
    if (!size.isValid() || size.isEmpty()) return QStringLiteral("-");
    return QStringLiteral("%1 x %2 px").arg(size.width()).arg(size.height());
}

QString format_stamp(const QDateTime& dt) {
    if (!dt.isValid()) return QStringLiteral("-");
    return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

// O relatorio escreve o sinalizador como booleano JSON, mas um relatorio
// editado a mao ou gerado por outra ferramenta pode trazer "true", "1" ou
// "sim". Aceitar as tres formas custa nada e evita uma marca que some.
bool flag_true(const QString& value) {
    const QString text = value.trimmed().toLower();
    return text == QStringLiteral("true") || text == QStringLiteral("1") ||
           text == QStringLiteral("sim") || text == QStringLiteral("yes");
}

// ---- media kinds ----------------------------------------------------------
MediaKind kind_for_suffix(const QString& suffix) {
    static const QStringList kImage = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"),
        QStringLiteral("jfif"), QStringLiteral("png"), QStringLiteral("webp"),
        QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("tif"),
        QStringLiteral("tiff"), QStringLiteral("ico")};
    static const QStringList kVideo = {
        QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"),
        QStringLiteral("3gp"), QStringLiteral("3g2"), QStringLiteral("mkv"),
        QStringLiteral("avi"), QStringLiteral("webm"), QStringLiteral("mpg"),
        QStringLiteral("mpeg")};
    static const QStringList kAudio = {
        QStringLiteral("opus"), QStringLiteral("ogg"), QStringLiteral("oga"),
        QStringLiteral("m4a"), QStringLiteral("mp3"), QStringLiteral("aac"),
        QStringLiteral("amr"), QStringLiteral("wav"), QStringLiteral("flac")};
    if (kImage.contains(suffix)) return MediaKind::Image;
    if (kVideo.contains(suffix)) return MediaKind::Video;
    if (kAudio.contains(suffix)) return MediaKind::Audio;
    return MediaKind::Other;
}

QString kind_label(MediaKind kind) {
    switch (kind) {
    case MediaKind::Image: return QStringLiteral("Imagem");
    case MediaKind::Video: return QStringLiteral("Video");
    case MediaKind::Audio: return QStringLiteral("Audio");
    case MediaKind::Other: return QStringLiteral("Outro");
    }
    return QStringLiteral("Outro");
}

int kind_rank(MediaKind kind) { return static_cast<int>(kind); }

// Files the recovery step leaves beside the media that are not evidence: the
// report itself and half-written download debris.
bool is_transient_sidecar(const QString& name) {
    const QString lower = name.toLower();
    if (lower == QStringLiteral("report.json")) return true;
    return lower.endsWith(QStringLiteral(".log")) || lower.endsWith(QStringLiteral(".tmp")) ||
           lower.endsWith(QStringLiteral(".part")) || lower.endsWith(QStringLiteral(".crdownload"));
}

// ---- SHA-256, sliced ------------------------------------------------------
// Hashing a 300 MB video on the GUI thread would freeze the window, so the
// digest is produced a few megabytes per event-loop turn.
class Sha256Stream {
public:
    explicit Sha256Stream(const QString& path) : file_(new QFile(path)) {}

    bool open() { return file_ && file_->open(QIODevice::ReadOnly); }

    // 1 = more work left, 0 = finished, -1 = read error.
    int pump(qint64 budget) {
        if (!file_ || !file_->isOpen()) {
            error_ = QStringLiteral("arquivo indisponivel");
            return -1;
        }
        qint64 left = budget;
        while (left > 0) {
            const QByteArray chunk = file_->read(left > 1024 * 1024 ? 1024 * 1024 : left);
            if (chunk.isEmpty()) {
                if (file_->atEnd()) return 0;
                error_ = QStringLiteral("falha de leitura");
                return -1;
            }
            hash_.addData(chunk);
            left -= chunk.size();
            if (file_->atEnd()) return 0;
        }
        return 1;
    }

    QString hex() const { return QString::fromLatin1(hash_.result().toHex()); }
    QString error() const { return error_; }

private:
    std::unique_ptr<QFile> file_;
    QCryptographicHash hash_ = QCryptographicHash(QCryptographicHash::Sha256);
    QString error_;
};

// ---- TIFF reader ----------------------------------------------------------
// Bounds-checked, byte-order aware access to the block that follows the
// "Exif\0\0" signature inside an APP1 segment.
class TiffBlock {
public:
    TiffBlock(const uchar* data, qsizetype size) : data_(data), size_(size) {
        if (data_ == nullptr || size_ < 8) return;
        if (data_[0] == 'I' && data_[1] == 'I') {
            little_ = true;
        } else if (data_[0] == 'M' && data_[1] == 'M') {
            little_ = false;
        } else {
            return;
        }
        if (!bounds(2, 2) || u16(2) != 42) return;
        ok_ = true;
    }

    bool ok() const { return ok_; }
    bool little() const { return little_; }

    // Pure range check on the block, deliberately independent of ok_: the
    // constructor needs it to validate the header it is still deciding about.
    bool bounds(qsizetype off, qsizetype len) const {
        if (data_ == nullptr || off < 0 || len < 0) return false;
        if (off > size_ || len > size_ - off) return false;
        return true;
    }

    qsizetype size() const { return size_; }
    const uchar* at(qsizetype off) const { return data_ + off; }

    quint16 u16(qsizetype off) const {
        if (!bounds(off, 2)) return 0;
        const uchar* p = data_ + off;
        return little_ ? static_cast<quint16>(p[0] | (p[1] << 8))
                       : static_cast<quint16>((p[0] << 8) | p[1]);
    }

    quint32 u32(qsizetype off) const {
        if (!bounds(off, 4)) return 0;
        const uchar* p = data_ + off;
        if (little_) {
            return static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8) |
                   (static_cast<quint32>(p[2]) << 16) | (static_cast<quint32>(p[3]) << 24);
        }
        return (static_cast<quint32>(p[0]) << 24) | (static_cast<quint32>(p[1]) << 16) |
               (static_cast<quint32>(p[2]) << 8) | static_cast<quint32>(p[3]);
    }

private:
    const uchar* data_ = nullptr;
    qsizetype size_ = 0;
    bool little_ = true;
    bool ok_ = false;
};

struct IfdEntry {
    quint16 tag = 0;
    quint16 type = 0;
    quint32 count = 0;
    qsizetype data = 0;       // absolute offset of the value bytes
    qsizetype bytes = 0;
};

int tiff_type_size(quint16 type) {
    switch (type) {
    case 1: return 1;   // BYTE
    case 2: return 1;   // ASCII
    case 3: return 2;   // SHORT
    case 4: return 4;   // LONG
    case 5: return 8;   // RATIONAL
    case 6: return 1;   // SBYTE
    case 7: return 1;   // UNDEFINED
    case 8: return 2;   // SSHORT
    case 9: return 4;   // SLONG
    case 10: return 8;  // SRATIONAL
    case 11: return 4;  // FLOAT
    case 12: return 8;  // DOUBLE
    default: return 0;
    }
}

QVector<IfdEntry> read_ifd(const TiffBlock& tiff, qsizetype offset) {
    QVector<IfdEntry> entries;
    if (!tiff.ok() || !tiff.bounds(offset, 2)) return entries;
    const quint32 count = tiff.u16(offset);
    if (count == 0 || count > kMaxIfdEntries) return entries;
    if (!tiff.bounds(offset + 2, static_cast<qsizetype>(count) * 12)) return entries;
    entries.reserve(static_cast<int>(count));
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype slot = offset + 2 + static_cast<qsizetype>(i) * 12;
        IfdEntry entry;
        entry.tag = tiff.u16(slot);
        entry.type = tiff.u16(slot + 2);
        entry.count = tiff.u32(slot + 4);
        const int unit = tiff_type_size(entry.type);
        if (unit <= 0 || entry.count == 0) continue;
        const qsizetype total = static_cast<qsizetype>(unit) * static_cast<qsizetype>(entry.count);
        if (total <= 0 || total > tiff.size()) continue;
        if (total <= 4) {
            entry.data = slot + 8;  // value is inline in the entry
        } else {
            const quint32 off = tiff.u32(slot + 8);
            if (!tiff.bounds(static_cast<qsizetype>(off), total)) continue;
            entry.data = static_cast<qsizetype>(off);
        }
        if (!tiff.bounds(entry.data, total)) continue;
        entry.bytes = total;
        entries.push_back(entry);
    }
    return entries;
}

QString ascii_value(const TiffBlock& tiff, const IfdEntry& entry) {
    if (!tiff.bounds(entry.data, entry.bytes) || entry.bytes <= 0) return QString();
    const char* raw = reinterpret_cast<const char*>(tiff.at(entry.data));
    qsizetype len = 0;
    const qsizetype cap = entry.bytes > 512 ? 512 : entry.bytes;
    while (len < cap && raw[len] != '\0') ++len;
    return QString::fromLatin1(raw, static_cast<int>(len)).trimmed();
}

QVector<double> rational_values(const TiffBlock& tiff, const IfdEntry& entry) {
    QVector<double> values;
    if (!tiff.bounds(entry.data, entry.bytes)) return values;
    const qsizetype rational_count = entry.bytes / 8;
    values.reserve(static_cast<int>(rational_count));
    for (qsizetype i = 0; i < rational_count; ++i) {
        const qsizetype off = entry.data + i * 8;
        const quint32 num = tiff.u32(off);
        const quint32 den = tiff.u32(off + 4);
        values.push_back(den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den));
    }
    return values;
}

quint32 uint_value(const TiffBlock& tiff, const IfdEntry& entry) {
    if (!tiff.bounds(entry.data, entry.bytes) || entry.bytes <= 0) return 0;
    switch (entry.type) {
    case 1:
    case 7: return static_cast<quint32>(tiff.at(entry.data)[0]);
    case 3: return tiff.u16(entry.data);
    case 4:
    case 9: return tiff.u32(entry.data);
    default: return 0;
    }
}

// Degrees + minutes + seconds -> decimal degrees, with a sanity bound so a
// corrupt triplet cannot turn into a coordinate in the wrong hemisphere.
bool dms_to_decimal(const QVector<double>& dms, double* out) {
    if (out == nullptr || dms.size() < 3) return false;
    const double deg = dms.at(0);
    const double min = dms.at(1);
    const double sec = dms.at(2);
    if (!std::isfinite(deg) || !std::isfinite(min) || !std::isfinite(sec)) return false;
    if (deg < 0.0 || deg > 180.0) return false;
    if (min < 0.0 || min > 60.0) return false;
    if (sec < 0.0 || sec > 60.0) return false;
    *out = deg + min / 60.0 + sec / 3600.0;
    return true;
}

QString orientation_label(int value) {
    switch (value) {
    case 1: return QStringLiteral("1 (normal)");
    case 2: return QStringLiteral("2 (espelhada)");
    case 3: return QStringLiteral("3 (180 graus)");
    case 4: return QStringLiteral("4 (espelhada + 180)");
    case 5: return QStringLiteral("5 (espelhada + 90)");
    case 6: return QStringLiteral("6 (90 graus CW)");
    case 7: return QStringLiteral("7 (espelhada + 270)");
    case 8: return QStringLiteral("8 (270 graus CW)");
    default: return QString::number(value);
    }
}

// Renders 1/125, 1/60, 2.5 as a shutter speed. Kept deterministic on purpose:
// the oracle compares these strings byte for byte.
QString render_exposure(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0) return QString();
    if (seconds < 1.0) {
        const double den = 1.0 / seconds;
        const double rounded = std::floor(den + 0.5);
        if (std::fabs(den - rounded) < 0.05 && rounded > 1.0) {
            return QStringLiteral("1/%1 s").arg(static_cast<qint64>(rounded));
        }
        return QStringLiteral("%1 s").arg(QString::number(seconds, 'f', 4));
    }
    return QStringLiteral("%1 s").arg(QString::number(seconds, 'f', 2));
}

QString render_aperture(double fnumber) {
    if (!std::isfinite(fnumber) || fnumber <= 0.0) return QString();
    return QStringLiteral("f/%1").arg(QString::number(fnumber, 'f', 1));
}

QString render_focal(double mm) {
    if (!std::isfinite(mm) || mm <= 0.0) return QString();
    return QStringLiteral("%1 mm").arg(QString::number(mm, 'f', 1));
}

QString render_gps_time(const QVector<double>& parts) {
    if (parts.size() < 3) return QString();
    const int h = static_cast<int>(parts.at(0));
    const int m = static_cast<int>(parts.at(1));
    const int s = static_cast<int>(parts.at(2));
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 60) return QString();
    return QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

// ---- APP1 -> ExifData -----------------------------------------------------
ExifData parse_exif_segment(const QByteArray& segment) {
    ExifData exif;
    if (segment.size() < 12) {
        exif.error = QStringLiteral("segmento APP1 curto demais");
        return exif;
    }
    if (segment.at(0) != 'E' || segment.at(1) != 'x' || segment.at(2) != 'i' ||
        segment.at(3) != 'f' || segment.at(4) != '\0' || segment.at(5) != '\0') {
        exif.error = QStringLiteral("APP1 sem assinatura Exif");
        return exif;
    }
    exif.present = true;
    const uchar* tiff_bytes = reinterpret_cast<const uchar*>(segment.constData()) + 6;
    const qsizetype tiff_size = segment.size() - 6;
    const TiffBlock tiff(tiff_bytes, tiff_size);
    if (!tiff.ok()) {
        exif.error = QStringLiteral("cabecalho TIFF invalido");
        return exif;
    }
    exif.little_endian = tiff.little();

    const QVector<IfdEntry> ifd0 = read_ifd(tiff, static_cast<qsizetype>(tiff.u32(4)));
    if (ifd0.isEmpty()) {
        exif.error = QStringLiteral("IFD0 ilegivel");
        return exif;
    }

    qsizetype exif_ifd = -1;
    qsizetype gps_ifd = -1;
    for (const IfdEntry& entry : ifd0) {
        switch (entry.tag) {
        case 0x010F: exif.make = ascii_value(tiff, entry); break;
        case 0x0110: exif.model = ascii_value(tiff, entry); break;
        case 0x0131: exif.software = ascii_value(tiff, entry); break;
        case 0x0132: exif.date_time = ascii_value(tiff, entry); break;
        case 0x0112: exif.orientation = static_cast<int>(uint_value(tiff, entry)); break;
        case 0x8769: exif_ifd = static_cast<qsizetype>(uint_value(tiff, entry)); break;
        case 0x8825: gps_ifd = static_cast<qsizetype>(uint_value(tiff, entry)); break;
        default: break;
        }
    }

    if (exif_ifd >= 8 && tiff.bounds(exif_ifd, 2)) {
        for (const IfdEntry& entry : read_ifd(tiff, exif_ifd)) {
            switch (entry.tag) {
            case 0x9003: exif.date_time_original = ascii_value(tiff, entry); break;
            case 0x9004: exif.date_time_digitized = ascii_value(tiff, entry); break;
            case 0xA002: exif.pixel_width = static_cast<int>(uint_value(tiff, entry)); break;
            case 0xA003: exif.pixel_height = static_cast<int>(uint_value(tiff, entry)); break;
            case 0x8827: exif.iso = static_cast<int>(uint_value(tiff, entry)); break;
            case 0x829A: {
                const QVector<double> v = rational_values(tiff, entry);
                if (!v.isEmpty()) exif.exposure_time = render_exposure(v.at(0));
                break;
            }
            case 0x829D: {
                const QVector<double> v = rational_values(tiff, entry);
                if (!v.isEmpty()) exif.f_number = render_aperture(v.at(0));
                break;
            }
            case 0x920A: {
                const QVector<double> v = rational_values(tiff, entry);
                if (!v.isEmpty()) exif.focal_length = render_focal(v.at(0));
                break;
            }
            default: break;
            }
        }
    }

    if (gps_ifd >= 8 && tiff.bounds(gps_ifd, 2)) {
        QVector<double> lat;
        QVector<double> lon;
        bool alt_ok = false;
        double alt = 0.0;
        int alt_ref = 0;
        for (const IfdEntry& entry : read_ifd(tiff, gps_ifd)) {
            switch (entry.tag) {
            case 0x0001: exif.latitude_ref = ascii_value(tiff, entry); break;
            case 0x0002: lat = rational_values(tiff, entry); break;
            case 0x0003: exif.longitude_ref = ascii_value(tiff, entry); break;
            case 0x0004: lon = rational_values(tiff, entry); break;
            case 0x0005: alt_ref = static_cast<int>(uint_value(tiff, entry)); break;
            case 0x0006: {
                const QVector<double> v = rational_values(tiff, entry);
                if (!v.isEmpty() && std::isfinite(v.at(0))) {
                    alt = v.at(0);
                    alt_ok = true;
                }
                break;
            }
            case 0x0007: exif.gps_time_stamp = render_gps_time(rational_values(tiff, entry)); break;
            case 0x001D: exif.gps_date_stamp = ascii_value(tiff, entry); break;
            default: break;
            }
        }
        double value = 0.0;
        if (dms_to_decimal(lat, &value)) {
            // The hemisphere lives in the ref tag, not in the sign of the triplet.
            exif.latitude = exif.latitude_ref.compare(QStringLiteral("S"), Qt::CaseInsensitive) == 0 ? -value : value;
            exif.has_latitude = true;
        }
        if (dms_to_decimal(lon, &value)) {
            exif.longitude = exif.longitude_ref.compare(QStringLiteral("W"), Qt::CaseInsensitive) == 0 ? -value : value;
            exif.has_longitude = true;
        }
        if (alt_ok) {
            exif.altitude = alt;
            exif.below_sea_level = alt_ref == 1;
            exif.has_altitude = true;
        }
    }

    exif.valid = true;
    return exif;
}

ExifData exif_from_jpeg(const QString& path) {
    ExifData exif;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        exif.error = QStringLiteral("nao foi possivel abrir o arquivo");
        return exif;
    }

    uchar soi[2] = {0, 0};
    if (file.read(reinterpret_cast<char*>(soi), 2) != 2 || soi[0] != 0xFF || soi[1] != 0xD8) {
        exif.error = QStringLiteral("nao e JPEG (sem marcador SOI)");
        return exif;
    }

    // Walk marker segments only. The entropy-coded pixel data after SOS is never
    // touched, so a 40 MB photo costs a handful of reads.
    for (int guard = 0; guard < 512; ++guard) {
        uchar marker[2] = {0, 0};
        if (file.read(reinterpret_cast<char*>(marker), 2) != 2) break;
        if (marker[0] != 0xFF) break;  // out of sync: stop rather than guess
        uchar code = marker[1];
        while (code == 0xFF) {
            uchar next = 0;
            if (file.read(reinterpret_cast<char*>(&next), 1) != 1) {
                code = 0xD9;  // ran out of bytes inside a fill run: treat as EOI
                break;
            }
            code = next;
        }
        if (code == 0xD9 || code == 0xDA) break;                 // EOI / SOS
        if (code == 0x01 || (code >= 0xD0 && code <= 0xD7)) continue;  // standalone

        uchar length_bytes[2] = {0, 0};
        if (file.read(reinterpret_cast<char*>(length_bytes), 2) != 2) break;
        const quint32 segment_length =
            (static_cast<quint32>(length_bytes[0]) << 8) | static_cast<quint32>(length_bytes[1]);
        if (segment_length < 2 || segment_length > 65535) break;
        const qsizetype payload = static_cast<qsizetype>(segment_length) - 2;

        if (code == 0xE1) {
            const QByteArray segment = file.read(payload);
            if (segment.size() != payload) {
                exif.present = true;
                exif.error = QStringLiteral("segmento APP1 truncado");
                return exif;
            }
            if (segment.size() >= 6 && segment.at(0) == 'E' && segment.at(1) == 'x' &&
                segment.at(2) == 'i' && segment.at(3) == 'f' && segment.at(4) == '\0' &&
                segment.at(5) == '\0') {
                return parse_exif_segment(segment);
            }
            continue;  // APP1 carrying something else (XMP, MPF, JUMBF...)
        }
        if (!file.seek(file.pos() + payload)) break;
    }

    if (!exif.present) exif.error = QStringLiteral("sem segmento APP1/Exif");
    return exif;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public EXIF / hashing surface
// ---------------------------------------------------------------------------
ExifData read_exif_jpeg(const QString& path) { return exif_from_jpeg(path); }

QString file_sha256(const QString& path) {
    Sha256Stream stream(path);
    if (!stream.open()) return QString();
    for (int guard = 0; guard < 1000000; ++guard) {
        const int state = stream.pump(kHashSlice);
        if (state == 0) return stream.hex();
        if (state < 0) return QString();
    }
    return QString();
}

QString exif_datetime_iso(const QString& exif_stamp) {
    const QString trimmed = exif_stamp.trimmed();
    if (trimmed.size() < 19) return QString();
    QString text = trimmed.left(19);
    // EXIF writes YYYY:MM:DD HH:MM:SS. Only the date separators are rewritten;
    // the time separators stay as colons, which is what the panel shows.
    for (int i = 0; i < 19; ++i) {
        const char16_t c = text.at(i).unicode();
        if (i == 4 || i == 7) {
            text[i] = QLatin1Char('-');
            continue;
        }
        if (i == 10) {
            if (c != u' ' && c != u'T') return QString();
            text[i] = QLatin1Char(' ');
            continue;
        }
        if (i == 13 || i == 16) {
            if (c != u':') return QString();
            continue;
        }
        if (c < u'0' || c > u'9') return QString();
    }
    return text;
}

QVector<std::pair<QString, QString>> exif_rows(const ExifData& exif) {
    QVector<std::pair<QString, QString>> rows;
    const auto add = [&rows](const QString& label, const QString& value) {
        if (value.isEmpty()) return;
        rows.push_back(std::make_pair(label, value));
    };

    add(QStringLiteral("Make"), exif.make);
    add(QStringLiteral("Model"), exif.model);
    add(QStringLiteral("Software"), exif.software);
    if (exif.orientation > 0) add(QStringLiteral("Orientation"), orientation_label(exif.orientation));
    add(QStringLiteral("DateTimeOriginal"), exif.date_time_original);
    add(QStringLiteral("DateTimeDigitized"), exif.date_time_digitized);
    add(QStringLiteral("DateTime"), exif.date_time);
    if (exif.pixel_width > 0 && exif.pixel_height > 0) {
        add(QStringLiteral("PixelXDimension"), QString::number(exif.pixel_width));
        add(QStringLiteral("PixelYDimension"), QString::number(exif.pixel_height));
    }
    if (exif.iso > 0) add(QStringLiteral("ISOSpeedRatings"), QString::number(exif.iso));
    add(QStringLiteral("ExposureTime"), exif.exposure_time);
    add(QStringLiteral("FNumber"), exif.f_number);
    add(QStringLiteral("FocalLength"), exif.focal_length);

    add(QStringLiteral("GPSLatitudeRef"), exif.latitude_ref);
    add(QStringLiteral("GPSLongitudeRef"), exif.longitude_ref);
    if (exif.has_latitude) {
        add(QStringLiteral("GPSLatitude"), QString::number(exif.latitude, 'f', 6));
    }
    if (exif.has_longitude) {
        add(QStringLiteral("GPSLongitude"), QString::number(exif.longitude, 'f', 6));
    }
    if (exif.has_altitude) {
        const double magnitude = std::fabs(exif.altitude);
        const QString text = QString::number(magnitude, 'f', 1) + QStringLiteral(" m");
        add(QStringLiteral("GPSAltitude"), exif.below_sea_level ? QStringLiteral("-") + text : text);
    }
    add(QStringLiteral("GPSDateStamp"), exif.gps_date_stamp);
    add(QStringLiteral("GPSTimeStamp"), exif.gps_time_stamp);
    return rows;
}

// ---------------------------------------------------------------------------
// Small painting helpers shared by the grid and the zoom view
// ---------------------------------------------------------------------------
namespace {

constexpr double kZoomMin = 0.02;
constexpr double kZoomMax = 32.0;

void draw_image_glyph(QPainter& painter, const QRect& rect, const QColor& color) {
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, 1.4));
    painter.drawRoundedRect(rect, 3, 3);
    const double cx = rect.left() + rect.width() * 0.29;
    const double cy = rect.top() + rect.height() * 0.33;
    painter.drawEllipse(QPointF(cx, cy), 3.4, 3.4);
    QPainterPath path;
    path.moveTo(rect.left() + 4.0, rect.bottom() - 5.0);
    path.lineTo(rect.left() + rect.width() * 0.40, rect.top() + rect.height() * 0.52);
    path.lineTo(rect.left() + rect.width() * 0.63, rect.bottom() - 5.0);
    painter.drawPath(path);
    painter.restore();
}

void draw_card(QPainter& painter, const QRect& card) {
    painter.save();
    painter.setPen(QPen(theme::selectionBorder(), 1));
    painter.setBrush(theme::surface());
    painter.drawRoundedRect(card, 4, 4);
    painter.restore();
}

QString badge_text(const MediaItem& item) {
    if (item.kind == MediaKind::Image) return QStringLiteral("FALHA");
    if (item.suffix.isEmpty()) return QStringLiteral("ARQ");
    return item.suffix.left(4).toUpper();
}

}  // namespace

// ---------------------------------------------------------------------------
// Thumbnail grid
//
// Painted, not a QListWidget: a folder with thousands of photos would otherwise
// allocate one item plus one icon per file. Here only the tiles inside the
// exposed rect are decoded, and GalleryView::Impl caps how many thumbnails stay
// resident.
// ---------------------------------------------------------------------------
class ThumbGrid : public QWidget {
public:
    explicit ThumbGrid(QWidget* parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    // Data is borrowed from GalleryView::Impl; the grid never owns it.
    const QVector<MediaItem>* items = nullptr;
    const QVector<int>* visible = nullptr;
    QScrollArea* scroll = nullptr;

    int current = -1;
    QString empty_title;
    QString empty_hint;
    bool scanning = false;
    QString scanning_hint;

    std::function<QPixmap(const MediaItem&)> thumb_provider;
    std::function<void(int)> on_activate;
    std::function<void(int)> on_current;

    int tile_count() const {
        if (items == nullptr || visible == nullptr) return 0;
        return static_cast<int>(visible->size());
    }

    void reload(bool keep_scroll) {
        const int previous = (scroll != nullptr && keep_scroll) ? scroll->verticalScrollBar()->value() : 0;
        recompute_columns();
        apply_height();
        if (scroll != nullptr) {
            if (keep_scroll) {
                scroll->verticalScrollBar()->setValue(previous);
            } else {
                scroll->verticalScrollBar()->setValue(0);
            }
        }
        update();
    }

    void set_current(int index, bool scroll_to) {
        current = index;
        if (scroll_to && index >= 0 && index < tile_count()) {
            const QRect rect = tile_rect(index);
            if (scroll != nullptr) scroll->ensureVisible(rect.center().x(), rect.center().y(), 60, 60);
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.fillRect(event->rect(), theme::document());

        const int count = tile_count();
        if (count == 0) {
            paint_empty(painter);
            return;
        }

        const QRect clip = event->rect();
        const int row_height = kTileH + kTileGap;
        const int first_row = qMax(0, (clip.top() - kGridPad) / row_height);
        const int last_row = qMax(0, (clip.bottom() - kGridPad) / row_height);
        for (int row = first_row; row <= last_row; ++row) {
            for (int column = 0; column < columns_; ++column) {
                const int index = row * columns_ + column;
                if (index >= count) return;
                const QRect rect = tile_rect(index);
                if (rect.intersects(clip)) paint_tile(painter, index, rect);
            }
        }
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        recompute_columns();
        apply_height();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            const int index = index_at(event->pos());
            if (index >= 0) {
                setFocus(Qt::MouseFocusReason);
                set_current(index, false);
                if (on_activate) on_activate(index);
                event->accept();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const int index = index_at(event->pos());
        if (index != hover_) {
            hover_ = index;
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        if (hover_ != -1) {
            hover_ = -1;
            update();
        }
        QWidget::leaveEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        const int count = tile_count();
        if (count == 0) {
            QWidget::keyPressEvent(event);
            return;
        }
        int index = current < 0 ? 0 : current;
        switch (event->key()) {
        case Qt::Key_Left: index = qMax(0, index - 1); break;
        case Qt::Key_Right: index = qMin(count - 1, index + 1); break;
        case Qt::Key_Up: index = qMax(0, index - columns_); break;
        case Qt::Key_Down: index = qMin(count - 1, index + columns_); break;
        case Qt::Key_Home: index = 0; break;
        case Qt::Key_End: index = count - 1; break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (on_activate && current >= 0) on_activate(current);
            event->accept();
            return;
        default:
            QWidget::keyPressEvent(event);
            return;
        }
        if (on_current) on_current(index);
        event->accept();
    }

    void focusInEvent(QFocusEvent* event) override {
        QWidget::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent* event) override {
        QWidget::focusOutEvent(event);
        update();
    }

private:
    int columns_ = 1;
    int hover_ = -1;

    void recompute_columns() {
        const int usable = width() - kGridPad * 2 + kTileGap;
        columns_ = usable <= 0 ? 1 : qMax(1, usable / (kTileW + kTileGap));
    }

    void apply_height() {
        const int count = tile_count();
        const int rows = count == 0 ? 0 : (count + columns_ - 1) / columns_;
        const int content = rows * (kTileH + kTileGap) - kTileGap + kGridPad * 2;
        setMinimumHeight(content > 0 ? content : 0);
    }

    QRect tile_rect(int index) const {
        const int column = columns_ > 0 ? index % columns_ : 0;
        const int row = columns_ > 0 ? index / columns_ : 0;
        return QRect(kGridPad + column * (kTileW + kTileGap),
                     kGridPad + row * (kTileH + kTileGap), kTileW, kTileH);
    }

    int index_at(const QPoint& position) const {
        const int count = tile_count();
        if (count == 0 || columns_ <= 0) return -1;
        const int row_height = kTileH + kTileGap;
        const int column_width = kTileW + kTileGap;
        const int x = position.x() - kGridPad;
        const int y = position.y() - kGridPad;
        if (x < 0 || y < 0) return -1;
        const int column = x / column_width;
        const int row = y / row_height;
        if (column >= columns_) return -1;
        const int index = row * columns_ + column;
        if (index < 0 || index >= count) return -1;
        // Reject the gap between tiles so hover does not flicker.
        if (!tile_rect(index).contains(position)) return -1;
        return index;
    }

    void paint_tile(QPainter& painter, int index, const QRect& rect) const {
        if (items == nullptr || visible == nullptr) return;
        const int count = static_cast<int>(visible->size());
        if (index < 0 || index >= count) return;
        const int item_index = visible->at(index);
        if (item_index < 0 || item_index >= static_cast<int>(items->size())) return;
        const MediaItem& item = items->at(item_index);

        const bool is_current = index == current;
        const bool is_hover = index == hover_;

        QColor background = theme::gridAlt();
        if (is_hover) background = theme::hoverSoft();
        if (is_current) background = theme::hoverRow();
        painter.fillRect(rect, background);
        if (is_current) {
            painter.setPen(QPen(hasFocus() ? theme::accent() : theme::selectionBorder(), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(rect.adjusted(0, 0, -1, -1));
        }

        const QRect thumb_rect(rect.x() + kTilePad, rect.y() + kTilePad, kThumbW, kThumbH);
        const QPixmap thumb = thumb_provider ? thumb_provider(item) : QPixmap();
        if (thumb.isNull()) {
            paint_badge(painter, thumb_rect, badge_text(item));
        } else {
            if (thumb.hasAlphaChannel()) {
                painter.save();
                painter.setClipRect(thumb_rect);
                const int step = 8;
                for (int y = thumb_rect.top(); y < thumb_rect.bottom(); y += step) {
                    for (int x = thumb_rect.left(); x < thumb_rect.right(); x += step) {
                        const bool even = ((x - thumb_rect.left()) / step + (y - thumb_rect.top()) / step) % 2 == 0;
                        painter.fillRect(QRect(x, y, step, step), even ? theme::gridAlt() : theme::document());
                    }
                }
                painter.restore();
            }
            const QSizeF logical = thumb.deviceIndependentSize();
            const QRect target(static_cast<int>(thumb_rect.center().x() - logical.width() / 2.0),
                               static_cast<int>(thumb_rect.center().y() - logical.height() / 2.0),
                               static_cast<int>(logical.width()), static_cast<int>(logical.height()));
            painter.drawPixmap(target, thumb);
        }

        QFont name_font = font();
        name_font.setPixelSize(11);
        painter.setFont(name_font);
        painter.setPen(is_current ? theme::textPrimary() : theme::textSecondary());
        const QFontMetrics metrics(name_font);
        const QString label = metrics.elidedText(item.name, Qt::ElideMiddle, kThumbW);
        const QRect label_rect(rect.x() + kTilePad, rect.bottom() - kTileLabelH + 1, kThumbW, kTileLabelH - 2);
        painter.drawText(label_rect, Qt::AlignHCenter | Qt::AlignVCenter, label);

        // Midia efemera: a marca fica por cima da miniatura e nao depende de ela
        // ter decodificado, porque um video "ver uma vez" nunca mostra quadro.
        if (item.view_once) paint_view_once(painter, thumb_rect);
    }

    // Etiqueta de "ver uma vez". Vermelho do tema semantico, texto preto: em
    // #F44336 o preto rende contraste melhor que o branco e nao compete com o
    // roxo do accent, que nesta tela significa selecao.
    void paint_view_once(QPainter& painter, const QRect& thumb_rect) const {
        const QString text = QStringLiteral("VER 1X");
        QFont badge = font();
        badge.setPixelSize(9);
        badge.setBold(true);
        const QFontMetrics metrics(badge);
        const int width = metrics.horizontalAdvance(text) + 10;
        const QRect box(thumb_rect.right() - width - 3, thumb_rect.top() + 3, width, 14);
        painter.fillRect(box, theme::semantic::error());
        painter.setFont(badge);
        painter.setPen(QColor(0, 0, 0));
        painter.drawText(box, Qt::AlignCenter, text);
    }

    void paint_badge(QPainter& painter, const QRect& rect, const QString& text) const {
        const QRect box(rect.center().x() - 34, rect.center().y() - 15, 68, 30);
        painter.setPen(QPen(theme::selectionBorder(), 1));
        painter.setBrush(theme::surface());
        painter.drawRoundedRect(box, 3, 3);
        QFont badge = font();
        badge.setPixelSize(11);
        badge.setBold(true);
        painter.setFont(badge);
        painter.setPen(theme::textSecondary());
        painter.drawText(box, Qt::AlignCenter, text);
    }

    void paint_empty(QPainter& painter) const {
        const QString title = scanning ? QStringLiteral("Varrendo a pasta...") : empty_title;
        const QString hint = scanning ? scanning_hint : empty_hint;

        const int card_width = qBound(360, width() - 64, 560);
        const int card_height = 176;
        const QRect card((width() - card_width) / 2, qMax(20, (height() - card_height) / 2),
                         card_width, card_height);
        draw_card(painter, card);

        const QRect glyph(card.center().x() - 22, card.top() + 24, 44, 34);
        draw_image_glyph(painter, glyph, theme::textSecondary());

        QFont title_font = font();
        title_font.setPixelSize(13);
        title_font.setBold(true);
        painter.setFont(title_font);
        painter.setPen(theme::textPrimary());
        painter.drawText(QRect(card.left() + 20, glyph.bottom() + 16, card.width() - 40, 20),
                         Qt::AlignHCenter | Qt::AlignTop, title);

        QFont hint_font = font();
        hint_font.setPixelSize(11);
        painter.setFont(hint_font);
        painter.setPen(theme::textSecondary());
        painter.drawText(QRect(card.left() + 28, glyph.bottom() + 42, card.width() - 56,
                               card.bottom() - glyph.bottom() - 52),
                         Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, hint);
    }
};

// ---------------------------------------------------------------------------
// Zoom / pan inspection view
// ---------------------------------------------------------------------------
class ZoomView : public QWidget {
public:
    explicit ZoomView(QWidget* parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setCursor(Qt::OpenHandCursor);
    }

    std::function<void()> on_back;
    std::function<void(int)> on_step;
    bool view_once_ = false;   // midia efemera em exibicao

    void set_image(const QImage& image, const QString& caption) {
        image_ = image;
        caption_ = caption;
        message_.clear();
        fit_to_window();
        setCursor(Qt::OpenHandCursor);
        update();
    }

    // Midia efemera: a vista ampliada e onde o perito decide o que aquele
    // arquivo significa, entao a marca tem que estar la tambem - nao so na
    // miniatura e no painel.
    void set_ephemeral(bool on) {
        view_once_ = on;
        update();
    }

    void set_message(const QString& caption, const QString& message) {
        image_ = QImage();
        caption_ = caption;
        message_ = message;
        pan_ = QPointF();
        scale_ = 1.0;
        setCursor(Qt::ArrowCursor);
        update();
    }

    void clear() {
        image_ = QImage();
        caption_.clear();
        message_.clear();
        pan_ = QPointF();
        scale_ = 1.0;
        update();
    }

    void fit_to_window() {
        if (image_.isNull()) {
            scale_ = 1.0;
            pan_ = QPointF();
            return;
        }
        const double fit_w = double(qMax(1, width() - 32)) / double(qMax(1, image_.width()));
        const double fit_h = double(qMax(1, height() - 32)) / double(qMax(1, image_.height()));
        scale_ = qMin(1.0, qMin(fit_w, fit_h));
        if (scale_ <= 0.0) scale_ = 1.0;
        const QSizeF size(image_.width() * scale_, image_.height() * scale_);
        pan_ = QPointF((width() - size.width()) / 2.0, (height() - size.height()) / 2.0);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.fillRect(event->rect(), theme::document());

        if (image_.isNull()) {
            paint_message(painter);
            paint_ephemeral(painter);
            return;
        }

        const QRectF target = image_rect();
        const QRectF visible = target.intersected(QRectF(event->rect()));
        if (!visible.isEmpty() && scale_ > 0.0) {
            const double source_x = (visible.x() - target.x()) / scale_;
            const double source_y = (visible.y() - target.y()) / scale_;
            const double source_w = visible.width() / scale_;
            const double source_h = visible.height() / scale_;
            painter.save();
            painter.setClipRect(visible);
            // At or past 1:1 the examiner is looking at pixels, so do not smear
            // them with interpolation; below that, smooth beats aliasing.
            painter.setRenderHint(QPainter::SmoothPixmapTransform, scale_ < 1.0);
            painter.drawImage(visible, image_, QRectF(source_x, source_y, source_w, source_h));
            painter.restore();
            painter.setPen(QPen(theme::selectionBorder(), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(target.adjusted(0.5, 0.5, -0.5, -0.5));
        }

        paint_caption(painter);
        paint_ephemeral(painter);
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (!image_.isNull()) {
            fit_to_window();
            update();
        }
    }

    void wheelEvent(QWheelEvent* event) override {
        if (image_.isNull()) {
            QWidget::wheelEvent(event);
            return;
        }
        const int delta = event->angleDelta().y();
        if (delta == 0) {
            QWidget::wheelEvent(event);
            return;
        }
        zoom_at(event->position(), std::pow(1.0018, double(delta)));
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && !image_.isNull()) {
            dragging_ = true;
            drag_from_ = event->position();
            pan_from_ = pan_;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_) {
            pan_ = pan_from_ + (event->position() - drag_from_);
            clamp_pan();
            update();
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (dragging_ && event->button() == Qt::LeftButton) {
            dragging_ = false;
            setCursor(Qt::OpenHandCursor);
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            fit_to_window();
            update();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        switch (event->key()) {
        case Qt::Key_Escape:
            if (on_back) on_back();
            event->accept();
            return;
        case Qt::Key_Left:
            if (on_step) on_step(-1);
            event->accept();
            return;
        case Qt::Key_Right:
            if (on_step) on_step(1);
            event->accept();
            return;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            zoom_at(QPointF(width() / 2.0, height() / 2.0), 1.25);
            event->accept();
            return;
        case Qt::Key_Minus:
            zoom_at(QPointF(width() / 2.0, height() / 2.0), 0.8);
            event->accept();
            return;
        case Qt::Key_0:
            fit_to_window();
            update();
            event->accept();
            return;
        default:
            break;
        }
        switch (event->key()) {
        case Qt::Key_Up:
            pan_ += QPointF(0.0, 48.0);
            clamp_pan();
            update();
            event->accept();
            return;
        case Qt::Key_Down:
            pan_ += QPointF(0.0, -48.0);
            clamp_pan();
            update();
            event->accept();
            return;
        default:
            break;
        }
        QWidget::keyPressEvent(event);
    }

private:
    QString caption_;
    QString message_;
    QImage image_;
    double scale_ = 1.0;
    QPointF pan_ = QPointF(0.0, 0.0);
    bool dragging_ = false;
    QPointF drag_from_;
    QPointF pan_from_;

    QRectF image_rect() const {
        return QRectF(pan_, QSizeF(image_.width() * scale_, image_.height() * scale_));
    }

    void clamp_pan() {
        if (image_.isNull()) {
            pan_ = QPointF();
            return;
        }
        const QSizeF size(image_.width() * scale_, image_.height() * scale_);
        const double w = double(width());
        const double h = double(height());
        if (size.width() <= w) {
            pan_.setX((w - size.width()) / 2.0);
        } else {
            pan_.setX(qBound(w - size.width(), pan_.x(), 0.0));
        }
        if (size.height() <= h) {
            pan_.setY((h - size.height()) / 2.0);
        } else {
            pan_.setY(qBound(h - size.height(), pan_.y(), 0.0));
        }
    }

    void zoom_at(const QPointF& anchor, double factor) {
        if (image_.isNull() || factor <= 0.0) return;
        const double next = qBound(kZoomMin, scale_ * factor, kZoomMax);
        if (std::fabs(next - scale_) < 1e-9) return;
        const QPointF image_point((anchor.x() - pan_.x()) / scale_,
                                  (anchor.y() - pan_.y()) / scale_);
        scale_ = next;
        pan_ = QPointF(anchor.x() - image_point.x() * scale_,
                       anchor.y() - image_point.y() * scale_);
        clamp_pan();
        update();
    }

    // Etiqueta de midia efemera na vista ampliada. Fica no canto inferior
    // direito, ao lado da legenda, e nao sobre a imagem: um selo em cima do
    // pixel que o perito esta medindo nao e marcacao, e obstrucao.
    void paint_ephemeral(QPainter& painter) {
        if (!view_once_) return;
        const QString text = QStringLiteral("MIDIA EFEMERA (VER UMA VEZ)");
        QFont tag = font();
        tag.setPixelSize(10);
        tag.setBold(true);
        const QFontMetrics metrics(tag);
        const int box_width = qMin(metrics.horizontalAdvance(text) + 16, width() - 24);
        const QRect pill(qMax(12, width() - box_width - 12), height() - 30, box_width, 20);
        painter.setFont(tag);
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme::semantic::error());
        painter.drawRoundedRect(pill, 3, 3);
        painter.setPen(QColor(0, 0, 0));
        painter.drawText(pill, Qt::AlignCenter,
                         metrics.elidedText(text, Qt::ElideRight, pill.width() - 12));
    }

    void paint_caption(QPainter& painter) {
        if (caption_.isEmpty()) return;
        const QFont caption_font = [] {
            QFont font;
            font.setPixelSize(11);
            return font;
        }();
        painter.setFont(caption_font);
        const QString text = QStringLiteral("%1   -   %2%")
                                 .arg(caption_)
                                 .arg(QString::number(scale_ * 100.0, 'f', 0));
        const QFontMetrics metrics(caption_font);
        const int text_width = metrics.horizontalAdvance(text);
        const QRect pill(12, height() - 30, qMin(text_width + 20, width() - 24), 20);
        QColor background(theme::kOutputPane);
        background.setAlpha(215);
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(pill, 3, 3);
        painter.setPen(theme::textSecondary());
        painter.drawText(pill.adjusted(10, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         metrics.elidedText(text, Qt::ElideMiddle, pill.width() - 18));
    }

    void paint_message(QPainter& painter) {
        const QString title = caption_.isEmpty() ? QStringLiteral("Nada selecionado") : caption_;
        const QString hint = message_;
        const int card_width = qBound(360, width() - 64, 560);
        const int card_height = 152;
        const QRect card((width() - card_width) / 2, qMax(20, (height() - card_height) / 2),
                         card_width, card_height);
        draw_card(painter, card);

        const QRect glyph(card.center().x() - 22, card.top() + 22, 44, 34);
        draw_image_glyph(painter, glyph, theme::textSecondary());

        QFont title_font = font();
        title_font.setPixelSize(13);
        title_font.setBold(true);
        painter.setFont(title_font);
        painter.setPen(theme::textPrimary());
        painter.drawText(QRect(card.left() + 20, glyph.bottom() + 14, card.width() - 40, 20),
                         Qt::AlignHCenter | Qt::AlignTop,
                         QFontMetrics(title_font).elidedText(title, Qt::ElideMiddle, card.width() - 40));

        QFont hint_font = font();
        hint_font.setPixelSize(11);
        painter.setFont(hint_font);
        painter.setPen(theme::textSecondary());
        painter.drawText(QRect(card.left() + 28, glyph.bottom() + 40, card.width() - 56,
                               card.bottom() - glyph.bottom() - 50),
                         Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, hint);
    }
};

// ---------------------------------------------------------------------------
// Panel helpers
// ---------------------------------------------------------------------------
namespace {

bool is_jpeg_suffix(const QString& suffix) {
    return suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg") ||
           suffix == QStringLiteral("jpe") || suffix == QStringLiteral("jfif");
}

void clear_layout(QLayout* layout) {
    if (layout == nullptr) return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            delete widget;
        }
        delete item;
    }
}

// A 64 character hex digest has no place to wrap, and a label that cannot wrap
// forces the whole side panel wider than its viewport. Two 32 character lines
// keep the panel narrow and the value exact.
QString split_digest(const QString& digest) {
    if (digest.size() <= 32) return digest;
    return digest.left(32) + QStringLiteral("\n") + digest.mid(32);
}

const QString& key_style() {
    static const QString style = QStringLiteral("color:%1;").arg(theme::kTextSecondary);
    return style;
}

const QString& value_style() {
    static const QString style = QStringLiteral("color:%1;").arg(theme::kTextPrimary);
    return style;
}

QLabel* make_key_label(const QString& text) {
    auto* label = new QLabel(text);
    QFont font = label->font();
    font.setPixelSize(11);
    label->setFont(font);
    label->setStyleSheet(key_style());
    label->setAlignment(Qt::AlignRight | Qt::AlignTop);
    // Wide enough for the longest tag the parser emits (DateTimeOriginal), so
    // the right-aligned text never gets cut on its left edge.
    label->setFixedWidth(108);
    return label;
}

QLabel* make_value_label(const QString& text = QStringLiteral("-")) {
    auto* label = new QLabel(text);
    QFont font = label->font();
    font.setPixelSize(11);
    label->setFont(font);
    label->setStyleSheet(value_style());
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return label;
}

QLabel* add_field_row(QGridLayout* grid, int row, const QString& key) {
    QLabel* value = make_value_label();
    if (grid != nullptr) {
        grid->addWidget(make_key_label(key), row, 0, Qt::AlignTop);
        grid->addWidget(value, row, 1, Qt::AlignTop);
    }
    return value;
}

QLabel* make_section_header(const QString& text) {
    auto* label = new QLabel(text);
    QFont font = label->font();
    font.setPixelSize(10);
    font.setBold(true);
    label->setFont(font);
    label->setStyleSheet(QStringLiteral("color:%1;").arg(theme::kTextMuted));
    return label;
}

QString scrollbar_qss() {
    return QString(
               "QScrollBar:vertical{background:%1; width:14px; margin:0; border:none;}"
               "QScrollBar::handle:vertical{background:%2; min-height:28px; border:none;}"
               "QScrollBar::handle:vertical:hover{background:%3;}"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical{height:0; width:0;}"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical{background:transparent;}"
               "QScrollBar:horizontal{background:%1; height:14px; margin:0; border:none;}"
               "QScrollBar::handle:horizontal{background:%2; min-width:28px; border:none;}"
               "QScrollBar::handle:horizontal:hover{background:%3;}"
               "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal{height:0; width:0;}"
               "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal{background:transparent;}")
        .arg(theme::kDocument, theme::kSelectionBorder, theme::kLineSoft);
}

}  // namespace

// ---------------------------------------------------------------------------
// GalleryView::Impl
// ---------------------------------------------------------------------------
struct GalleryView::Impl {
    GalleryView* q = nullptr;

    // data
    QVector<MediaItem> items;
    QVector<int> visible;
    QHash<QString, QPixmap> thumbs;                  // path -> thumb (null = decode failed)
    QStringList thumb_order;                         // LRU of live thumbs
    QHash<QString, QHash<QString, QString>> report;  // lower-case file name -> fields

    // scan
    QString dir;
    std::unique_ptr<QDirIterator> iterator;
    QTimer* scan_timer = nullptr;
    int scanned = 0;
    int ignored = 0;
    bool complete = true;

    // widgets
    QLabel* path_label = nullptr;
    QLabel* status_label = nullptr;
    QComboBox* kind_filter = nullptr;
    QComboBox* ext_filter = nullptr;
    QCheckBox* view_once_filter = nullptr;
    bool only_view_once = false;   // filtro "ver uma vez" ligado
    QPushButton* back_button = nullptr;
    QPushButton* prev_button = nullptr;
    QPushButton* next_button = nullptr;
    QStackedWidget* stack = nullptr;
    QScrollArea* grid_scroll = nullptr;
    ThumbGrid* grid = nullptr;
    ZoomView* zoom = nullptr;
    QLabel* preview = nullptr;
    QLabel* value_name = nullptr;
    QLabel* value_kind = nullptr;
    QLabel* value_ephemeral = nullptr;
    QLabel* value_dims = nullptr;
    QLabel* value_size = nullptr;
    QLabel* value_sha = nullptr;
    QLabel* value_modified = nullptr;
    QLabel* value_captured = nullptr;
    QGridLayout* exif_grid = nullptr;
    QWidget* exif_host = nullptr;
    QLabel* exif_header = nullptr;
    QGridLayout* report_grid = nullptr;
    QWidget* report_host = nullptr;
    QLabel* report_header = nullptr;

    // selection
    int current_item = -1;
    int current_visible = -1;

    // hashing
    std::unique_ptr<Sha256Stream> hasher;
    QTimer* hash_timer = nullptr;
    int hash_item = -1;

    // ---------------------------------------------------------------- build
    void build(QWidget* owner, QVBoxLayout* root);
    void build_toolbar(QWidget* owner, QVBoxLayout* root);
    void build_center(QSplitter* splitter);
    void build_panel(QSplitter* splitter);
    void build_status(QWidget* owner, QVBoxLayout* root);

    // ---------------------------------------------------------------- scan
    void start_scan(const QString& path);
    void scan_tick();
    void finish_scan();
    void load_report();
    void collect_report(const QJsonValue& node, int depth);

    // ------------------------------------------------------------ selection
    void select(int item_index, bool zoom_in);
    void show_zoom(int item_index);
    void close_zoom();
    void step(int delta);
    void probe(MediaItem& item);
    void fill_panel(MediaItem& item);
    void clear_panel();
    void update_preview(const MediaItem& item);

    // -------------------------------------------------------------- filters
    bool passes_filter(const MediaItem& item) const;
    void rebuild_visible(bool keep_scroll);
    void refresh_filters();
    void update_status();
    void set_zoom_chrome(bool visible);

    // ------------------------------------------------------------ thumbs/sha
    QPixmap thumbnail(const MediaItem& item);
    void store_thumb(const QString& path, const QPixmap& pixmap);
    void touch_thumb(const QString& path);
    void begin_hash(int item_index);
    void hash_tick();
};

// ---- build ----------------------------------------------------------------
void GalleryView::Impl::build(QWidget* owner, QVBoxLayout* root) {
    scan_timer = new QTimer(owner);
    scan_timer->setInterval(0);

    hash_timer = new QTimer(owner);
    hash_timer->setInterval(0);

    build_toolbar(owner, root);

    auto* splitter = new QSplitter(Qt::Horizontal, owner);
    splitter->setHandleWidth(1);
    splitter->setStyleSheet(QStringLiteral("QSplitter::handle{background:%1;}").arg(theme::kStrokeDefault));
    build_center(splitter);
    build_panel(splitter);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({980, 340});
    root->addWidget(splitter, 1);

    build_status(owner, root);

    QObject::connect(scan_timer, &QTimer::timeout, owner, [this]() { scan_tick(); });
    QObject::connect(hash_timer, &QTimer::timeout, owner, [this]() { hash_tick(); });

    clear_panel();
    update_status();
}

void GalleryView::Impl::build_toolbar(QWidget* owner, QVBoxLayout* root) {
    auto* bar = new QWidget(owner);
    bar->setObjectName(QStringLiteral("galleryBar"));
    bar->setFixedHeight(32);
    bar->setStyleSheet(QStringLiteral("background:%1;").arg(theme::kCommandBar));

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(6);

    const QString button_style =
        QStringLiteral("QPushButton{background:transparent; border:none; color:%1;"
                       " padding:3px 8px; font-size:12px;}"
                       "QPushButton:hover{background:%2;}"
                       "QPushButton:pressed{background:%3;}"
                       "QPushButton:disabled{color:%4;}")
            .arg(theme::kTextPrimary, theme::kHoverBar, theme::kPressed, theme::kTextDisabled);

    back_button = new QPushButton(QStringLiteral("< Galeria"), bar);
    back_button->setCursor(Qt::PointingHandCursor);
    back_button->setStyleSheet(button_style);
    back_button->setVisible(false);

    prev_button = new QPushButton(QStringLiteral("< Anterior"), bar);
    next_button = new QPushButton(QStringLiteral("Proxima >"), bar);
    for (QPushButton* button : {prev_button, next_button}) {
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(button_style);
        button->setVisible(false);
    }

    auto* title = new QLabel(QStringLiteral("Midia recuperada"), bar);
    QFont title_font = title->font();
    title_font.setPixelSize(12);
    title_font.setBold(true);
    title->setFont(title_font);
    title->setStyleSheet(QStringLiteral("color:%1;").arg(theme::kTextPrimary));

    path_label = new QLabel(QStringLiteral("nenhuma pasta"), bar);
    QFont small = path_label->font();
    small.setPixelSize(11);
    path_label->setFont(small);
    path_label->setStyleSheet(QStringLiteral("color:%1;").arg(theme::kTextSecondary));

    const QString combo_style =
        QStringLiteral("QComboBox{background:%1; color:%2; border:1px solid %3;"
                       " padding:2px 6px; font-size:11px;}"
                       "QComboBox::drop-down{border:none; width:14px;}"
                       "QComboBox QAbstractItemView{background:%1; color:%2;"
                       " selection-background-color:%4; border:1px solid %3; outline:none;}")
            .arg(theme::kSurface, theme::kTextPrimary, theme::kSelectionBorder, theme::kHoverRow);

    kind_filter = new QComboBox(bar);
    kind_filter->setStyleSheet(combo_style);
    kind_filter->setMinimumWidth(132);
    kind_filter->addItem(QStringLiteral("Todos"), QString());

    ext_filter = new QComboBox(bar);
    ext_filter->setStyleSheet(combo_style);
    ext_filter->setMinimumWidth(150);
    ext_filter->addItem(QStringLiteral("Todas as extensoes"), QString());

    // Midia efemera e o recorte que um perito procura primeiro depois de uma
    // recuperacao de WhatsApp: o filtro existe para chegar nela em um clique.
    view_once_filter = new QCheckBox(QStringLiteral("Ver uma vez"), bar);
    view_once_filter->setCursor(Qt::PointingHandCursor);
    view_once_filter->setEnabled(false);
    view_once_filter->setStyleSheet(
        QStringLiteral("QCheckBox{color:%1; font-size:11px;}"
                       "QCheckBox:disabled{color:%2;}"
                       "QCheckBox::indicator{width:12px; height:12px;"
                       " border:1px solid %3; background:%4;}"
                       "QCheckBox::indicator:checked{background:%5; border-color:%5;}")
            .arg(theme::kTextSecondary, theme::kTextDisabled, theme::kSelectionBorder,
                 theme::kSurface, theme::kAccent));

    layout->addWidget(back_button);
    layout->addWidget(prev_button);
    layout->addWidget(next_button);
    layout->addWidget(title);
    layout->addWidget(path_label, 1);
    layout->addWidget(kind_filter);
    layout->addWidget(ext_filter);
    layout->addWidget(view_once_filter);
    root->addWidget(bar);

    QObject::connect(back_button, &QPushButton::clicked, owner, [this]() { close_zoom(); });
    QObject::connect(prev_button, &QPushButton::clicked, owner, [this]() { step(-1); });
    QObject::connect(next_button, &QPushButton::clicked, owner, [this]() { step(1); });
    QObject::connect(kind_filter, &QComboBox::currentIndexChanged, owner,
                     [this](int) { rebuild_visible(false); });
    QObject::connect(ext_filter, &QComboBox::currentIndexChanged, owner,
                     [this](int) { rebuild_visible(false); });
    QObject::connect(view_once_filter, &QCheckBox::toggled, owner, [this](bool checked) {
        only_view_once = checked;
        rebuild_visible(false);
    });
}

void GalleryView::Impl::build_center(QSplitter* splitter) {
    stack = new QStackedWidget(splitter);

    grid_scroll = new QScrollArea(stack);
    grid_scroll->setWidgetResizable(true);
    grid_scroll->setFrameShape(QFrame::NoFrame);
    grid_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    grid_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:%1; border:none;}")
                                   .arg(theme::kDocument) +
                               scrollbar_qss());

    grid = new ThumbGrid(grid_scroll);
    grid->scroll = grid_scroll;
    grid->items = &items;
    grid->visible = &visible;
    grid->empty_title = QStringLiteral("Nenhuma midia carregada");
    grid->empty_hint = QStringLiteral(
        "Aponte a galeria para a pasta que o motor de recuperacao gerou. "
        "Imagens, videos e audio aparecem aqui; clique abre a vista ampliada.");
    grid->thumb_provider = [this](const MediaItem& item) { return thumbnail(item); };
    grid->on_activate = [this](int index) {
        if (index < 0 || index >= static_cast<int>(visible.size())) return;
        select(visible.at(index), true);
    };
    grid->on_current = [this](int index) {
        if (index < 0 || index >= static_cast<int>(visible.size())) return;
        select(visible.at(index), false);
    };
    grid_scroll->setWidget(grid);

    zoom = new ZoomView(stack);
    zoom->on_back = [this]() { close_zoom(); };
    zoom->on_step = [this](int delta) { step(delta); };

    stack->addWidget(grid_scroll);
    stack->addWidget(zoom);

    splitter->addWidget(stack);
}

void GalleryView::Impl::build_panel(QSplitter* splitter) {
    auto* panel_scroll = new QScrollArea(splitter);
    panel_scroll->setWidgetResizable(true);
    panel_scroll->setFrameShape(QFrame::NoFrame);
    panel_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    panel_scroll->setMinimumWidth(286);
    panel_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:%1; border:none;}")
                                    .arg(theme::kSurface) +
                                scrollbar_qss());

    auto* host = new QWidget(panel_scroll);
    host->setStyleSheet(QStringLiteral("background:%1;").arg(theme::kSurface));
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);

    preview = new QLabel(host);
    preview->setFixedHeight(168);
    preview->setMinimumWidth(0);
    preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet(QStringLiteral("background:%1; color:%2; font-size:11px;")
                               .arg(theme::kDocument, theme::kTextSecondary));
    preview->setText(QStringLiteral("sem selecao"));

    auto* fields = new QWidget(host);
    auto* fields_grid = new QGridLayout(fields);
    fields_grid->setContentsMargins(0, 0, 0, 0);
    fields_grid->setHorizontalSpacing(8);
    fields_grid->setVerticalSpacing(3);
    fields_grid->setColumnStretch(1, 1);

    value_name = add_field_row(fields_grid, 0, QStringLiteral("Nome"));
    value_kind = add_field_row(fields_grid, 1, QStringLiteral("Tipo"));
    value_ephemeral = add_field_row(fields_grid, 2, QStringLiteral("Efemera"));
    value_dims = add_field_row(fields_grid, 3, QStringLiteral("Dimensoes"));
    value_size = add_field_row(fields_grid, 4, QStringLiteral("Tamanho"));
    value_modified = add_field_row(fields_grid, 5, QStringLiteral("Modificado"));
    value_sha = add_field_row(fields_grid, 6, QStringLiteral("SHA-256"));
    value_captured = add_field_row(fields_grid, 7, QStringLiteral("Capturada"));
    {
        QFont mono = value_sha->font();
        mono.setFamily(QStringLiteral("Consolas"));
        mono.setPixelSize(10);
        value_sha->setFont(mono);
    }

    exif_header = make_section_header(QStringLiteral("EXIF"));
    exif_host = new QWidget(host);
    exif_grid = new QGridLayout(exif_host);
    exif_grid->setContentsMargins(0, 0, 0, 0);
    exif_grid->setHorizontalSpacing(8);
    exif_grid->setVerticalSpacing(3);
    exif_grid->setColumnStretch(1, 1);

    report_header = make_section_header(QStringLiteral("ORIGEM (report.json)"));
    report_host = new QWidget(host);
    report_grid = new QGridLayout(report_host);
    report_grid->setContentsMargins(0, 0, 0, 0);
    report_grid->setHorizontalSpacing(8);
    report_grid->setVerticalSpacing(3);
    report_grid->setColumnStretch(1, 1);
    report_header->setVisible(false);
    report_host->setVisible(false);

    layout->addWidget(preview);
    layout->addWidget(fields);
    layout->addSpacing(4);
    layout->addWidget(exif_header);
    layout->addWidget(exif_host);
    layout->addWidget(report_header);
    layout->addWidget(report_host);
    layout->addStretch(1);

    panel_scroll->setWidget(host);
    splitter->addWidget(panel_scroll);
}

void GalleryView::Impl::build_status(QWidget* owner, QVBoxLayout* root) {
    auto* bar = new QWidget(owner);
    bar->setFixedHeight(22);
    bar->setStyleSheet(QStringLiteral("background:%1;").arg(theme::kCommandBar));
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 0, 8, 0);
    status_label = new QLabel(QStringLiteral("pronto"), bar);
    QFont font = status_label->font();
    font.setPixelSize(11);
    status_label->setFont(font);
    status_label->setStyleSheet(QStringLiteral("color:%1;").arg(theme::kTextSecondary));
    layout->addWidget(status_label, 1);
    root->addWidget(bar);
}

// ---- scan -----------------------------------------------------------------
void GalleryView::Impl::start_scan(const QString& path) {
    scan_timer->stop();
    hash_timer->stop();
    hasher.reset();
    hash_item = -1;

    items.clear();
    visible.clear();
    thumbs.clear();
    thumb_order.clear();
    report.clear();
    current_item = -1;
    current_visible = -1;
    scanned = 0;
    ignored = 0;
    dir = path;
    complete = false;

    // Apontar a galeria para outra pasta com a vista ampliada aberta deixava a
    // foto anterior na tela enquanto a varredura nova corria no fundo - o
    // operador olhando para uma imagem que nao pertence mais ao conjunto.
    if (zoom != nullptr) zoom->clear();
    if (stack != nullptr && stack->currentIndex() != 0) close_zoom();

    grid->scanning = true;
    grid->scanning_hint = QStringLiteral("Contando os arquivos. A grade aparece assim que os primeiros forem lidos.");
    grid->current = -1;

    const QFontMetrics metrics(path_label->font());
    path_label->setText(metrics.elidedText(path, Qt::ElideMiddle, 420));

    load_report();

    iterator.reset(new QDirIterator(path,
                                    QDir::Files | QDir::NoDotAndDotDot | QDir::Readable | QDir::Hidden,
                                    QDirIterator::Subdirectories));
    grid->reload(false);
    clear_panel();
    update_status();
    scan_timer->start();
}

void GalleryView::Impl::scan_tick() {
    if (!iterator) {
        scan_timer->stop();
        if (!complete) {
            complete = true;
            finish_scan();
        }
        return;
    }

    int processed = 0;
    bool drained = false;
    while (processed < kScanBatch) {
        if (!iterator->hasNext()) {
            drained = true;
            break;
        }
        const QString file_path = iterator->next();
        ++processed;
        ++scanned;
        const QFileInfo info(file_path);
        if (is_transient_sidecar(info.fileName())) {
            ++ignored;
            continue;
        }
        MediaItem item;
        item.path = file_path;
        item.name = info.fileName();
        item.suffix = info.suffix().toLower();
        item.kind = kind_for_suffix(item.suffix);
        item.bytes = info.size();
        item.modified = info.lastModified();
        item.rel = QDir(dir).relativeFilePath(file_path);
        // A marca de efemera chega pelo relatorio, indexada pelo nome do
        // arquivo: o banco sabe que a linha era "ver uma vez", a pasta sabe
        // qual JPEG saiu dela, e o relatorio e o que liga os dois.
        const auto entry = report.constFind(item.name.toLower());
        if (entry != report.constEnd()) {
            item.view_once = flag_true(entry->value(QStringLiteral("view_once")));
        }
        items.push_back(item);
        if (passes_filter(item)) visible.push_back(static_cast<int>(items.size()) - 1);
    }

    if (drained) {
        iterator.reset();
        scan_timer->stop();
        complete = true;
        finish_scan();
        return;
    }

    grid->reload(true);
    update_status();
}

void GalleryView::Impl::finish_scan() {
    grid->scanning = false;

    std::sort(items.begin(), items.end(), [](const MediaItem& a, const MediaItem& b) {
        const int rank_a = kind_rank(a.kind);
        const int rank_b = kind_rank(b.kind);
        if (rank_a != rank_b) return rank_a < rank_b;
        return QString::compare(a.rel, b.rel, Qt::CaseInsensitive) < 0;
    });

    if (items.isEmpty()) {
        grid->empty_title = QStringLiteral("Nenhuma midia nesta pasta");
        grid->empty_hint = QStringLiteral(
            "A varredura leu %1 arquivos e nenhum deles e imagem, video ou audio. "
            "Se a recuperacao falhou antes de decifrar, a pasta pode conter apenas os "
            "arquivos cifrados originais.")
                                .arg(scanned);
    }

    refresh_filters();
    rebuild_visible(false);
    grid->reload(false);
    update_status();
}

// ---- report.json ----------------------------------------------------------
void GalleryView::Impl::collect_report(const QJsonValue& node, int depth) {
    if (depth > 6) return;
    if (node.isArray()) {
        const QJsonArray array = node.toArray();
        for (const QJsonValue& value : array) collect_report(value, depth + 1);
        return;
    }
    if (!node.isObject()) return;

    const QJsonObject object = node.toObject();
    QString key;
    for (const QString& field : {QStringLiteral("path"), QStringLiteral("file"),
                                 QStringLiteral("name"), QStringLiteral("filename")}) {
        const QJsonValue value = object.value(field);
        if (value.isString() && !value.toString().isEmpty()) {
            key = value.toString();
            break;
        }
    }
    if (!key.isEmpty()) {
        QHash<QString, QString> fields;
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const QJsonValue value = it.value();
            if (value.isString() || value.isDouble() || value.isBool()) {
                fields.insert(it.key(), value.toVariant().toString());
            }
        }
        const QString base = QFileInfo(key).fileName().toLower();
        if (!base.isEmpty() && !fields.isEmpty()) report.insert(base, fields);
    }

    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.value().isObject() || it.value().isArray()) collect_report(it.value(), depth + 1);
    }
}

void GalleryView::Impl::load_report() {
    // O motor de recuperacao escreve report.json ao lado da pasta de midia, e a
    // GUI aponta a galeria para media/. Procurar so no diretorio atual deixaria
    // a marca de efemera invisivel justamente no fluxo do produto, entao o
    // diretorio pai entra como segundo lugar - um nivel, nunca uma varredura.
    QString report_path = QDir(dir).filePath(QStringLiteral("report.json"));
    QFile file(report_path);
    if (!file.exists()) {
        report_path = QDir(QDir(dir).filePath(QStringLiteral(".."))).filePath(QStringLiteral("report.json"));
        file.setFileName(report_path);
    }
    if (!file.open(QIODevice::ReadOnly)) return;
    const QByteArray raw = file.read(32 * 1024 * 1024);
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (document.isNull()) return;
    if (document.isObject()) {
        collect_report(QJsonValue(document.object()), 0);
    } else if (document.isArray()) {
        collect_report(QJsonValue(document.array()), 0);
    }
}

// ---- selection ------------------------------------------------------------
void GalleryView::Impl::probe(MediaItem& item) {
    if (item.probed) return;
    item.probed = true;

    if (item.kind == MediaKind::Image) {
        QImageReader reader(item.path);
        const QSize size = reader.size();
        if (size.isValid() && !size.isEmpty()) item.image_size = size;
    }
    if (item.kind == MediaKind::Image && is_jpeg_suffix(item.suffix)) {
        item.exif = read_exif_jpeg(item.path);
    }
}

void GalleryView::Impl::select(int item_index, bool zoom_in) {
    if (item_index < 0 || item_index >= static_cast<int>(items.size())) {
        current_item = -1;
        current_visible = -1;
        grid->set_current(-1, false);
        clear_panel();
        return;
    }

    current_item = item_index;
    current_visible = -1;
    for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
        if (visible.at(i) == item_index) {
            current_visible = i;
            break;
        }
    }
    grid->set_current(current_visible, true);

    MediaItem& item = items[item_index];
    probe(item);
    fill_panel(item);
    update_preview(item);
    begin_hash(item_index);

    if (zoom_in) show_zoom(item_index);
}

void GalleryView::Impl::show_zoom(int item_index) {
    if (item_index < 0 || item_index >= static_cast<int>(items.size())) return;
    MediaItem& item = items[item_index];
    zoom->set_ephemeral(item.view_once);

    if (item.kind != MediaKind::Image) {
        zoom->set_message(item.name,
                          QStringLiteral("Sem pre-visualizacao embutida para %1. "
                                         "Os metadados continuam disponiveis no painel.")
                              .arg(kind_label(item.kind).toLower()));
    } else {
        QImageReader reader(item.path);
        reader.setAutoTransform(true);
        const QSize original = reader.size();
        if (original.isEmpty()) {
            zoom->set_message(item.name, QStringLiteral("Nao foi possivel ler a imagem: %1")
                                             .arg(reader.errorString()));
        } else {
            QSize target = original;
            if (target.width() > kMaxPreviewSide || target.height() > kMaxPreviewSide) {
                target = original.scaled(kMaxPreviewSide, kMaxPreviewSide, Qt::KeepAspectRatio);
            }
            reader.setScaledSize(target);
            const QImage image = reader.read();
            if (image.isNull()) {
                zoom->set_message(item.name, QStringLiteral("Falha ao decodificar: %1")
                                                 .arg(reader.errorString()));
            } else {
                zoom->set_image(image, QStringLiteral("%1  %2x%3")
                                           .arg(item.name)
                                           .arg(image.width())
                                           .arg(image.height()));
            }
        }
    }

    stack->setCurrentWidget(zoom);
    set_zoom_chrome(true);
    zoom->setFocus(Qt::OtherFocusReason);
}

void GalleryView::Impl::set_zoom_chrome(bool shown) {
    back_button->setVisible(shown);
    prev_button->setVisible(shown);
    next_button->setVisible(shown);
    if (shown) {
        prev_button->setEnabled(current_visible > 0);
        next_button->setEnabled(current_visible >= 0 &&
                                current_visible + 1 < static_cast<int>(visible.size()));
    }
}

void GalleryView::Impl::close_zoom() {
    stack->setCurrentIndex(0);
    set_zoom_chrome(false);
    grid->setFocus(Qt::OtherFocusReason);
}

void GalleryView::Impl::step(int delta) {
    if (current_visible < 0 || visible.isEmpty()) return;
    const int last = static_cast<int>(visible.size()) - 1;
    const int next = qBound(0, current_visible + delta, last);
    if (next == current_visible) return;
    select(visible.at(next), true);
}

void GalleryView::Impl::update_preview(const MediaItem& item) {
    if (preview == nullptr) return;
    if (item.kind != MediaKind::Image) {
        preview->setPixmap(QPixmap());
        preview->setText(QStringLiteral("sem previa (%1)").arg(kind_label(item.kind).toLower()));
        return;
    }
    QImageReader reader(item.path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isEmpty()) {
        preview->setPixmap(QPixmap());
        preview->setText(QStringLiteral("falha ao abrir"));
        return;
    }
    const double dpr = q->devicePixelRatioF();
    const QSize box(static_cast<int>(250 * dpr), static_cast<int>(160 * dpr));
    QSize target = original.scaled(box, Qt::KeepAspectRatio);
    if (target.isEmpty()) target = box;
    reader.setScaledSize(target);
    const QImage image = reader.read();
    if (image.isNull()) {
        preview->setPixmap(QPixmap());
        preview->setText(QStringLiteral("falha ao decodificar"));
        return;
    }
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);
    preview->setText(QString());
    preview->setPixmap(pixmap);
}

void GalleryView::Impl::fill_panel(MediaItem& item) {
    value_name->setText(item.name);
    value_kind->setText(QStringLiteral("%1  %2")
                            .arg(kind_label(item.kind))
                            .arg(item.suffix.toUpper()));
    // Efemera nao e uma curiosidade do arquivo: e a frase que diz ao perito
    // que aquele arquivo nao existe mais no aparelho de origem. Fica em
    // vermelho quando verdadeira e em cinza quando nao.
    if (item.view_once) {
        value_ephemeral->setText(
            QStringLiteral("SIM - o aplicativo apaga o arquivo depois da visualizacao"));
        value_ephemeral->setStyleSheet(
            QStringLiteral("color:%1; font-weight:bold;").arg(theme::semantic::error().name()));
    } else {
        value_ephemeral->setText(QStringLiteral("nao"));
        value_ephemeral->setStyleSheet(value_style());
    }

    value_dims->setText(item.kind == MediaKind::Image ? format_dimensions(item.image_size)
                                                      : QStringLiteral("-"));
    value_size->setText(QStringLiteral("%1  (%2 bytes)")
                            .arg(format_bytes(item.bytes))
                            .arg(item.bytes));
    value_modified->setText(format_stamp(item.modified));

    const QString iso = exif_datetime_iso(item.exif.date_time_original.isEmpty()
                                              ? item.exif.date_time_digitized
                                              : item.exif.date_time_original);
    value_captured->setText(iso.isEmpty() ? QStringLiteral("-") : iso);

    clear_layout(exif_grid);
    int row = 0;
    if (!item.exif.present) {
        QLabel* note = make_value_label(QString::fromLatin1(
            is_jpeg_suffix(item.suffix) ? "sem EXIF neste arquivo"
                                        : "EXIF so e lido em JPEG"));
        note->setStyleSheet(QStringLiteral("color:%1;").arg(theme::kTextMuted));
        exif_grid->addWidget(note, row++, 0, 1, 2);
    } else if (!item.exif.valid) {
        QLabel* note = make_value_label(QStringLiteral("EXIF ilegivel: %1").arg(item.exif.error));
        note->setStyleSheet(QStringLiteral("color:%1;").arg(theme::kTextMuted));
        exif_grid->addWidget(note, row++, 0, 1, 2);
    } else {
        const QVector<std::pair<QString, QString>> rows = exif_rows(item.exif);
        if (rows.isEmpty()) {
            exif_grid->addWidget(make_value_label(QStringLiteral("segmento Exif sem os campos lidos")),
                                 row++, 0, 1, 2);
        }
        for (const std::pair<QString, QString>& pair : rows) {
            exif_grid->addWidget(make_key_label(pair.first), row, 0, Qt::AlignTop);
            exif_grid->addWidget(make_value_label(pair.second), row, 1, Qt::AlignTop);
            ++row;
        }
        QLabel* endian = make_value_label(QString::fromLatin1(item.exif.little_endian
                                                                   ? "TIFF II (little endian)"
                                                                   : "TIFF MM (big endian)"));
        endian->setStyleSheet(QStringLiteral("color:%1;").arg(theme::kTextMuted));
        exif_grid->addWidget(endian, row++, 0, 1, 2);
    }

    clear_layout(report_grid);
    const QHash<QString, QString> extra = report.value(item.name.toLower());
    report_header->setVisible(!extra.isEmpty());
    report_host->setVisible(!extra.isEmpty());
    if (!extra.isEmpty()) {
        int report_row = 0;
        QStringList keys = extra.keys();
        keys.sort();
        for (const QString& key : keys) {
            report_grid->addWidget(make_key_label(key), report_row, 0, Qt::AlignTop);
            report_grid->addWidget(make_value_label(extra.value(key)), report_row, 1, Qt::AlignTop);
            ++report_row;
        }
    }
}

void GalleryView::Impl::clear_panel() {
    if (value_name == nullptr) return;
    for (QLabel* label : {value_name, value_kind, value_ephemeral, value_dims, value_size,
                          value_sha, value_modified, value_captured}) {
        label->setText(QStringLiteral("-"));
    }
    value_ephemeral->setStyleSheet(value_style());
    if (preview != nullptr) {
        preview->setPixmap(QPixmap());
        preview->setText(QStringLiteral("sem selecao"));
    }
    clear_layout(exif_grid);
    clear_layout(report_grid);
    report_header->setVisible(false);
    report_host->setVisible(false);
    hash_timer->stop();
    hasher.reset();
    hash_item = -1;
}

// ---- filters --------------------------------------------------------------
bool GalleryView::Impl::passes_filter(const MediaItem& item) const {
    const int kind_index = kind_filter != nullptr ? kind_filter->currentIndex() : 0;
    if (kind_index > 0 && static_cast<int>(item.kind) != kind_index - 1) return false;
    const QString suffix = ext_filter != nullptr ? ext_filter->currentData().toString() : QString();
    if (!suffix.isEmpty() && item.suffix != suffix) return false;
    if (only_view_once && !item.view_once) return false;
    return true;
}

void GalleryView::Impl::rebuild_visible(bool keep_scroll) {
    const int previous_item = current_item;
    visible.clear();
    visible.reserve(items.size());
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (passes_filter(items.at(i))) visible.push_back(i);
    }

    current_visible = -1;
    if (previous_item >= 0) {
        for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
            if (visible.at(i) == previous_item) {
                current_visible = i;
                break;
            }
        }
    }

    grid->reload(keep_scroll);
    grid->set_current(current_visible, false);

    if (current_visible < 0 && !items.isEmpty()) {
        current_item = -1;
        clear_panel();
        grid->empty_title = QStringLiteral("Nada com este filtro");
        grid->empty_hint = QStringLiteral(
            "Os %1 arquivos encontrados nao passam pelo filtro atual. "
            "Volte o filtro de tipo ou desligue \"Ver uma vez\" para ver a pasta inteira.")
                                .arg(items.size());
        grid->update();
    }
    set_zoom_chrome(stack != nullptr && stack->currentIndex() == 1);
    update_status();
}

void GalleryView::Impl::refresh_filters() {
    int counts[4] = {0, 0, 0, 0};
    QHash<QString, int> suffix_counts;
    for (const MediaItem& item : items) {
        counts[static_cast<int>(item.kind)] += 1;
        suffix_counts[item.suffix] += 1;
    }

    {
        const QSignalBlocker blocker(kind_filter);
        const int previous = kind_filter->currentIndex();
        kind_filter->clear();
        kind_filter->addItem(QStringLiteral("Todos (%1)").arg(items.size()), QString());
        kind_filter->addItem(QStringLiteral("Imagens (%1)").arg(counts[0]), QString());
        kind_filter->addItem(QStringLiteral("Videos (%1)").arg(counts[1]), QString());
        kind_filter->addItem(QStringLiteral("Audio (%1)").arg(counts[2]), QString());
        kind_filter->addItem(QStringLiteral("Outros (%1)").arg(counts[3]), QString());
        kind_filter->setCurrentIndex(qBound(0, previous, kind_filter->count() - 1));
    }

    {
        const QSignalBlocker blocker(ext_filter);
        const QString previous = ext_filter->currentData().toString();
        ext_filter->clear();
        ext_filter->addItem(QStringLiteral("Todas as extensoes"), QString());
        QStringList suffixes = suffix_counts.keys();
        suffixes.sort();
        for (const QString& suffix : suffixes) {
            if (suffix.isEmpty()) continue;
            ext_filter->addItem(QStringLiteral(".%1 (%2)").arg(suffix.toUpper()).arg(suffix_counts.value(suffix)),
                                suffix);
        }
        int restore = 0;
        for (int i = 0; i < ext_filter->count(); ++i) {
            if (ext_filter->itemData(i).toString() == previous) {
                restore = i;
                break;
            }
        }
        ext_filter->setCurrentIndex(restore);
    }

    {
        int ephemeral = 0;
        for (const MediaItem& item : items) {
            if (item.view_once) ++ephemeral;
        }
        const QSignalBlocker blocker(view_once_filter);
        view_once_filter->setText(QStringLiteral("Ver uma vez (%1)").arg(ephemeral));
        view_once_filter->setEnabled(ephemeral > 0);
        // Sem nenhuma marcada o filtro nao tem o que mostrar; deixa-lo ligado
        // esconderia a pasta inteira sem explicar por que.
        if (ephemeral == 0) {
            view_once_filter->setChecked(false);
            only_view_once = false;
        }
    }
}

void GalleryView::Impl::update_status() {
    if (status_label == nullptr) return;
    QStringList parts;
    parts << QStringLiteral("%1 arquivos").arg(items.size());
    if (visible.size() != items.size()) {
        parts << QStringLiteral("%1 visiveis").arg(visible.size());
    }
    int images = 0;
    int ephemeral = 0;
    for (const MediaItem& item : items) {
        if (item.kind == MediaKind::Image) ++images;
        if (item.view_once) ++ephemeral;
    }
    parts << QStringLiteral("%1 imagens").arg(images);
    if (ephemeral > 0) parts << QStringLiteral("%1 ver-uma-vez").arg(ephemeral);
    if (ignored > 0) parts << QStringLiteral("%1 ignorados").arg(ignored);
    if (complete) {
        parts << QStringLiteral("varredura concluida");
    } else {
        parts << QStringLiteral("varrendo... %1 lidos").arg(scanned);
    }
    status_label->setText(parts.join(QStringLiteral("   -   ")));
}

// ---- thumbnails -----------------------------------------------------------
void GalleryView::Impl::touch_thumb(const QString& path) {
    const int at = thumb_order.indexOf(path);
    if (at < 0 || at == thumb_order.size() - 1) return;
    thumb_order.move(at, thumb_order.size() - 1);
}

void GalleryView::Impl::store_thumb(const QString& path, const QPixmap& pixmap) {
    thumbs.insert(path, pixmap);
    if (pixmap.isNull()) return;  // remember the failure, but do not spend a slot on it
    thumb_order.removeAll(path);
    thumb_order.append(path);
    while (thumb_order.size() > kThumbCacheMax) {
        const QString oldest = thumb_order.takeFirst();
        thumbs.remove(oldest);
    }
}

QPixmap GalleryView::Impl::thumbnail(const MediaItem& item) {
    if (item.kind != MediaKind::Image) return QPixmap();
    if (item.probed && item.image_size.isEmpty()) return QPixmap();

    const auto found = thumbs.constFind(item.path);
    if (found != thumbs.constEnd()) {
        touch_thumb(item.path);
        return found.value();
    }

    QImageReader reader(item.path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isEmpty()) {
        store_thumb(item.path, QPixmap());
        return QPixmap();
    }

    // Decode straight to thumbnail size: setScaledSize makes the decoder produce
    // a 160x120-ish image instead of a 4000x3000 one we would throw away.
    const double dpr = q->devicePixelRatioF();
    const QSize box(qMax(1, static_cast<int>(kThumbW * dpr)),
                    qMax(1, static_cast<int>(kThumbH * dpr)));
    QSize target = original.scaled(box, Qt::KeepAspectRatio);
    if (target.isEmpty()) target = box;
    reader.setScaledSize(target);

    const QImage image = reader.read();
    if (image.isNull()) {
        store_thumb(item.path, QPixmap());
        return QPixmap();
    }
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);
    store_thumb(item.path, pixmap);
    return pixmap;
}

// ---- hashing --------------------------------------------------------------
void GalleryView::Impl::begin_hash(int item_index) {
    hash_timer->stop();
    hasher.reset();
    hash_item = item_index;
    if (value_sha == nullptr) return;
    if (item_index < 0 || item_index >= static_cast<int>(items.size())) {
        value_sha->setText(QStringLiteral("-"));
        return;
    }
    MediaItem& item = items[item_index];
    if (!item.sha256.isEmpty()) {
        value_sha->setText(split_digest(item.sha256));
        return;
    }
    auto stream = std::make_unique<Sha256Stream>(item.path);
    if (!stream->open()) {
        value_sha->setText(QStringLiteral("arquivo indisponivel"));
        return;
    }
    value_sha->setText(QStringLiteral("calculando..."));
    hasher = std::move(stream);
    hash_timer->start();
}

void GalleryView::Impl::hash_tick() {
    if (!hasher) {
        hash_timer->stop();
        return;
    }
    const int state = hasher->pump(kHashSlice);
    if (state > 0) return;

    hash_timer->stop();
    if (state == 0) {
        const QString digest = hasher->hex();
        if (hash_item >= 0 && hash_item < static_cast<int>(items.size())) {
            items[hash_item].sha256 = digest;
        }
        if (value_sha != nullptr && hash_item == current_item) value_sha->setText(split_digest(digest));
    } else if (value_sha != nullptr && hash_item == current_item) {
        value_sha->setText(QStringLiteral("falha ao ler"));
    }
    hasher.reset();
}

// ---------------------------------------------------------------------------
// GalleryView
// ---------------------------------------------------------------------------
GalleryView::GalleryView(QWidget* parent) : QWidget(parent), d_(new Impl) {
    d_->q = this;
    setObjectName(QStringLiteral("galleryView"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    d_->build(this, root);
}

GalleryView::~GalleryView() {
    if (!d_) return;
    // The child widgets outlive Impl (they are QObject children of this widget),
    // so every callback that captured Impl has to be dropped before it dies.
    d_->scan_timer->stop();
    d_->hash_timer->stop();
    if (d_->grid != nullptr) {
        d_->grid->on_activate = nullptr;
        d_->grid->on_current = nullptr;
        d_->grid->thumb_provider = nullptr;
        d_->grid->items = nullptr;
        d_->grid->visible = nullptr;
        d_->grid->scroll = nullptr;
    }
    if (d_->zoom != nullptr) {
        d_->zoom->on_back = nullptr;
        d_->zoom->on_step = nullptr;
    }
}

bool GalleryView::load_directory(const QString& dir, QString* error) {
    const QFileInfo info(dir);
    if (!info.exists() || !info.isDir()) {
        if (error != nullptr) *error = QStringLiteral("a pasta nao existe: %1").arg(dir);
        return false;
    }
    if (!info.isReadable()) {
        if (error != nullptr) *error = QStringLiteral("sem permissao de leitura: %1").arg(dir);
        return false;
    }
    d_->start_scan(info.absoluteFilePath());
    return true;
}

bool GalleryView::try_autoload() {
    const QString base = QCoreApplication::applicationDirPath();
    const QStringList candidates = {base + QStringLiteral("/recovered"),
                                    base + QStringLiteral("/recovered_media"),
                                    QDir::currentPath() + QStringLiteral("/recovered")};
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isDir()) return load_directory(candidate, nullptr);
    }
    return false;
}

QString GalleryView::directory() const { return d_ ? d_->dir : QString(); }

int GalleryView::item_count() const { return d_ ? static_cast<int>(d_->items.size()) : 0; }

int GalleryView::image_count() const {
    if (!d_) return 0;
    int count = 0;
    for (const MediaItem& item : d_->items) {
        if (item.kind == MediaKind::Image) ++count;
    }
    return count;
}

bool GalleryView::scan_complete() const { return d_ ? d_->complete : true; }

void GalleryView::run_scan_to_completion() {
    if (!d_) return;
    d_->scan_timer->stop();
    for (int guard = 0; guard < 1000000; ++guard) {
        if (d_->iterator == nullptr) break;
        d_->scan_tick();
    }
    if (!d_->complete) {
        d_->complete = true;
        d_->finish_scan();
    }
}

bool GalleryView::open_index(int index) {
    if (!d_) return false;
    if (index < 0 || index >= static_cast<int>(d_->visible.size())) return false;
    d_->select(d_->visible.at(index), true);
    return true;
}

QStringList GalleryView::media_paths() const {
    QStringList paths;
    if (!d_) return paths;
    paths.reserve(d_->items.size());
    for (const MediaItem& item : d_->items) paths << item.path;
    return paths;
}

int GalleryView::view_once_count() const {
    if (!d_) return 0;
    int count = 0;
    for (const MediaItem& item : d_->items) {
        if (item.view_once) ++count;
    }
    return count;
}

bool GalleryView::item_view_once(int index) const {
    if (!d_) return false;
    if (index < 0 || index >= static_cast<int>(d_->items.size())) return false;
    return d_->items.at(index).view_once;
}

export_result GalleryView::export_manifest_csv(const QString& path) {
    export_result result;
    if (!d_) {
        result.error = QStringLiteral("galeria nao inicializada");
        return result;
    }
    // Mesma regra de CSV do resto do produto (RFC 4180): sempre entre aspas.
    const auto field = [](QByteArray& out, const QString& value) {
        QByteArray bytes = value.toUtf8();
        bytes.replace('"', "\"\"");
        out.append('"');
        out.append(bytes);
        out.append('"');
    };
    QByteArray out;
    out.append("\xEF\xBB\xBF", 3);
    out.append("\"arquivo\",\"caminho\",\"bytes\",\"sha256\",\"tipo\",\"extensao\","
               "\"exif_data\",\"view_once\"\r\n");
    qint64 rows = 0;
    for (int index : d_->visible) {
        MediaItem& item = d_->items[index];
        if (item.sha256.isEmpty()) item.sha256 = file_sha256(item.path);
        field(out, item.name);
        out.append(',');
        field(out, item.rel);
        out.append(',');
        field(out, QString::number(item.bytes));
        out.append(',');
        field(out, item.sha256);
        out.append(',');
        field(out, kind_label(item.kind));
        out.append(',');
        field(out, item.suffix);
        out.append(',');
        field(out, item.exif.date_time_original);
        out.append(',');
        field(out, item.view_once ? QStringLiteral("1") : QStringLiteral("0"));
        out.append("\r\n");
        ++rows;
    }
    QJsonObject extra;
    extra.insert(QStringLiteral("section"), QStringLiteral("galeria"));
    extra.insert(QStringLiteral("directory"), QDir(d_->dir).absolutePath());
    const QString kind = d_->kind_filter != nullptr ? d_->kind_filter->currentText() : QString();
    const QString ext = d_->ext_filter != nullptr ? d_->ext_filter->currentText() : QString();
    extra.insert(QStringLiteral("filter"),
                 QStringLiteral("tipo=%1; extensao=%2; somente ver-uma-vez=%3")
                     .arg(kind.isEmpty() ? QStringLiteral("todos") : kind,
                          ext.isEmpty() ? QStringLiteral("todas") : ext,
                          d_->only_view_once ? QStringLiteral("sim") : QStringLiteral("nao")));
    extra.insert(QStringLiteral("scope"),
                 QStringLiteral("grade da Galeria (o que esta na tela) - arquivos como estavam "
                                "no disco no momento do export"));
    extra.insert(QStringLiteral("payload"),
                 QStringLiteral("colunas: arquivo, caminho, bytes, sha256, tipo, extensao, "
                                "exif_data, view_once"));
    return publish_export(path, out, QStringLiteral("olho-galeria-csv/1"),
                          QStringLiteral("sha256_csv"), rows, extra);
}


}  // namespace olho::ui
