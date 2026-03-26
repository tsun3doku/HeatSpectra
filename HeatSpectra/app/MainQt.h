#pragma once

#include <QMainWindow>

class App;
class QCheckBox;
class QDoubleSpinBox;
class QAction;
class ModelSelection;
class NodeGraphDock;
class NodeGraphBridge;
class RenderSettingsController;
class RuntimeQuery;
class SceneController;
class QCloseEvent;
class QWidget;
class VulkanWindow;
class QSplitter;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void setApp(App* application);

private slots:
    void onWireframeModeChanged(int index);
    void onIntrinsicToggled(bool checked);
    void onHeatOverlayToggled(bool checked);
    void onIntrinsicNormalsToggled(bool checked);
    void onIntrinsicVertexNormalsToggled(bool checked);
    void onSurfelsToggled(bool checked);
    void onVoronoiToggled(bool checked);
    void onPointsToggled(bool checked);
    void onContactLinesToggled(bool checked);
    void onNormalLengthChanged(double value);
    void onPanSensitivityChanged(double value);
    void onOpenModel();
    void onExit();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createMenuBar();
    void createDockWidget();
    void createNodeGraphDock();
    void syncNodeGraphBridge();
    void setNodeGraphVisible(bool visible);
    void raiseNativeSplitterHandles();

    App* app = nullptr;
    
    class QComboBox* wireframeModeCombo = nullptr;
    QCheckBox* intrinsicCheck = nullptr;
    QCheckBox* heatOverlayCheck = nullptr;
    QCheckBox* intrinsicNormalsCheck = nullptr;
    QCheckBox* intrinsicVertexNormalsCheck = nullptr;
    QCheckBox* surfelsCheck = nullptr;
    QCheckBox* voronoiCheck = nullptr;
    QCheckBox* pointsCheck = nullptr;
    QCheckBox* contactLinesCheck = nullptr;
    QDoubleSpinBox* normalLengthSpinBox = nullptr;
    QDoubleSpinBox* panSensitivitySpinBox = nullptr;
    
    QAction* remeshOverlayAction = nullptr;
    QAction* nodeGraphAction = nullptr;
    VulkanWindow* viewportWindow = nullptr;
    QWidget* viewportContainer = nullptr;
    QWidget* controlsPanel = nullptr;
    NodeGraphDock* nodeGraphDock = nullptr;
    NodeGraphBridge* boundNodeGraphBridge = nullptr;
    const SceneController* boundSceneController = nullptr;
    ModelSelection* boundModelSelection = nullptr;
    RenderSettingsController* settingsController = nullptr;
    const RuntimeQuery* boundRuntimeQuery = nullptr;
    QSplitter* mainSplitter = nullptr;
    int lastNodeGraphWidth = 320;
};
