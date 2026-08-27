#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <optional>

namespace ihv::update {

struct PackageInfo {
    QString url;
    QString sha256;
    qint64 size = 0;
};

struct UpdateInfo {
    QString latest;
    QString published;
    QStringList notes;
    PackageInfo package;  // resolved for this platform's RID
};

// Manifest fetch + version compare, ported from iHateCards.NET's
// UpdateChecker. Network errors are swallowed silently — an update check
// must never interrupt or block offline use. Default manifest URL points
// at this repo's own GitHub Releases (single public repo, unlike
// iHateCards' private-source/public-updates split — no privacy concern
// here since ihatevisa is already public).
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    static QString manifestUrl();
    static bool autoCheckEnabled();
    static void setAutoCheckEnabled(bool on);
    static bool isSkipped(const QString& version);
    static void skip(const QString& version);

    // Calls back on the Qt event loop with the update info, or std::nullopt
    // if there's nothing to offer (no newer version, no package for this
    // RID, the version was skipped, or the check failed for any reason).
    void checkAsync(std::function<void(std::optional<UpdateInfo>)> callback, bool ignoreSkipped = false);

private:
    std::function<void(std::optional<UpdateInfo>)> callback_;
    bool ignoreSkipped_ = false;
};

}  // namespace ihv::update
