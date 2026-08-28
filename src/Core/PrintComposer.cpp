#include "Core/PrintComposer.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

#include "Core/EditorRenderer.h"

namespace ihv::core::PrintComposer {

namespace {
int mmToPx(double mm, int dpi) { return static_cast<int>(std::round(mm / 25.4 * dpi)); }
}  // namespace

Layout computeLayout(const EditorState& s, int photoPxW, int photoPxH) {
    int ppW = mmToPx(SheetWidthMm, s.printDpi), ppH = mmToPx(SheetHeightMm, s.printDpi);
    int mgPx = mmToPx(s.printMarginMm, s.printDpi), gPx = mmToPx(s.printGapMm, s.printDpi);
    Layout l;
    l.cols = std::max(0, (ppW - 2 * mgPx + gPx) / (photoPxW + gPx));
    l.rows = std::max(0, (ppH - 2 * mgPx + gPx) / (photoPxH + gPx));
    l.maxFit = l.cols * l.rows;
    l.count = std::min(s.printPhotoCount, l.maxFit);
    return l;
}

QImage buildSheet(const EditorState& s, const PhotoDocument& doc) {
    int dpi = s.printDpi > 0 ? s.printDpi : 300;
    QImage photo = EditorRenderer::renderFinal(s, doc, dpi);

    int ppW = mmToPx(SheetWidthMm, dpi), ppH = mmToPx(SheetHeightMm, dpi);
    int mgPx = mmToPx(s.printMarginMm, dpi), gPx = mmToPx(s.printGapMm, dpi);
    Layout layout = computeLayout(s, photo.width(), photo.height());
    int cols = layout.cols, rows = std::max(1, static_cast<int>(std::ceil(layout.count / double(std::max(1, cols)))));

    QImage sheet(ppW, ppH, QImage::Format_ARGB32);
    sheet.fill(Qt::white);
    QPainter p(&sheet);
    int bw = mmToPx(0.3, dpi);

    // Anchor the grid at the margin by default (matches the old
    // behavior), or center the whole used block within the sheet if
    // requested — leftover space split evenly instead of all pushed to
    // the bottom-right. Either way, printOffsetXMm/Ymm lets the user nudge
    // the whole block afterwards, e.g. to dodge a printer's streaking band
    // that always falls in the same spot on the page.
    int usedW = cols * photo.width() + (cols - 1) * gPx;
    int usedH = rows * photo.height() + (rows - 1) * gPx;
    int baseX = mgPx, baseY = mgPx;
    if (s.printCenter) {
        baseX = std::max(mgPx, (ppW - usedW) / 2);
        baseY = std::max(mgPx, (ppH - usedH) / 2);
    }
    baseX += mmToPx(s.printOffsetXMm, dpi);
    baseY += mmToPx(s.printOffsetYMm, dpi);

    int placed = 0;
    for (int r = 0; r < rows && placed < layout.count; ++r) {
        for (int c = 0; c < cols && placed < layout.count; ++c) {
            int px = baseX + c * (photo.width() + gPx);
            int py = baseY + r * (photo.height() + gPx);
            if (s.printBorder) {
                p.fillRect(QRect(px - bw, py - bw, photo.width() + 2 * bw, photo.height() + 2 * bw), Qt::black);
            }
            p.drawImage(QPoint(px, py), photo);
            ++placed;
        }
    }
    return sheet;
}

}  // namespace ihv::core::PrintComposer
