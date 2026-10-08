// Renders the app icon (an SVG) to PNG files at the sizes the Windows .ico
// and macOS .icns files need. make_icons.py then packs them.
//
//   vox_render_icon <icon.svg> <output folder>

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <array>
#include <cstdio>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        static_cast<void>(
            std::fprintf(stderr, "usage: vox_render_icon <icon.svg> <output folder>\n"));
        return 2;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    const QGuiApplication app(argc, argv);
    QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));
    if (!svg.isValid()) {
        static_cast<void>(std::fprintf(stderr, "error: %s is not a valid SVG file\n", argv[1]));
        return 1;
    }
    const QDir out(QString::fromLocal8Bit(argv[2]));
    if (!QDir().mkpath(out.path())) {
        static_cast<void>(std::fprintf(stderr, "error: cannot create %s\n", argv[2]));
        return 1;
    }
    constexpr std::array kSizes{16, 24, 32, 48, 64, 128, 256, 512, 1024};
    for (const int size : kSizes) {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        svg.render(&painter);
        painter.end();
        const QString path = out.filePath(QStringLiteral("icon_%1.png").arg(size));
        if (!image.save(path)) {
            static_cast<void>(std::fprintf(stderr, "error: cannot write %s\n", qPrintable(path)));
            return 1;
        }
    }
    return 0;
}
