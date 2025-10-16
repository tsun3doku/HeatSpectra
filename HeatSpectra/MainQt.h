#pragma once

#include <QMainWindow>

class VulkanWindow;
class App;
class QCheckBox;
class QPushButton;
class QSpinBox;
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
    void onWireframeToggled(bool checked);
    void onIntrinsicToggled(bool checked);
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
    
    QCheckBox* wireframeCheck;
    QCheckBox* intrinsicCheck;
    QPushButton* remeshBtn;
    QSpinBox* remeshIterationsSpinBox;
    QPushButton* toggleHeatBtn;
    QPushButton* pauseHeatBtn;
    QPushButton* resetHeatBtn;
    
    QAction* wireframeAction;
    QAction* remeshOverlayAction;
};
