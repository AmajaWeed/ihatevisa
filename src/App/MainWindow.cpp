#include "App/MainWindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QImageWriter>
#include <QKeyEvent>
#include <QKeySequence>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>

#include "Core/EditorRenderer.h"
#include "Core/PrintComposer.h"
#include "Imaging/BackgroundTools.h"
#include "Imaging/CmykPipeline.h"
#include "Pdf/CmykPdfWriter.h"
#include "Project/HateFile.h"
#include "Update/UpdateChecker.h"
#include "Update/UpdateToast.h"

namespace ihv::app {

using core::EditorEngine::applyFormat;
using core::EditorEngine::onBotMarginChange;
using core::EditorEngine::onHeadPctChange;
using core::EditorEngine::onHeadSizeChange;
using core::EditorEngine::onTopMarginChange;
using core::EditorEngine::onWidthOrHeightChange;

namespace {
QSlider* makeSlider(int lo, int hi, int val) {
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(lo, hi);
    s->setValue(val);
    return s;
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    applyFormat(state_, "passport_rf");
    setAcceptDrops(true);
    buildUi();
    syncFormatFieldsToUi();
    QSignalBlocker b1(ovalCheck_), b2(cornerCheck_);
    ovalCheck_->setChecked(state_.ovalOverlay);
    cornerCheck_->setChecked(state_.cornerOverlay);
    switchTab(0);
    dirty_ = false;  // syncFormatFieldsToUi()/switchTab() above must not mark a fresh project dirty
    updateWindowTitle();
}

core::PhotoDocumentPtr MainWindow::activePhoto() const {
    for (auto& p : photos_)
        if (p->id == activePhotoId_) return p;
    return nullptr;
}

// ================= UI construction =================

void MainWindow::buildUi() {
    setWindowTitle("iHateVisa");
    resize(1300, 860);
    setAcceptDrops(true);

    auto* fileMenu = menuBar()->addMenu("Файл");
    auto* newAct = fileMenu->addAction("Новый проект");
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, [this] {
        if (!confirmDiscardUnsaved()) return;
        state_ = core::EditorState{};
        core::EditorEngine::applyFormat(state_, "passport_rf");
        photos_.clear();
        activePhotoId_ = -1;
        undoStack_.clear();
        projectPath_.clear();
        editor_->setPhoto(nullptr);
        syncFormatFieldsToUi();
        refresh();
        refreshThumbs();
        dirty_ = false;
        updateWindowTitle();
    });
    auto* openAct = fileMenu->addAction("Открыть проект...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openProjectDialog);
    auto* saveAct = fileMenu->addAction("Сохранить проект");
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::saveProjectOrPrompt);
    auto* saveAsAct = fileMenu->addAction("Сохранить проект как...");
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::saveProjectAs);
    fileMenu->addSeparator();
    auto* importAct = fileMenu->addAction("Импорт фото...");
    connect(importAct, &QAction::triggered, this, [this] {
        QStringList paths = QFileDialog::getOpenFileNames(this, "Импорт фото");
        if (!paths.isEmpty()) importFiles(paths);
    });
    auto* exportPhotoAct = fileMenu->addAction("Сохранить фото...");
    connect(exportPhotoAct, &QAction::triggered, this, &MainWindow::exportPhoto);
    auto* exportSheetAct = fileMenu->addAction("Сохранить лист (JPEG)...");
    connect(exportSheetAct, &QAction::triggered, this, &MainWindow::exportSheet);
    auto* exportCmykAct = fileMenu->addAction("Экспорт CMYK PDF...");
    connect(exportCmykAct, &QAction::triggered, this, &MainWindow::exportCmykPdf);

    auto* editMenu = menuBar()->addMenu("Правка");
    auto* undoAct = editMenu->addAction("Отменить");
    undoAct->setShortcut(QKeySequence::Undo);
    connect(undoAct, &QAction::triggered, this, &MainWindow::undo);

    auto* helpMenu = menuBar()->addMenu("Справка");
    auto* checkUpdateAct = helpMenu->addAction("Проверить обновления...");
    connect(checkUpdateAct, &QAction::triggered, this, [this] {
        auto* checker = new update::UpdateChecker(this);
        checker->checkAsync(
            [this, checker](std::optional<update::UpdateInfo> info) {
                checker->deleteLater();
                if (!info) {
                    QMessageBox::information(this, "Обновления", "У вас установлена последняя версия.");
                    return;
                }
                auto* toast = new update::UpdateToast(*info, nullptr);
                toast->placeBottomRight();
                toast->show();
            },
            /*ignoreSkipped=*/true);
    });

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // Top bar
    auto* topBar = new QWidget(central);
    topBar->setFixedHeight(44);
    auto* topLayout = new QHBoxLayout(topBar);
    auto* logo = new QLabel("📸 iHateVisa", topBar);
    logo->setStyleSheet("font-weight:800;");
    topInfo_ = new QLabel("Загрузите фотографию", topBar);
    topInfo_->setStyleSheet("color:#888;");
    auto* importBtn = new QPushButton("＋ Добавить фото", topBar);
    connect(importBtn, &QPushButton::clicked, this, [this] {
        QStringList paths = QFileDialog::getOpenFileNames(
            this, "Импорт фото", QString(),
            "Изображения (*.png *.jpg *.jpeg *.heic *.heif *.webp *.bmp *.tif *.tiff);;Все файлы (*)");
        if (!paths.isEmpty()) importFiles(paths);
    });
    topLayout->addWidget(logo);
    topLayout->addWidget(topInfo_, 1);
    topLayout->addWidget(importBtn);
    rootLayout->addWidget(topBar);

