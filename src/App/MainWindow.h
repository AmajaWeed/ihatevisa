#pragma once

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <vector>

#include "App/EditorWidget.h"
#include "Core/EditorState.h"
#include "Core/PhotoDocument.h"

namespace ihv::app {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    // UI construction
    void buildUi();
    QWidget* buildSizesPanel();
    QWidget* buildReadyPanel();
    QWidget* buildPrintPanel();
    QWidget* buildThumbStrip(QBoxLayout* parentLayout);

    // Actions
    void importFiles(const QStringList& paths);
    void selectPhoto(int id);
    void switchTab(int index);
    void applyFormatChange();
    void syncFormatFieldsToUi();
    void pushUndo();
    void undo();
    void resetGuide();
    void refresh();
    void refreshThumbs();
    void refreshInfo();
    void exportPhoto();
    void exportSheet();
    void printSheet();
    void autoCleanBackground();
    void onWandClick(QPoint sourcePx);
    void undoWand();
    void updatePrintLayoutInfo();

    core::PhotoDocumentPtr activePhoto() const;

    // State
    core::EditorState state_;
    std::vector<core::PhotoDocumentPtr> photos_;
    int activePhotoId_ = -1;
    int nextPhotoId_ = 1;
    std::vector<core::EditorState> undoStack_;  // guide/color/overlay snapshots (not the photo list)
    int currentTab_ = 0;

    // Widgets
    EditorWidget* editor_ = nullptr;
    QLabel* printPreview_ = nullptr;
    QStackedWidget* centerStack_ = nullptr;
    QTabBar* tabBar_ = nullptr;
    QStackedWidget* sidePanels_ = nullptr;
    QListWidget* thumbList_ = nullptr;
    QLabel* topInfo_ = nullptr;

    // Sizes panel
    QComboBox* formatCombo_ = nullptr;
    QLabel* formatDesc_ = nullptr;
    QDoubleSpinBox* widthSpin_ = nullptr;
    QDoubleSpinBox* heightSpin_ = nullptr;
    QDoubleSpinBox* topMarginSpin_ = nullptr;
    QDoubleSpinBox* botMarginSpin_ = nullptr;
    QDoubleSpinBox* headPctSpin_ = nullptr;
    QDoubleSpinBox* headSizeSpin_ = nullptr;
    QCheckBox* lockVerticalCheck_ = nullptr;
    QSlider* rotationSlider_ = nullptr;
    QLabel* rotationValueLabel_ = nullptr;
    QCheckBox* ovalCheck_ = nullptr;
    QCheckBox* cornerCheck_ = nullptr;
    QCheckBox* bwCheck_ = nullptr;

    // Ready panel
    QSlider* acThresholdSlider_ = nullptr;
    QSlider* mwThresholdSlider_ = nullptr;
    QPushButton* wandBtn_ = nullptr;
    QSlider* brightSlider_ = nullptr;
    QSlider* contrastSlider_ = nullptr;
    QSlider* gammaSlider_ = nullptr;
    QSlider* satSlider_ = nullptr;
    std::string ccTab_ = "shadows";
    QSlider *ccCSlider_ = nullptr, *ccMSlider_ = nullptr, *ccYSlider_ = nullptr;
    QLabel *ccCVal_ = nullptr, *ccMVal_ = nullptr, *ccYVal_ = nullptr;

    // Print panel
    QSpinBox* printDpiSpin_ = nullptr;
    QDoubleSpinBox* printMarginSpin_ = nullptr;
    QDoubleSpinBox* printGapSpin_ = nullptr;
    QCheckBox* printBorderCheck_ = nullptr;
    QLabel* printLayoutInfo_ = nullptr;
    std::vector<QPushButton*> countButtons_;
};

}  // namespace ihv::app
