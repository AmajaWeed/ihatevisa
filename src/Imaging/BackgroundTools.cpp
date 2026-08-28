#include "Imaging/BackgroundTools.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ihv::imaging::BackgroundTools {

QImage autoClean(const QImage& src, int threshold, const QColor& targetColor) {
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    int w = img.width(), h = img.height();
    if (w <= 0 || h <= 0) return img;

    auto samplePatch = [&](int x0, int y0, int size) {
        double r = 0, g = 0, b = 0;
        int n = 0;
        for (int y = y0; y < y0 + size; ++y) {
            for (int x = x0; x < x0 + size; ++x) {
                QRgb px = img.pixel(x, y);
                r += qRed(px);
                g += qGreen(px);
                b += qBlue(px);
                ++n;
            }
        }
        return std::array<double, 3>{r / n, g / n, b / n};
    };

    int ps = std::max(4, static_cast<int>(std::round(std::min(w, h) * 0.03)));
    ps = std::min(ps, std::min(w, h));
    auto c1 = samplePatch(0, 0, ps);
    auto c2 = samplePatch(std::max(0, w - ps), 0, ps);
    auto c3 = samplePatch(0, std::max(0, h - ps), ps);
    auto c4 = samplePatch(std::max(0, w - ps), std::max(0, h - ps), ps);
    double bgR = (c1[0] + c2[0] + c3[0] + c4[0]) / 4;
    double bgG = (c1[1] + c2[1] + c3[1] + c4[1]) / 4;
    double bgB = (c1[2] + c2[2] + c3[2] + c4[2]) / 4;

    // Whitening by color-distance alone would also hit anything in the
    // photo that happens to be a similar shade — a light shirt, pale skin,
    // hair with a bright highlight — even in the middle of the frame,
    // nowhere near the actual backdrop. The fix is connectivity, not just
    // color: only pixels reachable from the image border through an
    // unbroken chain of background-colored neighbors count as background.
    // A white collar surrounded by non-background color is not connected
    // to the border and is correctly left alone, no matter how close its
    // color is to the sampled corners.
    double th = threshold > 0 ? threshold : 45;
    std::vector<unsigned char> visited(static_cast<size_t>(w) * h, 0);
    std::vector<float> strength(static_cast<size_t>(w) * h, 0.0f);
    std::vector<QPoint> stack;
    stack.reserve(2 * (w + h));
    for (int x = 0; x < w; ++x) {
        stack.emplace_back(x, 0);
        stack.emplace_back(x, h - 1);
    }
    for (int y = 0; y < h; ++y) {
        stack.emplace_back(0, y);
        stack.emplace_back(w - 1, y);
    }

    // Pass 1: flood fill, recording per-pixel strength (how background-like
    // it is) without touching pixel colors yet.
    while (!stack.empty()) {
        QPoint p = stack.back();
        stack.pop_back();
        int x = p.x(), y = p.y();
        if (x < 0 || y < 0 || x >= w || y >= h) continue;
        size_t vi = static_cast<size_t>(y) * w + x;
        if (visited[vi]) continue;
        visited[vi] = 1;

        QRgb px = img.pixel(x, y);
        double dr = qRed(px) - bgR, dg = qGreen(px) - bgG, db = qBlue(px) - bgB;
        double dist = std::sqrt(dr * dr + dg * dg + db * db);
        if (dist >= th) continue;  // not background-colored: stop, don't propagate past it either

        // A linear 1-dist/th ramp meant real (non-uniform, softly lit)
        // backgrounds rarely reached full white, staying visibly tinted
        // wherever they drifted from the corner-sampled average — plateau
        // at full strength well before the threshold instead, so only
        // pixels genuinely near the *edge* of what counts as background
        // stay partial.
        strength[vi] = static_cast<float>(std::clamp(1.0 - dist / (th * 0.6), 0.0, 1.0));

        stack.emplace_back(x + 1, y);
        stack.emplace_back(x - 1, y);
        stack.emplace_back(x, y + 1);
        stack.emplace_back(x, y - 1);
    }

    // Pass 2: erode the strength map by one pixel (min over the 4-neighborhood,
    // treating anything not reached by the flood fill as strength 0). Real
    // photos anti-alias the subject/background boundary — those blended
    // pixels are genuinely partway toward the background color and pass
    // the flood fill's own test, which otherwise paints a visible white
    // fringe/halo tracing the subject's hair and face outline. Pulling the
    // affected region back by a pixel keeps the flat background fully
    // covered (its interior neighbors are unaffected by an erosion this
    // small) while stopping short of the subject's real edge.
    std::vector<float> eroded(strength.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t vi = static_cast<size_t>(y) * w + x;
            if (!visited[vi] || strength[vi] <= 0.0f) {
                eroded[vi] = 0.0f;
                continue;
            }
            float m = strength[vi];
            if (x > 0) m = std::min(m, strength[vi - 1]);
            if (x + 1 < w) m = std::min(m, strength[vi + 1]);
            if (y > 0) m = std::min(m, strength[vi - static_cast<size_t>(w)]);
            if (y + 1 < h) m = std::min(m, strength[vi + static_cast<size_t>(w)]);
            eroded[vi] = m;
        }
    }

    // Pass 3: apply the eroded strength as the actual color blend.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t vi = static_cast<size_t>(y) * w + x;
            float s = eroded[vi];
            if (s <= 0.0f) continue;
            QRgb px = img.pixel(x, y);
            int r = qRed(px) + static_cast<int>((targetColor.red() - qRed(px)) * s);
            int g = qGreen(px) + static_cast<int>((targetColor.green() - qGreen(px)) * s);
            int b = qBlue(px) + static_cast<int>((targetColor.blue() - qBlue(px)) * s);
            img.setPixel(x, y,
                         qRgba(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), qAlpha(px)));
        }
    }
    return img;
}

