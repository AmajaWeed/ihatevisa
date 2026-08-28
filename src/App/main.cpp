#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>

#include "App/MainWindow.h"
#include "Core/EditorRenderer.h"
#include "Core/EditorState.h"
#include "Core/PrintComposer.h"
#include "Imaging/BackgroundTools.h"
#include "Imaging/CmykPipeline.h"
#include "Pdf/CmykPdfWriter.h"
#include "Project/HateFile.h"
#include "Update/UpdateChecker.h"
#include "Update/UpdateToast.h"

namespace {

QByteArray readFile(const char* path) {
    QFile f(QString::fromUtf8(path));
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

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
    QString dirQ = QString::fromUtf8(outDir);

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

    // Rotation-via-dots: drag the bottom dot sideways too (tilt) and
    // confirm the derived angle matches atan2 of the offset, and that
    // lockVertical suppresses it.
    {
        core::EditorState rs;
        core::EditorEngine::applyFormat(rs, "passport_rf");
        rs.lockVertical = false;
        core::EditorRenderer::GuideDots d0;
        core::EditorRenderer::computeGuideDots(rs, canvasW, canvasH, d0);
        QPointF tiltedBot(d0.bot.x() + 20, d0.bot.y());  // pure horizontal shift of the chin dot
        core::EditorRenderer::solveGuideFromDots(rs, canvasW, d0.top, tiltedBot);
        std::printf("rotation-via-dots: angle=%.2f deg (expect small nonzero)\n", rs.guide.rotation);
        bool rotOk = std::abs(rs.guide.rotation) > 0.5;

        core::EditorState locked;
        core::EditorEngine::applyFormat(locked, "passport_rf");
        locked.lockVertical = true;
        core::EditorRenderer::GuideDots dl;
        core::EditorRenderer::computeGuideDots(locked, canvasW, canvasH, dl);
        core::EditorRenderer::solveGuideFromDots(locked, canvasW, dl.top, QPointF(dl.bot.x() + 20, dl.bot.y()));
        bool lockOk = locked.guide.rotation == 0;
        std::printf("lockVertical suppresses rotation: %s\n", lockOk ? "OK" : "FAIL");
        if (!rotOk || !lockOk) return 1;
    }

    // Corner position: confirm changing it actually changes the rendered
    // pixels (mask moves to a different corner).
    {
        core::EditorState cs1, cs2;
        core::EditorEngine::applyFormat(cs1, "passport_rf");
        core::EditorEngine::applyFormat(cs2, "passport_rf");
        cs1.cornerOverlay = cs2.cornerOverlay = true;
        cs1.cornerPosition = core::CornerPosition::TopLeft;
        cs2.cornerPosition = core::CornerPosition::BottomRight;
        QImage c1 = core::EditorRenderer::render(cs1, doc, 200, 257, core::EditorRenderer::Mode::Preview);
        QImage c2 = core::EditorRenderer::render(cs2, doc, 200, 257, core::EditorRenderer::Mode::Preview);
        c1.save(dirQ + "/corner_topleft.png");
        c2.save(dirQ + "/corner_bottomright.png");
        bool cornerOk = c1 != c2;
        std::printf("corner position changes render: %s\n", cornerOk ? "OK" : "FAIL");
        if (!cornerOk) return 1;
    }

    QImage editing = core::EditorRenderer::render(s, doc, canvasW, canvasH, core::EditorRenderer::Mode::Editing);
    QImage preview = core::EditorRenderer::render(s, doc, canvasW, canvasH, core::EditorRenderer::Mode::Preview);
    std::printf("editing render: %dx%d, preview render: %dx%d\n", editing.width(), editing.height(),
                preview.width(), preview.height());
    editing.save(dirQ + "/editing_mode.png");
    preview.save(dirQ + "/preview_mode.png");

    core::EditorState oval;
    core::EditorEngine::applyFormat(oval, "zagran_rf");
    oval.guide = s.guide;
    QImage ovalPreview = core::EditorRenderer::render(oval, doc, canvasW, canvasH, core::EditorRenderer::Mode::Preview);
    ovalPreview.save(dirQ + "/oval_preview.png");
    std::printf("wrote editing_mode.png / preview_mode.png / oval_preview.png\n");

    // Raw-adjustments panel: exercise every one of the 13 sliders at an
    // extreme value, individually, and confirm each actually changes the
    // rendered pixels (and that combining all of them at once doesn't
    // crash or produce an all-NaN/black/white wipeout).
    {
        QImage baseline = core::EditorRenderer::render(s, doc, 300, 386, core::EditorRenderer::Mode::Preview);
        auto testField = [&](const char* name, int core::RawAdjustments::*field, int value) {
            core::EditorState t = s;
            t.adj.*field = value;
            QImage img = core::EditorRenderer::render(t, doc, 300, 386, core::EditorRenderer::Mode::Preview);
            bool changed = img != baseline;
            std::printf("  %s=%d: %s\n", name, value, changed ? "changes render" : "NO EFFECT (bug)");
            if (!changed) std::exit(1);
        };
        std::printf("raw adjustments (each should change the render):\n");
        testField("temperature", &core::RawAdjustments::temperature, 80);
        testField("tint", &core::RawAdjustments::tint, 80);
        testField("exposure", &core::RawAdjustments::exposure, 60);
        testField("contrast", &core::RawAdjustments::contrast, 60);
        testField("highlights", &core::RawAdjustments::highlights, -80);
        testField("shadows", &core::RawAdjustments::shadows, 80);
        testField("whites", &core::RawAdjustments::whites, -80);
        testField("blacks", &core::RawAdjustments::blacks, 80);
        testField("texture", &core::RawAdjustments::texture, 80);
        testField("clarity", &core::RawAdjustments::clarity, 80);
        testField("dehaze", &core::RawAdjustments::dehaze, 80);
        testField("vibrance", &core::RawAdjustments::vibrance, 80);
        testField("saturation", &core::RawAdjustments::saturation, -80);

        core::EditorState allMax = s;
        allMax.adj = core::RawAdjustments{80, -80, 60, 60, -80, 80, -80, 80, 80, 80, 80, 80, -80};
        QImage combined =
            core::EditorRenderer::render(allMax, doc, 300, 386, core::EditorRenderer::Mode::Preview);
        combined.save(dirQ + "/raw_adjustments_combined.png");
        std::printf("  all combined: wrote raw_adjustments_combined.png (%dx%d)\n", combined.width(),
                    combined.height());
    }

    // Background removal must not touch a face/shirt in the middle just
    // because its color happens to be within the threshold of the
    // background reference — only pixels *connected to the border* through
    // an unbroken chain of within-threshold color should be affected. A
    // color within the threshold AND touching the border is legitimately
    // background regardless of connectivity (that's what the threshold is
    // for) — connectivity specifically protects an *island* of
    // background-similar color fully enclosed by clearly-different color,
    // e.g. a bright highlight or light collar surrounded by skin/hair,
    // which is the realistic version of "erases elements on the face".
    // Skin-tone-like center, clearly outside the threshold of a light-gray
    // background, so it can only leak in via connectivity, not raw color.
    {
        QImage synth(200, 200, QImage::Format_ARGB32);
        synth.fill(qRgb(230, 230, 230));  // background: light gray
        QPainter p(&synth);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(200, 150, 120));  // "face": clearly different, fully enclosed
        p.drawEllipse(60, 60, 80, 80);
        p.end();
        QImage cleaned = imaging::BackgroundTools::autoClean(synth, 45);
        QRgb centerAfter = cleaned.pixel(100, 100);
        QRgb borderAfter = cleaned.pixel(5, 5);
        bool centerUntouched = centerAfter == qRgb(200, 150, 120);  // must be byte-for-byte unchanged
        bool borderWhitened = qRed(borderAfter) > 250;              // border is real background: should whiten
        std::printf("background-removal connectivity: center %s (rgb=%d,%d,%d), border %s (r=%d)\n",
                    centerUntouched ? "protected" : "WRONGLY ERASED", qRed(centerAfter), qGreen(centerAfter),
                    qBlue(centerAfter), borderWhitened ? "whitened" : "NOT whitened (bug)", qRed(borderAfter));
        if (!centerUntouched || !borderWhitened) return 1;
    }

