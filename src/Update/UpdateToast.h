#pragma once

#include <QLabel>
#include <QProgressBar>
#include <QWidget>

#include "Update/UpdateChecker.h"
#include "Update/UpdateInstaller.h"

namespace ihv::update {

// Frameless, always-on-top toast in the screen's bottom-right corner, with
// three buttons — «Обновить» / «Не сейчас» / «Пропустить версию» — ported
// from iHateCards.NET's UpdateToast.
class UpdateToast : public QWidget {
    Q_OBJECT

public:
    explicit UpdateToast(const UpdateInfo& info, QWidget* parent = nullptr);

    void placeBottomRight();

private:
    void beginUpdate();

    UpdateInfo info_;
    UpdateInstaller* installer_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QWidget* buttonRow_ = nullptr;
};

}  // namespace ihv::update
