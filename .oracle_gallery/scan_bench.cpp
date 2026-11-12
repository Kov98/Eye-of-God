// language: C++, file: scan_bench.cpp
// Measures what the gallery costs on a big folder: directory walk, then the
// painted pass that decodes the thumbnails actually on screen. Numbers only --
// this exists so the claim "5.000 photos do not blow up the RAM" has a
// measurement behind it instead of an adjective.
#include "galleryview.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QString>
#include <QTextStream>

#include <windows.h>
#include <psapi.h>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTextStream out(stdout);
    const QString dir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("../stress");

    olho::ui::GalleryView view;
    view.resize(1360, 860);
    view.show();

    QElapsedTimer timer;
    timer.start();
    const bool loaded = view.load_directory(dir, nullptr);
    view.run_scan_to_completion();
    const qint64 scan_ms = timer.elapsed();

    timer.restart();
    const QPixmap first = view.grab();  // paints the visible grid: thumbs decode here
    const qint64 paint_ms = timer.elapsed();

    PROCESS_MEMORY_COUNTERS counters;
    ZeroMemory(&counters, sizeof(counters));
    const bool have_memory = GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0;

    out << "loaded=" << (loaded ? 1 : 0) << " items=" << view.item_count()
        << " images=" << view.image_count() << " scan_ms=" << scan_ms
        << " first_paint_ms=" << paint_ms << " pixmap=" << first.width() << "x" << first.height()
        << "\n";
    if (have_memory) {
        out << "working_set_mb=" << (counters.WorkingSetSize / (1024.0 * 1024.0))
            << " peak_working_set_mb=" << (counters.PeakWorkingSetSize / (1024.0 * 1024.0)) << "\n";
    }
    out.flush();
    return 0;
}