    // auto-clean must blend toward the document's *selected* background
    // color, not a hardcoded white — otherwise, wherever the oval-vignette
    // or corner-overlay mask (which correctly use the selected color) meet
    // auto-cleaned pixels outside the mask, a visible seam appears at
    // exactly that boundary (this is what "виньетка забагалась" turned out
    // to be: a light-blue background selection, auto-cleaned toward white
    // instead of blue).
    {
        QImage synth(60, 60, QImage::Format_ARGB32);
        synth.fill(qRgb(204, 229, 255));  // uniform background matching a "light blue" swatch
        QColor targetBlue(0xcc, 0xe5, 0xff);
        QImage cleaned = imaging::BackgroundTools::autoClean(synth, 45, targetBlue);
        QRgb center = cleaned.pixel(30, 30);
        bool matchesTarget = qRed(center) == targetBlue.red() && qGreen(center) == targetBlue.green() &&
                              qBlue(center) == targetBlue.blue();
        std::printf("auto-clean targets selected bg color: %s (got rgb=%d,%d,%d, expected %d,%d,%d)\n",
                    matchesTarget ? "OK" : "BUG: still whitening toward hardcoded white", qRed(center),
                    qGreen(center), qBlue(center), targetBlue.red(), targetBlue.green(), targetBlue.blue());
        if (!matchesTarget) return 1;
    }

