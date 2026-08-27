#include "Update/UpdateChecker.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>

#include "Update/AppVersion.h"

namespace ihv::update {

namespace {
QSettings& settings() {
    static QSettings s("iHateVisa", "iHateVisa");
    return s;
}
}  // namespace

QString UpdateChecker::manifestUrl() {
    QByteArray env = qgetenv("IHATEVISA_UPDATE_URL");
    if (!env.isEmpty()) return QString::fromUtf8(env);
    return "https://github.com/AmajaWeed/ihatevisa/releases/latest/download/updates.json";
}

bool UpdateChecker::autoCheckEnabled() { return settings().value("update/autoCheck", true).toBool(); }

void UpdateChecker::setAutoCheckEnabled(bool on) { settings().setValue("update/autoCheck", on); }

bool UpdateChecker::isSkipped(const QString& version) {
    QStringList skipped = settings().value("update/skippedVersions").toStringList();
    return skipped.contains(version);
}

void UpdateChecker::skip(const QString& version) {
    QStringList skipped = settings().value("update/skippedVersions").toStringList();
    if (!skipped.contains(version)) {
        skipped << version;
        settings().setValue("update/skippedVersions", skipped);
    }
}

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {}

void UpdateChecker::checkAsync(std::function<void(std::optional<UpdateInfo>)> callback, bool ignoreSkipped) {
    callback_ = std::move(callback);
    ignoreSkipped_ = ignoreSkipped;

    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(manifestUrl())};
    req.setHeader(QNetworkRequest::UserAgentHeader, QString("iHateVisa/%1").arg(AppVersion::current()));
    req.setTransferTimeout(20000);

    auto fail = [this] {
        if (callback_) callback_(std::nullopt);
    };

    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, fail] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail();
            return;
        }
        QJsonParseError perr;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            fail();
            return;
        }
        QJsonObject root = doc.object();
        QString latest = root.value("latest").toString();
        if (latest.isEmpty()) {
            fail();
            return;
        }
        if (!AppVersion::isNewer(latest, AppVersion::current())) {
            fail();
            return;
        }
        if (!ignoreSkipped_ && isSkipped(latest)) {
            fail();
            return;
        }
        QJsonObject packages = root.value("packages").toObject();
        QJsonObject pkg = packages.value(AppVersion::rid()).toObject();
        if (pkg.isEmpty()) {
            fail();
            return;
        }

        UpdateInfo info;
        info.latest = latest;
        info.published = root.value("published").toString();
        for (const QJsonValue& v : root.value("notes").toArray()) info.notes << v.toString();
        info.package.url = pkg.value("url").toString();
        info.package.sha256 = pkg.value("sha256").toString();
        info.package.size = static_cast<qint64>(pkg.value("size").toDouble());

        settings().setValue("update/lastCheck", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (callback_) callback_(info);
    });
}

}  // namespace ihv::update
