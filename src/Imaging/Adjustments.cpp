#include "Imaging/Adjustments.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ihv::imaging::Adjustments {

namespace {

inline unsigned char clampByte(double v) { return static_cast<unsigned char>(std::clamp(v, 0.0, 255.0)); }
inline double smoothstep(double edge0, double edge1, double x) {
    double t = std::clamp(edge1 != edge0 ? (x - edge0) / (edge1 - edge0) : 0.0, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// QImage::scaled(..., SmoothTransformation) is a single bilinear pass — for
// a large reduction ratio (a multi-thousand-pixel photo down to a few
// hundred px, easily >10x for modern phone cameras) that just sparsely
// samples the source instead of averaging it, aliasing high-frequency
// detail (skin texture, fabric weave) into visible noise. The standard fix
// is a mipmap-style chain: repeatedly halve (each halving is a true
// area-average at that ratio) until within 2x of the target, then one
// final precise scale.
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

// Separable box blur (2 passes) on an RGB buffer — the "low-frequency" pass
// that texture/clarity/dehaze subtract from the original to get a local-
// contrast (unsharp-mask-style) map. Not edge-aware like Lightroom's real
// implementation, but a legitimate, real local-contrast algorithm rather
// than a no-op — halos on hard edges are the known tradeoff of this
// simplified approach.
struct RgbBuf {
    int w, h;
    std::vector<float> r, g, b;
};

RgbBuf toRgbBuf(const QImage& img) {
    RgbBuf buf{img.width(), img.height(), {}, {}, {}};
    size_t n = static_cast<size_t>(buf.w) * buf.h;
    buf.r.resize(n);
    buf.g.resize(n);
    buf.b.resize(n);
    for (int y = 0; y < buf.h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < buf.w; ++x) {
            size_t i = static_cast<size_t>(y) * buf.w + x;
            buf.r[i] = qRed(line[x]);
            buf.g[i] = qGreen(line[x]);
            buf.b[i] = qBlue(line[x]);
        }
    }
    return buf;
}

void boxBlur1D(std::vector<float>& data, int w, int h, int radius, bool horizontal) {
    if (radius <= 0) return;
    std::vector<float> out(data.size());
    int len = horizontal ? w : h;
    int lines = horizontal ? h : w;
    for (int l = 0; l < lines; ++l) {
        auto at = [&](int p) -> size_t { return horizontal ? (static_cast<size_t>(l) * w + p) : (static_cast<size_t>(p) * w + l); };
        double sum = 0;
        int count = 0;
        for (int p = 0; p < std::min(len, radius + 1); ++p) {
            sum += data[at(p)];
            ++count;
        }
        for (int p = 0; p < len; ++p) {
            out[at(p)] = static_cast<float>(sum / count);
            int addP = p + radius + 1, remP = p - radius;
            if (addP < len) {
                sum += data[at(addP)];
                ++count;
            }
            if (remP >= 0) {
                sum -= data[at(remP)];
                --count;
            }
        }
    }
    data.swap(out);
}

RgbBuf blur(const RgbBuf& src, int radius) {
    RgbBuf out = src;
    boxBlur1D(out.r, out.w, out.h, radius, true);
    boxBlur1D(out.r, out.w, out.h, radius, false);
    boxBlur1D(out.g, out.w, out.h, radius, true);
    boxBlur1D(out.g, out.w, out.h, radius, false);
    boxBlur1D(out.b, out.w, out.h, radius, true);
    boxBlur1D(out.b, out.w, out.h, radius, false);
    return out;
}

}  // namespace

QImage apply(const QImage& src, const core::EditorState& s, int w, int h) {
    w = std::max(1, w);
    h = std::max(1, h);

    // Dragging the guide dots (crop position/rotation) repaints on every
    // mouse-move, but none of that touches the source pixels, the tone
    // adjustments, or (in editing mode) the target size — yet this used to
    // redo the full mipmap downscale from the source photo's *original*
    // resolution from scratch on every single frame, which is genuinely
    // expensive for a modern multi-thousand-pixel camera photo and was the
    // actual cause of "перетаскивание овала сильно лагает". Cache the
    // result and only recompute when something that actually changes the
    // output does. QImage::cacheKey() changes whenever the pixel data
    // changes (even via in-place reassignment of the same QImage object,
    // e.g. after auto-clean/undo), so it's the right identity check here,
    // not the QImage's own address.
    static qint64 cacheSrcKey = 0;
    static core::RawAdjustments cacheAdj{};
    static bool cacheBw = false;
    static int cacheW = -1, cacheH = -1;
    static QImage cacheResult;
    qint64 srcKey = src.cacheKey();
    if (!cacheResult.isNull() && srcKey == cacheSrcKey && w == cacheW && h == cacheH &&
        cacheBw == s.blackAndWhite && cacheAdj == s.adj) {
        return cacheResult;
    }

    QImage scaled = (src.width() > w || src.height() > h)
                         ? smoothDownscale(src, w, h).convertToFormat(QImage::Format_ARGB32)
                         : src.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_ARGB32);

    const core::RawAdjustments& a = s.adj;
    bool needsLocalContrast = a.texture != 0 || a.clarity != 0 || a.dehaze != 0;

    RgbBuf buf = toRgbBuf(scaled);
    // Radii scaled to image size so texture/clarity/dehaze feel consistent
    // whether this is a small preview or a full-resolution export.
    int baseRadius = std::max(1, std::min(w, h) / 100);
    RgbBuf blurTexture, blurClarity, blurDehaze;
    if (a.texture != 0) blurTexture = blur(buf, baseRadius);
    if (a.clarity != 0) blurClarity = blur(buf, baseRadius * 4);
    if (a.dehaze != 0) blurDehaze = blur(buf, baseRadius * 6);

