// language: C++, file: harness_main.cpp
// Minimal host for the lead to run and photograph: one QApplication, one
// GalleryView, nothing else. No main window, no docking, no theme bootstrap
// beyond the Fusion style the VS palette notes ask for.
//
// usage:
//   harness.exe <fixtures dir>                        interactive
//   harness.exe <fixtures dir> --shot out.png         scans, saves a grid shot, quits
//   harness.exe <fixtures dir> --shot out.png --open 1 scans, opens item 1, saves, quits
#include "galleryview.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QPixmap>
#include <QStackedWidget>
#include <QString>
#include <QTimer>
#include <QWheelEvent>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Fusion before any stylesheet: the native Windows style fights the VS
    // palette in several controls.
    QApplication::setStyle(QStringLiteral("Fusion"));

    QString dir;
    QString shot;
    int open_index = -1;
    int zoom_notches = 0;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--shot") && i + 1 < argc) {
            shot = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == QStringLiteral("--open") && i + 1 < argc) {
            open_index = QString::fromLocal8Bit(argv[++i]).toInt();
        } else if (arg == QStringLiteral("--zoom") && i + 1 < argc) {
            zoom_notches = QString::fromLocal8Bit(argv[++i]).toInt();
        } else if (!arg.startsWith(QStringLiteral("--"))) {
            dir = arg;
        }
    }
    if (dir.isEmpty()) {
        dir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../fixtures"));
    }

    auto* view = new olho::ui::GalleryView();
    view->setWindowTitle(QStringLiteral("Olho de Deus - Midia recuperada (GalleryView)"));
    view->resize(1360, 860);
    view->show();

    QString error;
    if (!view->load_directory(dir, &error)) {
        qWarning("gallery: nao abriu %s: %s", qPrintable(dir), qPrintable(error));
    }

    if (!shot.isEmpty()) {
        QTimer::singleShot(300, view, [view, open_index, zoom_notches, shot]() {
            view->run_scan_to_completion();
            if (open_index >= 0) view->open_index(open_index);
            // Drive the wheel for real instead of faking a zoom factor: one
            // notch is 120 eighths of a degree, exactly what a mouse sends.
            auto* stack = view->findChild<QStackedWidget*>();
            if (zoom_notches > 0 && stack != nullptr && stack->currentWidget() != nullptr) {
                QWidget* target = stack->currentWidget();
                const QPointF centre(target->width() / 2.0, target->height() / 2.0);
                for (int i = 0; i < zoom_notches; ++i) {
                    QWheelEvent event(centre, target->mapToGlobal(centre.toPoint()), QPoint(),
                                      QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                                      Qt::NoScrollPhase, false);
                    QCoreApplication::sendEvent(target, &event);
                }
            }
            QTimer::singleShot(300, view, [view, shot]() {
                const QPixmap pixmap = view->grab();
                const bool saved = pixmap.save(shot, "PNG");
                qInfo("gallery: shot=%s saved=%d size=%dx%d items=%d images=%d",
                      qPrintable(shot), saved ? 1 : 0, pixmap.width(), pixmap.height(),
                      view->item_count(), view->image_count());
                QCoreApplication::exit(saved ? 0 : 2);
            });
        });
    }

    return app.exec();
}
