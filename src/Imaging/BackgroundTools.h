#pragma once

#include <QImage>
#include <QPoint>

namespace ihv::imaging::BackgroundTools {

// Samples the 4 photo corners for a background reference color and
// progressively whitens pixels within `threshold` distance of it. Ported
// from autoCleanBackground() (line 390). Returns a new ARGB32 image.
QImage autoClean(const QImage& src, int threshold);

// Stack-based flood fill from `seed` (in source-image pixel coordinates),
// setting alpha=0 on the connected region whose color is within
// `threshold` of the seed pixel. Ported from doMagicWand()'s fill loop
// (line 446-466) — the coordinate-mapping half of doMagicWand (screen px
// -> source px through the inverse guide transform) is the caller's job
// (EditorWidget), this function only does the flood fill itself. Returns a
// new ARGB32 image; `seed` must be within bounds or the input is returned
// unchanged.
QImage magicWandFill(const QImage& src, QPoint seed, int threshold);

}  // namespace ihv::imaging::BackgroundTools
