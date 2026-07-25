#pragma once

#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickView>

#include "GraphThread.hpp"
#include "RuntimeNotifier.hpp"
#include "UiModel.hpp"
#include "project/ProjectController.hpp"
#include "runtime/RuntimeSystems.hpp"

#include <memory>

class GraphHost;
class ViewportItem;

class App final {
public:
    App(int& argc, char** argv);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int run();

private:
    int initializePresentation();
    void connectUi(ViewportItem& viewport, GraphHost& graphHost);
    void connectRuntime(GraphHost& graphHost);
    void connectGraph(ViewportItem& viewport, GraphHost& graphHost);

    QGuiApplication application;
    UiModel uiModel;
    std::unique_ptr<ProjectController> projectController;
    GraphThread graphThread;
    RuntimeNotifier runtimeNotifier;
    RuntimeSystems runtimeSystems;
    QQuickView window;
};
