#include "NodePanel.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeGraphUtils.hpp"

#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphDebugCache.hpp"
#include "NodeContactPanel.hpp"
#include "NodeHeatSourcePanel.hpp"
#include "NodeGroupPanel.hpp"
#include "NodeHeatSolverPanel.hpp"
#include "NodeModelPanel.hpp"
#include "NodeTransformPanel.hpp"
#include "NodeRemeshPanel.hpp"
#include "NodeVoronoiPanel.hpp"
#include "NodeGraphWidgetStyle.hpp"
#include "runtime/RuntimeInterfaces.hpp"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <sstream>
#include <string>
#include <utility>

namespace {

QString nodeTypeDisplayName(const NodeTypeId& typeId) {
    const NodeTypeDefinition* definition = NodeGraphRegistry::findNodeById(typeId);
    if (definition) {
        return QString::fromStdString(definition->displayName);
    }

    return QString::fromStdString(typeId.empty() ? std::string("Unknown") : typeId);
}

QString nodeTypeDescription(const NodeTypeId& typeId) {
    if (typeId == nodegraphtypes::Model) {
        return "Choose a 3D model";
    }
    if (typeId == nodegraphtypes::Transform) {
        return "Adjust placement, orientation and scale of a 3D model";
    }
    if (typeId == nodegraphtypes::Group) {
        return "Target source groups and write grouped mesh selections";
    }
    if (typeId == nodegraphtypes::Remesh) {
        return "Intrisically remesh an underlying 3D model preserving its shape";
    }
    if (typeId == nodegraphtypes::Voronoi) {
        return "Generate volumetric voronoi domain";
    }
    if (typeId == nodegraphtypes::Contact) {
        return "Assign a contact pairing between 3D models";
    }
    if (typeId == nodegraphtypes::HeatSource) {
        return "Assign source temperatures for models that emit heat";
    }
    if (typeId == nodegraphtypes::HeatSolve) {
        return "Simulate the transient transfer of heat between 3D geometry";
    }
    if (typeId == nodegraphtypes::HeatReceiver) {
        return "Assign a 3D model as a heat recipient";
    }
    return "Select a node in the graph to inspect its parameters";
}

std::string formatLineagePath(const std::vector<NodeGraphNodeId>& lineageNodeIds) {
    if (lineageNodeIds.empty()) {
        return std::string("none");
    }

    std::ostringstream stream;
    for (std::size_t index = 0; index < lineageNodeIds.size(); ++index) {
        if (index > 0) {
            stream << " -> ";
        }
        stream << lineageNodeIds[index].value;
    }
    return stream.str();
}

void appendSocketDebugText(std::ostringstream& stream, const NodeGraphRuntimeSocketDebugInfo& socketDebug) {
    stream << "  [" << (socketDebug.direction == NodeGraphSocketDirection::Input ? "In" : "Out")
           << "] " << socketDebug.socketName << " (#" << socketDebug.socketId.value << ")\n";
    if (!socketDebug.hasValue) {
        stream << "    value: <none>\n";
        return;
    }

    stream << "    data type: " << socketDebug.dataType << "\n";
    stream << "    lineage: " << formatLineagePath(socketDebug.lineageNodeIds) << "\n";

    if (socketDebug.metadata.empty()) {
        stream << "    metadata: <none>\n";
        return;
    }

    std::vector<std::pair<std::string, std::string>> metadataEntries(
        socketDebug.metadata.begin(),
        socketDebug.metadata.end());
    std::sort(
        metadataEntries.begin(),
        metadataEntries.end(),
        [](const std::pair<std::string, std::string>& lhs, const std::pair<std::string, std::string>& rhs) {
            return lhs.first < rhs.first;
        });

    stream << "    metadata:\n";
    for (const auto& metadataEntry : metadataEntries) {
        stream << "      " << metadataEntry.first << " = " << metadataEntry.second << "\n";
    }
}

}

NodePanel::NodePanel(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(nodegraphwidgets::panelMinimumHeight);

    buildUi();
    nodegraphwidgets::applyNodePanelStyle(this);
}

