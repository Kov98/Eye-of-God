// language: C++, file: gen_base.cpp
// Writes the EXIF-free JPEG base images that make_fixtures.py splices the
// hand-built APP1/TIFF block into. The EXIF itself is written in Python; these
// files only supply real, decodable JPEG scan data so the thumbnails actually
// render and the on-screen dimensions are the truth.
//
// build:  g++ -std=c++17 -O2 -I .. @qtflags gen_base.cpp -o ../build/gen_base.exe
// run:    gen_base.exe <output dir>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QString>
#include <QTextStream>

namespace {

void paint_sunset(QImage& image) {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient sky(0, 0, 0, image.height());
    sky.setColorAt(0.0, QColor(38, 26, 62));
    sky.setColorAt(0.45, QColor(196, 92, 74));
    sky.setColorAt(0.72, QColor(238, 156, 92));
    sky.setColorAt(1.0, QColor(52, 40, 52));
    painter.fillRect(image.rect(), sky);

    const QPointF sun(image.width() * 0.68, image.height() * 0.58);
    QRadialGradient glow(sun, image.width() * 0.30);
    glow.setColorAt(0.0, QColor(255, 238, 196, 255));
    glow.setColorAt(0.35, QColor(255, 186, 120, 170));
    glow.setColorAt(1.0, QColor(255, 150, 90, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawEllipse(sun, image.width() * 0.30, image.width() * 0.30);

    QPainterPath hills;
    hills.moveTo(0, image.height());
    hills.lineTo(0, image.height() * 0.72);
    hills.quadTo(image.width() * 0.28, image.height() * 0.55, image.width() * 0.52,
                 image.height() * 0.76);
    hills.quadTo(image.width() * 0.74, image.height() * 0.92, image.width(), image.height() * 0.70);
    hills.lineTo(image.width(), image.height());
    hills.closeSubpath();
    painter.setBrush(QColor(28, 20, 30));
    painter.drawPath(hills);
}

void paint_tiles(QImage& image) {
    QPainter painter(&image);
    painter.fillRect(image.rect(), QColor(24, 26, 30));
    const int step = image.width() / 24;
    for (int y = 0; y < image.height(); y += step) {
        for (int x = 0; x < image.width(); x += step) {
            const bool even = ((x / step) + (y / step)) % 2 == 0;
            QColor colour = even ? QColor(52, 58, 70) : QColor(38, 42, 52);
            if ((x / step) % 7 == 3 && (y / step) % 5 == 2) colour = QColor(145, 132, 238);
            painter.fillRect(QRect(x, y, step, step), colour);
        }
    }
    painter.setPen(QPen(QColor(230, 230, 240), 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(image.rect().adjusted(step / 2, step / 2, -step / 2, -step / 2));
}

void paint_portrait(QImage& image) {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient background(0, 0, image.width(), image.height());
    background.setColorAt(0.0, QColor(30, 44, 56));
    background.setColorAt(1.0, QColor(88, 120, 112));
    painter.fillRect(image.rect(), background);

    painter.setPen(QPen(QColor(214, 224, 228, 190), 2));
    painter.setBrush(QColor(24, 32, 38, 110));
    for (int i = 0; i < 7; ++i) {
        const int inset = 40 + i * 26;
        painter.drawEllipse(QPointF(image.width() / 2.0, image.height() * 0.46),
                            image.width() * 0.44 - inset, image.height() * 0.34 - inset);
    }
    painter.setBrush(QColor(240, 236, 226));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(image.width() * 0.5, image.height() * 0.34), 46, 46);
    painter.drawRoundedRect(QRectF(image.width() * 0.32, image.height() * 0.42,
                                   image.width() * 0.36, image.height() * 0.30),
                            40, 40);
}

void paint_bars(QImage& image) {
    QPainter painter(&image);
    painter.fillRect(image.rect(), QColor(18, 18, 18));
    const QColor bars[] = {QColor(196, 196, 196), QColor(196, 196, 32), QColor(32, 196, 196),
                           QColor(32, 196, 32),   QColor(196, 32, 196), QColor(196, 32, 32),
                           QColor(32, 32, 196)};
    const int bar_width = image.width() / 7;
    for (int i = 0; i < 7; ++i) {
        painter.fillRect(QRect(i * bar_width, 0, bar_width, image.height() * 2 / 3), bars[i]);
    }
    QLinearGradient ramp(0, 0, image.width(), 0);
    ramp.setColorAt(0.0, QColor(0, 0, 0));
    ramp.setColorAt(1.0, QColor(255, 255, 255));
    painter.fillRect(QRect(0, image.height() * 2 / 3, image.width(), image.height() / 3), ramp);
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);

    const QString target = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                    : QStringLiteral("../base");
    QDir().mkpath(target);

    struct Spec {
        const char* name;
        int width;
        int height;
        int scene;
    };
    const Spec specs[] = {
        {"base_1.jpg", 1600, 1200, 0},
        {"base_2.jpg", 1024, 768, 1},
        {"base_3.jpg", 900, 1200, 2},
        {"base_4.jpg", 640, 480, 3},
    };

    int written = 0;
    for (const Spec& spec : specs) {
        QImage image(spec.width, spec.height, QImage::Format_RGB32);
        image.fill(Qt::black);
        switch (spec.scene) {
        case 0: paint_sunset(image); break;
        case 1: paint_tiles(image); break;
        case 2: paint_portrait(image); break;
        default: paint_bars(image); break;
        }
        const QString path = QDir(target).filePath(QString::fromLatin1(spec.name));
        if (!image.save(path, "JPEG", 84)) {
            out << "FALHOU " << path << "\n";
            return 1;
        }
        out << "wrote " << path << " " << spec.width << "x" << spec.height << "\n";
        ++written;
    }
    out << "base images: " << written << "\n";
    return 0;
}
