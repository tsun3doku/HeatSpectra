#include "MainQt.h"
#include "VulkanWindow.h"
#include "App.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDockWidget>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QTimer>

#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("HeatSpectra");
    resize(1280, 720);
    
    // Create Vulkan window as central widget
    vulkanWindow = new VulkanWindow();
    QWidget* container = QWidget::createWindowContainer(vulkanWindow, this);
    container->setMinimumSize(640, 480);
    setCentralWidget(container);
    
    createMenuBar();
    createDockWidget();

    QTimer* heatButtonSyncTimer = new QTimer(this);
    heatButtonSyncTimer->setInterval(100);
    connect(heatButtonSyncTimer, &QTimer::timeout, this, &MainWindow::syncHeatSystemButtons);
    heatButtonSyncTimer->start();
}

MainWindow::~MainWindow() {
}

void MainWindow::setApp(App* application) {
    app = application;
    syncHeatSystemButtons();
}

void MainWindow::createMenuBar() {
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);
    
    // File menu
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
    
    // View menu
    QMenu* viewMenu = menuBar->addMenu("&View");
    
    remeshOverlayAction = new QAction("&Remesh Overlay", this);
    remeshOverlayAction->setCheckable(true);
    remeshOverlayAction->setShortcut(Qt::Key_C);
    connect(remeshOverlayAction, &QAction::triggered, this, &MainWindow::onIntrinsicToggled);
    viewMenu->addAction(remeshOverlayAction);
}

void MainWindow::createDockWidget() {
    QDockWidget* dock = new QDockWidget("Controls", this);
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    
    QWidget* dockWidget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout();
    
    // Remesh section
    QLabel* remeshLabel = new QLabel("<b>Mesh Operations</b>");
    layout->addWidget(remeshLabel);
    
    remeshBtn = new QPushButton("Remesh");
    connect(remeshBtn, &QPushButton::clicked, this, &MainWindow::onRemeshClicked);
    layout->addWidget(remeshBtn);
    
    QHBoxLayout* iterationLayout = new QHBoxLayout();
    QLabel* iterationLabel = new QLabel("Iterations:");
    iterationLayout->addWidget(iterationLabel);
    
    remeshIterationsSpinBox = new QSpinBox();
    remeshIterationsSpinBox->setMinimum(1);
    remeshIterationsSpinBox->setMaximum(1000);
    remeshIterationsSpinBox->setValue(1);
    remeshIterationsSpinBox->setToolTip("Number of remeshing iterations");
    iterationLayout->addWidget(remeshIterationsSpinBox);
    iterationLayout->addStretch();
    
    layout->addLayout(iterationLayout);
    
    // Min angle control
    QHBoxLayout* minAngleLayout = new QHBoxLayout();
    QLabel* minAngleLabel = new QLabel("Min Angle (°):");
    minAngleLayout->addWidget(minAngleLabel);
    
    minAngleSpinBox = new QDoubleSpinBox();
    minAngleSpinBox->setMinimum(0.0);
    minAngleSpinBox->setMaximum(60.0);
    minAngleSpinBox->setValue(30.0);
    minAngleSpinBox->setSingleStep(1.0);
    minAngleSpinBox->setDecimals(1);
    minAngleSpinBox->setToolTip("Minimum angle threshold for triangle quality (degrees)");
    minAngleLayout->addWidget(minAngleSpinBox);
    minAngleLayout->addStretch();
    
    layout->addLayout(minAngleLayout);
    
    // Max edge length control
    QHBoxLayout* maxEdgeLengthLayout = new QHBoxLayout();
    QLabel* maxEdgeLengthLabel = new QLabel("Max Edge Length:");
    maxEdgeLengthLayout->addWidget(maxEdgeLengthLabel);
    
    maxEdgeLengthSpinBox = new QDoubleSpinBox();
    maxEdgeLengthSpinBox->setMinimum(0.001);
    maxEdgeLengthSpinBox->setMaximum(10.0);
    maxEdgeLengthSpinBox->setValue(0.1);
    maxEdgeLengthSpinBox->setSingleStep(0.01);
    maxEdgeLengthSpinBox->setDecimals(4);
    maxEdgeLengthSpinBox->setToolTip("Maximum edge length for mesh refinement");
    maxEdgeLengthLayout->addWidget(maxEdgeLengthSpinBox);
    maxEdgeLengthLayout->addStretch();
    
    layout->addLayout(maxEdgeLengthLayout);
    
    // Step size control
    QHBoxLayout* stepSizeLayout = new QHBoxLayout();
    QLabel* stepSizeLabel = new QLabel("Step Size:");
    stepSizeLayout->addWidget(stepSizeLabel);
    
    stepSizeSpinBox = new QDoubleSpinBox();
    stepSizeSpinBox->setMinimum(0.01);
    stepSizeSpinBox->setMaximum(1.0);
    stepSizeSpinBox->setValue(0.25);
    stepSizeSpinBox->setSingleStep(0.05);
    stepSizeSpinBox->setDecimals(2);
    stepSizeSpinBox->setToolTip("Step size for vertex repositioning (0-1). Lower values prevent intersections.");
    stepSizeLayout->addWidget(stepSizeSpinBox);
    stepSizeLayout->addStretch();
    
    layout->addLayout(stepSizeLayout);
    
    layout->addSpacing(10);
    
    // View options
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

    // Normal length control
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

    // Pan Sensitivity control
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
      
    // Heat simulation
    QLabel* heatLabel = new QLabel("<b>Heat Simulation</b>");
    layout->addWidget(heatLabel);
    
    toggleHeatBtn = new QPushButton("Start Simulation (Space)");
    connect(toggleHeatBtn, &QPushButton::clicked, this, &MainWindow::onToggleHeatClicked);
    layout->addWidget(toggleHeatBtn);
    
    pauseHeatBtn = new QPushButton("Pause (P)");
    connect(pauseHeatBtn, &QPushButton::clicked, this, &MainWindow::onPauseHeatClicked);
    layout->addWidget(pauseHeatBtn);
    
    resetHeatBtn = new QPushButton("Reset (R)");
    connect(resetHeatBtn, &QPushButton::clicked, this, &MainWindow::onResetHeatClicked);
    layout->addWidget(resetHeatBtn);
    
    layout->addStretch();
    
    dockWidget->setLayout(layout);
    dock->setWidget(dockWidget);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    syncHeatSystemButtons();
}

