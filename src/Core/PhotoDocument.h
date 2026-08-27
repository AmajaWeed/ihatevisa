#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <memory>
#include <optional>
#include <vector>

namespace ihv::core {

// One imported photo. Ported from the `photos[]` entries (line 337) plus
// the `processed` field used by the background-removal tools.
struct PhotoDocument {
    int id = 0;
    QString name;
    QString extension;                // e.g. ".heic" — for re-decoding originalBytes
    QByteArray originalBytes;         // exact source file bytes, for .hate round-trip
    QImage original;                  // as imported (decoded), never mutated
    std::optional<QImage> processed;  // background-removal result, if any

    const QImage& displayImage() const { return processed ? *processed : original; }
};

using PhotoDocumentPtr = std::shared_ptr<PhotoDocument>;

}  // namespace ihv::core
