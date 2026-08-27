#pragma once

#include <vector>

#include "Pdf/PdfObjectWriter.h"

namespace ihv::pdf {

// One page for the CMYK PDF: raw DeviceCMYK pixels (w*h*4 bytes).
struct CmykPage {
    Bytes cmyk;
    int width = 0;
    int height = 0;
    double widthMm = 0;
    double heightMm = 0;
};

// PDF/X-1a:2003 writer for the print-shop CMYK export of the 10x15 sheet —
// ported 1:1 from iHateCards.CPP's CmykPdfWriter (itself a port of
// build_cmyk_pdf from the original cmyk_export.py): DeviceCMYK (SWOP),
// lossless FlateDecode, SWOP ICC embedded as OutputIntent, TrimBox,
// document /ID.
Bytes buildCmykPdf(const std::vector<CmykPage>& pages, const Bytes& iccBytes);

}  // namespace ihv::pdf
