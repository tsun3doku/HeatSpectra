#pragma once

#include <QMainWindow>

class VulkanWindow;
class App;
class QCheckBox;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    
    VulkanWindow* getVulkanWindow() { return vulkanWindow; }
    void setApp(App* application) { app = application; }

private slots:
    void onRemeshClicked();
    void onWireframeModeChanged(int index);
    void onIntrinsicToggled(bool checked);
    void onHeatOverlayToggled(bool checked);
    void onIntrinsicNormalsToggled(bool checked);
    void onIntrinsicVertexNormalsToggled(bool checked);
    void onHashGridToggled(bool checked);
    void onSurfelsToggled(bool checked);
    void onVoronoiToggled(bool checked);
    void onPointsToggled(bool checked);
    void onNormalLengthChanged(double value);
    void onPanSensitivityChanged(double value);
    void onToggleHeatClicked();
    void onPauseHeatClicked();
    void onResetHeatClicked();
    void onOpenModel();
    void onExit();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createMenuBar();
    void createDockWidget();
    
    VulkanWindow* vulkanWindow;
    App* app;
    
    class QComboBox* wireframeModeCombo;
    QCheckBox* intrinsicCheck;
    QCheckBox* heatOverlayCheck;
    QCheckBox* intrinsicNormalsCheck;
    QCheckBox* intrinsicVertexNormalsCheck;
    QCheckBox* hashGridCheck;
    QCheckBox* surfelsCheck;
    QCheckBox* voronoiCheck;
    QCheckBox* pointsCheck;
    QDoubleSpinBox* normalLengthSpinBox;
    QPushButton* remeshBtn;
    QSpinBox* remeshIterationsSpinBox;
    QDoubleSpinBox* minAngleSpinBox;
    QDoubleSpinBox* maxEdgeLengthSpinBox;
    QDoubleSpinBox* stepSizeSpinBox;
    QDoubleSpinBox* panSensitivitySpinBox;
    QPushButton* toggleHeatBtn;
    QPushButton* pauseHeatBtn;
    QPushButton* resetHeatBtn;
    
    QAction* remeshOverlayAction;
};
