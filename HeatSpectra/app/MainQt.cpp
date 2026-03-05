#include "MainQt.h"
#include "App.h"
#include "nodegraph/NodeGraphDock.hpp"
#include "runtime/RenderSettingsController.hpp"
#include "util/UiTheme.hpp"
#include "VulkanWindow.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
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
    createDockWidget();
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

    if (controlsPanel) {
        controlsPanel->setMinimumWidth(160);
        controlsPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        mainSplitter->addWidget(controlsPanel);
    }

    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(2, 0);
    mainSplitter->setSizes({320, 880, 220});

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
    settingsController = app ? app->getSettingsController() : nullptr;
    if (nodeGraphDock) {
        boundRuntimeQuery = app ? app->runtimeQuery() : nullptr;
        nodeGraphDock->setRuntimeQuery(boundRuntimeQuery);
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

    remeshOverlayAction = new QAction("&Remesh Overlay", this);
    remeshOverlayAction->setCheckable(true);
    remeshOverlayAction->setShortcut(Qt::Key_C);
    connect(remeshOverlayAction, &QAction::triggered, this, &MainWindow::onIntrinsicToggled);
    viewMenu->addAction(remeshOverlayAction);

    nodeGraphAction = new QAction("Node &Graph", this);
    nodeGraphAction->setCheckable(true);
    nodeGraphAction->setChecked(true);
    connect(nodeGraphAction, &QAction::toggled, this, [this](bool checked) {
        setNodeGraphVisible(checked);
    });
    viewMenu->addAction(nodeGraphAction);
}

void MainWindow::createDockWidget() {
    controlsPanel = new QWidget(this);
    controlsPanel->setObjectName("ControlsPanel");
    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    QLabel* viewLabel = new QLabel("<b>View Options</b>");
    layout->addWidget(viewLabel);

    wireframeModeCombo = new QComboBox();
    wireframeModeCombo->addItem("Normal");
    wireframeModeCombo->addItem("Wireframe");
    wireframeModeCombo->addItem("Shaded Wire");
    wireframeModeCombo->setCurrentIndex(0);
    connect(wireframeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MainWindow::onWireframeModeChanged);
    layout->addWidget(wireframeModeCombo);

    intrinsicCheck = new QCheckBox("Remesh Overlay (Ctrl+C)");
    connect(intrinsicCheck, &QCheckBox::toggled, this, &MainWindow::onIntrinsicToggled);
    layout->addWidget(intrinsicCheck);

    heatOverlayCheck = new QCheckBox("Heat Overlay (Ctrl+V)");
    connect(heatOverlayCheck, &QCheckBox::toggled, this, &MainWindow::onHeatOverlayToggled);
    layout->addWidget(heatOverlayCheck);

    intrinsicNormalsCheck = new QCheckBox("Normal Vectors");
    connect(intrinsicNormalsCheck, &QCheckBox::toggled, this, &MainWindow::onIntrinsicNormalsToggled);
    layout->addWidget(intrinsicNormalsCheck);

    intrinsicVertexNormalsCheck = new QCheckBox("Vertex Normals");
    connect(intrinsicVertexNormalsCheck, &QCheckBox::toggled, this, &MainWindow::onIntrinsicVertexNormalsToggled);
    layout->addWidget(intrinsicVertexNormalsCheck);

    QHBoxLayout* normalLengthLayout = new QHBoxLayout();
    QLabel* normalLengthLabel = new QLabel("  Normal Length:");
    normalLengthLayout->addWidget(normalLengthLabel);

    normalLengthSpinBox = new QDoubleSpinBox();
    normalLengthSpinBox->setMinimum(0.001);
    normalLengthSpinBox->setMaximum(10.0);
    normalLengthSpinBox->setValue(0.05);
    normalLengthSpinBox->setSingleStep(0.01);
    normalLengthSpinBox->setDecimals(3);
    normalLengthSpinBox->setToolTip("Length of normal vectors for visualization");
    connect(normalLengthSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this, &MainWindow::onNormalLengthChanged);
    normalLengthLayout->addWidget(normalLengthSpinBox);
    normalLengthLayout->addStretch();
    layout->addLayout(normalLengthLayout);

    QHBoxLayout* panSensLayout = new QHBoxLayout();
    QLabel* panSensLabel = new QLabel("  Pan Sensitivity:");
    panSensLayout->addWidget(panSensLabel);

    panSensitivitySpinBox = new QDoubleSpinBox();
    panSensitivitySpinBox->setMinimum(0.0);
    panSensitivitySpinBox->setMaximum(10.0);
    panSensitivitySpinBox->setValue(1.0);
    panSensitivitySpinBox->setSingleStep(0.1);
    panSensitivitySpinBox->setDecimals(2);
    panSensitivitySpinBox->setToolTip("Sensitivity of camera panning (Default: 1.0)");
    connect(panSensitivitySpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this, &MainWindow::onPanSensitivityChanged);
    panSensLayout->addWidget(panSensitivitySpinBox);
    panSensLayout->addStretch();
    layout->addLayout(panSensLayout);

    layout->addSpacing(10);

    surfelsCheck = new QCheckBox("Show Surfels");
    connect(surfelsCheck, &QCheckBox::toggled, this, &MainWindow::onSurfelsToggled);
    layout->addWidget(surfelsCheck);

    voronoiCheck = new QCheckBox("View Voronoi");
    connect(voronoiCheck, &QCheckBox::toggled, this, &MainWindow::onVoronoiToggled);
    layout->addWidget(voronoiCheck);

    pointsCheck = new QCheckBox("View Points");
    connect(pointsCheck, &QCheckBox::toggled, this, &MainWindow::onPointsToggled);
    layout->addWidget(pointsCheck);

    contactLinesCheck = new QCheckBox("Show Contact Lines");
    connect(contactLinesCheck, &QCheckBox::toggled, this, &MainWindow::onContactLinesToggled);
    layout->addWidget(contactLinesCheck);

    layout->addStretch();

    controlsPanel->setLayout(layout);
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

    if (!app) {
        return;
    }

    NodeGraphBridge* bridge = app->getNodeGraphBridge();
    if (bridge == boundNodeGraphBridge) {
        return;
    }

    nodeGraphDock->setBridge(bridge);
    boundNodeGraphBridge = bridge;
}