    // Tab bar
    tabBar_ = new QTabBar(central);
    tabBar_->addTab("📐 Размеры");
    tabBar_->addTab("🖼 Готовая фотография");
    tabBar_->addTab("🖨 Печать");
    connect(tabBar_, &QTabBar::currentChanged, this, &MainWindow::switchTab);
    rootLayout->addWidget(tabBar_);

    // Body: thumbs | center | side panel
    auto* body = new QWidget(central);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);

    auto* leftCol = new QWidget(body);
    leftCol->setFixedWidth(160);
    auto* leftLayout = new QVBoxLayout(leftCol);
    buildThumbStrip(leftLayout);
    bodyLayout->addWidget(leftCol);

    centerStack_ = new QStackedWidget(body);
    editor_ = new EditorWidget(centerStack_);
    editor_->setState(&state_);
    editor_->onDragStart = [this] { pushUndo(); };
    editor_->onGuideChanged = [this] { refreshInfo(); };
    editor_->onWandClick = [this](QPoint p) { onWandClick(p); };
    editor_->onBrushPaint = [this](QPoint p, bool restore) { onBrushPaint(p, restore); };
    printPreview_ = new QLabel(centerStack_);
    printPreview_->setAlignment(Qt::AlignCenter);
    centerStack_->addWidget(editor_);
    centerStack_->addWidget(printPreview_);
    bodyLayout->addWidget(centerStack_, 1);

    sidePanels_ = new QStackedWidget(body);
    sidePanels_->setFixedWidth(300);
    sidePanels_->addWidget(buildSizesPanel());
    sidePanels_->addWidget(buildReadyPanel());
    sidePanels_->addWidget(buildPrintPanel());
    bodyLayout->addWidget(sidePanels_);

    rootLayout->addWidget(body, 1);
    setCentralWidget(central);
}

QWidget* MainWindow::buildThumbStrip(QBoxLayout* parentLayout) {
    thumbList_ = new QListWidget();
    thumbList_->setViewMode(QListView::IconMode);
    thumbList_->setIconSize(QSize(64, 64));
    thumbList_->setResizeMode(QListView::Adjust);
    thumbList_->setSpacing(4);
    connect(thumbList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= static_cast<int>(photos_.size())) return;
        selectPhoto(photos_[row]->id);
    });
    parentLayout->addWidget(thumbList_, 1);
    auto* addBtn = new QPushButton("＋ Фото");
    connect(addBtn, &QPushButton::clicked, this, [this] {
        QStringList paths = QFileDialog::getOpenFileNames(this, "Импорт фото");
        if (!paths.isEmpty()) importFiles(paths);
    });
    parentLayout->addWidget(addBtn);
    return nullptr;
}