void NodePanel::bind(NodeGraphBridge* nodeGraphBridgePtr, const RuntimeQuery* runtimeQueryPtr) {
    nodeGraphBridge = nodeGraphBridgePtr;
    runtimeQuery = runtimeQueryPtr;
    groupPanel->bind(nodeGraphBridgePtr);
    modelPanel->bind(nodeGraphBridgePtr);
    transformPanel->bind(nodeGraphBridgePtr);
    remeshPanel->bind(nodeGraphBridgePtr);
    voronoiPanel->bind(nodeGraphBridgePtr);
    contactPanel->bind(nodeGraphBridgePtr);
    heatSourcePanel->bind(nodeGraphBridgePtr);
    heatSolverPanel->bind(nodeGraphBridgePtr, runtimeQueryPtr);

    if (isVisible() && currentNodeId.isValid()) {
        setNode(currentNodeId);
    }
}

bool NodePanel::setNode(NodeGraphNodeId nodeId) {
    if (!nodeGraphBridge) {
        return false;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(nodeId, node)) {
        return false;
    }

    currentNodeId = nodeId;
    currentNodeTypeId = getNodeTypeId(node.typeId);

    titleLabel->setText(nodeTypeDisplayName(currentNodeTypeId));
    subtitleLabel->setText(nodeTypeDescription(currentNodeTypeId));
    statusLabel->clear();

    if (currentNodeTypeId == nodegraphtypes::Model) {
        pageStack->setCurrentWidget(modelPage);
        modelPanel->setNode(currentNodeId);
        heatSolverPanel->stopStatusTimer();
    } else if (currentNodeTypeId == nodegraphtypes::Transform) {
        pageStack->setCurrentWidget(transformPage);
        transformPanel->setNode(currentNodeId);
        heatSolverPanel->stopStatusTimer();
    } else if (currentNodeTypeId == nodegraphtypes::Group) {
        pageStack->setCurrentWidget(groupPage);
        groupPanel->setNode(currentNodeId);
        heatSolverPanel->stopStatusTimer();
    } else if (currentNodeTypeId == nodegraphtypes::Remesh) {
        pageStack->setCurrentWidget(remeshPage);
        remeshPanel->setNode(currentNodeId);
        heatSolverPanel->stopStatusTimer();
    } else if (currentNodeTypeId == nodegraphtypes::Voronoi) {
        pageStack->setCurrentWidget(voronoiPage);
        voronoiPanel->setNode(currentNodeId);
        heatSolverPanel->stopStatusTimer();
    } else if (currentNodeTypeId == nodegraphtypes::Contact) {
        pageStack->setCurrentWidget(contactPage);
        contactPanel->setNode(currentNodeId);
        heatSolverPanel->stopStatusTimer();
    } else if (currentNodeTypeId == nodegraphtypes::HeatSource) {
        pageStack->setCurrentWidget(heatSourcePage);
        heatSourcePanel->setNode(currentNodeId);
        heatSolverPanel->stopStatusTimer();
    } else if (currentNodeTypeId == nodegraphtypes::HeatSolve) {
        pageStack->setCurrentWidget(heatPage);
        heatSolverPanel->setNode(currentNodeId);
        if (runtimeQuery) {
            heatSolverPanel->startStatusTimer();
        } else {
            heatSolverPanel->stopStatusTimer();
        }
    } else {
        pageStack->setCurrentWidget(genericPage);
        heatSolverPanel->stopStatusTimer();
    }

    refreshRuntimeDebugViews();

    return true;
}

void NodePanel::hideEvent(QHideEvent* event) {
    heatSolverPanel->stopStatusTimer();

    QWidget::hideEvent(event);
}

