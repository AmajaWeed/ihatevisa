#pragma once

#include <QWidget>
#include <functional>
#include <optional>

#include "Core/EditorRenderer.h"
#include "Core/EditorState.h"
#include "Core/PhotoDocument.h"

namespace ihv::app {

// Custom-painted editing surface — direct port of the reference's single
// <canvas id="mainCanvas"> plus its mousedown/mousemove/mouseup handlers
// (setupCanvas(), line 509-556). Owns no state itself: it reads/writes the
// EditorState the host (MainWindow) points it at, and calls back via the
// std::function hooks below instead of Qt signals, to keep this a plain
// rendering+input surface.
class EditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit EditorWidget(QWidget* parent = nullptr);

    void setMode(core::EditorRenderer::Mode mode) { mode_ = mode; update(); }
    void setPhoto(core::PhotoDocumentPtr photo) { photo_ = std::move(photo); update(); }
    void setState(core::EditorState* state) { state_ = state; update(); }
    void setWandMode(bool on) { wandMode_ = on; setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor); }
    bool wandMode() const { return wandMode_; }

    // Called before any drag mutates state, so the host can push an undo
    // snapshot first (mirrors pushUndo() at mousedown, lines 521/528/533).
    std::function<void()> onDragStart;
    // Called after guide.x/y/scale changes during a drag, so the host can
    // refresh other panels (mini preview, "70%"-style info label, etc).
    std::function<void()> onGuideChanged;
    // Called on a wand-mode click, with the *source-image* pixel coordinate
    // already resolved through the inverse guide transform.
    std::function<void(QPoint)> onWandClick;

    QSize lastCanvasSize() const { return canvasSize_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QSize fitCanvasSize() const;
    QPoint mapToSourcePixel(const QPoint& widgetPos) const;

    core::EditorRenderer::Mode mode_ = core::EditorRenderer::Mode::Editing;
    core::PhotoDocumentPtr photo_;
    core::EditorState* state_ = nullptr;
    bool wandMode_ = false;
    QSize canvasSize_;

    enum class DragType { None, GuideMove, GuideDotTop, GuideDotBot };
    DragType drag_ = DragType::None;
    QPoint dragStartMouse_;
    double dragStartGuideX_ = 0, dragStartGuideY_ = 0;
    QPointF dragFixedDot_;
};

}  // namespace ihv::app