QWidget* MainWindow::buildSizesPanel() {
    auto* panel = new QWidget();
    auto* layout = new QVBoxLayout(panel);

    auto* formatBox = new QGroupBox("Формат документа");
    auto* fl = new QVBoxLayout(formatBox);
    formatCombo_ = new QComboBox();
    for (const auto& f : core::FormatPresets::All) formatCombo_->addItem(QString::fromStdString(f.name),
                                                                          QString::fromStdString(f.key));
    connect(formatCombo_, &QComboBox::currentIndexChanged, this, [this](int) { applyFormatChange(); });
    fl->addWidget(formatCombo_);
    formatDesc_ = new QLabel();
    formatDesc_->setWordWrap(true);
    fl->addWidget(formatDesc_);
    layout->addWidget(formatBox);

    auto* paramBox = new QGroupBox("Параметры формата");
    auto* pl = new QVBoxLayout(paramBox);
    auto addRow = [&](const QString& label, QDoubleSpinBox*& spin, double lo, double hi, double step) {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(label));
        spin = new QDoubleSpinBox();
        spin->setRange(lo, hi);
        spin->setSingleStep(step);
        row->addWidget(spin);
        pl->addLayout(row);
    };
    addRow("Ширина (мм)", widthSpin_, 10, 100, 1);
    addRow("Высота (мм)", heightSpin_, 10, 100, 1);
    addRow("Верх. поле (мм)", topMarginSpin_, 0, 30, 0.5);
    addRow("Нижн. поле (мм)", botMarginSpin_, 0, 60, 0.5);
    addRow("% лица на фото", headPctSpin_, 30, 95, 0.5);
    addRow("Голова (мм)", headSizeSpin_, 5, 95, 0.5);
    connect(widthSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        state_.widthMm = v;
        onWidthOrHeightChange(state_);
        syncFormatFieldsToUi();
        refresh();
    });
    connect(heightSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        state_.heightMm = v;
        onWidthOrHeightChange(state_);
        syncFormatFieldsToUi();
        refresh();
    });
    connect(topMarginSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        state_.topMarginMm = v;
        onTopMarginChange(state_);
        syncFormatFieldsToUi();
        refresh();
    });
    connect(botMarginSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        state_.botMarginMm = v;
        onBotMarginChange(state_);
        syncFormatFieldsToUi();
        refresh();
    });
    connect(headPctSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        state_.headPct = v;
        onHeadPctChange(state_);
        syncFormatFieldsToUi();
        refresh();
    });
    connect(headSizeSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        state_.headSizeMm = v;
        onHeadSizeChange(state_);
        syncFormatFieldsToUi();
        refresh();
    });

    lockVerticalCheck_ = new QCheckBox("Зафиксировать вертикаль (без поворота головы)");
    lockVerticalCheck_->setChecked(true);
    connect(lockVerticalCheck_, &QCheckBox::toggled, this, [this](bool locked) {
        state_.lockVertical = locked;
        if (locked) {
            state_.guide.rotation = 0;
            rotationValueLabel_->setText("0°");
        }
        refresh();
    });
    pl->addWidget(lockVerticalCheck_);

    // No rotation slider — drag the crown/chin dots off the vertical to
    // tilt (see EditorRenderer::solveGuideFromDots). This just reflects
    // the current angle.
    auto* rotRow = new QHBoxLayout();
    rotRow->addWidget(new QLabel("Угол наклона"));
    rotationValueLabel_ = new QLabel("0°");
    rotRow->addWidget(rotationValueLabel_, 1, Qt::AlignRight);
    pl->addLayout(rotRow);
    layout->addWidget(paramBox);

    auto* overlayBox = new QGroupBox("Накладки");
    auto* ol = new QVBoxLayout(overlayBox);
    ovalCheck_ = new QCheckBox("Овал лица (виньетка)");
    cornerCheck_ = new QCheckBox("Уголок");
    connect(ovalCheck_, &QCheckBox::toggled, this, [this](bool v) {
        state_.ovalOverlay = v;
        refresh();
    });
    connect(cornerCheck_, &QCheckBox::toggled, this, [this](bool v) {
        state_.cornerOverlay = v;
        refresh();
    });
    ol->addWidget(ovalCheck_);
    ol->addWidget(cornerCheck_);

    auto* cornerPosRow = new QHBoxLayout();
    struct CornerBtn {
        core::CornerPosition pos;
        QString label;
    };
    for (const auto& cb : {CornerBtn{core::CornerPosition::TopLeft, "↖"}, CornerBtn{core::CornerPosition::TopRight, "↗"},
                            CornerBtn{core::CornerPosition::BottomLeft, "↙"},
                            CornerBtn{core::CornerPosition::BottomRight, "↘"}}) {
        auto* b = new QPushButton(cb.label);
        b->setCheckable(true);
        b->setFixedWidth(32);
        b->setChecked(cb.pos == state_.cornerPosition);
        connect(b, &QPushButton::clicked, this, [this, pos = cb.pos, b] {
            state_.cornerPosition = pos;
            for (auto* btn : cornerPosButtons_) btn->setChecked(btn == b);
            refresh();
        });
        cornerPosButtons_.push_back(b);
        cornerPosRow->addWidget(b);
    }
    ol->addLayout(cornerPosRow);
    layout->addWidget(overlayBox);

    auto* actionsBox = new QGroupBox("Действия");
    auto* al = new QVBoxLayout(actionsBox);
    auto* exportBtn = new QPushButton("💾 Сохранить фото");
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportPhoto);
    al->addWidget(exportBtn);
    auto* resetBtn = new QPushButton("↺ Сброс");
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        pushUndo();
        resetGuide();
        refresh();
    });
    al->addWidget(resetBtn);
    layout->addWidget(actionsBox);

    layout->addStretch();
    return panel;
}