void NodePanel::buildUi() {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(scrollArea, 1);

    QWidget* inspectorContent = new QWidget(scrollArea);
    inspectorContent->setObjectName("NodePanelContent");
    QVBoxLayout* contentLayout = new QVBoxLayout(inspectorContent);
    contentLayout->setContentsMargins(
        nodegraphwidgets::panelContentMarginLeft,
        nodegraphwidgets::panelContentMarginTop,
        nodegraphwidgets::panelContentMarginRight,
        nodegraphwidgets::panelContentMarginBottom);
    contentLayout->setSpacing(nodegraphwidgets::panelContentSpacing);
    scrollArea->setWidget(inspectorContent);

    titleLabel = new QLabel(inspectorContent);
    titleLabel->setObjectName("NodePanelTitle");
    titleLabel->setText("No Node Selected");
    contentLayout->addWidget(titleLabel);

    subtitleLabel = new QLabel(inspectorContent);
    subtitleLabel->setObjectName("NodePanelSubtitle");
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setText("Select a node in the graph to inspect parameters and dataflow");
    contentLayout->addWidget(subtitleLabel);

    pageStack = new QStackedWidget(inspectorContent);

    genericPage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(genericPage);
        layout->setContentsMargins(0, 0, 0, 0);
        QLabel* msg = new QLabel("This node has no editable actions yet", genericPage);
        msg->setWordWrap(true);
        layout->addWidget(msg);
        layout->addStretch();
    }
    pageStack->addWidget(genericPage);

    {
        modelPanel = new NodeModelPanel(inspectorContent);
        modelPanel->setStatusSink([this](const QString& text) {
            statusLabel->setText(text);
        });
        modelPage = nodegraphwidgets::buildPanelCardPage(inspectorContent, modelPanel);
    }
    pageStack->addWidget(modelPage);

    {
        transformPanel = new NodeTransformPanel(inspectorContent);
        transformPanel->setStatusSink([this](const QString& text) {
            statusLabel->setText(text);
        });
        transformPage = nodegraphwidgets::buildPanelCardPage(inspectorContent, transformPanel);
    }
    pageStack->addWidget(transformPage);

    {
        groupPanel = new NodeGroupPanel(inspectorContent);
        groupPanel->setStatusSink([this](const QString& text) {
            statusLabel->setText(text);
        });
        groupPage = nodegraphwidgets::buildPanelCardPage(inspectorContent, groupPanel);
    }
    pageStack->addWidget(groupPage);

    {
        remeshPanel = new NodeRemeshPanel(inspectorContent);
        remeshPanel->setStatusSink([this](const QString& text) {
            statusLabel->setText(text);
        });
        remeshPage = nodegraphwidgets::buildPanelCardPage(inspectorContent, remeshPanel);
    }
    pageStack->addWidget(remeshPage);

    {
        voronoiPanel = new NodeVoronoiPanel(inspectorContent);
        voronoiPanel->setStatusSink([this](const QString& text) {
            statusLabel->setText(text);
        });
        voronoiPage = nodegraphwidgets::buildPanelCardPage(inspectorContent, voronoiPanel);
    }
    pageStack->addWidget(voronoiPage);

    {
        contactPanel = new NodeContactPanel(inspectorContent);
        contactPanel->setStatusSink([this](const QString& text) {
            statusLabel->setText(text);
        });
        contactPage = nodegraphwidgets::buildPanelCardPage(inspectorContent, contactPanel);
    }
    pageStack->addWidget(contactPage);

    {
        heatSourcePanel = new NodeHeatSourcePanel(inspectorContent);
        heatSourcePanel->setStatusSink([this](const QString& text) {
            statusLabel->setText(text);
        });
        heatSourcePage = nodegraphwidgets::buildPanelCardPage(inspectorContent, heatSourcePanel);
    }
    pageStack->addWidget(heatSourcePage);

    {
        heatSolverPanel = new NodeHeatSolverPanel(inspectorContent);
        heatSolverPanel->setStatusSink([this](const QString& text) {
            statusLabel->setText(text);
        });
        heatPage = nodegraphwidgets::buildPanelCardPage(inspectorContent, heatSolverPanel);
    }
    pageStack->addWidget(heatPage);

    mainTabWidget = new QTabWidget(inspectorContent);

    {
        QWidget* nodeTab = new QWidget(mainTabWidget);
        QVBoxLayout* nodeLayout = new QVBoxLayout(nodeTab);
        nodeLayout->setContentsMargins(0, 0, 0, 0);
        nodeLayout->addWidget(pageStack, 1);
        mainTabWidget->addTab(nodeTab, "Node");
    }
    {
        QWidget* spreadsheetTab = new QWidget(mainTabWidget);
        QVBoxLayout* spreadsheetLayout = new QVBoxLayout(spreadsheetTab);
        spreadsheetLayout->setContentsMargins(0, 0, 0, 0);

        QFrame* card = new QFrame(spreadsheetTab);
        card->setObjectName("NodePanelCard");
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(
            nodegraphwidgets::panelMarginLeft,
            nodegraphwidgets::panelMarginTop,
            nodegraphwidgets::panelMarginRight,
            nodegraphwidgets::panelMarginBottom);
        cardLayout->setSpacing(nodegraphwidgets::panelCardInnerSpacing);

        QHBoxLayout* topRow = new QHBoxLayout();
        topRow->addWidget(new QLabel("Output Socket:", card));
        spreadsheetSocketComboBox = new QComboBox(card);
        topRow->addWidget(spreadsheetSocketComboBox, 1);
        spreadsheetRefreshButton = new QPushButton("Refresh Spreadsheet", card);
        topRow->addWidget(spreadsheetRefreshButton);
        cardLayout->addLayout(topRow);

        spreadsheetSummaryLabel = new QLabel(card);
        spreadsheetSummaryLabel->setWordWrap(true);
        spreadsheetSummaryLabel->setText("No node selected");
        cardLayout->addWidget(spreadsheetSummaryLabel);

        spreadsheetLayout->addWidget(card);
        spreadsheetLayout->addStretch();
        mainTabWidget->addTab(spreadsheetTab, "Spreadsheet");
    }
    {
        QWidget* dataflowTab = new QWidget(mainTabWidget);
        QVBoxLayout* dataflowLayout = new QVBoxLayout(dataflowTab);
        dataflowLayout->setContentsMargins(0, 0, 0, 0);

        QFrame* card = new QFrame(dataflowTab);
        card->setObjectName("NodePanelCard");
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(
            nodegraphwidgets::panelMarginLeft,
            nodegraphwidgets::panelMarginTop,
            nodegraphwidgets::panelMarginRight,
            nodegraphwidgets::panelMarginBottom);
        cardLayout->setSpacing(nodegraphwidgets::panelCardInnerSpacing);

        dataflowRefreshButton = new QPushButton("Refresh Dataflow", card);
        cardLayout->addWidget(dataflowRefreshButton);

        dataflowTextEdit = new QTextEdit(card);
        dataflowTextEdit->setReadOnly(true);
        dataflowTextEdit->setMinimumHeight(nodegraphwidgets::panelDataflowHeight);
        dataflowTextEdit->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        cardLayout->addWidget(dataflowTextEdit, 1);
        dataflowLayout->addWidget(card, 1);
        mainTabWidget->addTab(dataflowTab, "Dataflow");
    }
    contentLayout->addWidget(mainTabWidget, 1);

    statusLabel = new QLabel(inspectorContent);
    statusLabel->setObjectName("NodePanelStatus");
    statusLabel->setWordWrap(true);
    contentLayout->addWidget(statusLabel);
    contentLayout->addStretch();

    connect(dataflowRefreshButton, &QPushButton::clicked, this, [this]() {
        refreshRuntimeDebugViews();
    });
    connect(spreadsheetRefreshButton, &QPushButton::clicked, this, [this]() {
        refreshRuntimeDebugViews();
    });
    connect(spreadsheetSocketComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        updateSpreadsheetView();
    });
}

