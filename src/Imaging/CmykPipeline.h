#pragma once

#include <QImage>
#include <vector>

namespace ihv::imaging::CmykPipeline {

using Bytes = std::vector<unsigned char>;

// Bytes of the embedded USWebCoatedSWOP.icc profile (Qt resource).
const Bytes& swopIcc();

// RGB raster -> DeviceCMYK buffer (w*h*4 bytes), SWOP. sRGB -> SWOP via
// LittleCMS2 directly: Relative Colorimetric intent + black point
// compensation. Same pipeline/intent as iHateCards.CPP's CmykPipeline, for
// the print-shop CMYK PDF/X-1a export of the 10x15 sheet.
Bytes toCmyk(const QImage& rgbImage);

}  // namespace ihv::imaging::CmykPipeline
