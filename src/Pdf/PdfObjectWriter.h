#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ihv::pdf {

using Bytes = std::vector<unsigned char>;

Bytes zlibCompress(const Bytes& data);
Bytes latin1(const std::string& s);
std::string num(double v);   // "0.####"-equivalent
std::string num2(double v);  // "0.##"-equivalent

// Byte-level PDF object writer with a classic xref table — shared machinery
// for both PDF builders. Ported 1:1 from
// iHateCards.NET/src/iHateCards/Pdf/PdfUtil.cs (itself a port of the manual
// PDF writer in the original cmyk_export.py — no PDF library used).
class PdfObjectWriter {
public:
    PdfObjectWriter();

    void write(const Bytes& data);
    void writeObj(int num, const Bytes& body);
    void writeObj(int num, const std::string& body);

    // Bytes written so far (used for the /ID hash, computed after xref, before trailer).
    Bytes currentBytes() const { return out_; }

    // Writes the xref table, returns its byte offset (for startxref).
    long long writeXref();

    Bytes finishWithTrailer(const std::string& trailerDict, long long xrefPos);

    int maxNum() const { return maxNum_; }

private:
    Bytes out_;
    std::map<int, long long> offsets_;
    int maxNum_ = 0;
};

}  // namespace ihv::pdf
