#include "Project/HateFile.h"

#include <zip.h>

#include <QBuffer>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <vector>

namespace ihv::project::HateFile {

namespace {

QByteArray toJson(const QJsonObject& obj) { return QJsonDocument(obj).toJson(QJsonDocument::Indented); }

QJsonObject ccZoneToJson(const core::CcZone& z) {
    QJsonObject o;
    o["c"] = z.c;
    o["m"] = z.m;
    o["y"] = z.y;
    return o;
}

core::CcZone ccZoneFromJson(const QJsonObject& o) {
    core::CcZone z;
    z.c = o.value("c").toInt(0);
    z.m = o.value("m").toInt(0);
    z.y = o.value("y").toInt(0);
    return z;
}

QString assetName(int id, const QString& ext, bool processed) {
    QString base = QString("assets/photo-%1").arg(id, 3, 10, QChar('0'));
    return processed ? base + "-processed.png" : base + (ext.isEmpty() ? ".png" : ext);
}

// libzip defers actually reading source buffers until zip_close(), so the
// QByteArray backing each one must stay alive until then — `keepAlive`
// holds one persistent copy per entry (QByteArray's payload is a separate
// refcounted heap block, so it doesn't move when the vector reallocates).
bool zipAddBuffer(zip_t* z, const QString& name, QByteArray data, std::vector<QByteArray>& keepAlive,
                   QString* error) {
    keepAlive.push_back(std::move(data));
    const QByteArray& stored = keepAlive.back();
    zip_source_t* src = zip_source_buffer(z, stored.constData(), static_cast<zip_uint64_t>(stored.size()), 0);
    if (!src) {
        if (error) *error = "zip_source_buffer failed";
        return false;
    }
    zip_int64_t idx = zip_file_add(z, name.toUtf8().constData(), src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
    if (idx < 0) {
        zip_source_free(src);
        if (error) *error = QString("zip_file_add failed for %1").arg(name);
        return false;
    }
    // Assets are already-compressed image formats (or FlateDecoded JSON is
    // tiny either way) — store JSON deflated, images uncompressed to avoid
    // wasting CPU double-compressing, matching the .docx/.hate convention.
    zip_uint8_t method = name.startsWith("assets/") ? ZIP_CM_STORE : ZIP_CM_DEFLATE;
    zip_set_file_compression(z, static_cast<zip_uint64_t>(idx), method, 0);
    return true;
}

bool zipReadEntry(zip_t* z, const QString& name, QByteArray& out) {
    zip_stat_t st;
    if (zip_stat(z, name.toUtf8().constData(), 0, &st) != 0) return false;
    zip_file_t* f = zip_fopen(z, name.toUtf8().constData(), 0);
    if (!f) return false;
    out.resize(static_cast<int>(st.size));
    zip_int64_t read = zip_fread(f, out.data(), st.size);
    zip_fclose(f);
    return read == static_cast<zip_int64_t>(st.size);
}

}  // namespace

bool save(const QString& path, const Project& project, QString* error) {
    int errCode = 0;
    zip_t* z = zip_open(path.toUtf8().constData(), ZIP_CREATE | ZIP_TRUNCATE, &errCode);
    if (!z) {
        if (error) *error = QString("zip_open failed (code %1)").arg(errCode);
        return false;
    }
    std::vector<QByteArray> keepAlive;

    QJsonObject manifest;
    manifest["formatVersion"] = FormatVersion;
    manifest["app"] = "iHateVisa";
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    manifest["created"] = now;
    manifest["modified"] = now;
    manifest["activePhotoId"] = project.activePhotoId;
    if (!zipAddBuffer(z, "manifest.json", toJson(manifest), keepAlive, error)) {
        zip_discard(z);
        return false;
    }

    const core::EditorState& s = project.state;
    QJsonObject layout;
    layout["formatKey"] = QString::fromStdString(s.formatKey);
    layout["widthMm"] = s.widthMm;
    layout["heightMm"] = s.heightMm;
    layout["topMarginMm"] = s.topMarginMm;
    layout["headPct"] = s.headPct;
    layout["headSizeMm"] = s.headSizeMm;
    layout["botMarginMm"] = s.botMarginMm;
    layout["dpi"] = s.dpi;
    layout["lockVertical"] = s.lockVertical;
    QJsonObject guide;
    guide["x"] = s.guide.x;
    guide["y"] = s.guide.y;
    guide["scale"] = s.guide.scale;
    guide["rotation"] = s.guide.rotation;
    layout["guide"] = guide;
    layout["bgColorHex"] = QString::fromStdString(s.bgColorHex);
    QJsonObject cc;
    cc["shadows"] = ccZoneToJson(s.cc.shadows);
    cc["mid"] = ccZoneToJson(s.cc.mid);
    cc["high"] = ccZoneToJson(s.cc.high);
    layout["cc"] = cc;
    layout["brightness"] = s.brightness;
    layout["contrast"] = s.contrast;
    layout["gammaPercent"] = s.gammaPercent;
    layout["saturationPercent"] = s.saturationPercent;
    layout["blackAndWhite"] = s.blackAndWhite;
    layout["ovalOverlay"] = s.ovalOverlay;
    layout["cornerOverlay"] = s.cornerOverlay;

    QJsonArray photosArr;
    for (const auto& ph : project.photos) {
        QJsonObject po;
        po["id"] = ph->id;
        po["name"] = ph->name;
        po["extension"] = ph->extension;
        po["asset"] = assetName(ph->id, ph->extension, false);
        po["hasProcessed"] = ph->processed.has_value();
        if (ph->processed) po["processedAsset"] = assetName(ph->id, ph->extension, true);
        photosArr.append(po);
    }
    layout["photos"] = photosArr;
    if (!zipAddBuffer(z, "layout.json", toJson(layout), keepAlive, error)) {
        zip_discard(z);
        return false;
    }

    QJsonObject printSettings;
    printSettings["printDpi"] = s.printDpi;
    printSettings["printMarginMm"] = s.printMarginMm;
    printSettings["printGapMm"] = s.printGapMm;
    printSettings["printPhotoCount"] = s.printPhotoCount;
    printSettings["printBorder"] = s.printBorder;
    if (!zipAddBuffer(z, "print-settings.json", toJson(printSettings), keepAlive, error)) {
        zip_discard(z);
        return false;
    }

    for (const auto& ph : project.photos) {
        QByteArray originalBytes = ph->originalBytes;
        if (originalBytes.isEmpty()) {
            // Fallback (e.g. camera-captured frame with no source file): encode as PNG.
            QBuffer buf(&originalBytes);
            buf.open(QIODevice::WriteOnly);
            ph->original.save(&buf, "PNG");
        }
        if (!zipAddBuffer(z, assetName(ph->id, ph->extension, false), originalBytes, keepAlive, error)) {
            zip_discard(z);
            return false;
        }
        if (ph->processed) {
            QByteArray processedBytes;
            QBuffer buf(&processedBytes);
            buf.open(QIODevice::WriteOnly);
            ph->processed->save(&buf, "PNG");
            if (!zipAddBuffer(z, assetName(ph->id, ph->extension, true), processedBytes, keepAlive, error)) {
                zip_discard(z);
                return false;
            }
        }
    }

    if (zip_close(z) != 0) {
        if (error) *error = "zip_close failed";
        return false;
    }
    return true;
}

bool open(const QString& path, Project& outProject, QString* error) {
    int errCode = 0;
    zip_t* z = zip_open(path.toUtf8().constData(), ZIP_RDONLY, &errCode);
    if (!z) {
        if (error) *error = QString("zip_open failed (code %1)").arg(errCode);
        return false;
    }

    QByteArray manifestBytes;
    if (!zipReadEntry(z, "manifest.json", manifestBytes)) {
        zip_close(z);
        if (error) *error = "manifest.json missing — not a .hate file";
        return false;
    }
    QJsonObject manifest = QJsonDocument::fromJson(manifestBytes).object();
    int formatVersion = manifest.value("formatVersion").toInt(0);
    if (formatVersion < 1) {
        zip_close(z);
        if (error) *error = "invalid or missing formatVersion";
        return false;
    }
    // formatVersion > FormatVersion is accepted (forward-compat: open what we understand).
    outProject.activePhotoId = manifest.value("activePhotoId").toInt(-1);

    QByteArray layoutBytes;
    zipReadEntry(z, "layout.json", layoutBytes);
    QJsonObject layout = QJsonDocument::fromJson(layoutBytes).object();

    core::EditorState& s = outProject.state;
    s.formatKey = layout.value("formatKey").toString("passport_rf").toStdString();
    s.widthMm = layout.value("widthMm").toDouble(35);
    s.heightMm = layout.value("heightMm").toDouble(45);
    s.topMarginMm = layout.value("topMarginMm").toDouble(5);
    s.headPct = layout.value("headPct").toDouble(75.6);
    s.headSizeMm = layout.value("headSizeMm").toDouble(34);
    s.botMarginMm = layout.value("botMarginMm").toDouble(6);
    s.dpi = layout.value("dpi").toInt(300);
    s.lockVertical = layout.value("lockVertical").toBool(true);
    QJsonObject guide = layout.value("guide").toObject();
    s.guide.x = guide.value("x").toDouble(0);
    s.guide.y = guide.value("y").toDouble(0);
    s.guide.scale = guide.value("scale").toDouble(1);
    s.guide.rotation = guide.value("rotation").toDouble(0);
    s.bgColorHex = layout.value("bgColorHex").toString("#ffffff").toStdString();
    QJsonObject cc = layout.value("cc").toObject();
    s.cc.shadows = ccZoneFromJson(cc.value("shadows").toObject());
    s.cc.mid = ccZoneFromJson(cc.value("mid").toObject());
    s.cc.high = ccZoneFromJson(cc.value("high").toObject());
    s.brightness = layout.value("brightness").toInt(0);
    s.contrast = layout.value("contrast").toInt(0);
    s.gammaPercent = layout.value("gammaPercent").toInt(100);
    s.saturationPercent = layout.value("saturationPercent").toInt(100);
    s.blackAndWhite = layout.value("blackAndWhite").toBool(false);
    s.ovalOverlay = layout.value("ovalOverlay").toBool(false);
    s.cornerOverlay = layout.value("cornerOverlay").toBool(false);

    QByteArray printBytes;
    zipReadEntry(z, "print-settings.json", printBytes);
    QJsonObject print = QJsonDocument::fromJson(printBytes).object();
    s.printDpi = print.value("printDpi").toInt(300);
    s.printMarginMm = print.value("printMarginMm").toDouble(3);
    s.printGapMm = print.value("printGapMm").toDouble(2);
    s.printPhotoCount = print.value("printPhotoCount").toInt(6);
    s.printBorder = print.value("printBorder").toBool(false);

    outProject.photos.clear();
    for (const QJsonValue& v : layout.value("photos").toArray()) {
        QJsonObject po = v.toObject();
        auto ph = std::make_shared<core::PhotoDocument>();
        ph->id = po.value("id").toInt();
        ph->name = po.value("name").toString();
        ph->extension = po.value("extension").toString();

        QByteArray originalBytes;
        if (zipReadEntry(z, po.value("asset").toString(), originalBytes)) {
            ph->originalBytes = originalBytes;
            ph->original.loadFromData(originalBytes);
        }
        if (po.value("hasProcessed").toBool(false)) {
            QByteArray processedBytes;
            if (zipReadEntry(z, po.value("processedAsset").toString(), processedBytes)) {
                QImage img;
                img.loadFromData(processedBytes, "PNG");
                ph->processed = img;
            }
        }
        if (!ph->original.isNull()) outProject.photos.push_back(ph);
    }

    zip_close(z);
    if (outProject.photos.empty()) outProject.activePhotoId = -1;
    return true;
}

}  // namespace ihv::project::HateFile