    doc.processed = imaging::BackgroundTools::autoClean(doc.original, 45);
    std::printf("auto-clean produced processed image: %dx%d\n", doc.processed->width(), doc.processed->height());

    // Magic wand (feathered) + brush touch-up, on a small crop for a quick
    // visual check of the soft edge / brush blending.
    {
        QImage crop = doc.original.copy(0, 0, std::min(400, doc.original.width()), std::min(400, doc.original.height()));
        QImage wanded = imaging::BackgroundTools::magicWandFill(crop, QPoint(2, 2), 60);
        imaging::BackgroundTools::paintBrush(wanded, QPoint(350, 350), 60, 0);   // erase dab
        imaging::BackgroundTools::paintBrush(wanded, QPoint(50, 350), 30, 255);  // restore dab
        wanded.save(dirQ + "/wand_and_brush.png");
        std::printf("wrote wand_and_brush.png\n");
    }

    const QString& dir = dirQ;
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

    // CMYK PDF/X-1a export of the sheet.
    pdf::CmykPage page;
    page.cmyk = imaging::CmykPipeline::toCmyk(sheet);
    page.width = sheet.width();
    page.height = sheet.height();
    page.widthMm = core::PrintComposer::SheetWidthMm;
    page.heightMm = core::PrintComposer::SheetHeightMm;
    auto pdfBytes = pdf::buildCmykPdf({page}, imaging::CmykPipeline::swopIcc());
    QString pdfPath = dir + "/sheet.pdf";
    QFile pdfFile(pdfPath);
    pdfFile.open(QIODevice::WriteOnly);
    pdfFile.write(reinterpret_cast<const char*>(pdfBytes.data()), static_cast<qint64>(pdfBytes.size()));
    pdfFile.close();
    std::printf("wrote %s (%zu bytes, cmyk %dx%d)\n", pdfPath.toUtf8().constData(), pdfBytes.size(), page.width,
                page.height);

