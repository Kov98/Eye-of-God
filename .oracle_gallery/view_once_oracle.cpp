// language: C++, file: view_once_oracle.cpp
// Oracle for the "ver uma vez" (ephemeral media) marking in the gallery.
//
// WHAT IS BEING PROVEN
//   1. report.json -> flag: the recovery engine names the file, the report
//      carries "view_once": true, and the widget counts exactly that one.
//   2. flag -> pixels: the tile actually paints the badge. Asserting the count
//      alone would pass even if nothing reached the screen, so the oracle grabs
//      the widget and counts badge-coloured pixels. A folder with no flag must
//      render zero of them, which is what makes the positive case meaningful.
//   3. The report is looked up in the scanned folder AND in its parent, because
//      wa_recover writes report.json beside media/ while the GUI points the
//      gallery at media/.
//
// The fixtures are written here, mirroring the exact shape wa_recover emits
// (verified against a real .oracle/wa_e2e/report.json).
#include "galleryview.hpp"
#include "theme_vs2026.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QWidget>

namespace {

int failures = 0;
int checks = 0;

void check(const char* what, bool ok, const QString& detail = QString()) {
    ++checks;
    if (!ok) ++failures;
    QTextStream out(stdout);
    out << (ok ? "[ok]   " : "[FALHA] ") << what;
    if (!detail.isEmpty()) out << "  (" << detail << ")";
    out << "\n";
}

bool write_text(const QString& path, const QString& body) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(body.toUtf8()) == body.toUtf8().size();
}

// A file that is not an image on purpose: the flag must not depend on the
// thumbnail decoding, and a video "ver uma vez" never has one either.
bool write_stub(const QString& path) {
    return write_text(path, QStringLiteral("NAO E IMAGEM\n"));
}

// Same counters the badge uses, so the oracle and the widget agree by
// construction on what a "badge pixel" is.
int count_badge_pixels(const QImage& image) {
    const QRgb badge = theme::semantic::error().rgb();
    int found = 0;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = line[x];
            if (qAbs(qRed(pixel) - qRed(badge)) <= 2 && qAbs(qGreen(pixel) - qGreen(badge)) <= 2 &&
                qAbs(qBlue(pixel) - qBlue(badge)) <= 2) {
                ++found;
            }
        }
    }
    return found;
}

struct Shot {
    int items = 0;
    int flagged = 0;
    int red_pixels = 0;
};

// Loads a folder the way the product does, scans it to completion, and reports
// both what the API says and what the widget painted.
Shot shoot(olho::ui::GalleryView* view, const QString& dir) {
    Shot shot;
    QString error;
    view->load_directory(dir, &error);
    view->resize(1100, 780);
    view->show();
    QApplication::processEvents();
    view->run_scan_to_completion();
    QApplication::processEvents();
    shot.items = view->item_count();
    shot.flagged = view->view_once_count();
    shot.red_pixels = count_badge_pixels(view->grab().toImage());
    return shot;
}

