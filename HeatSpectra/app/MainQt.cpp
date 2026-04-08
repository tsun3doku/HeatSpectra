#include "MainQt.h"
#include "App.h"
#include "nodegraph/NodeGraphDock.hpp"
#include "util/UiTheme.hpp"
#include "VulkanWindow.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSizePolicy>
#include <QSplitter>
#include <QSplitterHandle>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("HeatSpectra");
    resize(1280, 720);
    setStyleSheet(QString::fromStdString(ui::splitterStyleSheet()));

    createMenuBar();
    createNodeGraphDock();

    QWidget* centralHost = new QWidget(this);
    QHBoxLayout* hostLayout = new QHBoxLayout(centralHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    mainSplitter = new QSplitter(Qt::Horizontal, centralHost);
    ui::configureSplitter(*mainSplitter);
    mainSplitter->setOpaqueResize(true);
    mainSplitter->setChildrenCollapsible(false);
    mainSplitter->setAttribute(Qt::WA_NativeWindow, true);
    hostLayout->addWidget(mainSplitter);

    if (nodeGraphDock) {
        nodeGraphDock->setMinimumWidth(160);
        nodeGraphDock->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        mainSplitter->addWidget(nodeGraphDock);
    }

    QWidget* viewportHost = new QWidget(mainSplitter);
    QVBoxLayout* viewportLayout = new QVBoxLayout(viewportHost);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->setSpacing(0);
    viewportWindow = new VulkanWindow();
    viewportContainer = QWidget::createWindowContainer(viewportWindow, viewportHost);
    viewportContainer->setFocusPolicy(Qt::StrongFocus);
    viewportContainer->setMinimumSize(320, 240);
    viewportLayout->addWidget(viewportContainer);
    viewportHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainSplitter->addWidget(viewportHost);

    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({320, 1100});

    setCentralWidget(centralHost);
    connect(mainSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        raiseNativeSplitterHandles();
    });

    QTimer* uiSyncTimer = new QTimer(this);
    uiSyncTimer->setInterval(100);
    connect(uiSyncTimer, &QTimer::timeout, this, &MainWindow::syncNodeGraphBridge);
    uiSyncTimer->start();
    
    QTimer::singleShot(0, this, [this]() {
        raiseNativeSplitterHandles();        
#ifdef Q_OS_WIN
        if (viewportWindow) {
            const HWND vulkanHwnd = reinterpret_cast<HWND>(viewportWindow->winId());
            if (vulkanHwnd) {
                LONG_PTR exStyle = GetWindowLongPtr(vulkanHwnd, GWL_EXSTYLE);
                SetWindowLongPtr(vulkanHwnd, GWL_EXSTYLE, exStyle | WS_EX_NOREDIRECTIONBITMAP);
            }
        }
#endif
    });
}

MainWindow::~MainWindow() {
}

void MainWindow::setApp(App* application) {
    app = application;
    if (viewportWindow) {
        viewportWindow->setApp(app);
    }
    if (nodeGraphDock) {
        boundRuntimeQuery = app ? app->runtimeQuery() : nullptr;
        nodeGraphDock->setRuntimeQuery(boundRuntimeQuery);
        boundSceneController = app ? app->getSceneController() : nullptr;
        nodeGraphDock->setSceneController(boundSceneController);
        boundModelSelection = app ? app->getModelSelection() : nullptr;
        nodeGraphDock->setModelSelection(boundModelSelection);
    }
    syncNodeGraphBridge();
}