QWidget* MainWindow::buildReadyPanel() {
    auto* panel = new QWidget();
    auto* layout = new QVBoxLayout(panel);

    auto* bgBox = new QGroupBox("Удаление фона");
    auto* bl = new QVBoxLayout(bgBox);
    auto* cleanBtn = new QPushButton("🧹 Удаление фона");
    connect(cleanBtn, &QPushButton::clicked, this, &MainWindow::autoCleanBackground);
    bl->addWidget(cleanBtn);
    auto* acRow = new QHBoxLayout();
    acRow->addWidget(new QLabel("Порог"));
    acThresholdSlider_ = makeSlider(10, 120, 45);
    acRow->addWidget(acThresholdSlider_, 1);
    bl->addLayout(acRow);

    wandBtn_ = new QPushButton("🪄 Волшебная палочка");
    wandBtn_->setCheckable(true);
    bl->addWidget(wandBtn_);
    auto* mwRow = new QHBoxLayout();
    mwRow->addWidget(new QLabel("Порог"));
    mwThresholdSlider_ = makeSlider(5, 80, 25);
    mwRow->addWidget(mwThresholdSlider_, 1);
    bl->addLayout(mwRow);

    // Touch-up brushes: paint restore/erase alpha directly, with a soft
    // (feathered) edge — for cleaning up after auto-clean/magic wand
    // without having to redo the whole selection.
    restoreBrushBtn_ = new QPushButton("🖌 Восстановить кистью");
    restoreBrushBtn_->setCheckable(true);
    bl->addWidget(restoreBrushBtn_);
    eraseBrushBtn_ = new QPushButton("🧽 Стереть кистью");
    eraseBrushBtn_->setCheckable(true);
    bl->addWidget(eraseBrushBtn_);
    auto* brushRow = new QHBoxLayout();
    brushRow->addWidget(new QLabel("Размер"));
    brushSizeSlider_ = makeSlider(5, 150, 40);
    brushRow->addWidget(brushSizeSlider_, 1);
    bl->addLayout(brushRow);

    auto updateToolMode = [this] {
        using Tool = EditorWidget::ToolMode;
        Tool t = wandBtn_->isChecked()          ? Tool::Wand
                 : restoreBrushBtn_->isChecked() ? Tool::BrushRestore
                 : eraseBrushBtn_->isChecked()   ? Tool::BrushErase
                                                  : Tool::Guide;
        editor_->setToolMode(t);
    };
    connect(wandBtn_, &QPushButton::toggled, this, [this, updateToolMode](bool on) {
        if (on) {
            restoreBrushBtn_->setChecked(false);
            eraseBrushBtn_->setChecked(false);
        }
        updateToolMode();
    });
    connect(restoreBrushBtn_, &QPushButton::toggled, this, [this, updateToolMode](bool on) {
        if (on) {
            wandBtn_->setChecked(false);
            eraseBrushBtn_->setChecked(false);
        }
        updateToolMode();
    });
    connect(eraseBrushBtn_, &QPushButton::toggled, this, [this, updateToolMode](bool on) {
        if (on) {
            wandBtn_->setChecked(false);
            restoreBrushBtn_->setChecked(false);
        }
        updateToolMode();
    });
    connect(brushSizeSlider_, &QSlider::valueChanged, this, [this](int v) { editor_->setBrushRadiusPx(v); });

    auto* undoWandBtn = new QPushButton("↺ Восстановить фон");
    connect(undoWandBtn, &QPushButton::clicked, this, &MainWindow::undoWand);
    bl->addWidget(undoWandBtn);
    layout->addWidget(bgBox);

    auto* colorBox = new QGroupBox("Цвет фона");
    auto* cl = new QHBoxLayout(colorBox);
    static const char* presets[] = {"#ffffff", "#f0f0f0", "#e0e0e0", "#d5dbe6", "#cce5ff", "#ffeedd", "#f5f5dc"};
    for (const char* hex : presets) {
        auto* sw = new QPushButton();
        sw->setFixedSize(22, 22);
        sw->setStyleSheet(QString("background:%1;border:1px solid #555;").arg(hex));
        connect(sw, &QPushButton::clicked, this, [this, hex] {
            pushUndo();
            state_.bgColorHex = hex;
            refresh();
        });
        cl->addWidget(sw);
    }
    layout->addWidget(colorBox);

    auto* adjBox = new QGroupBox("Инструменты");
    auto* adl = new QVBoxLayout(adjBox);
    auto addSlider = [&](const QString& label, QSlider*& slider, int lo, int hi, int val) {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(label));
        slider = makeSlider(lo, hi, val);
        row->addWidget(slider, 1);
        adl->addLayout(row);
    };
    addSlider("Яркость", brightSlider_, -100, 100, 0);
    addSlider("Контраст", contrastSlider_, -100, 100, 0);
    addSlider("Гамма", gammaSlider_, 20, 300, 100);
    addSlider("Насыщ.", satSlider_, 0, 300, 100);
    connect(brightSlider_, &QSlider::valueChanged, this, [this](int v) {
        state_.brightness = v;
        refresh();
    });
    connect(contrastSlider_, &QSlider::valueChanged, this, [this](int v) {
        state_.contrast = v;
        refresh();
    });
    connect(gammaSlider_, &QSlider::valueChanged, this, [this](int v) {
        state_.gammaPercent = v;
        refresh();
    });
    connect(satSlider_, &QSlider::valueChanged, this, [this](int v) {
        state_.saturationPercent = v;
        refresh();
    });
    bwCheck_ = new QCheckBox("Чёрно-белое");
    connect(bwCheck_, &QCheckBox::toggled, this, [this](bool v) {
        state_.blackAndWhite = v;
        refresh();
    });
    adl->addWidget(bwCheck_);
    layout->addWidget(adjBox);

    auto* ccBox = new QGroupBox("Цветокоррекция");
    auto* ccl = new QVBoxLayout(ccBox);
    auto* ccTabsRow = new QHBoxLayout();
    for (const auto& [key, title] : {std::pair{"shadows", "Тени"}, {"mid", "Центр"}, {"high", "Блики"}}) {
        auto* b = new QPushButton(title);
        b->setCheckable(true);
        b->setChecked(key == std::string("shadows"));
        connect(b, &QPushButton::clicked, this, [this, key = std::string(key)] {
            ccTab_ = key;
            const core::CcZone& z = key == "shadows" ? state_.cc.shadows : (key == "mid" ? state_.cc.mid : state_.cc.high);
            QSignalBlocker b1(ccCSlider_), b2(ccMSlider_), b3(ccYSlider_);
            ccCSlider_->setValue(z.c);
            ccMSlider_->setValue(z.m);
            ccYSlider_->setValue(z.y);
            ccCVal_->setText(QString::number(z.c));
            ccMVal_->setText(QString::number(z.m));
            ccYVal_->setText(QString::number(z.y));
        });
        ccTabsRow->addWidget(b);
    }
    ccl->addLayout(ccTabsRow);
    auto addCcRow = [&](const QString& swatchColor, QSlider*& slider, QLabel*& val) {
        auto* row = new QHBoxLayout();
        auto* sw = new QLabel();
        sw->setFixedSize(14, 14);
        sw->setStyleSheet(QString("background:%1;").arg(swatchColor));
        row->addWidget(sw);
        slider = makeSlider(-50, 50, 0);
        row->addWidget(slider, 1);
        val = new QLabel("0");
        row->addWidget(val);
        ccl->addLayout(row);
    };
    addCcRow("cyan", ccCSlider_, ccCVal_);
    addCcRow("magenta", ccMSlider_, ccMVal_);
    addCcRow("yellow", ccYSlider_, ccYVal_);
    auto onCcSlider = [this] {
        core::CcZone& z = ccTab_ == "shadows" ? state_.cc.shadows : (ccTab_ == "mid" ? state_.cc.mid : state_.cc.high);
        z.c = ccCSlider_->value();
        z.m = ccMSlider_->value();
        z.y = ccYSlider_->value();
        ccCVal_->setText(QString::number(z.c));
        ccMVal_->setText(QString::number(z.m));
        ccYVal_->setText(QString::number(z.y));
        refresh();
    };
    connect(ccCSlider_, &QSlider::valueChanged, this, onCcSlider);
    connect(ccMSlider_, &QSlider::valueChanged, this, onCcSlider);
    connect(ccYSlider_, &QSlider::valueChanged, this, onCcSlider);
    layout->addWidget(ccBox);

    layout->addStretch();
    return panel;
}

