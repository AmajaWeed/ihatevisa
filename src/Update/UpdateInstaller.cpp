#include "Update/UpdateInstaller.h"

#include <sys/stat.h>
#include <zip.h>
#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

#include "Update/AppVersion.h"

namespace ihv::update {

QString UpdateInstaller::updatesDir() {
#if defined(Q_OS_MAC)
    QString dir = QDir::homePath() + "/Library/Application Support/iHateVisa/updates";
#elif defined(Q_OS_WIN)
    QString base = qEnvironmentVariable("LOCALAPPDATA");
    QString dir = base + "/iHateVisa/updates";
#else
    QString dir = QDir::homePath() + "/.local/share/iHateVisa/updates";
#endif
    QDir().mkpath(dir);
    return dir;
}

QString UpdateInstaller::targetDirectory() {
    QString dir = QCoreApplication::applicationDirPath();
#if defined(Q_OS_MAC)
    QDir d(dir);
    while (!d.isRoot()) {
        if (d.dirName().endsWith(".app", Qt::CaseInsensitive)) return d.absolutePath();
        if (!d.cdUp()) break;
    }
#endif
    return dir;
}

namespace {

void appendLog(const QString& line) {
    QFile log(UpdateInstaller::updatesDir() + "/apply-update.log");
    if (log.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&log);
        ts << QDateTime::currentDateTime().toString(Qt::ISODate) << " " << line << "\n";
    }
}

}  // namespace

UpdateInstaller::UpdateInstaller(QObject* parent) : QObject(parent) {}

bool UpdateInstaller::extractStaging(const QString& zipPath, const QString& stagingDir, QString* error) {
    QDir stagingQDir(stagingDir);
    if (stagingQDir.exists()) stagingQDir.removeRecursively();
    QDir().mkpath(stagingDir);

    int errCode = 0;
    zip_t* z = zip_open(zipPath.toUtf8().constData(), ZIP_RDONLY, &errCode);
    if (!z) {
        if (error) *error = QString("zip_open failed (code %1)").arg(errCode);
        return false;
    }

    zip_int64_t n = zip_get_num_entries(z, 0);
    for (zip_int64_t i = 0; i < n; ++i) {
        const char* rawName = zip_get_name(z, static_cast<zip_uint64_t>(i), 0);
        if (!rawName) continue;
        QString name = QString::fromUtf8(rawName);
        QString outPath = stagingDir + "/" + name;

        if (name.endsWith('/')) {
            QDir().mkpath(outPath);
            continue;
        }
        QDir().mkpath(QFileInfo(outPath).absolutePath());

        zip_uint8_t opsys = 0;
        zip_uint32_t attrs = 0;
        zip_file_get_external_attributes(z, static_cast<zip_uint64_t>(i), 0, &opsys, &attrs);
        mode_t mode = (opsys == ZIP_OPSYS_UNIX && attrs != 0) ? static_cast<mode_t>(attrs >> 16) : 0;

        zip_file_t* f = zip_fopen_index(z, static_cast<zip_uint64_t>(i), 0);
        if (!f) continue;
        zip_stat_t st;
        zip_stat_index(z, static_cast<zip_uint64_t>(i), 0, &st);
        QByteArray buf(static_cast<int>(st.size), Qt::Uninitialized);
        zip_fread(f, buf.data(), st.size);
        zip_fclose(f);

#if !defined(Q_OS_WIN)
        // .app bundles (in particular Qt's *.framework directories) rely
        // on real symlinks for their Versions/Current structure — a zip
        // entry with the S_IFLNK bit set stores the link *target path* as
        // its "file content", not actual file data. Writing that out as a
        // regular file silently corrupts the framework (codesign then
        // fails with "modified or invalid version", and the binary is
        // effectively missing) instead of erroring loudly, so this has to
        // be checked before ever falling through to the plain-file path.
        if (mode != 0 && S_ISLNK(mode)) {
            QFile::remove(outPath);  // mkpath above may have raced a stray file/dir here
            ::symlink(buf.constData(), outPath.toUtf8().constData());
            continue;
        }
#endif

        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly)) continue;
        out.write(buf);
        out.close();

#if !defined(Q_OS_WIN)
        // Preserve unix permissions (in particular the executable bit on
        // the bundle's main binary) from the zip's external attributes.
        if (mode != 0) ::chmod(outPath.toUtf8().constData(), mode);
