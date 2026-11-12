// language: C++, file: exif_oracle.cpp
// Headless oracle for task-4: dumps what the EXIF parser and the SHA-256
// streaming code actually return, then points the real widget at the same
// folder (which contains truncated and hostile files on purpose) and reports
// what it scanned. verify.py compares the dump against expected.json.
#include "galleryview.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QDirIterator>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTextStream>

namespace {

QStringList list_jpegs(const QString& dir) {
    QStringList names;
    QDirIterator iterator(dir,
                          QStringList() << QStringLiteral("*.jpg") << QStringLiteral("*.jpeg"),
                          QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) names << iterator.next();
    names.sort();
    return names;
}

QStringList list_all(const QString& dir) {
    QStringList names;
    QDirIterator iterator(dir, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) names << iterator.next();
    names.sort();
    return names;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTextStream console(stdout);

    const QString dir = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                 : QStringLiteral("../fixtures");

    // Second argument, when present, is the dump file. Writing it here instead
    // of piping keeps tabs and byte values intact on the way to verify.py.
    QFile dump;
    QTextStream dump_stream;
    QTextStream* sink = &console;
    if (argc > 2) {
        dump.setFileName(QString::fromLocal8Bit(argv[2]));
        if (dump.open(QIODevice::WriteOnly | QIODevice::Text)) {
            dump_stream.setDevice(&dump);
            sink = &dump_stream;
        }
    }
    QTextStream& out = *sink;

    const QStringList jpegs = list_jpegs(dir);
    out << "#JPEG_COUNT\t" << jpegs.size() << "\n";
    for (const QString& path : jpegs) {
        const olho::ui::ExifData exif = olho::ui::read_exif_jpeg(path);
        out << "#FILE\t" << QFileInfo(path).fileName() << "\n";
        out << "#PRESENT\t" << (exif.present ? 1 : 0) << "\n";
        out << "#VALID\t" << (exif.valid ? 1 : 0) << "\n";
        out << "#ENDIAN\t" << (exif.little_endian ? QStringLiteral("II") : QStringLiteral("MM"))
            << "\n";
        out << "#ERROR\t" << exif.error << "\n";
        const QVector<std::pair<QString, QString> > rows = olho::ui::exif_rows(exif);
        out << "#FIELDS\t" << rows.size() << "\n";
        const QString stamp = exif.date_time_original.isEmpty()
                                  ? (exif.date_time_digitized.isEmpty() ? exif.date_time
                                                                        : exif.date_time_digitized)
                                  : exif.date_time_original;
        out << "#ISO_DATE\t" << olho::ui::exif_datetime_iso(stamp) << "\n";
        for (const std::pair<QString, QString>& row : rows) {
            out << row.first << "\t" << row.second << "\n";
        }
    }

    const QStringList all = list_all(dir);
    out << "#FILES\t" << all.size() << "\n";
    for (const QString& path : all) {
        out << "#SHA256\t" << QFileInfo(path).fileName() << "\t" << olho::ui::file_sha256(path)
            << "\n";
    }

    // The widget itself. It is never shown: constructing it, pointing it at the
    // folder and draining the scan is exactly the code path a hostile file would
    // crash, and this run has to stay at exit code 0.
    olho::ui::GalleryView view;
    view.resize(1280, 820);
    QString error;
    const bool loaded = view.load_directory(dir, &error);
    view.run_scan_to_completion();
    out << "#SCAN\tloaded=" << (loaded ? 1 : 0) << "\titems=" << view.item_count()
        << "\timages=" << view.image_count() << "\tcomplete=" << (view.scan_complete() ? 1 : 0)
        << "\terror=" << error << "\n";
    const QStringList items = view.media_paths();
    out << "#PATHS\t" << items.size() << "\n";
    for (const QString& path : items) out << "#ITEM\t" << QFileInfo(path).fileName() << "\n";
    out << "#OPEN_FIRST\t" << (view.open_index(0) ? 1 : 0) << "\n";
    out << "#OPEN_OOR\t" << (view.open_index(99999) ? 1 : 0) << "\n";
    out << "#MISSING_DIR\t" << (view.load_directory(QStringLiteral("Z:/nao/existe"), nullptr) ? 1 : 0)
        << "\n";
    out.flush();
    if (dump.isOpen()) dump.close();

    console << "exif_oracle: jpegs=" << jpegs.size() << " files=" << all.size()
            << " items=" << items.size() << " images=" << view.image_count() << "\n";
    console.flush();
    return 0;
}