QWidget* MainWindow::buildPrintPanel() {
    auto* panel = new QWidget();
    auto* layout = new QVBoxLayout(panel);

    auto* actionsBox = new QGroupBox("Действия");
    auto* al = new QVBoxLayout(actionsBox);
    auto* printBtn = new QPushButton("🖨 Печать со страницы");
    connect(printBtn, &QPushButton::clicked, this, &MainWindow::printSheet);
    al->addWidget(printBtn);
    auto* exportBtn = new QPushButton("📄 Сохранить лист 10x15");
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportSheet);
    al->addWidget(exportBtn);
    layout->addWidget(actionsBox);

    auto* countBox = new QGroupBox("Количество фото");
    auto* cl = new QVBoxLayout(countBox);
    auto* countRow = new QHBoxLayout();
    for (int n : {2, 4, 6, 8}) {
        auto* b = new QPushButton(QString::number(n));
        b->setCheckable(true);
        b->setChecked(n == 6);
        connect(b, &QPushButton::clicked, this, [this, n] {
            state_.printPhotoCount = n;
            for (auto* btn : countButtons_) btn->setChecked(btn->text().toInt() == n);
            updatePrintLayoutInfo();
        });
        countButtons_.push_back(b);
        countRow->addWidget(b);
    }
    cl->addLayout(countRow);
    printLayoutInfo_ = new QLabel();
    cl->addWidget(printLayoutInfo_);
    layout->addWidget(countBox);

    auto* paramBox = new QGroupBox("Параметры листа");
    auto* pl = new QVBoxLayout(paramBox);
    auto* dpiRow = new QHBoxLayout();
    dpiRow->addWidget(new QLabel("DPI"));
    printDpiSpin_ = new QSpinBox();
    printDpiSpin_->setRange(72, 600);
    printDpiSpin_->setValue(300);
    dpiRow->addWidget(printDpiSpin_);
    pl->addLayout(dpiRow);
    auto* mgRow = new QHBoxLayout();
    mgRow->addWidget(new QLabel("Поля (мм)"));
    printMarginSpin_ = new QDoubleSpinBox();
    printMarginSpin_->setRange(0, 20);
    printMarginSpin_->setValue(3);
    mgRow->addWidget(printMarginSpin_);
    pl->addLayout(mgRow);
    auto* gapRow = new QHBoxLayout();
    gapRow->addWidget(new QLabel("Зазор (мм)"));
    printGapSpin_ = new QDoubleSpinBox();
    printGapSpin_->setRange(0, 10);
    printGapSpin_->setValue(2);
    gapRow->addWidget(printGapSpin_);
    pl->addLayout(gapRow);
    printBorderCheck_ = new QCheckBox("рамка для фото");
    pl->addWidget(printBorderCheck_);

    connect(printDpiSpin_, &QSpinBox::valueChanged, this, [this](int v) {
        state_.printDpi = v;
        updatePrintLayoutInfo();
    });
    connect(printMarginSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        state_.printMarginMm = v;
        updatePrintLayoutInfo();
    });
    connect(printGapSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        state_.printGapMm = v;
        updatePrintLayoutInfo();
    });
    connect(printBorderCheck_, &QCheckBox::toggled, this, [this](bool v) {
        state_.printBorder = v;
        updatePrintLayoutInfo();
    });
    layout->addWidget(paramBox);

    // Printer streaking/banding compensation: center the whole grid on the
    // sheet by default (rather than anchored top-left), plus a manual
    // offset since a specific printer's streak position doesn't move.
    auto* posBox = new QGroupBox("Положение на листе");
    auto* posLayout = new QVBoxLayout(posBox);
    printCenterCheck_ = new QCheckBox("Центрировать на листе");
    printCenterCheck_->setChecked(true);
    connect(printCenterCheck_, &QCheckBox::toggled, this, [this](bool v) {
        state_.printCenter = v;
        updatePrintLayoutInfo();
    });
    posLayout->addWidget(printCenterCheck_);
    auto* offXRow = new QHBoxLayout();
    offXRow->addWidget(new QLabel("Сдвиг X (мм)"));
    printOffsetXSpin_ = new QDoubleSpinBox();
    printOffsetXSpin_->setRange(-50, 50);
    offXRow->addWidget(printOffsetXSpin_);
    posLayout->addLayout(offXRow);
    auto* offYRow = new QHBoxLayout();
    offYRow->addWidget(new QLabel("Сдвиг Y (мм)"));
    printOffsetYSpin_ = new QDoubleSpinBox();
    printOffsetYSpin_->setRange(-50, 50);
    offYRow->addWidget(printOffsetYSpin_);
    posLayout->addLayout(offYRow);
    connect(printOffsetXSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        state_.printOffsetXMm = v;
        updatePrintLayoutInfo();
    });
    connect(printOffsetYSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        state_.printOffsetYMm = v;
        updatePrintLayoutInfo();
    });
    layout->addWidget(posBox);

    layout->addStretch();
    return panel;
}