void MainWindow::raiseNativeSplitterHandles() {
#ifdef Q_OS_WIN
    if (!mainSplitter) {
        return;
    }

    for (int i = 1; i < mainSplitter->count(); ++i) {
        QSplitterHandle* handle = mainSplitter->handle(i);
        if (!handle) {
            continue;
        }

        handle->setAttribute(Qt::WA_NativeWindow, true);
        const WId handleId = handle->winId();
        if (!handleId) {
            continue;
        }

        SetWindowPos(
            reinterpret_cast<HWND>(handleId),
            HWND_TOP,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#endif
}

void MainWindow::createMenuBar() {
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    QMenu* fileMenu = menuBar->addMenu("&File");

    QAction* openAction = new QAction("&Open Model...", this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenModel);
    fileMenu->addAction(openAction);

    fileMenu->addSeparator();

    QAction* exitAction = new QAction("E&xit", this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &MainWindow::onExit);
    fileMenu->addAction(exitAction);

    QMenu* viewMenu = menuBar->addMenu("&View");

    nodeGraphAction = new QAction("Node &Graph", this);
    nodeGraphAction->setCheckable(true);
    nodeGraphAction->setChecked(true);
    connect(nodeGraphAction, &QAction::toggled, this, [this](bool checked) {
        setNodeGraphVisible(checked);
    });
    viewMenu->addAction(nodeGraphAction);
}

void MainWindow::createNodeGraphDock() {
    nodeGraphDock = new NodeGraphDock(this);
    nodeGraphDock->setObjectName("NodeGraphDock");
    nodeGraphDock->setRuntimeQuery(app ? app->runtimeQuery() : nullptr);

    syncNodeGraphBridge();
}

void MainWindow::syncNodeGraphBridge() {
    if (app) {
        const auto errors = app->consumeSimulationErrors();
        for (const auto& error : errors) {
            QMessageBox::critical(
                this,
                "Simulation Validation Failed",
                QString::fromStdString(error.message));
        }
    }

    if (!nodeGraphDock) {
        return;
    }

    const RuntimeQuery* runtimeQuery = app ? app->runtimeQuery() : nullptr;
    if (runtimeQuery != boundRuntimeQuery) {
        nodeGraphDock->setRuntimeQuery(runtimeQuery);
        boundRuntimeQuery = runtimeQuery;
    }

    const SceneController* sceneController = app ? app->getSceneController() : nullptr;
    if (sceneController != boundSceneController) {
        nodeGraphDock->setSceneController(sceneController);
        boundSceneController = sceneController;
    }

    ModelSelection* modelSelection = app ? app->getModelSelection() : nullptr;
    if (modelSelection != boundModelSelection) {
        nodeGraphDock->setModelSelection(modelSelection);
        boundModelSelection = modelSelection;
    }

    if (!app) {
        nodeGraphDock->syncSelection();
        return;
    }

    NodeGraphBridge* bridge = app->getNodeGraphBridge();
    if (bridge == boundNodeGraphBridge) {
        nodeGraphDock->syncSelection();
        return;
    }

    nodeGraphDock->setBridge(bridge);
    boundNodeGraphBridge = bridge;
    nodeGraphDock->syncSelection();
}

void MainWindow::setNodeGraphVisible(bool visible) {
    if (!mainSplitter || !nodeGraphDock) {
        return;
    }

    QList<int> sizes = mainSplitter->sizes();
    if (sizes.size() < 2) {
        return;
    }

    if (visible) {
        nodeGraphDock->show();
        if (sizes[0] <= 1) {
            sizes[0] = std::max(nodeGraphDock->minimumWidth(), lastNodeGraphWidth);
            mainSplitter->setSizes(sizes);
        }
        return;
    }

    lastNodeGraphWidth = std::max(nodeGraphDock->minimumWidth(), sizes[0]);
    nodeGraphDock->hide();
}

void MainWindow::onOpenModel() {
    if (app) {
        app->setRenderPaused(true);
    }

    QString filename = QFileDialog::getOpenFileName(this, "Open Model", "models", "3D Models (*.obj *.ply);;OBJ Files (*.obj);;PLY Files (*.ply)");

    if (!filename.isEmpty() && app) {
        std::string modelPath = filename.toStdString();
        const uint32_t modelId = app->loadModel(modelPath);
        if (modelId != 0) {
            QMessageBox::information(this, "Success", QString("Model loaded: %1").arg(filename));
        } else {
            QMessageBox::critical(this, "Error", QString("Failed to load model: %1").arg(filename));
        }
    }

    if (app) {
        app->setRenderPaused(false);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (app) {
        app->shutdown();
    }

    event->accept();
}

void MainWindow::onExit() {
    close();
}

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);

    MainWindow mainWindow;
    App app;
    mainWindow.setApp(&app);
    mainWindow.show();

    return qapp.exec();
}