#endif
    }
    zip_close(z);
    return true;
}

bool UpdateInstaller::launchApplier(const QString& stagingRoot, const QString& targetDir, qint64 waitPid,
                                     QString* error) {
    QString log = updatesDir() + "/apply-update.log";
#if defined(Q_OS_WIN)
    QString scriptPath = updatesDir() + "/apply-update.ps1";
    QString exePath = targetDir + "/iHateVisa.exe";
    QString script = QString(
                          "$targetPid = %1\n"
                          "Wait-Process -Id $targetPid -Timeout 120 -ErrorAction SilentlyContinue\n"
                          "Start-Sleep -Milliseconds 500\n"
                          "$log = \"%2\"\n"
                          "$target = \"%3\"\n"
                          "$staging = \"%4\"\n"
                          "$backup = \"$target.old\"\n"
                          "\"$(Get-Date) starting apply\" | Out-File -Append $log\n"
                          "if (Test-Path $backup) { Remove-Item -Recurse -Force $backup -ErrorAction "
                          "SilentlyContinue }\n"
                          "$ok = $false\n"
                          "for ($i = 1; $i -le 6; $i++) {\n"
                          "  try {\n"
                          "    Move-Item $target $backup -ErrorAction Stop\n"
                          "    try {\n"
                          "      Move-Item $staging $target -ErrorAction Stop\n"
                          "      $ok = $true\n"
                          "      break\n"
                          "    } catch {\n"
                          "      Move-Item $backup $target -ErrorAction SilentlyContinue\n"
                          "      break\n"
                          "    }\n"
                          "  } catch {\n"
                          "    Start-Sleep -Milliseconds (500 * $i)\n"
                          "  }\n"
                          "}\n"
                          "if ($ok) {\n"
                          "  if (Test-Path $backup) { Remove-Item -Recurse -Force $backup -ErrorAction "
                          "SilentlyContinue }\n"
                          "  \"$(Get-Date) applied ok\" | Out-File -Append $log\n"
                          "} else {\n"
                          "  \"$(Get-Date) apply FAILED, rolled back\" | Out-File -Append $log\n"
                          "}\n"
                          "Start-Process \"%5\"\n")
                          .arg(waitPid)
                          .arg(log)
                          .arg(targetDir)
                          .arg(stagingRoot)
                          .arg(exePath);
    QFile f(scriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "could not write apply-update.ps1";
        return false;
    }
    f.write(script.toUtf8());
    f.close();
    return QProcess::startDetached(
        "powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", scriptPath});
#else
    QString scriptPath = updatesDir() + "/apply-update.sh";
    bool isAppBundle = targetDir.endsWith(".app", Qt::CaseInsensitive);
    QString relaunchCmd = isAppBundle ? QString("open \"%1\"").arg(targetDir)
                                       : QString("\"%1/iHateVisa\" &").arg(targetDir);
    QString codesignCmd = isAppBundle ? QString("codesign --force --deep --sign - \"%1\" >> \"%2\" 2>&1\n")
                                             .arg(targetDir, log)
                                       : QString();
    QString script = QString(
                          "#!/bin/sh\n"
                          "PID=%1\n"
                          "for i in $(seq 1 120); do\n"
                          "  kill -0 \"$PID\" 2>/dev/null || break\n"
                          "  sleep 1\n"
                          "done\n"
                          "sleep 1\n"
                          "LOG=\"%2\"\n"
                          "TARGET=\"%3\"\n"
                          "STAGING=\"%4\"\n"
                          "BACKUP=\"$TARGET.old\"\n"
                          "echo \"$(date) starting apply\" >> \"$LOG\"\n"
                          "rm -rf \"$BACKUP\"\n"
                          "OK=0\n"
                          "i=1\n"
                          "while [ $i -le 6 ]; do\n"
                          "  if mv \"$TARGET\" \"$BACKUP\" 2>>\"$LOG\"; then\n"
                          "    if mv \"$STAGING\" \"$TARGET\" 2>>\"$LOG\"; then\n"
                          "      OK=1\n"
                          "      break\n"
                          "    else\n"
                          "      mv \"$BACKUP\" \"$TARGET\" 2>>\"$LOG\" || true\n"
                          "      break\n"
                          "    fi\n"
                          "  fi\n"
                          "  sleep $i\n"
                          "  i=$((i+1))\n"
                          "done\n"
                          "if [ \"$OK\" = \"1\" ]; then\n"
                          "  rm -rf \"$BACKUP\"\n"
                          "%5"
                          "  echo \"$(date) applied ok\" >> \"$LOG\"\n"
                          "else\n"
                          "  echo \"$(date) apply FAILED, rolled back\" >> \"$LOG\"\n"
                          "fi\n"
                          "%6\n")
                          .arg(waitPid)
                          .arg(log)
                          .arg(targetDir)
                          .arg(stagingRoot)
                          .arg(codesignCmd)
                          .arg(relaunchCmd);
    QFile f(scriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "could not write apply-update.sh";
        return false;
    }
    f.write(script.toUtf8());
    f.close();
    ::chmod(scriptPath.toUtf8().constData(), 0755);
    return QProcess::startDetached("/bin/sh", {scriptPath});