// ================= Actions =================

void MainWindow::switchTab(int index) {
    currentTab_ = index;
    sidePanels_->setCurrentIndex(index);
    if (index == 2) {
        centerStack_->setCurrentWidget(printPreview_);
        updatePrintLayoutInfo();
    } else {
        centerStack_->setCurrentWidget(editor_);
        editor_->setMode(index == 0 ? core::EditorRenderer::Mode::Editing : core::EditorRenderer::Mode::Preview);
        using Tool = EditorWidget::ToolMode;
        Tool t = Tool::Guide;
        if (index == 1) {
            if (wandBtn_->isChecked())
                t = Tool::Wand;
            else if (restoreBrushBtn_->isChecked())
                t = Tool::BrushRestore;
            else if (eraseBrushBtn_->isChecked())
                t = Tool::BrushErase;
        }
        editor_->setToolMode(t);
        editor_->update();
    }
}

void MainWindow::importFiles(const QStringList& paths) {
    QStringList failed;
    for (const QString& path : paths) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QImage img = reader.read();
        if (img.isNull()) {
            failed << QFileInfo(path).fileName() + ": " + reader.errorString();
            continue;
        }
        auto doc = std::make_shared<core::PhotoDocument>();
        doc->id = nextPhotoId_++;
        doc->name = QFileInfo(path).fileName();
        doc->original = img.convertToFormat(QImage::Format_ARGB32);
        photos_.push_back(doc);
        selectPhoto(doc->id);
    }
    if (!failed.isEmpty()) {
        QMessageBox::warning(this, "Не удалось открыть", "Не удалось прочитать:\n" + failed.join("\n"));
    }
    refreshThumbs();
}

void MainWindow::selectPhoto(int id) {
    activePhotoId_ = id;
    resetGuide();
    undoStack_.clear();
    editor_->setPhoto(activePhoto());
    refresh();
    refreshThumbs();
}

void MainWindow::resetGuide() { state_.guide = core::Guide{}; }

void MainWindow::applyFormatChange() {
    std::string key = formatCombo_->currentData().toString().toStdString();
    applyFormat(state_, key);
    syncFormatFieldsToUi();
    QSignalBlocker b1(ovalCheck_), b2(cornerCheck_);
    ovalCheck_->setChecked(state_.ovalOverlay);
    cornerCheck_->setChecked(state_.cornerOverlay);
    refresh();
}

void MainWindow::syncFormatFieldsToUi() {
    QSignalBlocker b1(widthSpin_), b2(heightSpin_), b3(topMarginSpin_), b4(botMarginSpin_), b5(headPctSpin_),
        b6(headSizeSpin_);
    widthSpin_->setValue(state_.widthMm);
    heightSpin_->setValue(state_.heightMm);
    topMarginSpin_->setValue(state_.topMarginMm);
    botMarginSpin_->setValue(state_.botMarginMm);
    headPctSpin_->setValue(state_.headPct);
    headSizeSpin_->setValue(state_.headSizeMm);
    const core::FormatPreset& f = core::FormatPresets::byKey(state_.formatKey);
    formatDesc_->setText(QString::fromStdString(f.name) + "\n" + QString::fromStdString(f.description));
}

void MainWindow::pushUndo() {
    undoStack_.push_back(state_);
    static constexpr size_t kMaxUndo = 40;
    if (undoStack_.size() > kMaxUndo) undoStack_.erase(undoStack_.begin());
}

void MainWindow::undo() {
    if (undoStack_.empty()) return;
    state_ = undoStack_.back();
    undoStack_.pop_back();
    syncFormatFieldsToUi();
    QSignalBlocker b2(brightSlider_), b3(contrastSlider_), b4(gammaSlider_), b5(satSlider_), b6(bwCheck_),
        b7(ovalCheck_), b8(cornerCheck_);
    rotationValueLabel_->setText(QString::number(state_.guide.rotation, 'f', 1) + "°");
    brightSlider_->setValue(state_.brightness);
    contrastSlider_->setValue(state_.contrast);
    gammaSlider_->setValue(state_.gammaPercent);
    satSlider_->setValue(state_.saturationPercent);
    bwCheck_->setChecked(state_.blackAndWhite);
    ovalCheck_->setChecked(state_.ovalOverlay);
    cornerCheck_->setChecked(state_.cornerOverlay);
    refresh();
}

void MainWindow::refresh() {
    dirty_ = true;
    editor_->update();
    refreshInfo();
    if (currentTab_ == 2) updatePrintLayoutInfo();
    updateWindowTitle();
}

void MainWindow::refreshThumbs() {
    thumbList_->blockSignals(true);
    thumbList_->clear();
    int selectedRow = -1;
    for (size_t i = 0; i < photos_.size(); ++i) {
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(photos_[i]->original.scaled(
                                              64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation))),
                                          QString());
        thumbList_->addItem(item);
        if (photos_[i]->id == activePhotoId_) selectedRow = static_cast<int>(i);
    }
    if (selectedRow >= 0) thumbList_->setCurrentRow(selectedRow);
    thumbList_->blockSignals(false);
}

void MainWindow::refreshInfo() {
    rotationValueLabel_->setText(QString::number(state_.guide.rotation, 'f', 1) + "°");
    // Top bar just shows transient status (saved/exported/etc, set directly
    // by those actions) rather than a permanent filename/px/dpi dump.
    auto ph = activePhoto();
    if (!ph) topInfo_->setText("Загрузите фотографию");
}