void MainWindow::setNodeGraphVisible(bool visible) {
    if (!mainSplitter || !nodeGraphDock) {
        return;
    }

    QList<int> sizes = mainSplitter->sizes();
    if (sizes.size() < 3) {
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

void MainWindow::onWireframeModeChanged(int index) {
    if (settingsController) {
        settingsController->setWireframeMode(static_cast<app::WireframeMode>(index));
    }
}

void MainWindow::onIntrinsicToggled(bool checked) {
    if (settingsController) {
        settingsController->setIntrinsicOverlayEnabled(checked);
    }

    if (remeshOverlayAction && remeshOverlayAction->isChecked() != checked) {
        remeshOverlayAction->setChecked(checked);
    }
    if (intrinsicCheck && intrinsicCheck->isChecked() != checked) {
        intrinsicCheck->setChecked(checked);
    }
}

void MainWindow::onHeatOverlayToggled(bool checked) {
    if (settingsController) {
        settingsController->setHeatOverlayEnabled(checked);
    }
    if (heatOverlayCheck && heatOverlayCheck->isChecked() != checked) {
        heatOverlayCheck->setChecked(checked);
    }
}

void MainWindow::onIntrinsicNormalsToggled(bool checked) {
    if (settingsController) {
        settingsController->setIntrinsicNormalsEnabled(checked);
    }
    if (intrinsicNormalsCheck && intrinsicNormalsCheck->isChecked() != checked) {
        intrinsicNormalsCheck->setChecked(checked);
    }
}

void MainWindow::onIntrinsicVertexNormalsToggled(bool checked) {
    if (settingsController) {
        settingsController->setIntrinsicVertexNormalsEnabled(checked);
    }
    if (intrinsicVertexNormalsCheck && intrinsicVertexNormalsCheck->isChecked() != checked) {
        intrinsicVertexNormalsCheck->setChecked(checked);
    }
}

void MainWindow::onSurfelsToggled(bool checked) {
    if (settingsController) {
        settingsController->setSurfelsEnabled(checked);
    }
    if (surfelsCheck && surfelsCheck->isChecked() != checked) {
        surfelsCheck->setChecked(checked);
    }
}

void MainWindow::onVoronoiToggled(bool checked) {
    if (settingsController) {
        settingsController->setVoronoiEnabled(checked);
    }
    if (voronoiCheck && voronoiCheck->isChecked() != checked) {
        voronoiCheck->setChecked(checked);
    }
}

void MainWindow::onPointsToggled(bool checked) {
    if (settingsController) {
        settingsController->setPointsEnabled(checked);
    }
    if (pointsCheck && pointsCheck->isChecked() != checked) {
        pointsCheck->setChecked(checked);
    }
}

void MainWindow::onContactLinesToggled(bool checked) {
    if (settingsController) {
        settingsController->setContactLinesEnabled(checked);
    }
    if (contactLinesCheck && contactLinesCheck->isChecked() != checked) {
        contactLinesCheck->setChecked(checked);
    }
}

void MainWindow::onNormalLengthChanged(double value) {
    if (settingsController) {
        settingsController->setIntrinsicNormalLength(static_cast<float>(value));
    }
}

void MainWindow::onPanSensitivityChanged(double value) {
    if (app) {
        app->setPanSensitivity(static_cast<float>(value) * 0.001f);
    }
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