QString make_root() {
    const QString root = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("vofix"));
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    return root;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));

    const QString root = make_root();
    olho::ui::GalleryView view;
    view.setWindowTitle(QStringLiteral("view once oracle"));

    // ---- 1. product layout: report.json beside media/, gallery on media/ ----
    {
        const QString folder = root + QStringLiteral("/product/media");
        QDir().mkpath(folder);
        write_stub(folder + QStringLiteral("/1_foto.jpg"));
        write_stub(folder + QStringLiteral("/2_efemera.jpg"));
        // Byte-for-byte the shape wa_recover writes: the array is "midia", the
        // file name is "file", and the flag only appears when it is true.
        write_text(root + QStringLiteral("/product/report.json"), QStringLiteral(
            "{\n"
            "  \"midia_ok\": 2,\n"
            "  \"midia_view_once\": 1,\n"
            "  \"midia_view_once_arquivos\": 1,\n"
            "  \"midia\": [\n"
            "    {\"file\": \"1_foto.jpg\", \"status\": \"ok\", \"tipo\": \"jpg\", \"bytes\": 2032},\n"
            "    {\"file\": \"2_efemera.jpg\", \"status\": \"ok\", \"tipo\": \"jpg\", \"bytes\": 2032, \"view_once\": true}\n"
            "  ],\n"
            "  \"registros\": []\n"
            "}\n"));

        const Shot shot = shoot(&view, folder);
        check("layout do produto: dois arquivos lidos", shot.items == 2,
              QStringLiteral("items=%1").arg(shot.items));
        check("layout do produto: uma efemera contada", shot.flagged == 1,
              QStringLiteral("flag=%1").arg(shot.flagged));
        check("layout do produto: a efemera e a 2_foto", view.item_view_once(1) && !view.item_view_once(0),
              QStringLiteral("ordem items"));
        check("layout do produto: selo desenhado na miniatura", shot.red_pixels > 0,
              QStringLiteral("pixels vermelhos=%1").arg(shot.red_pixels));

        // A vista ampliada e onde o perito decide o que o arquivo significa: a
        // marca tem que sobreviver a troca de pagina do stack.
        view.open_index(1);
        QApplication::processEvents();
        const int red_zoom = count_badge_pixels(view.grab().toImage());
        check("vista ampliada: etiqueta de efemera visivel", red_zoom > 0,
              QStringLiteral("pixels vermelhos=%1").arg(red_zoom));

        view.open_index(0);
        QApplication::processEvents();
        const int red_zoom_plain = count_badge_pixels(view.grab().toImage());
        check("vista ampliada: item comum sem etiqueta", red_zoom_plain == 0,
              QStringLiteral("pixels vermelhos=%1").arg(red_zoom_plain));
    }

    // ---- 2. report inside the scanned folder, flag written as a string ----
    {
        const QString folder = root + QStringLiteral("/inline");
        QDir().mkpath(folder);
        write_stub(folder + QStringLiteral("/a.jpg"));
        write_stub(folder + QStringLiteral("/b.jpg"));
        write_text(folder + QStringLiteral("/report.json"), QStringLiteral(
            "{\"midia\": [{\"file\": \"b.jpg\", \"status\": \"ok\", \"view_once\": \"true\"}]}\n"));

        const Shot shot = shoot(&view, folder);
        check("relatorio na pasta varrida: uma efemera", shot.flagged == 1,
              QStringLiteral("flag=%1").arg(shot.flagged));
        check("relatorio na pasta varrida: selo desenhado", shot.red_pixels > 0,
              QStringLiteral("pixels vermelhos=%1").arg(shot.red_pixels));
    }

    // ---- 3. negative control: flag false / absent must paint nothing ----
    {
        const QString folder = root + QStringLiteral("/sem_marca");
        QDir().mkpath(folder);
        write_stub(folder + QStringLiteral("/c.jpg"));
        write_stub(folder + QStringLiteral("/d.jpg"));
        write_text(folder + QStringLiteral("/report.json"), QStringLiteral(
            "{\"midia\": [{\"file\": \"c.jpg\", \"status\": \"ok\", \"view_once\": false},"
            " {\"file\": \"d.jpg\", \"status\": \"ok\"}]}\n"));

        const Shot shot = shoot(&view, folder);
        check("controle negativo: nenhuma efemera contada", shot.flagged == 0,
              QStringLiteral("flag=%1").arg(shot.flagged));
        check("controle negativo: nenhum selo pintado", shot.red_pixels == 0,
              QStringLiteral("pixels vermelhos=%1").arg(shot.red_pixels));
    }

    // ---- 4. no report at all: the gallery still works ----
    {
        const QString folder = root + QStringLiteral("/sem_relatorio");
        QDir().mkpath(folder);
        write_stub(folder + QStringLiteral("/e.jpg"));

        const Shot shot = shoot(&view, folder);
        check("sem relatorio: um arquivo, nenhuma efemera",
              shot.items == 1 && shot.flagged == 0,
              QStringLiteral("items=%1 flag=%2").arg(shot.items).arg(shot.flagged));
        check("sem relatorio: nenhum selo pintado", shot.red_pixels == 0,
              QStringLiteral("pixels vermelhos=%1").arg(shot.red_pixels));
    }

    QTextStream(stdout) << QStringLiteral("\nview_once_oracle: %1 verificacoes, %2 falha(s)\n")
                               .arg(checks)
                               .arg(failures);
    return failures == 0 ? 0 : 1;
}
