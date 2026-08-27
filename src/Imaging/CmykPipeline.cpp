#include "Imaging/CmykPipeline.h"

#include <lcms2.h>

#include <QFile>
#include <QPainter>
#include <stdexcept>

namespace ihv::imaging::CmykPipeline {

namespace {

const Bytes& loadIccBytes() {
    static Bytes bytes = [] {
        QFile f(":/USWebCoatedSWOP.icc");
        if (!f.open(QIODevice::ReadOnly))
            throw std::runtime_error("Embedded ICC profile USWebCoatedSWOP.icc not found");
        QByteArray data = f.readAll();
        return Bytes(data.begin(), data.end());
    }();
    return bytes;
}

struct LcmsHandles {
    cmsHPROFILE srgb = nullptr;
    cmsHPROFILE swop = nullptr;
    cmsHTRANSFORM toCmyk = nullptr;

    LcmsHandles() {
        srgb = cmsCreate_sRGBProfile();
        const Bytes& icc = loadIccBytes();
        swop = cmsOpenProfileFromMem(icc.data(), static_cast<cmsUInt32Number>(icc.size()));
        if (!srgb || !swop) throw std::runtime_error("Failed to open ICC profiles for CMYK pipeline");
        toCmyk = cmsCreateTransform(srgb, TYPE_RGB_8, swop, TYPE_CMYK_8, INTENT_RELATIVE_COLORIMETRIC,
                                     cmsFLAGS_BLACKPOINTCOMPENSATION);
        if (!toCmyk) throw std::runtime_error("Failed to create sRGB->SWOP transform");
    }

    ~LcmsHandles() {
        if (toCmyk) cmsDeleteTransform(toCmyk);
        if (swop) cmsCloseProfile(swop);
        if (srgb) cmsCloseProfile(srgb);
    }
};

LcmsHandles& handles() {
    static LcmsHandles h;
    return h;
}

// Composite onto white as opaque RGB888 (matches iHateCards.CPP's
// RasterUtil::normalizeToRgb) — the CMYK transform expects tight RGB triples.
QImage normalizeToRgb(const QImage& src) {
    QImage out(src.size(), QImage::Format_RGB888);
    out.fill(Qt::white);
    QPainter p(&out);
    p.drawImage(0, 0, src);
    p.end();
    return out;
}

std::vector<unsigned char> toRgbBuffer(const QImage& rgb888) {
    QImage img = rgb888.format() == QImage::Format_RGB888 ? rgb888 : rgb888.convertToFormat(QImage::Format_RGB888);
    std::vector<unsigned char> out(static_cast<size_t>(img.width()) * img.height() * 3);
    size_t o = 0;
    for (int y = 0; y < img.height(); ++y) {
        const unsigned char* line = img.constScanLine(y);
        std::copy(line, line + static_cast<size_t>(img.width()) * 3, out.begin() + o);
        o += static_cast<size_t>(img.width()) * 3;
    }
    return out;
}

}  // namespace

const Bytes& swopIcc() { return loadIccBytes(); }

Bytes toCmyk(const QImage& rgbImage) {
    QImage normalized = normalizeToRgb(rgbImage);
    std::vector<unsigned char> rgb = toRgbBuffer(normalized);
    size_t pixelCount = static_cast<size_t>(normalized.width()) * normalized.height();
    Bytes cmyk(pixelCount * 4);
    cmsDoTransform(handles().toCmyk, rgb.data(), cmyk.data(), static_cast<cmsUInt32Number>(pixelCount));
    return cmyk;
}

}  // namespace ihv::imaging::CmykPipeline
