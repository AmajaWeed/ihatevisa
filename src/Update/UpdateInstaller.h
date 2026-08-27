#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include "Update/UpdateChecker.h"

namespace ihv::update {

// Download -> verify SHA-256 -> extract to staging -> launch a generated
// external apply script that waits for this process to exit, then
// atomically swaps staging in for the running install (with rollback and
// guaranteed relaunch on any failure). Ported 1:1 from iHateCards.NET's
// UpdateInstaller design (itself carried over from an earlier C# project,
// per the project's own update-architecture notes).
class UpdateInstaller : public QObject {
    Q_OBJECT

public:
    explicit UpdateInstaller(QObject* parent = nullptr);

    // Directory holding downloads/staging/logs, e.g.
    // ~/Library/Application Support/iHateVisa/updates (macOS) or
    // %LOCALAPPDATA%/iHateVisa/updates (Windows).
    static QString updatesDir();

    // The app's own install root: the .app bundle on macOS (walks up from
    // the running executable to find it), or the executable's containing
    // folder elsewhere (flat self-contained publish).
    static QString targetDirectory();

    // Runs the full pipeline. `onProgress` receives 0..1 for the download
    // phase. `onError` is called with a message on any failure (nothing
    // has been applied yet at that point — safe to just report and let the
    // user retry). On success, the apply script has been launched and the
    // caller must exit the application immediately (the script waits for
    // this process's PID before touching any files).
    void prepareAndApply(const UpdateInfo& info, std::function<void(double)> onProgress,
                          std::function<void(QString)> onError, std::function<void()> onReadyToExit);

private:
    bool extractStaging(const QString& zipPath, const QString& stagingDir, QString* error);
    bool launchApplier(const QString& stagingRoot, const QString& targetDir, qint64 waitPid, QString* error);
};

}  // namespace ihv::update
