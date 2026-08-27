#include "Imaging/BackgroundTools.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ihv::imaging::BackgroundTools {

QImage autoClean(const QImage& src, int threshold) {
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

    double th = threshold > 0 ? threshold : 45;
    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb px = line[x];
            double dr = qRed(px) - bgR, dg = qGreen(px) - bgG, db = qBlue(px) - bgB;
            double dist = std::sqrt(dr * dr + dg * dg + db * db);
            if (dist < th) {
                double strength = 1.0 - (dist / th);
                int r = qRed(px) + static_cast<int>((255 - qRed(px)) * strength);
                int g = qGreen(px) + static_cast<int>((255 - qGreen(px)) * strength);
                int b = qBlue(px) + static_cast<int>((255 - qBlue(px)) * strength);
                line[x] = qRgba(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), qAlpha(px));
            }
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
        if (std::sqrt(dr * dr + dg * dg + db * db) >= threshold) continue;

        img.setPixel(x, y, qRgba(qRed(px), qGreen(px), qBlue(px), 0));
        stack.emplace_back(x + 1, y);
        stack.emplace_back(x - 1, y);
        stack.emplace_back(x, y + 1);
        stack.emplace_back(x, y - 1);
    }
    return img;
}

}  // namespace ihv::imaging::BackgroundTools