void NodePanel::refreshRuntimeDebugViews() {
    if (currentNodeTypeId == nodegraphtypes::Group) {
        groupPanel->refreshSourceOptions();
    }
    if (currentNodeTypeId == nodegraphtypes::HeatSolve) {
        heatSolverPanel->refreshBindingGroupOptions();
    }
    updateDataflowView();
    updateSpreadsheetView();
}

void NodePanel::updateDataflowView() {
    if (!currentNodeId.isValid()) {
        dataflowTextEdit->setPlainText("Select a node to inspect dataflow packets");
        return;
    }

    NodeGraphRuntimeNodeDebugInfo debugInfo{};
    if (!NodeGraphDebugCache::tryGetLatestNodeDebugInfo(currentNodeId, debugInfo)) {
        dataflowTextEdit->setPlainText("No runtime packet data for this node yet \n Run the graph for at least one frame");
        return;
    }

    std::ostringstream stream;
    stream << "Revision: " << debugInfo.revision << "\n";
    stream << "Node: #" << debugInfo.nodeId.value << " (" << debugInfo.nodeTypeId << ")\n\n";

    stream << "Inputs:\n";
    if (debugInfo.inputs.empty()) {
        stream << "  <none>\n";
    } else {
        for (const NodeGraphRuntimeSocketDebugInfo& inputSocketDebug : debugInfo.inputs) {
            appendSocketDebugText(stream, inputSocketDebug);
        }
    }

    stream << "\nOutputs:\n";
    if (debugInfo.outputs.empty()) {
        stream << "  <none>\n";
    } else {
        for (const NodeGraphRuntimeSocketDebugInfo& outputSocketDebug : debugInfo.outputs) {
            appendSocketDebugText(stream, outputSocketDebug);
        }
    }

    dataflowTextEdit->setPlainText(QString::fromStdString(stream.str()));
}

