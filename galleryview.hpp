// language: C++, file: galleryview.hpp, target: Windows 11, Qt 6 Widgets
// Recovered-media gallery for the Olho de Deus forensic workstation.
//
// WHAT THIS IS
//   The recovery engine hands the examiner a folder of media that was encrypted
//   inside a live WhatsApp store and decrypted on the way out, plus a
//   report.json. This widget is how that folder gets looked at: a thumbnail
//   grid, a zoom/pan inspection view and a metadata panel carrying the pieces a
//   report needs (type, dimensions, on-disk size, SHA-256, EXIF date/camera/GPS).
//
// NO Q_OBJECT -- ON PURPOSE
//   This GUI is compiled with a plain g++ line and there is no moc step, so a
//   class carrying Q_OBJECT would fail to link the whole binary. Every
//   connection here is a lambda and the child widgets report back through
//   std::function callbacks.
//
// THE PARSERS ARE SELF-CONTAINED
//   read_exif_jpeg() walks the JPEG marker chain and the TIFF IFD0 / ExifIFD /
//   GPSIFD structures by hand because the build must not grow a libexif
//   dependency. It reads a bounded number of bytes (never the whole file) and
//   is written to survive truncated or hostile input instead of crashing.
#pragma once

#include <QDateTime>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <memory>
#include <utility>

namespace olho::ui {

// ---------------------------------------------------------------------------
// EXIF (JPEG APP1 segment -> TIFF header -> IFD chain)
// ---------------------------------------------------------------------------
struct ExifData {
    bool present = false;        // an APP1/Exif segment exists
    bool valid = false;          // the TIFF block inside it parsed
    bool little_endian = true;   // TIFF byte order actually found
    QString error;               // why not valid, when it is not

    // IFD0
    QString make;                // 0x010F
    QString model;               // 0x0110
    QString software;            // 0x0131
    QString date_time;           // 0x0132
    int orientation = 0;         // 0x0112

    // ExifIFD
    QString date_time_original;  // 0x9003
    QString date_time_digitized; // 0x9004
    int pixel_width = 0;         // 0xA002
    int pixel_height = 0;        // 0xA003
    int iso = 0;                 // 0x8827
    QString exposure_time;       // 0x829A, rendered
    QString f_number;            // 0x829D, rendered
    QString focal_length;        // 0x920A, rendered

    // GPSIFD, converted: decimal degrees, N/E positive and S/W negative.
    bool has_latitude = false;
    bool has_longitude = false;
    QString latitude_ref;        // "N" / "S" as written in the file
    QString longitude_ref;       // "E" / "W"
    double latitude = 0.0;
    double longitude = 0.0;
    bool has_altitude = false;
    double altitude = 0.0;
    bool below_sea_level = false;
    QString gps_date_stamp;      // 0x001D
    QString gps_time_stamp;      // 0x0007, rendered hh:mm:ss

    bool has_gps() const { return has_latitude && has_longitude; }
};

// Parses EXIF from a JPEG file. Never throws; a non-JPEG or a damaged file
// comes back with present/valid false and a reason in the error string.
ExifData read_exif_jpeg(const QString& path);

// (rotulo, valor) rows for the metadata panel; also the dump format used by the
// oracle, so the numbers on screen are the numbers under test. Only populated
// fields appear, in a fixed order.
QVector<std::pair<QString, QString>> exif_rows(const ExifData& exif);

// "2026:02:11 14:03:22" -> "2026-02-11 14:03:22". Empty on an unusable stamp.
QString exif_datetime_iso(const QString& exif_stamp);

// Streaming SHA-256 of a file; same digest as sha256sum.
QString file_sha256(const QString& path);

// ---------------------------------------------------------------------------
// Gallery
// ---------------------------------------------------------------------------
class GalleryView : public QWidget {
public:
    explicit GalleryView(QWidget* parent = nullptr);
    ~GalleryView() override;

    GalleryView(const GalleryView&) = delete;
    GalleryView& operator=(const GalleryView&) = delete;

    // Points the gallery at a folder and starts scanning it in timed slices, so
    // the caller returns immediately and the window keeps painting. Returns
    // false (and fills *error) when the path is not a readable directory.
    bool load_directory(const QString& dir, QString* error = nullptr);

    // Loads ./recovered or ./recovered_media next to the executable, when one
    // exists. Returns true when a folder was picked up.
    bool try_autoload();

    QString directory() const;
    int item_count() const;      // media files found (all kinds)
    int image_count() const;     // the subset that is an image
    bool scan_complete() const;  // false while the staged scan is still running

    // Drains the staged scan synchronously. Present for the harness/oracle: the
    // UI never needs it, it is what lets a test assert on a finished scan.
    void run_scan_to_completion();

    // Absolute paths of the files found, in gallery order.
    QStringList media_paths() const;

    // Ephemeral media ("ver uma vez"). WhatsApp deletes the file after the
    // first view, so the database row can be the only proof it existed; the
    // mark reaches this widget through report.json, keyed by file name.
    int view_once_count() const;
    bool item_view_once(int index) const;  // same order as media_paths()

    // Opens the nth item of the current (filtered) list in the zoom view and
    // fills the metadata panel. Returns false when the index is out of range.
    bool open_index(int index);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace olho::ui