void MainWindow::exportPhoto() {
    auto ph = activePhoto();
    if (!ph) {
        QMessageBox::information(this, "Нет фото", "Сначала импортируйте фотографию.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, "Сохранить фото",
        QString("photo_%1x%2.jpg").arg(state_.widthMm).arg(state_.heightMm), "JPEG (*.jpg)");
    if (path.isEmpty()) return;

    QImage img = core::EditorRenderer::renderFinal(state_, *ph, state_.dpi);
    img.setDotsPerMeterX(static_cast<int>(std::round(state_.dpi / 0.0254)));
    img.setDotsPerMeterY(static_cast<int>(std::round(state_.dpi / 0.0254)));
    QImageWriter writer(path, "jpg");
    writer.setQuality(95);
    if (!writer.write(img)) {
        QMessageBox::warning(this, "Ошибка", "Не удалось сохранить файл.");
        return;
    }
    topInfo_->setText(QString("Сохранено: %1 (%2 dpi)").arg(path).arg(state_.dpi));
}

void MainWindow::exportSheet() {
    auto ph = activePhoto();
    if (!ph) {
        QMessageBox::information(this, "Нет фото", "Сначала импортируйте фотографию.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Сохранить лист", "print_10x15.jpg", "JPEG (*.jpg)");
    if (path.isEmpty()) return;
    QImage sheet = core::PrintComposer::buildSheet(state_, *ph);
    sheet.setDotsPerMeterX(static_cast<int>(std::round(state_.printDpi / 0.0254)));
    sheet.setDotsPerMeterY(static_cast<int>(std::round(state_.printDpi / 0.0254)));
    QImageWriter writer(path, "jpg");
    writer.setQuality(95);
    writer.write(sheet);
    topInfo_->setText("Лист сохранён: " + path);
}

void MainWindow::exportCmykPdf() {
    auto ph = activePhoto();
    if (!ph) {
        QMessageBox::information(this, "Нет фото", "Сначала импортируйте фотографию.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Экспорт CMYK PDF", "print_10x15.pdf", "PDF (*.pdf)");
    if (path.isEmpty()) return;

    QImage sheet = core::PrintComposer::buildSheet(state_, *ph);
    pdf::CmykPage page;
    page.cmyk = imaging::CmykPipeline::toCmyk(sheet);
    page.width = sheet.width();
    page.height = sheet.height();
    page.widthMm = core::PrintComposer::SheetWidthMm;
    page.heightMm = core::PrintComposer::SheetHeightMm;

    auto pdfBytes = pdf::buildCmykPdf({page}, imaging::CmykPipeline::swopIcc());
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Ошибка", "Не удалось сохранить файл.");
        return;
    }
    out.write(reinterpret_cast<const char*>(pdfBytes.data()), static_cast<qint64>(pdfBytes.size()));
    out.close();
    topInfo_->setText("CMYK PDF сохранён: " + path);
}

void MainWindow::printSheet() {
    auto ph = activePhoto();
    if (!ph) return;
    QImage sheet = core::PrintComposer::buildSheet(state_, *ph);
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QSizeF(core::PrintComposer::SheetWidthMm, core::PrintComposer::SheetHeightMm),
                                   QPageSize::Millimeter));
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QPainter painter(&printer);
    QRect target = painter.viewport();
    QImage scaled = sheet.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter.drawImage(target.topLeft(), scaled);
}

void MainWindow::autoCleanBackground() {
    auto ph = activePhoto();
    if (!ph) return;
    pushUndo();
    ph->processed = imaging::BackgroundTools::autoClean(ph->displayImage(), acThresholdSlider_->value());
    refresh();
    refreshThumbs();
}

void MainWindow::onWandClick(QPoint sourcePx) {
    auto ph = activePhoto();
    if (!ph) return;
    pushUndo();
    int threshold = mwThresholdSlider_->value() * 3;
    ph->processed = imaging::BackgroundTools::magicWandFill(ph->displayImage(), sourcePx, threshold);
    refresh();
    refreshThumbs();
}

void MainWindow::onBrushPaint(QPoint sourcePx, bool restore) {
    auto ph = activePhoto();
    if (!ph) return;
    // Undo was already pushed once at drag-start (EditorWidget::onDragStart)
    // — a fresh snapshot per dab would flood the stack with one entry per
    // mouse-move event.
    if (!ph->processed) ph->processed = ph->original;
    imaging::BackgroundTools::paintBrush(*ph->processed, sourcePx, brushSizeSlider_->value(), restore ? 255 : 0);
    refresh();
}

void MainWindow::undoWand() {
    auto ph = activePhoto();
    if (!ph) return;
    pushUndo();
    ph->processed.reset();
    refresh();
    refreshThumbs();
}

void MainWindow::updatePrintLayoutInfo() {
    auto ph = activePhoto();
    if (!ph) {
        if (printLayoutInfo_) printLayoutInfo_->clear();
        return;
    }
    QImage sheet = core::PrintComposer::buildSheet(state_, *ph);
    if (currentTab_ == 2) {
        printPreview_->setPixmap(QPixmap::fromImage(sheet).scaled(printPreview_->size(), Qt::KeepAspectRatio,
                                                                    Qt::SmoothTransformation));
    }
    int photoPxW = static_cast<int>(std::round(state_.widthMm / 25.4 * state_.printDpi));
    int photoPxH = static_cast<int>(std::round(state_.heightMm / 25.4 * state_.printDpi));
    core::PrintComposer::Layout layout = core::PrintComposer::computeLayout(state_, photoPxW, photoPxH);
    printLayoutInfo_->setText(QString("Макс.: %1 (%2x%3) · Выбрано: %4")
                                   .arg(layout.maxFit)
                                   .arg(layout.cols)
                                   .arg(layout.rows)
                                   .arg(layout.count));
}

