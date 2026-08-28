#include "Core/EditorRenderer.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

#include "Imaging/Adjustments.h"

namespace ihv::core::EditorRenderer {

namespace {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

QColor bgColor(const EditorState& s) { return QColor(QString::fromStdString(s.bgColorHex)); }

// Ported from the oval-mask fill('evenodd') trick (lines 656/715): fills
// everything inside `rect` but outside the ellipse with bg color, leaving an
// oval "window" onto the photo underneath.
void paintOvalMask(QPainter& p, const QRectF& rect, const QColor& bg) {
    double cy = rect.center().y();
    double ry = rect.height() / 2 * 1.35;
    double rx = ry * 0.78;
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    path.addRect(rect);
    path.addEllipse(QPointF(rect.center().x(), cy), rx, ry);
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawPath(path);
    p.restore();
}

void paintCornerMask(QPainter& p, double w, double h, double pxMm, const QColor& color, CornerPosition pos) {
    double cs = 12 * pxMm;
    double cx = (pos == CornerPosition::TopLeft || pos == CornerPosition::BottomLeft) ? 0 : w;
    double cy = (pos == CornerPosition::TopLeft || pos == CornerPosition::TopRight) ? 0 : h;
    double sx = cx == 0 ? cs : -cs;
    double sy = cy == 0 ? cs : -cs;
    QPainterPath path;
    path.moveTo(cx, cy);
    path.lineTo(cx + sx, cy);
    path.lineTo(cx, cy + sy);
    path.closeSubpath();
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);
    p.restore();
}

}  // namespace

Geometry computeGeometry(const EditorState& s, int canvasW, int) {
    double pxMm = canvasW / s.widthMm;
    double targetY = pxMm * (s.topMarginMm + s.headSizeMm / 2);
    return {pxMm, targetY};
}

QImage render(const EditorState& s, const PhotoDocument& doc, int canvasW, int canvasH, Mode mode) {
    int dw = std::max(1, canvasW), dh = std::max(1, canvasH);
    Geometry geo = computeGeometry(s, dw, dh);
    double pxMm = geo.pxMm, targetY = geo.targetY;

    const QImage& src = doc.displayImage();
    double phA = src.height() != 0 ? static_cast<double>(src.width()) / src.height() : 1.0;
    double imgW = dw, imgH = dw / phA;
    if (imgH < dh) {
        imgH = dh;
        imgW = dh * phA;
    }
    double imgX = (dw - imgW) / 2, imgY = (dh - imgH) / 2;

    bool preview = mode == Mode::Preview;
    if (preview) {
        double sc = s.guide.scale, k = 1.0 / sc;
        double nw = imgW * k, nh = imgH * k;
        double X0 = (dw - imgW) / 2, Y0 = (dh - imgH) / 2;
        double Fx = dw / 2.0 + s.guide.x * pxMm;
        double Fy = targetY * sc + s.guide.y * pxMm;
        imgX = dw / 2.0 - k * (Fx - X0);
        imgY = targetY - k * (Fy - Y0);
        imgW = nw;
        imgH = nh;
    }

    QImage canvas(dw, dh, QImage::Format_ARGB32);
    canvas.fill(bgColor(s));
    QImage adj = imaging::Adjustments::apply(src, s, std::max(1, static_cast<int>(std::round(imgW))),
                                              std::max(1, static_cast<int>(std::round(imgH))));

    {
        QPainter p(&canvas);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setClipRect(QRectF(0, 0, dw, dh));
        if (preview) {
            p.translate(dw / 2.0, targetY);
            p.rotate(-s.guide.rotation);
            p.translate(-dw / 2.0, -targetY);
        }
        p.drawImage(QRectF(std::round(imgX), std::round(imgY), std::round(imgW), std::round(imgH)), adj);
    }

    if (!preview) {
        // === Editing-mode guide overlay (lines 613-654) ===
        QPainter p(&canvas);
        p.setRenderHint(QPainter::Antialiasing, true);
        double gs = s.guide.scale, gx = s.guide.x * pxMm, gy = s.guide.y * pxMm;
        double topY = s.topMarginMm * gs * pxMm + gy;
        double chinY = (s.topMarginMm + s.headSizeMm) * gs * pxMm + gy;
        double cxG = dw / 2.0 + gx, gcx = cxG, gcy = (topY + chinY) / 2;
        double rad = s.guide.rotation * M_PI / 180.0;
        double ry = (chinY - topY) / 2, rx = ry * 0.72;

        p.save();
        p.translate(gcx, gcy);
        p.rotate(s.guide.rotation);
        p.translate(-gcx, -gcy);
        QPen ovalPen(QColor(233, 69, 96, 178));
        ovalPen.setWidthF(3);
        ovalPen.setStyle(Qt::DashLine);
        p.setPen(ovalPen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(gcx, gcy), rx, ry);
        p.restore();

        QPolygonF quad = computeCropQuad(s, dw, dh);
        QPen quadPen(QColor(80, 160, 255, 217));
        quadPen.setWidthF(3);
        quadPen.setStyle(Qt::DashLine);
        p.setPen(quadPen);
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(quad);

        GuideDots dots;
        if (computeGuideDots(s, dw, dh, dots)) {
            for (const QPointF& pt : {dots.top, dots.bot}) {
                p.setPen(QPen(Qt::white, 2.5));
                p.setBrush(QColor(233, 69, 96, 230));
                p.drawEllipse(pt, 8, 8);
            }
        }
    } else {
        if (s.ovalOverlay) {
            double topY = s.topMarginMm * pxMm, chinY = (s.topMarginMm + s.headSizeMm) * pxMm;
            QRectF r(0, topY, dw, chinY - topY);
            QPainter p(&canvas);
            paintOvalMask(p, r, bgColor(s));
        }
    }
    if (s.cornerOverlay) {
        QPainter p(&canvas);
        QColor c = preview ? bgColor(s) : QColor(200, 200, 200, 128);
        paintCornerMask(p, dw, dh, pxMm, c, s.cornerPosition);
    }

    return canvas;
}