#endif
}

void UpdateInstaller::prepareAndApply(const UpdateInfo& info, std::function<void(double)> onProgress,
                                       std::function<void(QString)> onError,
                                       std::function<void()> onReadyToExit) {
    QString zipPath = updatesDir() + QString("/iHateVisa-%1-%2.zip").arg(info.latest, AppVersion::rid());
    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(info.package.url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QFile* outFile = new QFile(zipPath, this);
    if (!outFile->open(QIODevice::WriteOnly)) {
        onError("не удалось создать файл загрузки");
        return;
    }

    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::readyRead, this, [reply, outFile] { outFile->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [onProgress, info](qint64 got, qint64 total) {
                qint64 denom = total > 0 ? total : info.package.size;
                if (denom > 0 && onProgress) onProgress(static_cast<double>(got) / denom);
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, outFile, zipPath, info, onError, onReadyToExit] {
        outFile->close();
        reply->deleteLater();
        outFile->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(zipPath);
            onError("ошибка загрузки: " + reply->errorString());
            return;
        }

        QFile f(zipPath);
        if (!f.open(QIODevice::ReadOnly)) {
            onError("не удалось открыть загруженный файл");
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&f);
        QString actual = hash.result().toHex();
        f.close();
        if (!info.package.sha256.isEmpty() && actual.compare(info.package.sha256, Qt::CaseInsensitive) != 0) {
            QFile::remove(zipPath);
            appendLog("SHA-256 mismatch, expected " + info.package.sha256 + " got " + actual);
            onError("проверка SHA-256 не пройдена — файл повреждён");
            return;
        }

        QString stagingDir = updatesDir() + "/staging";
        QString err;
        if (!extractStaging(zipPath, stagingDir, &err)) {
            onError("не удалось распаковать обновление: " + err);
            return;
        }

        // The real root is a top-level *.app bundle if the zip has one —
        // found by extension, not just "exactly one entry", because
        // ditto's --sequesterRsrc (used when packaging the release) adds a
        // __MACOSX sidecar folder alongside the bundle, so a naive
        // single-entry check misses it and drags that metadata folder
        // along as if it were part of the app. Otherwise (no .app found)
        // the whole staging dir is the root (flat Windows publish).
        QDir sd(stagingDir);
        QStringList topEntries = sd.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        QString stagingRoot = stagingDir;
        QString appEntry;
        for (const QString& e : topEntries) {
            if (e.endsWith(".app", Qt::CaseInsensitive)) {
                appEntry = e;
                break;
            }
        }
        if (!appEntry.isEmpty())
            stagingRoot = stagingDir + "/" + appEntry;
        else if (topEntries.size() == 1)
            stagingRoot = stagingDir + "/" + topEntries.first();

        QString targetDir = targetDirectory();
        qint64 pid = QCoreApplication::applicationPid();
        appendLog(QString("prepared: staging=%1 target=%2 pid=%3").arg(stagingRoot, targetDir).arg(pid));
        if (!launchApplier(stagingRoot, targetDir, pid, &err)) {
            onError("не удалось запустить скрипт обновления: " + err);
            return;
        }
        if (onReadyToExit) onReadyToExit();
    });
}

}  // namespace ihv::update