void MainWindow::syncHeatSystemButtons() {
    if (!toggleHeatBtn || !pauseHeatBtn || !resetHeatBtn) {
        return;
    }

    const bool hasApp = (app != nullptr);
    const bool isActive = hasApp && app->isHeatSystemActive();
    const bool isPaused = hasApp && app->isHeatSystemPaused();

    if (!hasApp || !isActive) {
        toggleHeatBtn->setText("Start Simulation (Space)");
        pauseHeatBtn->setText("Pause (P)");
        pauseHeatBtn->setEnabled(false);
        resetHeatBtn->setEnabled(hasApp);
        return;
    }

    if (isPaused) {
        toggleHeatBtn->setText("Resume Simulation (Space)");
        pauseHeatBtn->setText("Paused (P)");
        pauseHeatBtn->setEnabled(false);
    } else {
        toggleHeatBtn->setText("Stop Simulation (Space)");
        pauseHeatBtn->setText("Pause (P)");
        pauseHeatBtn->setEnabled(true);
    }

    resetHeatBtn->setEnabled(true);
}

void MainWindow::onRemeshClicked() {
    if (app) {
        int iterations = remeshIterationsSpinBox->value();
        double minAngleDegrees = minAngleSpinBox->value();
        double maxEdgeLength = maxEdgeLengthSpinBox->value();
        double stepSize = stepSizeSpinBox->value();
        app->performRemeshing(iterations, minAngleDegrees, maxEdgeLength, stepSize);
    }
}

void MainWindow::onWireframeModeChanged(int index) {
    if (app) {
        app->wireframeMode = static_cast<App::WireframeMode>(index);
    }
}

void MainWindow::onIntrinsicToggled(bool checked) {
    if (app) {
        app->intrinsicOverlayEnabled = checked;
    }
    // Sync menu action and checkbox
    if (remeshOverlayAction && remeshOverlayAction->isChecked() != checked) {
        remeshOverlayAction->setChecked(checked);
    }
    if (intrinsicCheck && intrinsicCheck->isChecked() != checked) {
        intrinsicCheck->setChecked(checked);
    }
}

void MainWindow::onHeatOverlayToggled(bool checked) {
    if (app) {
        app->heatOverlayEnabled = checked;
    }
    if (heatOverlayCheck && heatOverlayCheck->isChecked() != checked) {
        heatOverlayCheck->setChecked(checked);
    }
}