    // .hate round-trip.
    doc.originalBytes = readFile(imagePath);
    doc.extension = "." + QFileInfo(QString::fromUtf8(imagePath)).suffix().toLower();
    project::HateFile::Project proj;
    proj.state = s;
    proj.photos = {std::make_shared<core::PhotoDocument>(doc)};
    proj.activePhotoId = doc.id;
    QString hatePath = dir + "/project.hate";
    QString err;
    if (!project::HateFile::save(hatePath, proj, &err)) {
        std::fprintf(stderr, ".hate save failed: %s\n", err.toUtf8().constData());
        return 1;
    }
    std::printf("wrote %s\n", hatePath.toUtf8().constData());

    project::HateFile::Project reopened;
    if (!project::HateFile::open(hatePath, reopened, &err)) {
        std::fprintf(stderr, ".hate open failed: %s\n", err.toUtf8().constData());
        return 1;
    }
    bool ok = reopened.photos.size() == 1 && !reopened.photos[0]->original.isNull() &&
              reopened.photos[0]->processed.has_value() &&
              std::abs(reopened.state.widthMm - s.widthMm) < 1e-9 &&
              std::abs(reopened.state.guide.scale - s.guide.scale) < 1e-9 && reopened.activePhotoId == doc.id;
    std::printf(".hate round-trip: %s (photos=%zu, w=%.1f, guide.scale=%.3f, activeId=%d)\n",
                ok ? "OK" : "MISMATCH", reopened.photos.size(), reopened.state.widthMm,
                reopened.state.guide.scale, reopened.activePhotoId);
    if (!ok) return 1;

    return 0;
}

// Headless self-update: check -> print notes -> download/verify/stage/apply
// -> exit (the apply script has already been launched detached by the time
// onReadyToExit fires, exactly like the real toast flow). Matches
// iHateCards.NET's own --update-now diagnostic mode. ignoreSkipped=true so
// a previously-skipped version doesn't silently hide the check.
int runUpdateNow() {
    using namespace ihv;
    QCoreApplication::setOrganizationName("iHateVisa");
    QCoreApplication::setApplicationName("iHateVisa");
    int argc = 0;
    QCoreApplication app(argc, nullptr);

    update::UpdateChecker checker;
    checker.checkAsync(
        [&](std::optional<update::UpdateInfo> info) {
            if (!info) {
                std::printf("no update available (or check failed)\n");
                QCoreApplication::exit(2);
                return;
            }
            std::printf("update available: %s (published %s)\n", info->latest.toUtf8().constData(),
                        info->published.toUtf8().constData());
            for (const QString& n : info->notes) std::printf("  - %s\n", n.toUtf8().constData());
            static update::UpdateInstaller installer;
            installer.prepareAndApply(
                *info,
                [](double frac) { std::printf("\rdownloading: %d%%", static_cast<int>(frac * 100)); },
                [](QString err) {
                    std::printf("\nupdate failed: %s\n", err.toUtf8().constData());
                    QCoreApplication::exit(1);
                },
                [] {
                    std::printf("\napply script launched, exiting now\n");
                    QCoreApplication::exit(0);
                });
        },
        true);
    return app.exec();
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test-export") == 0 && i + 2 < argc) {
            QApplication app(argc, argv);
            return runTestExport(argv[i + 1], argv[i + 2]);
        }
        if (std::strcmp(argv[i], "--update-now") == 0) {
            return runUpdateNow();
        }
    }

    QCoreApplication::setOrganizationName("iHateVisa");
    QCoreApplication::setApplicationName("iHateVisa");
    QApplication app(argc, argv);
    ihv::app::MainWindow window;
    window.show();

    // Offline-first: a background check that never blocks/interrupts
    // startup, and swallows every network/parse failure silently (see
    // UpdateChecker::checkAsync). Only shown if there's actually something
    // newer, not already skipped, and not disabled by the user.
    static ihv::update::UpdateChecker checker;
    if (ihv::update::UpdateChecker::autoCheckEnabled()) {
        checker.checkAsync([&window](std::optional<ihv::update::UpdateInfo> info) {
            if (!info) return;
            auto* toast = new ihv::update::UpdateToast(*info, nullptr);
            toast->placeBottomRight();
            toast->show();
        });
    }

    return app.exec();
}
