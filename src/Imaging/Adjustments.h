#pragma once

#include <QImage>

#include "Core/EditorState.h"

namespace ihv::imaging::Adjustments {

// Ported from applyAdj() (lines 574-582): brightness/contrast/saturate (the
// CSS `filter` chain in the reference — replicated via the CSS Filter
// Effects brightness/contrast/saturate formulas, applied in that order),
// then a manual gamma pow-curve, then the 3-way shadows/mid/highlights
// color-correction shift. Returns a new image resized to (w,h).
QImage apply(const QImage& src, const core::EditorState& s, int w, int h);

}  // namespace ihv::imaging::Adjustments
