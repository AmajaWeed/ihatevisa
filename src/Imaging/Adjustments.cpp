#include "Imaging/Adjustments.h"

#include <algorithm>
#include <cmath>

namespace ihv::imaging::Adjustments {

namespace {

inline unsigned char clampByte(double v) { return static_cast<unsigned char>(std::clamp(v, 0.0, 255.0)); }

// QImage::scaled(..., SmoothTransformation) is a single bilinear pass — for
// a large reduction ratio (a multi-thousand-pixel photo down to a few
// hundred px, easily >10x for modern phone cameras) that just sparsely
// samples the source instead of averaging it, aliasing high-frequency
// detail (skin texture, fabric weave) into visible noise. The standard fix
// is a mipmap-style chain: repeatedly halve (each halving is a true
// area-average at that ratio) until within 2x of the target, then one
// final precise scale. Halving preserves the full-resolution source for
// the very first step, so this is exact box-filtering down to the last
// step, not a single undersampled jump.
QImage smoothDownscale(const QImage& src, int targetW, int targetH) {
    QImage cur = src;
    while (cur.width() > targetW * 2 && cur.height() > targetH * 2) {
        cur = cur.scaled(std::max(targetW, cur.width() / 2), std::max(targetH, cur.height() / 2),
                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (cur.width() != targetW || cur.height() != targetH)
        cur = cur.scaled(targetW, targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return cur;
}

}  // namespace

QImage apply(const QImage& src, const core::EditorState& s, int w, int h) {
    w = std::max(1, w);
    h = std::max(1, h);
    QImage scaled = (src.width() > w || src.height() > h)
                         ? smoothDownscale(src, w, h).convertToFormat(QImage::Format_ARGB32)
                         : src.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_ARGB32);

    double brightMul = (100.0 + s.brightness) / 100.0;
    double contrastMul = (100.0 + s.contrast) / 100.0;
    double satAmount = (s.blackAndWhite ? 0.0 : s.saturationPercent) / 100.0;
    double gammaInv = 1.0 / (s.gammaPercent / 100.0);
    bool applyGamma = std::abs(s.gammaPercent / 100.0 - 1.0) > 0.01;
    bool applyCc = s.cc.shadows.c || s.cc.shadows.m || s.cc.shadows.y || s.cc.mid.c || s.cc.mid.m || s.cc.mid.y ||
                   s.cc.high.c || s.cc.high.m || s.cc.high.y;

    // CSS Filter Effects saturate() matrix (Rec. 709 luma coefficients),
    // applied after brightness/contrast — same order as the reference's
    // `filter: brightness(..) contrast(..) saturate(..)` chain.
    double sm[3][3] = {{0.213 + 0.787 * satAmount, 0.715 - 0.715 * satAmount, 0.072 - 0.072 * satAmount},
                        {0.213 - 0.213 * satAmount, 0.715 + 0.285 * satAmount, 0.072 - 0.072 * satAmount},
                        {0.213 - 0.213 * satAmount, 0.715 - 0.715 * satAmount, 0.072 + 0.928 * satAmount}};

    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<QRgb*>(scaled.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb px = line[x];
            double r = qRed(px), g = qGreen(px), b = qBlue(px);

            r *= brightMul;
            g *= brightMul;
            b *= brightMul;
            r = (r - 127.5) * contrastMul + 127.5;
            g = (g - 127.5) * contrastMul + 127.5;
            b = (b - 127.5) * contrastMul + 127.5;
            r = std::clamp(r, 0.0, 255.0);
            g = std::clamp(g, 0.0, 255.0);
            b = std::clamp(b, 0.0, 255.0);

            double nr = sm[0][0] * r + sm[0][1] * g + sm[0][2] * b;
            double ng = sm[1][0] * r + sm[1][1] * g + sm[1][2] * b;
            double nb = sm[2][0] * r + sm[2][1] * g + sm[2][2] * b;
            r = std::clamp(nr, 0.0, 255.0);
            g = std::clamp(ng, 0.0, 255.0);
            b = std::clamp(nb, 0.0, 255.0);

            if (applyGamma) {
                r = 255.0 * std::pow(r / 255.0, gammaInv);
                g = 255.0 * std::pow(g / 255.0, gammaInv);
                b = 255.0 * std::pow(b / 255.0, gammaInv);
            }

            if (applyCc) {
                double lum = (r * 0.299 + g * 0.587 + b * 0.114) / 255.0;
                const core::CcZone& z = lum < 0.33 ? s.cc.shadows : (lum < 0.66 ? s.cc.mid : s.cc.high);
                r = r - z.c * 1.5 + z.m * 0.3 + z.y * 0.3;
                g = g + z.c * 0.3 - z.m * 1.5 + z.y * 0.3;
                b = b + z.c * 0.3 + z.m * 0.3 - z.y * 1.5;
            }

            line[x] = qRgba(clampByte(r), clampByte(g), clampByte(b), qAlpha(px));
        }
    }
    return scaled;
}

}  // namespace ihv::imaging::Adjustments