    double tempK = a.temperature / 100.0 * 40.0;
    double tintK = a.tint / 100.0 * 40.0;
    double ev = a.exposure / 100.0 * 2.0;  // +-2 stops at the extremes
    double expMul = std::pow(2.0, ev);
    double contrastMul = 1.0 + a.contrast / 100.0;
    double satMul = (s.blackAndWhite ? 0.0 : 1.0 + a.saturation / 100.0);
    double vibAmt = a.vibrance / 100.0;

    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<QRgb*>(scaled.scanLine(y));
        for (int x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            double r = buf.r[idx], g = buf.g[idx], b = buf.b[idx];
            unsigned char origAlpha = qAlpha(line[x]);

            // --- White balance ---
            r += tempK;
            b -= tempK;
            g -= tintK;

            // --- Exposure (in approximate linear light, not gamma space) ---
            if (ev != 0.0) {
                auto toLinear = [](double v) { return std::pow(std::clamp(v / 255.0, 0.0, 1.0), 2.2); };
                auto toGamma = [](double v) { return std::pow(std::clamp(v, 0.0, 1.0), 1.0 / 2.2) * 255.0; };
                r = toGamma(toLinear(r) * expMul);
                g = toGamma(toLinear(g) * expMul);
                b = toGamma(toLinear(b) * expMul);
            }

            // --- Contrast (linear S around mid-gray) ---
            r = (r - 127.5) * contrastMul + 127.5;
            g = (g - 127.5) * contrastMul + 127.5;
            b = (b - 127.5) * contrastMul + 127.5;

            // --- Highlights/Shadows/Whites/Blacks: luminance-weighted,
            // computed from the pixel's state *entering this stage* so the
            // four controls compose predictably in the order a photographer
            // expects (tone region shaping after basic exposure/contrast). ---
            if (a.highlights != 0 || a.shadows != 0 || a.whites != 0 || a.blacks != 0) {
                double lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
                double wHi = smoothstep(0.35, 1.0, lum);
                double wSh = smoothstep(0.65, 0.0, lum);
                double wWh = smoothstep(0.75, 1.0, lum);
                double wBl = smoothstep(0.25, 0.0, lum);
                double delta = a.highlights / 100.0 * 50.0 * wHi + a.shadows / 100.0 * 50.0 * wSh +
                               a.whites / 100.0 * 70.0 * wWh + a.blacks / 100.0 * 70.0 * wBl;
                r += delta;
                g += delta;
                b += delta;
            }

            // --- Texture / Clarity / Dehaze: local contrast via unsharp
            // mask against blurred versions at increasing radius. Dehaze
            // additionally lifts a small haze-veil and nudges saturation,
            // approximating (not replicating) a dark-channel-prior dehaze. ---
            if (needsLocalContrast) {
                if (a.texture != 0) {
                    double amt = a.texture / 100.0 * 0.6;
                    r += (buf.r[idx] - blurTexture.r[idx]) * amt;
                    g += (buf.g[idx] - blurTexture.g[idx]) * amt;
                    b += (buf.b[idx] - blurTexture.b[idx]) * amt;
                }
                if (a.clarity != 0) {
                    double amt = a.clarity / 100.0 * 0.6;
                    r += (buf.r[idx] - blurClarity.r[idx]) * amt;
                    g += (buf.g[idx] - blurClarity.g[idx]) * amt;
                    b += (buf.b[idx] - blurClarity.b[idx]) * amt;
                }
                if (a.dehaze != 0) {
                    double amt = a.dehaze / 100.0;
                    double veil = 30.0 * amt;  // positive dehaze: subtract an estimated haze veil
                    r = (r - veil) / (1.0 - 0.3 * amt) + (buf.r[idx] - blurDehaze.r[idx]) * amt * 0.5;
                    g = (g - veil) / (1.0 - 0.3 * amt) + (buf.g[idx] - blurDehaze.g[idx]) * amt * 0.5;
                    b = (b - veil) / (1.0 - 0.3 * amt) + (buf.b[idx] - blurDehaze.b[idx]) * amt * 0.5;
                }
            }

            r = std::clamp(r, 0.0, 255.0);
            g = std::clamp(g, 0.0, 255.0);
            b = std::clamp(b, 0.0, 255.0);

            // --- Vibrance (protects already-saturated pixels/skin tones)
            // then uniform Saturation, both via a simple max-channel
            // saturation measure blended toward/away from luma gray. ---
            double lumaGray = 0.299 * r + 0.587 * g + 0.114 * b;
            if (vibAmt != 0.0 && !s.blackAndWhite) {
                double mx = std::max({r, g, b}), mn = std::min({r, g, b});
                double curSat = mx > 0 ? (mx - mn) / mx : 0.0;
                double protect = 1.0 - curSat;  // less-saturated pixels get boosted more
                double amt = vibAmt * protect;
                r = lumaGray + (r - lumaGray) * (1.0 + amt);
                g = lumaGray + (g - lumaGray) * (1.0 + amt);
                b = lumaGray + (b - lumaGray) * (1.0 + amt);
            }
            if (satMul != 1.0) {
                r = lumaGray + (r - lumaGray) * satMul;
                g = lumaGray + (g - lumaGray) * satMul;
                b = lumaGray + (b - lumaGray) * satMul;
            }

            line[x] = qRgba(clampByte(r), clampByte(g), clampByte(b), origAlpha);
        }
    }

    cacheSrcKey = srcKey;
    cacheAdj = s.adj;
    cacheBw = s.blackAndWhite;
    cacheW = w;
    cacheH = h;
    cacheResult = scaled;
    return scaled;
}

}  // namespace ihv::imaging::Adjustments
