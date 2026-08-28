#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>

namespace ihv::imaging::BackgroundTools {

// Samples the 4 photo corners for a background reference color and
// progressively blends pixels within `threshold` distance of it toward
// `targetColor` (the document's selected background color — the oval
// vignette and corner overlay masks fill with this same color, so this
// must match or a visible seam appears where they meet unprocessed
// pixels). Ported from autoCleanBackground() (line 390). Returns a new
// ARGB32 image.
QImage autoClean(const QImage& src, int threshold, const QColor& targetColor = Qt::white);

// Stack-based flood fill from `seed` (in source-image pixel coordinates),
// setting alpha=0 on the connected region whose color is within
// `threshold` of the seed pixel. Ported from doMagicWand()'s fill loop
// (line 446-466) — the coordinate-mapping half of doMagicWand (screen px
// -> source px through the inverse guide transform) is the caller's job
// (EditorWidget), this function only does the flood fill itself. Returns a
// new ARGB32 image; `seed` must be within bounds or the input is returned
// unchanged.
QImage magicWandFill(const QImage& src, QPoint seed, int threshold);

// Paints a soft-edged circular brush of alpha `targetAlpha` (0 = erase,
// 255 = restore) into `img` in place, for touch-up after auto-clean/magic
// wand. `center`/`radiusPx` are in source-image pixel coordinates. The
// brush itself is feathered (full strength at the center, fading to no
// effect at the radius) so repeated dabs blend smoothly instead of
// leaving a hard stamped edge. `opacity` (0..1) scales how strongly a
// single dab moves a pixel toward targetAlpha — at 1.0 the center of the
// brush snaps straight there same as before; below that, even the center
// only partially blends, so a slow stroke can build up gradually instead
// of always fully restoring/erasing in one pass.
void paintBrush(QImage& img, QPoint center, int radiusPx, int targetAlpha, double opacity = 1.0);

}  // namespace ihv::imaging::BackgroundTools
