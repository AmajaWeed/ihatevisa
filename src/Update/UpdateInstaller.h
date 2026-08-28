#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include "Update/UpdateChecker.h"

namespace ihv::update {

// Download -> verify SHA-256 -> extract to a temporary staging folder ->
// launch a generated external apply script that waits for this process to
// exit, then copies the staged files over the installed files in place
// (overwriting changed files, removing ones no longer shipped, leaving the
// install directory itself untouched) and deletes the staging folder.
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
    // `stagingRoot` is the directory actually synced over `targetDir` (may
    // be a subdirectory of `stagingParentDir`, e.g. the *.app bundle inside
    // the raw extraction, which also holds a __MACOSX sidecar on macOS);
    // `stagingParentDir` is what gets deleted afterwards so no extraction
    // leftovers survive the update.
    bool launchApplier(const QString& stagingRoot, const QString& stagingParentDir, const QString& targetDir,
                        const QString& zipPath, qint64 waitPid, QString* error);
};

}  // namespace ihv::update
