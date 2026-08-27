#pragma once

#include <QImage>
#include <QPointF>
#include <QPolygonF>

#include "Core/EditorState.h"
#include "Core/PhotoDocument.h"

namespace ihv::core::EditorRenderer {

enum class Mode {
    // "Sizes" tab: the full source photo shown cover-fit (unguided), with
    // an oval/blue-quad/red-dot overlay indicating the future crop.
    Editing,
    // "Ready" tab and export: the photo actually cropped/positioned by the
    // guide transform to fill the frame, with oval/corner masks baked in.
    Preview,
};

// px-per-mm and the head-center anchor y (in canvas px) for a canvasW x
// canvasH render at this format. Ported from the repeated `pxMm`/`targetY`
// computation at the top of render()/renderFinal() (lines 593-595, 702-703).
struct Geometry {
    double pxMm;
    double targetY;
};
Geometry computeGeometry(const EditorState& s, int canvasW, int canvasH);

// The single shared renderer — ported from render()'s !isReady/isReady
// branches (lines 585-662) and renderFinal() (lines 699-718), which are the
// same transform at different target resolutions. `canvasW`/`canvasH` must
// already be sized to the format's aspect ratio (widthMm/heightMm) by the
// caller — layout-fitting to the available screen area is a UI concern, not
// the renderer's.
QImage render(const EditorState& s, const PhotoDocument& doc, int canvasW, int canvasH, Mode mode);

// Convenience: Preview-mode render at `mm/25.4*dpi` resolution — the exact
// path used for both on-screen "ready" preview and file export, guaranteeing
// what you see is what you export.
QImage renderFinal(const EditorState& s, const PhotoDocument& doc, int dpi);

// Crown/chin handle positions in canvas px, for hit-testing and dragging.
// Ported from getGuideDots() (line 472-485). Returns false if canvasW<=0.
struct GuideDots {
    QPointF top, bot;
};
bool computeGuideDots(const EditorState& s, int canvasW, int canvasH, GuideDots& out);

// Derives guide.scale/x/y from two dragged dot pixel positions. Rotation is
// deliberately untouched — ported from solveGuideFromDots() (line 493-504).
void solveGuideFromDots(EditorState& s, int canvasW, const QPointF& topPx, const QPointF& botPx);

// The dashed "actual future crop" quadrilateral shown in Editing mode,
// traced by mapping the render canvas's 4 corners back through the guide
// transform. Ported from the blue-quad block in render() (lines 629-641).
QPolygonF computeCropQuad(const EditorState& s, int canvasW, int canvasH);

}  // namespace ihv::core::EditorRenderer
