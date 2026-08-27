#include "Update/UpdateToast.h"

#include <QApplication>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include "Update/AppVersion.h"

namespace ihv::update {

UpdateToast::UpdateToast(const UpdateInfo& info, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool), info_(info) {
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFixedWidth(340);
    setStyleSheet(
        "background:#1d1d1d; border:1px solid #363636; border-radius:10px; color:#e0e0e0;");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);

    auto* title = new QLabel(QString("iHateVisa %1").arg(info_.latest), this);
    title->setStyleSheet("font-weight:800; font-size:14px; border:none;");
    layout->addWidget(title);

    QString sub = QString("Установлена %1").arg(AppVersion::current());
    if (!info_.published.isEmpty()) sub += " · обновление от " + info_.published;
    auto* subLabel = new QLabel(sub, this);
    subLabel->setStyleSheet("color:#999; font-size:11px; border:none;");
    layout->addWidget(subLabel);

    if (!info_.notes.isEmpty()) {
        QString notesText;
        int shown = std::min(5, static_cast<int>(info_.notes.size()));
        for (int i = 0; i < shown; ++i) notesText += "• " + info_.notes[i] + "\n";
        if (info_.notes.size() > shown) notesText += QString("…и ещё %1").arg(info_.notes.size() - shown);
        auto* notesLabel = new QLabel(notesText.trimmed(), this);
        notesLabel->setWordWrap(true);
        notesLabel->setStyleSheet("font-size:11px; color:#ccc; border:none; margin-top:6px;");
        layout->addWidget(notesLabel);
    }

    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet("color:#e94560; font-size:11px; border:none;");
    statusLabel_->setWordWrap(true);
    statusLabel_->hide();
    layout->addWidget(statusLabel_);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(6);
    progress_->hide();
    layout->addWidget(progress_);

    buttonRow_ = new QWidget(this);
    auto* btnLayout = new QHBoxLayout(buttonRow_);
    btnLayout->setContentsMargins(0, 8, 0, 0);
    auto* updateBtn = new QPushButton("Обновить", buttonRow_);
    updateBtn->setStyleSheet("background:#e94560; color:#fff; border:none; padding:6px 12px; border-radius:6px;");
    auto* laterBtn = new QPushButton("Не сейчас", buttonRow_);
    auto* skipBtn = new QPushButton("Пропустить версию", buttonRow_);
    for (auto* b : {laterBtn, skipBtn})
        b->setStyleSheet("background:transparent; color:#aaa; border:none; padding:6px 8px;");
    btnLayout->addWidget(updateBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(laterBtn);
    btnLayout->addWidget(skipBtn);
    layout->addWidget(buttonRow_);

    connect(updateBtn, &QPushButton::clicked, this, &UpdateToast::beginUpdate);
    connect(laterBtn, &QPushButton::clicked, this, &QWidget::close);
    connect(skipBtn, &QPushButton::clicked, this, [this] {
        UpdateChecker::skip(info_.latest);
        close();
    });

    adjustSize();
}

void UpdateToast::placeBottomRight() {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    QRect area = screen->availableGeometry();
    move(area.right() - width() - 24, area.bottom() - height() - 24);
}

void UpdateToast::beginUpdate() {
    buttonRow_->setEnabled(false);
    progress_->show();
    statusLabel_->hide();

    installer_ = new UpdateInstaller(this);
    installer_->prepareAndApply(
        info_,
        [this](double frac) {
            progress_->setValue(static_cast<int>(frac * 100));
        },
        [this](QString error) {
            statusLabel_->setText(error);
            statusLabel_->show();
            progress_->hide();
            buttonRow_->setEnabled(true);
        },
        [] { QCoreApplication::quit(); });
}

}  // namespace ihv::update
