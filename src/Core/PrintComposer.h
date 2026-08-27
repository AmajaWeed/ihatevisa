#pragma once

#include <QImage>

#include "Core/EditorState.h"
#include "Core/PhotoDocument.h"

namespace ihv::core::PrintComposer {

// Sheet size is fixed at 100x150mm (10x15cm) regardless of the photo
// format — matches the reference's hardcoded print size (line 122, 724).
inline constexpr double SheetWidthMm = 100;
inline constexpr double SheetHeightMm = 150;

struct Layout {
    int cols = 0;
    int rows = 0;
    int maxFit = 0;   // how many copies actually fit given margin/gap
    int count = 0;    // min(printPhotoCount, maxFit)
};

// How many copies of the rendered photo fit on the sheet at this DPI/
// margin/gap. Exposed separately so the UI can show "Макс.: N (colxrow)"
// without re-rendering the whole sheet (mirrors renderPrint()'s
// maxC/maxR/count, line 737-740).
Layout computeLayout(const EditorState& s, int photoPxW, int photoPxH);

// Builds the full print sheet: N copies of the final-rendered photo
// (rendered fresh at printDpi, independent of the format's on-screen dpi)
// on a white 100x150mm sheet, with margins/gap and an optional border.
// Ported 1:1 from buildFullPrintCanvas() (line 761-780).
QImage buildSheet(const EditorState& s, const PhotoDocument& doc);

}  // namespace ihv::core::PrintComposer
