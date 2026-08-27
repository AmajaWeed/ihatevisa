#include "Pdf/PdfObjectWriter.h"

#include <zlib.h>

#include <cstdio>

namespace ihv::pdf {

Bytes zlibCompress(const Bytes& data) {
    uLongf boundLen = compressBound(static_cast<uLong>(data.size()));
    Bytes out(boundLen);
    // Level 9 (smallest size), matching the reference's ZLibStream(CompressionLevel.SmallestSize).
    int rc = compress2(out.data(), &boundLen, data.data(), static_cast<uLong>(data.size()), 9);
    (void)rc;
    out.resize(boundLen);
    return out;
}

Bytes latin1(const std::string& s) { return Bytes(s.begin(), s.end()); }

std::string num(double v) {
    // Mirrors C#'s "0.####": up to 4 decimals, trailing zeros/dot trimmed.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string num2(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

PdfObjectWriter::PdfObjectWriter() {
    write(latin1("%PDF-1.4\n"));
    write(Bytes{0x25, 0xe2, 0xe3, 0xcf, 0xd3, 0x0a});  // %âãÏÓ (binary marker)
}

void PdfObjectWriter::write(const Bytes& data) { out_.insert(out_.end(), data.begin(), data.end()); }

void PdfObjectWriter::writeObj(int n, const Bytes& body) {
    offsets_[n] = static_cast<long long>(out_.size());
    if (n > maxNum_) maxNum_ = n;
    write(latin1(std::to_string(n) + " 0 obj\n"));
    write(body);
    write(latin1("\nendobj\n"));
}

void PdfObjectWriter::writeObj(int n, const std::string& body) { writeObj(n, latin1(body)); }

long long PdfObjectWriter::writeXref() {
    long long xrefPos = static_cast<long long>(out_.size());
    write(latin1("xref\n0 " + std::to_string(maxNum_ + 1) + "\n"));
    write(latin1("0000000000 65535 f \n"));
    for (int n = 1; n <= maxNum_; ++n) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010lld 00000 n \n", offsets_[n]);
        write(latin1(buf));
    }
    return xrefPos;
}

Bytes PdfObjectWriter::finishWithTrailer(const std::string& trailerDict, long long xrefPos) {
    write(latin1("trailer\n" + trailerDict + "\nstartxref\n" + std::to_string(xrefPos) + "\n%%EOF"));
    return out_;
}

}  // namespace ihv::pdf
