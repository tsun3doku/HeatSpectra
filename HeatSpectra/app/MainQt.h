#pragma once

#include <QMainWindow>

class App;
class QAction;
class ModelSelection;
class NodeGraphDock;
class NodeGraphBridge;
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

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createMenuBar();
    void createNodeGraphDock();
    void syncNodeGraphBridge();
    void setNodeGraphVisible(bool visible);
    void raiseNativeSplitterHandles();
    void onOpenModel();
    void onExit();

    App* app = nullptr;

    QAction* nodeGraphAction = nullptr;
    VulkanWindow* viewportWindow = nullptr;
    QWidget* viewportContainer = nullptr;
    NodeGraphDock* nodeGraphDock = nullptr;
    NodeGraphBridge* boundNodeGraphBridge = nullptr;
    const SceneController* boundSceneController = nullptr;
    ModelSelection* boundModelSelection = nullptr;
    const RuntimeQuery* boundRuntimeQuery = nullptr;
    QSplitter* mainSplitter = nullptr;
    int lastNodeGraphWidth = 320;
};