QImage magicWandFill(const QImage& src, QPoint seed, int threshold) {
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    int w = img.width(), h = img.height();
    if (seed.x() < 0 || seed.y() < 0 || seed.x() >= w || seed.y() >= h) return img;

    QRgb target = img.pixel(seed);
    double tr = qRed(target), tg = qGreen(target), tb = qBlue(target);
    std::vector<unsigned char> visited(static_cast<size_t>(w) * h, 0);
    std::vector<QPoint> stack{seed};
    int minX = w, minY = h, maxX = -1, maxY = -1;

    while (!stack.empty()) {
        QPoint p = stack.back();
        stack.pop_back();
        int x = p.x(), y = p.y();
        if (x < 0 || y < 0 || x >= w || y >= h) continue;
        size_t vi = static_cast<size_t>(y) * w + x;
        if (visited[vi]) continue;
        visited[vi] = 1;

        QRgb px = img.pixel(x, y);
        double dr = qRed(px) - tr, dg = qGreen(px) - tg, db = qBlue(px) - tb;
        double dist = std::sqrt(dr * dr + dg * dg + db * db);
        if (dist >= threshold) continue;

        // Soft cutout: alpha ramps from 0 (exact target-color match) up to
        // 255 at the threshold boundary, instead of a hard binary cut —
        // this alone removes most of the jaggy/hard edge.
        int alpha = threshold > 0 ? static_cast<int>(std::clamp(dist / threshold, 0.0, 1.0) * 255) : 0;
        img.setPixel(x, y, qRgba(qRed(px), qGreen(px), qBlue(px), alpha));
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
        stack.emplace_back(x + 1, y);
        stack.emplace_back(x - 1, y);
        stack.emplace_back(x, y + 1);
        stack.emplace_back(x, y - 1);
    }

    // Feather the selection edge geometrically too: a small box blur of
    // just the alpha channel over the affected region smooths the
    // staircase edge the flood fill itself produces.
    if (maxX >= minX) {
        constexpr int kRadius = 2;
        int bx0 = std::max(0, minX - kRadius), by0 = std::max(0, minY - kRadius);
        int bx1 = std::min(w - 1, maxX + kRadius), by1 = std::min(h - 1, maxY + kRadius);
        std::vector<unsigned char> alphaCopy(static_cast<size_t>(bx1 - bx0 + 1) * (by1 - by0 + 1));
        int bw = bx1 - bx0 + 1;
        for (int y = by0; y <= by1; ++y)
            for (int x = bx0; x <= bx1; ++x)
                alphaCopy[static_cast<size_t>(y - by0) * bw + (x - bx0)] = qAlpha(img.pixel(x, y));
        for (int y = by0; y <= by1; ++y) {
            for (int x = bx0; x <= bx1; ++x) {
                int sum = 0, n = 0;
                for (int dy = -kRadius; dy <= kRadius; ++dy) {
                    for (int dx = -kRadius; dx <= kRadius; ++dx) {
                        int sx = x + dx, sy = y + dy;
                        if (sx < bx0 || sx > bx1 || sy < by0 || sy > by1) continue;
                        sum += alphaCopy[static_cast<size_t>(sy - by0) * bw + (sx - bx0)];
                        ++n;
                    }
                }
                QRgb px = img.pixel(x, y);
                img.setPixel(x, y, qRgba(qRed(px), qGreen(px), qBlue(px), n > 0 ? sum / n : qAlpha(px)));
            }
        }
    }
    return img;
}

void paintBrush(QImage& img, QPoint center, int radiusPx, int targetAlpha) {
    if (img.format() != QImage::Format_ARGB32) img = img.convertToFormat(QImage::Format_ARGB32);
    int w = img.width(), h = img.height();
    radiusPx = std::max(1, radiusPx);
    int x0 = std::max(0, center.x() - radiusPx), x1 = std::min(w - 1, center.x() + radiusPx);
    int y0 = std::max(0, center.y() - radiusPx), y1 = std::min(h - 1, center.y() + radiusPx);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            double dist = std::hypot(x - center.x(), y - center.y());
            if (dist > radiusPx) continue;
            // Feathered falloff: full strength within the inner 60% of the
            // radius, tapering smoothly to zero effect at the edge.
            double inner = radiusPx * 0.6;
            double coverage = dist <= inner ? 1.0 : std::clamp(1.0 - (dist - inner) / (radiusPx - inner), 0.0, 1.0);
            QRgb px = img.pixel(x, y);
            int newAlpha = static_cast<int>(std::round(qAlpha(px) * (1 - coverage) + targetAlpha * coverage));
            img.setPixel(x, y, qRgba(qRed(px), qGreen(px), qBlue(px), std::clamp(newAlpha, 0, 255)));
        }
    }
}

}  // namespace ihv::imaging::BackgroundTools
