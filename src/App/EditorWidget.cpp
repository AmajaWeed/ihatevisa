#include "App/EditorWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

#include "Imaging/BackgroundTools.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ihv::app {

using core::EditorRenderer::Mode;

EditorWidget::EditorWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(200, 200);
}

QSize EditorWidget::fitCanvasSize() const {
    if (!state_) return QSize(1, 1);
    // Ported from render()'s canvas-size fit (lines 589-592): fit the
    // format's aspect ratio into the available widget area, capped at
    // 900px tall / 700px wide, mirroring the reference's own caps.
    double aw = std::max(1, width() - 40), ah = std::max(1, height() - 40);
    double fA = state_->widthMm / state_->heightMm;
    double dw, dh;
    if (aw / ah > fA) {
        dh = std::min(ah, 900.0);
        dw = dh * fA;
    } else {
        dw = std::min(aw, 700.0);
        dh = dw / fA;
    }
    return QSize(std::max(1, static_cast<int>(std::round(dw))), std::max(1, static_cast<int>(std::round(dh))));
}

void EditorWidget::paintEvent(QPaintEvent*) {
    QPainter widgetPainter(this);
    widgetPainter.fillRect(rect(), QColor(0x1a, 0x1a, 0x2e));
    if (!state_ || !photo_) return;

    canvasSize_ = fitCanvasSize();
    QImage frame = core::EditorRenderer::render(*state_, *photo_, canvasSize_.width(), canvasSize_.height(), mode_);
    QPoint origin((width() - canvasSize_.width()) / 2, (height() - canvasSize_.height()) / 2);
    widgetPainter.drawImage(origin, frame);
}

void EditorWidget::resizeEvent(QResizeEvent*) { update(); }

QPoint EditorWidget::mapToSourcePixel(const QPoint& widgetPos) const {
    // Ported from doMagicWand()'s screen->source mapping (lines 420-444).
    QPoint origin((width() - canvasSize_.width()) / 2, (height() - canvasSize_.height()) / 2);
    double px = widgetPos.x() - origin.x(), py = widgetPos.y() - origin.y();

    int dw = canvasSize_.width(), dh = canvasSize_.height();
    core::EditorRenderer::Geometry geo = core::EditorRenderer::computeGeometry(*state_, dw, dh);
    double pxMm = geo.pxMm, targetY = geo.targetY;
    bool isReady = mode_ == Mode::Preview;

    const QImage& src = photo_->displayImage();
    int srcW = src.width(), srcH = src.height();
    if (srcW <= 0 || srcH <= 0) return QPoint(-1, -1);
    double phA = static_cast<double>(srcW) / srcH;
    double imgW = dw, imgH = dw / phA;
    if (imgH < dh) {
        imgH = dh;
        imgW = dh * phA;
    }
    double imgX = (dw - imgW) / 2, imgY = (dh - imgH) / 2;

    if (isReady) {
        double sc = state_->guide.scale, k = 1.0 / sc;
        double nw = imgW * k, nh = imgH * k;
        double X0 = (dw - imgW) / 2, Y0 = (dh - imgH) / 2;
        double Fx = dw / 2.0 + state_->guide.x * pxMm;
        double Fy = targetY * sc + state_->guide.y * pxMm;
        imgX = dw / 2.0 - k * (Fx - X0);
        imgY = targetY - k * (Fy - Y0);
        imgW = nw;
        imgH = nh;
    }

    double ux = px, uy = py;
    if (isReady && std::abs(state_->guide.rotation) > 0.01) {
        double rad = state_->guide.rotation * M_PI / 180.0, c = std::cos(rad), sn = std::sin(rad);
        double ddx = px - dw / 2.0, ddy = py - targetY;
        ux = dw / 2.0 + ddx * c - ddy * sn;
        uy = targetY + ddx * sn + ddy * c;
    }

    int sx = static_cast<int>(std::round((ux - imgX) / imgW * srcW));
    int sy = static_cast<int>(std::round((uy - imgY) / imgH * srcH));
    if (sx < 0 || sy < 0 || sx >= srcW || sy >= srcH) return QPoint(-1, -1);
    return QPoint(sx, sy);
}