void NodePanel::updateSpreadsheetView() {
    if (!currentNodeId.isValid()) {
        spreadsheetSocketComboBox->blockSignals(true);
        spreadsheetSocketComboBox->clear();
        spreadsheetSocketComboBox->blockSignals(false);
        clearSpreadsheetView("Select a node to inspect socket data");
        return;
    }

    NodeGraphRuntimeNodeDebugInfo debugInfo{};
    if (!NodeGraphDebugCache::tryGetLatestNodeDebugInfo(currentNodeId, debugInfo)) {
        spreadsheetSocketComboBox->blockSignals(true);
        spreadsheetSocketComboBox->clear();
        spreadsheetSocketComboBox->blockSignals(false);
        clearSpreadsheetView("No runtime data yet \n Run the graph for at least one frame");
        return;
    }

    uint32_t previousSocketId = 0;
    if (spreadsheetSocketComboBox->currentIndex() >= 0) {
        previousSocketId = spreadsheetSocketComboBox->currentData().toUInt();
    }

    int selectedIndex = -1;
    spreadsheetSocketComboBox->blockSignals(true);
    spreadsheetSocketComboBox->clear();
    for (std::size_t outputIndex = 0; outputIndex < debugInfo.outputs.size(); ++outputIndex) {
        const NodeGraphRuntimeSocketDebugInfo& outputSocketDebug = debugInfo.outputs[outputIndex];
        QString socketLabel = QString("%1 (#%2)")
                                  .arg(QString::fromStdString(outputSocketDebug.socketName))
                                  .arg(outputSocketDebug.socketId.value);
        if (!outputSocketDebug.hasValue) {
            socketLabel += " <none>";
        }

        spreadsheetSocketComboBox->addItem(socketLabel, static_cast<uint>(outputSocketDebug.socketId.value));
        if (outputSocketDebug.socketId.value == previousSocketId) {
            selectedIndex = static_cast<int>(outputIndex);
        }
    }
    if (selectedIndex < 0 && !debugInfo.outputs.empty()) {
        selectedIndex = 0;
    }
    if (selectedIndex >= 0) {
        spreadsheetSocketComboBox->setCurrentIndex(selectedIndex);
    }
    spreadsheetSocketComboBox->blockSignals(false);

    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(debugInfo.outputs.size())) {
        clearSpreadsheetView("Selected node has no output sockets.");
        return;
    }

    const NodeGraphRuntimeSocketDebugInfo& selectedSocketDebug = debugInfo.outputs[static_cast<std::size_t>(selectedIndex)];
    if (!selectedSocketDebug.hasValue) {
        clearSpreadsheetView("Selected output socket has no runtime value");
        return;
    }

    spreadsheetSummaryLabel->setText(
        QString("Data Type: %1 | Lineage: %2")
            .arg(QString::fromStdString(selectedSocketDebug.dataType))
            .arg(QString::fromStdString(formatLineagePath(selectedSocketDebug.lineageNodeIds))));
}

void NodePanel::clearSpreadsheetView(const QString& message) {
    spreadsheetSummaryLabel->setText(message);
}
