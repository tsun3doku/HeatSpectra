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
#include <QDockWidget>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>

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
}

MainWindow::~MainWindow() {
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
    
    wireframeAction = new QAction("&Wireframe", this);
    wireframeAction->setCheckable(true);
    wireframeAction->setShortcut(Qt::Key_H);
    connect(wireframeAction, &QAction::triggered, this, &MainWindow::onWireframeToggled);
    viewMenu->addAction(wireframeAction);
    
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
    minAngleSpinBox->setValue(35.0);
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
    maxEdgeLengthSpinBox->setMinimum(0.01);
    maxEdgeLengthSpinBox->setMaximum(10.0);
    maxEdgeLengthSpinBox->setValue(0.1);
    maxEdgeLengthSpinBox->setSingleStep(0.01);
    maxEdgeLengthSpinBox->setDecimals(3);
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
    
    wireframeCheck = new QCheckBox("Wireframe (Ctrl+H)");
    connect(wireframeCheck, &QCheckBox::toggled, this, &MainWindow::onWireframeToggled);
    layout->addWidget(wireframeCheck);
    
    intrinsicCheck = new QCheckBox("Remesh Overlay (Ctrl+C)");
    connect(intrinsicCheck, &QCheckBox::toggled, this, &MainWindow::onIntrinsicToggled);
    layout->addWidget(intrinsicCheck);
    
    layout->addSpacing(10);
    
    // Heat simulation
    QLabel* heatLabel = new QLabel("<b>Heat Simulation</b>");
    layout->addWidget(heatLabel);
    
    toggleHeatBtn = new QPushButton("Start Simulation (Space)");
    connect(toggleHeatBtn, &QPushButton::clicked, this, &MainWindow::onToggleHeatClicked);
    layout->addWidget(toggleHeatBtn);
    
    pauseHeatBtn = new QPushButton("Pause (P)");
    connect(pauseHeatBtn, &QPushButton::clicked, this, &MainWindow::onPauseHeatClicked);
    layout->addWidget(pauseHeatBtn);
    
    resetHeatBtn = new QPushButton("Reset (Ctrl+R)");
    connect(resetHeatBtn, &QPushButton::clicked, this, &MainWindow::onResetHeatClicked);
    layout->addWidget(resetHeatBtn);
    
    layout->addStretch();
    
    dockWidget->setLayout(layout);
    dock->setWidget(dockWidget);
    addDockWidget(Qt::RightDockWidgetArea, dock);
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

void MainWindow::onWireframeToggled(bool checked) {
    if (app) {
        app->wireframeEnabled = checked;
    }
    // Sync menu action and checkbox
    if (wireframeAction && wireframeAction->isChecked() != checked) {
        wireframeAction->setChecked(checked);
    }
    if (wireframeCheck && wireframeCheck->isChecked() != checked) {
        wireframeCheck->setChecked(checked);
    }
}

void MainWindow::onIntrinsicToggled(bool checked) {
    if (app) {
        app->commonSubdivisionEnabled = checked;
    }
    // Sync menu action and checkbox
    if (remeshOverlayAction && remeshOverlayAction->isChecked() != checked) {
        remeshOverlayAction->setChecked(checked);
    }
    if (intrinsicCheck && intrinsicCheck->isChecked() != checked) {
        intrinsicCheck->setChecked(checked);
    }
}

void MainWindow::onToggleHeatClicked() {
    if (app) {
        app->toggleHeatSystem();
        
        if (app->isHeatSystemActive()) {
            toggleHeatBtn->setText("Stop Simulation (Space)");
        } else {
            toggleHeatBtn->setText("Start Simulation (Space)");
        }
    }
}

void MainWindow::onPauseHeatClicked() {
    if (app) {
        app->pauseHeatSystem();
        toggleHeatBtn->setText("Start Simulation (Space)");
    }
}

void MainWindow::onResetHeatClicked() {
    if (app) {
        app->resetHeatSystem();
        
        if (app->isHeatSystemActive()) {
            toggleHeatBtn->setText("Stop Simulation (Space)");
        } else {
            toggleHeatBtn->setText("Start Simulation (Space)");
        }
    }
}

void MainWindow::onOpenModel() {
    QString filename = QFileDialog::getOpenFileName(this, "Open Model", "models", "3D Models (*.obj *.ply);;OBJ Files (*.obj);;PLY Files (*.ply)");
    if (!filename.isEmpty() && app) {
        try {
            std::string modelPath = filename.toStdString();
            app->loadModel(modelPath);
            
            toggleHeatBtn->setText("Start Simulation (Space)");
            
            QMessageBox::information(this, "Success", QString("Model loaded: %1").arg(filename));
        }
        catch (const std::exception& e) {
            QMessageBox::critical(this, "Error", QString("Failed to load model: %1").arg(e.what()));
        }
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
    
    // Get the Vulkan window
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
