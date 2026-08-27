#include <QApplication>
#include <QImageReader>
#include <QImageWriter>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "App/MainWindow.h"
#include "Core/EditorRenderer.h"
#include "Core/EditorState.h"
#include "Core/PrintComposer.h"
#include "Imaging/BackgroundTools.h"

namespace {

// Headless pipeline smoke test: import -> format -> guide-dot solve ->
// render (Editing + Preview) -> auto-clean -> export photo -> print sheet.
// Diagnostic-only, mirrors iHateCards' --test-export convention.
int runTestExport(const char* imagePath, const char* outDir) {
    using namespace ihv;
    QImageReader reader(QString::fromUtf8(imagePath));
    QImage img = reader.read();
    if (img.isNull()) {
        std::fprintf(stderr, "decode failed: %s\n", imagePath);
        return 1;
    }
    core::PhotoDocument doc;
    doc.id = 1;
    doc.name = "test";
    doc.original = img.convertToFormat(QImage::Format_ARGB32);

    core::EditorState s;
    core::EditorEngine::applyFormat(s, "passport_rf");
    std::printf("format: %.1fx%.1fmm top=%.1f head=%.1f(%.1f%%) bot=%.1f dpi=%d\n", s.widthMm, s.heightMm,
                s.topMarginMm, s.headSizeMm, s.headPct, s.botMarginMm, s.dpi);

    int canvasW = 500, canvasH = static_cast<int>(canvasW * s.heightMm / s.widthMm);
    core::EditorRenderer::GuideDots dots;
    if (!core::EditorRenderer::computeGuideDots(s, canvasW, canvasH, dots)) {
        std::fprintf(stderr, "computeGuideDots failed\n");
        return 1;
    }
    std::printf("guide dots: top=(%.1f,%.1f) bot=(%.1f,%.1f)\n", dots.top.x(), dots.top.y(), dots.bot.x(),
                dots.bot.y());
    // Simulate dragging the dots apart (zoom in a bit) and re-solve.
    QPointF top2(dots.top.x(), dots.top.y() - 10), bot2(dots.bot.x(), dots.bot.y() + 10);
    core::EditorRenderer::solveGuideFromDots(s, canvasW, top2, bot2);
    std::printf("guide after solve: scale=%.3f x=%.2f y=%.2f\n", s.guide.scale, s.guide.x, s.guide.y);

    QImage editing = core::EditorRenderer::render(s, doc, canvasW, canvasH, core::EditorRenderer::Mode::Editing);
    QImage preview = core::EditorRenderer::render(s, doc, canvasW, canvasH, core::EditorRenderer::Mode::Preview);
    std::printf("editing render: %dx%d, preview render: %dx%d\n", editing.width(), editing.height(),
                preview.width(), preview.height());
    QString dirQ = QString::fromUtf8(outDir);
    editing.save(dirQ + "/editing_mode.png");
    preview.save(dirQ + "/preview_mode.png");

    core::EditorState oval;
    core::EditorEngine::applyFormat(oval, "zagran_rf");
    oval.guide = s.guide;
    QImage ovalPreview = core::EditorRenderer::render(oval, doc, canvasW, canvasH, core::EditorRenderer::Mode::Preview);
    ovalPreview.save(dirQ + "/oval_preview.png");
    std::printf("wrote editing_mode.png / preview_mode.png / oval_preview.png\n");

    doc.processed = imaging::BackgroundTools::autoClean(doc.original, 45);
    std::printf("auto-clean produced processed image: %dx%d\n", doc.processed->width(), doc.processed->height());

    QString dir = QString::fromUtf8(outDir);
    QImage final = core::EditorRenderer::renderFinal(s, doc, s.dpi);
    final.setDotsPerMeterX(static_cast<int>(std::round(s.dpi / 0.0254)));
    final.setDotsPerMeterY(static_cast<int>(std::round(s.dpi / 0.0254)));
    QString photoPath = dir + "/photo.jpg";
    QImageWriter pw(photoPath, "jpg");
    pw.setQuality(95);
    if (!pw.write(final)) {
        std::fprintf(stderr, "photo export failed: %s\n", pw.errorString().toUtf8().constData());
        return 1;
    }
    std::printf("wrote %s (%dx%d px)\n", photoPath.toUtf8().constData(), final.width(), final.height());

    QImage sheet = core::PrintComposer::buildSheet(s, doc);
    QString sheetPath = dir + "/sheet.jpg";
    QImageWriter sw(sheetPath, "jpg");
    sw.setQuality(95);
    sw.write(sheet);
    std::printf("wrote %s (%dx%d px)\n", sheetPath.toUtf8().constData(), sheet.width(), sheet.height());
    core::PrintComposer::Layout layout = core::PrintComposer::computeLayout(s, final.width(), final.height());
    std::printf("sheet layout: %dx%d cols/rows, maxFit=%d count=%d\n", layout.cols, layout.rows, layout.maxFit,
                layout.count);

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test-export") == 0 && i + 2 < argc) {
            QApplication app(argc, argv);
            return runTestExport(argv[i + 1], argv[i + 2]);
        }
    }

    QApplication app(argc, argv);
    ihv::app::MainWindow window;
    window.show();
    return app.exec();
}