// ================= Events =================

void MainWindow::keyPressEvent(QKeyEvent* e) {
    // Ctrl+Z/S/Shift+S/O are wired as QAction shortcuts (menu bar) instead
    // of handled here, so they work even when a menu/dialog has focus.
    if (!activePhoto()) {
        QMainWindow::keyPressEvent(e);
        return;
    }
    double step = (e->modifiers() & Qt::ShiftModifier) ? 2.0 : 0.5;
    if (e->key() == Qt::Key_Left) {
        pushUndo();
        state_.guide.x += step;
        refresh();
    } else if (e->key() == Qt::Key_Right) {
        pushUndo();
        state_.guide.x -= step;
        refresh();
    } else if (e->key() == Qt::Key_Up) {
        pushUndo();
        state_.guide.y += step;
        refresh();
    } else if (e->key() == Qt::Key_Down) {
        pushUndo();
        state_.guide.y -= step;
        refresh();
    } else {
        QMainWindow::keyPressEvent(e);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    QStringList paths;
    for (const QUrl& url : e->mimeData()->urls()) paths << url.toLocalFile();
    if (paths.size() == 1 && paths.first().endsWith(".hate", Qt::CaseInsensitive)) {
        if (confirmDiscardUnsaved()) openProject(paths.first());
        return;
    }
    if (!paths.isEmpty()) importFiles(paths);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirmDiscardUnsaved()) {
        e->ignore();
        return;
    }
    e->accept();
}

// ================= .hate project =================

bool MainWindow::saveProject(const QString& path) {
    project::HateFile::Project proj;
    proj.state = state_;
    proj.photos = photos_;
    proj.activePhotoId = activePhotoId_;
    QString error;
    if (!project::HateFile::save(path, proj, &error)) {
        QMessageBox::warning(this, "Ошибка сохранения", error);
        return false;
    }
    projectPath_ = path;
    dirty_ = false;
    updateWindowTitle();
    topInfo_->setText("Проект сохранён: " + path);
    return true;
}

void MainWindow::saveProjectOrPrompt() {
    if (projectPath_.isEmpty()) {
        saveProjectAs();
    } else {
        saveProject(projectPath_);
    }
}

void MainWindow::saveProjectAs() {
    QString path = QFileDialog::getSaveFileName(this, "Сохранить проект", "project.hate", "iHateVisa (*.hate)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".hate", Qt::CaseInsensitive)) path += ".hate";
    saveProject(path);
}

void MainWindow::openProject(const QString& path) {
    project::HateFile::Project proj;
    QString error;
    if (!project::HateFile::open(path, proj, &error)) {
        QMessageBox::warning(this, "Ошибка открытия", error);
        return;
    }
    state_ = proj.state;
    photos_ = proj.photos;
    activePhotoId_ = proj.activePhotoId;
    nextPhotoId_ = 1;
    for (const auto& p : photos_) nextPhotoId_ = std::max(nextPhotoId_, p->id + 1);
    undoStack_.clear();
    projectPath_ = path;
    editor_->setPhoto(activePhoto());
    syncFormatFieldsToUi();
    QSignalBlocker b1(ovalCheck_), b2(cornerCheck_), b3(bwCheck_), b4(brightSlider_), b5(contrastSlider_),
        b6(gammaSlider_), b7(satSlider_), b8(lockVerticalCheck_);
    ovalCheck_->setChecked(state_.ovalOverlay);
    cornerCheck_->setChecked(state_.cornerOverlay);
    bwCheck_->setChecked(state_.blackAndWhite);
    brightSlider_->setValue(state_.brightness);
    contrastSlider_->setValue(state_.contrast);
    gammaSlider_->setValue(state_.gammaPercent);
    satSlider_->setValue(state_.saturationPercent);
    lockVerticalCheck_->setChecked(state_.lockVertical);
    rotationValueLabel_->setText(QString::number(state_.guide.rotation, 'f', 1) + "°");
    refresh();
    refreshThumbs();
    dirty_ = false;
    updateWindowTitle();
    topInfo_->setText("Проект открыт: " + path);
}

void MainWindow::openProjectDialog() {
    if (!confirmDiscardUnsaved()) return;
    QString path = QFileDialog::getOpenFileName(this, "Открыть проект", QString(), "iHateVisa (*.hate)");
    if (!path.isEmpty()) openProject(path);
}

bool MainWindow::confirmDiscardUnsaved() {
    if (!dirty_) return true;
    auto answer = QMessageBox::question(
        this, "Несохранённые изменения", "Сохранить изменения в проекте перед продолжением?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) {
        if (projectPath_.isEmpty()) {
            saveProjectAs();
            return !dirty_;  // saveProjectAs leaves dirty_ true if the user cancelled the file dialog
        }
        return saveProject(projectPath_);
    }
    return true;  // Discard
}

void MainWindow::updateWindowTitle() {
    QString name = projectPath_.isEmpty() ? "Новый проект" : QFileInfo(projectPath_).fileName();
    setWindowTitle(QString("%1%2 — iHateVisa").arg(dirty_ ? "*" : "").arg(name));
}

}  // namespace ihv::app