void MainWindow::onIntrinsicNormalsToggled(bool checked) {
    if (app) {
        app->intrinsicNormalsEnabled = checked;
    }
    if (intrinsicNormalsCheck && intrinsicNormalsCheck->isChecked() != checked) {
        intrinsicNormalsCheck->setChecked(checked);
    }
}

void MainWindow::onIntrinsicVertexNormalsToggled(bool checked) {
    if (app) {
        app->intrinsicVertexNormalsEnabled = checked;
    }
    if (intrinsicVertexNormalsCheck && intrinsicVertexNormalsCheck->isChecked() != checked) {
        intrinsicVertexNormalsCheck->setChecked(checked);
    }
}

void MainWindow::onSurfelsToggled(bool checked) {
    if (app) {
        app->surfelsEnabled = checked;
    }
    if (surfelsCheck && surfelsCheck->isChecked() != checked) {
        surfelsCheck->setChecked(checked);
    }
}

void MainWindow::onVoronoiToggled(bool checked) {
    if (app) {
        app->voronoiEnabled = checked;
    }
    if (voronoiCheck && voronoiCheck->isChecked() != checked) {
        voronoiCheck->setChecked(checked);
    }
}

void MainWindow::onPointsToggled(bool checked) {
    if (app) {
        app->pointsEnabled = checked;
    }
    if (pointsCheck && pointsCheck->isChecked() != checked) {
        pointsCheck->setChecked(checked);
    }
}

void MainWindow::onContactLinesToggled(bool checked) {
    if (app) {
        app->contactLinesEnabled = checked;
    }
    if (contactLinesCheck && contactLinesCheck->isChecked() != checked) {
        contactLinesCheck->setChecked(checked);
    }
}

void MainWindow::onNormalLengthChanged(double value) {
    if (app) {
        app->intrinsicNormalLength = static_cast<float>(value);
    }
}

void MainWindow::onPanSensitivityChanged(double value) {
    if (app) {
        app->setPanSensitivity(static_cast<float>(value) * 0.001f);
    }
}

void MainWindow::onToggleHeatClicked() {
    if (app) {
        app->toggleHeatSystem();
        syncHeatSystemButtons();
    }
}

void MainWindow::onPauseHeatClicked() {
    if (app) {
        app->pauseHeatSystem();
        syncHeatSystemButtons();
    }
}

void MainWindow::onResetHeatClicked() {
    if (app) {
        app->resetHeatSystem();
        syncHeatSystemButtons();
    }
}

void MainWindow::onOpenModel() {
    if (app) {
        app->setRenderPaused(true);
    }

    QString filename = QFileDialog::getOpenFileName(this, "Open Model", "models", "3D Models (*.obj *.ply);;OBJ Files (*.obj);;PLY Files (*.ply)");

    if (!filename.isEmpty() && app) {
        try {
            std::string modelPath = filename.toStdString();
            app->loadModel(modelPath);
            syncHeatSystemButtons();
            
            QMessageBox::information(this, "Success", QString("Model loaded: %1").arg(filename));
        }
        catch (const std::exception& e) {
            QMessageBox::critical(this, "Error", QString("Failed to load model: %1").arg(e.what()));
        }
    }

    if (app) {
        app->setRenderPaused(false);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Signal vulkan window to close
    if (vulkanWindow) {
        vulkanWindow->setShouldClose(true);
    }
    
    QApplication::quit(); 
    event->accept();
}

void MainWindow::onExit() {
    close();
}

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    
    // Create main window with Vulkan rendering
    MainWindow mainWindow;
    mainWindow.show();
    
    VulkanWindow* vulkanWindow = mainWindow.getVulkanWindow();    
    
    // Wait for window to be sized
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    qapp.processEvents();
    
    // Create and run App in a separate thread
    App app;
    mainWindow.setApp(&app);
    
    std::thread appThread([&app, vulkanWindow]() {
        try {
            app.run(vulkanWindow);
        }
        catch (const std::exception& e) {
            std::cerr << "App error: " << e.what() << std::endl;
        }
    });
    
    // Run Qt event loop
    int result = qapp.exec();
    
    // Wait for app thread to finish
    if (appThread.joinable()) {
        appThread.join();
    }
    
    return result;
}