QImage renderFinal(const EditorState& s, const PhotoDocument& doc, int dpi) {
    int pxW = static_cast<int>(std::round(s.widthMm / 25.4 * dpi));
    int pxH = static_cast<int>(std::round(s.heightMm / 25.4 * dpi));
    return render(s, doc, pxW, pxH, Mode::Preview);
}

bool computeGuideDots(const EditorState& s, int canvasW, int canvasH, GuideDots& out) {
    if (canvasW <= 0) return false;
    Geometry geo = computeGeometry(s, canvasW, canvasH);
    double pxMm = geo.pxMm;
    double gs = s.guide.scale, gx = s.guide.x * pxMm, gy = s.guide.y * pxMm;
    double topYBase = s.topMarginMm * gs * pxMm + gy;
    double chinYBase = (s.topMarginMm + s.headSizeMm) * gs * pxMm + gy;
    double cxBase = canvasW / 2.0 + gx;
    double gcx = cxBase, gcy = (topYBase + chinYBase) / 2;
    double rad = s.guide.rotation * M_PI / 180.0, c = std::cos(rad), sn = std::sin(rad);
    auto rot = [&](double x, double y) {
        double dx = x - gcx, dy = y - gcy;
        return QPointF(gcx + dx * c - dy * sn, gcy + dx * sn + dy * c);
    };
    out.top = rot(cxBase, topYBase);
    out.bot = rot(cxBase, chinYBase);
    return true;
}

void solveGuideFromDots(EditorState& s, int canvasW, const QPointF& topPx, const QPointF& botPx) {
    double pxMm = canvasW / s.widthMm;
    double dx = botPx.x() - topPx.x();
    double dy = botPx.y() - topPx.y();
    double dist = std::hypot(dx, dy);
    double baseDist = s.headSizeMm * pxMm;
    s.guide.scale = std::clamp(baseDist != 0 ? dist / baseDist : 1.0, 0.3, 3.0);
    // Rotation is now derived from the dot-drag itself (dragging the dots
    // off the vertical implies a tilt) instead of a separate slider — the
    // midpoint/scale math below is unaffected by rotation either way (the
    // rotation happens around the midpoint itself, so it cancels out of
    // the average). Locked-vertical mode ignores any horizontal deviation.
    if (!s.lockVertical) s.guide.rotation = std::atan2(-dx, dy) * 180.0 / M_PI;
    double mx = (topPx.x() + botPx.x()) / 2, my = (topPx.y() + botPx.y()) / 2;
    s.guide.x = (mx - canvasW / 2.0) / pxMm;
    s.guide.y = (my - (s.topMarginMm + s.headSizeMm / 2) * s.guide.scale * pxMm) / pxMm;
}

QPolygonF computeCropQuad(const EditorState& s, int canvasW, int canvasH) {
    Geometry geo = computeGeometry(s, canvasW, canvasH);
    double pxMm = geo.pxMm, targetY = geo.targetY;
    double gs = s.guide.scale, gx = s.guide.x * pxMm, gy = s.guide.y * pxMm;
    double rad = s.guide.rotation * M_PI / 180.0;
    double Fx = canvasW / 2.0 + gx, Fy = targetY * gs + gy;
    double pivX = canvasW / 2.0, pivY = targetY, cosA = std::cos(rad), sinA = std::sin(rad);

    QPolygonF poly;
    for (auto [px, py] : {std::pair{0.0, 0.0}, {double(canvasW), 0.0}, {double(canvasW), double(canvasH)},
                           {0.0, double(canvasH)}}) {
        double ddx = px - pivX, ddy = py - pivY;
        double rx2 = pivX + ddx * cosA - ddy * sinA, ry2 = pivY + ddx * sinA + ddy * cosA;
        poly << QPointF(Fx + (rx2 - canvasW / 2.0) * gs, Fy + (ry2 - targetY) * gs);
    }
    return poly;
}

}  // namespace ihv::core::EditorRenderer