void EditorWidget::mousePressEvent(QMouseEvent* e) {
    if (!state_ || !photo_) return;
    QPoint origin((width() - canvasSize_.width()) / 2, (height() - canvasSize_.height()) / 2);
    QPoint local = e->pos() - origin;

    if (tool_ == ToolMode::Wand && mode_ == Mode::Preview) {
        QPoint src = mapToSourcePixel(e->pos());
        if (src.x() >= 0 && onWandClick) onWandClick(src);
        return;
    }
    if ((tool_ == ToolMode::BrushRestore || tool_ == ToolMode::BrushErase) && mode_ == Mode::Preview) {
        if (onDragStart) onDragStart();
        drag_ = DragType::Brush;
        QPoint src = mapToSourcePixel(e->pos());
        if (src.x() >= 0 && onBrushPaint) onBrushPaint(src, tool_ == ToolMode::BrushRestore);
        return;
    }

    if (mode_ == Mode::Editing && tool_ == ToolMode::Guide) {
        core::EditorRenderer::GuideDots dots;
        if (core::EditorRenderer::computeGuideDots(*state_, canvasSize_.width(), canvasSize_.height(), dots)) {
            double dTop = std::hypot(local.x() - dots.top.x(), local.y() - dots.top.y());
            double dBot = std::hypot(local.x() - dots.bot.x(), local.y() - dots.bot.y());
            if (dTop < 16 || dBot < 16) {
                if (onDragStart) onDragStart();
                bool hitTop = dTop < 16;
                drag_ = hitTop ? DragType::GuideDotTop : DragType::GuideDotBot;
                dragFixedDot_ = hitTop ? dots.bot : dots.top;
                return;
            }
        }
    }

    if (onDragStart) onDragStart();
    drag_ = DragType::GuideMove;
    dragStartMouse_ = e->pos();
    dragStartGuideX_ = state_->guide.x;
    dragStartGuideY_ = state_->guide.y;
    setCursor(Qt::ClosedHandCursor);
}

void EditorWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!state_ || drag_ == DragType::None) return;

    if (drag_ == DragType::Brush) {
        QPoint src = mapToSourcePixel(e->pos());
        if (src.x() >= 0 && onBrushPaint) onBrushPaint(src, tool_ == ToolMode::BrushRestore);
        update();
        return;
    }

    double pxMm = canvasSize_.width() / state_->widthMm;
    if (drag_ == DragType::GuideMove) {
        state_->guide.x = dragStartGuideX_ + (e->pos().x() - dragStartMouse_.x()) / pxMm;
        state_->guide.y = dragStartGuideY_ + (e->pos().y() - dragStartMouse_.y()) / pxMm;
    } else {
        QPoint origin((width() - canvasSize_.width()) / 2, (height() - canvasSize_.height()) / 2);
        QPointF local = e->pos() - origin;
        QPointF topPx = drag_ == DragType::GuideDotTop ? local : dragFixedDot_;
        QPointF botPx = drag_ == DragType::GuideDotBot ? local : dragFixedDot_;
        core::EditorRenderer::solveGuideFromDots(*state_, canvasSize_.width(), topPx, botPx);
    }
    if (onGuideChanged) onGuideChanged();
    update();
}

void EditorWidget::mouseReleaseEvent(QMouseEvent*) {
    bool wasDragging = drag_ != DragType::None;
    drag_ = DragType::None;
    setCursor(tool_ == ToolMode::Guide ? Qt::ArrowCursor : Qt::CrossCursor);
    if (wasDragging && onDragEnd) onDragEnd();
}

}  // namespace ihv::app
