#include "Pdf/CmykPdfWriter.h"

#include <ctime>

#include "Pdf/Md5.h"

namespace ihv::pdf {

namespace {
Bytes concat(std::initializer_list<const Bytes*> parts) {
    size_t total = 0;
    for (auto* p : parts) total += p->size();
    Bytes res;
    res.reserve(total);
    for (auto* p : parts) res.insert(res.end(), p->begin(), p->end());
    return res;
}
}  // namespace

Bytes buildCmykPdf(const std::vector<CmykPage>& pages, const Bytes& iccBytes) {
    PdfObjectWriter w;
    Bytes iccComp = zlibCompress(iccBytes);

    // Fixed objects: 1 Catalog, 2 Pages, 3 Info, 4 ICC, 5 OutputIntent.
    constexpr int CAT = 1, PAGES = 2, INFO = 3, ICC = 4, OI = 5;
    int objNum = 6;
    struct Triple {
        int c, i, p;
        const CmykPage* pg;
    };
    std::vector<Triple> triples;
    for (const auto& pg : pages) {
        triples.push_back({objNum, objNum + 1, objNum + 2, &pg});
        objNum += 3;
    }

    std::string kids;
    for (size_t i = 0; i < triples.size(); ++i) {
        if (i) kids += " ";
        kids += std::to_string(triples[i].p) + " 0 R";
    }
    w.writeObj(CAT, "<< /Type /Catalog /Pages " + std::to_string(PAGES) + " 0 R /OutputIntents [" +
                        std::to_string(OI) + " 0 R] >>");
    w.writeObj(PAGES, "<< /Type /Pages /Count " + std::to_string(triples.size()) + " /Kids [" + kids + "] >>");

    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char dateBuf[32];
    std::strftime(dateBuf, sizeof(dateBuf), "D:%Y%m%d%H%M%S", &tmv);
    std::string date(dateBuf);

    w.writeObj(INFO, "<< /Title (iHateCards) /Creator (iHateCards) /Producer (iHateCards) "
                      "/GTS_PDFXVersion (PDF/X-1a:2003) /GTS_PDFXConformance (PDF/X-1a:2003) "
                      "/Trapped /False /CreationDate (" +
                          date + ") /ModDate (" + date + ") >>");

    Bytes iccHdr = latin1("<< /N 4 /Length " + std::to_string(iccComp.size()) + " /Filter /FlateDecode >>\nstream\n");
    Bytes endstream = latin1("\nendstream");
    w.writeObj(ICC, concat({&iccHdr, &iccComp, &endstream}));

    w.writeObj(OI, "<< /Type /OutputIntent /S /GTS_PDFX "
                   "/OutputConditionIdentifier (CGATS TR 001) /OutputCondition (SWOP) "
                   "/Info (U.S. Web Coated \\(SWOP\\) v2) /RegistryName (http://www.color.org) "
                   "/DestOutputProfile " +
                       std::to_string(ICC) + " 0 R >>");

    for (const auto& tr : triples) {
        const CmykPage& pg = *tr.pg;
        double wpt = pg.widthMm / 25.4 * 72.0;
        double hpt = pg.heightMm / 25.4 * 72.0;
        Bytes comp = zlibCompress(pg.cmyk);

        Bytes content = latin1("q\n" + num(wpt) + " 0 0 " + num(hpt) + " 0 0 cm\n/Im0 Do\nQ\n");
        Bytes ccomp = zlibCompress(content);
        Bytes contentHdr = latin1("<< /Length " + std::to_string(ccomp.size()) + " /Filter /FlateDecode >>\nstream\n");
        w.writeObj(tr.c, concat({&contentHdr, &ccomp, &endstream}));

        Bytes imgHdr = latin1("<< /Type /XObject /Subtype /Image /Width " + std::to_string(pg.width) +
                               " /Height " + std::to_string(pg.height) +
                               " /ColorSpace /DeviceCMYK /BitsPerComponent 8 "
                               "/Filter /FlateDecode /Length " +
                               std::to_string(comp.size()) + " >>\nstream\n");
        w.writeObj(tr.i, concat({&imgHdr, &comp, &endstream}));

        w.writeObj(tr.p, "<< /Type /Page /Parent " + std::to_string(PAGES) + " 0 R /MediaBox [0 0 " + num(wpt) +
                              " " + num(hpt) + "] " + "/TrimBox [0 0 " + num(wpt) + " " + num(hpt) + "] " +
                              "/Resources << /XObject << /Im0 " + std::to_string(tr.i) + " 0 R >> >> /Contents " +
                              std::to_string(tr.c) + " 0 R >>");
    }

    long long xrefPos = w.writeXref();
    std::string docId = md5Hex(w.currentBytes());
    return w.finishWithTrailer("<< /Size " + std::to_string(w.maxNum() + 1) + " /Root " + std::to_string(CAT) +
                                    " 0 R /Info " + std::to_string(INFO) + " 0 R /ID [<" + docId + "> <" + docId +
                                    ">] >>",
                                xrefPos);
}

}  // namespace ihv::pdf
