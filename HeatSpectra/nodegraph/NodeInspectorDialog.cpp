#include "NodeInspectorDialog.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphDebugStore.hpp"
#include "runtime/RuntimeInterfaces.hpp"

#include <QAbstractScrollArea>
#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

class AttributeSamplesTableModel final : public QAbstractTableModel {
public:
    explicit AttributeSamplesTableModel(QObject* parent = nullptr)
        : QAbstractTableModel(parent) {
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) {
            return 0;
        }
        return totalSampleRowCount;
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) {
            return 0;
        }
        return 3;
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || role != Qt::DisplayRole) {
            return {};
        }

        if (index.row() < 0 || index.row() >= totalSampleRowCount || index.column() < 0 || index.column() >= 3) {
            return {};
        }

        int elementIndex = 0;
        const NodeGraphRuntimeAttributeDebugInfo* attribute = attributeForRow(index.row(), elementIndex);
        if (!attribute) {
            return {};
        }

        switch (index.column()) {
        case 0:
            return QString::fromStdString(attribute->name);
        case 1:
            return elementIndex;
        case 2:
            if (elementIndex < 0 || elementIndex >= static_cast<int>(attribute->sampleValues.size())) {
                return {};
            }
            return QString::fromStdString(attribute->sampleValues[static_cast<std::size_t>(elementIndex)]);
        default:
            return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override {
        if (role != Qt::DisplayRole) {
            return {};
        }

        if (orientation == Qt::Horizontal) {
            switch (section) {
            case 0:
                return QStringLiteral("Attribute");
            case 1:
                return QStringLiteral("Element");
            case 2:
                return QStringLiteral("Value");
            default:
                return {};
            }
        }

        return section + 1;
    }

    void setAttributes(std::vector<NodeGraphRuntimeAttributeDebugInfo> attributes) {
        beginResetModel();
        attributeRows = std::move(attributes);
        rowOffsets.clear();
        rowOffsets.reserve(attributeRows.size() + 1);
        rowOffsets.push_back(0);

        totalSampleRowCount = 0;
        for (const NodeGraphRuntimeAttributeDebugInfo& attribute : attributeRows) {
            const std::size_t sampleCount = attribute.sampleValues.size();
            const int remainingRows = std::numeric_limits<int>::max() - totalSampleRowCount;
            if (sampleCount <= static_cast<std::size_t>(remainingRows)) {
                totalSampleRowCount += static_cast<int>(sampleCount);
            } else {
                totalSampleRowCount = std::numeric_limits<int>::max();
            }
            rowOffsets.push_back(totalSampleRowCount);
        }
        endResetModel();
    }

    void clear() {
        setAttributes({});
    }

private:
    const NodeGraphRuntimeAttributeDebugInfo* attributeForRow(int row, int& outElementIndex) const {
        if (row < 0 || row >= totalSampleRowCount || rowOffsets.empty()) {
            outElementIndex = 0;
            return nullptr;
        }

        const auto offsetIt = std::upper_bound(rowOffsets.begin(), rowOffsets.end(), row);
        if (offsetIt == rowOffsets.begin() || offsetIt == rowOffsets.end()) {
            outElementIndex = 0;
            return nullptr;
        }

        const std::size_t attributeIndex = static_cast<std::size_t>(std::distance(rowOffsets.begin(), offsetIt) - 1);
        if (attributeIndex >= attributeRows.size()) {
            outElementIndex = 0;
            return nullptr;
        }

        const int startRow = rowOffsets[attributeIndex];
        outElementIndex = row - startRow;
        return &attributeRows[attributeIndex];
    }

    std::vector<NodeGraphRuntimeAttributeDebugInfo> attributeRows;
    std::vector<int> rowOffsets;
    int totalSampleRowCount = 0;
};

QString nodeTypeDisplayName(const NodeTypeId& typeId) {
    const NodeTypeDefinition* definition = findNodeTypeDefinitionById(typeId);
    if (definition) {
        return QString::fromStdString(definition->displayName);
    }

    return QString::fromStdString(typeId.empty() ? std::string("Unknown") : typeId);
}

bool readBoolParam(const NodeGraphNode& node, uint32_t parameterId, bool defaultValue = false) {
    bool value = defaultValue;
    if (tryGetNodeParamBool(node, parameterId, value)) {
        return value;
    }

    return defaultValue;
}

double readFloatParam(const NodeGraphNode& node, uint32_t parameterId, double defaultValue) {
    double value = defaultValue;
    if (tryGetNodeParamFloat(node, parameterId, value)) {
        return value;
    }

    return defaultValue;
}

int readIntParam(const NodeGraphNode& node, uint32_t parameterId, int defaultValue) {
    int64_t value = defaultValue;
    if (tryGetNodeParamInt(node, parameterId, value)) {
        return static_cast<int>(value);
    }

    return defaultValue;
}

std::string readStringParam(const NodeGraphNode& node, uint32_t parameterId) {
    std::string value;
    if (tryGetNodeParamString(node, parameterId, value)) {
        return value;
    }

    return {};
}

bool writeBoolParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, bool value) {
    if (!nodeGraphBridge) {
        return false;
    }

    NodeGraphParamValue parameter{};
    parameter.id = parameterId;
    parameter.type = NodeGraphParamType::Bool;
    parameter.boolValue = value;
    return nodeGraphBridge->setNodeParameter(nodeId, parameter);
}

bool writeFloatParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, double value) {
    if (!nodeGraphBridge) {
        return false;
    }

    NodeGraphParamValue parameter{};
    parameter.id = parameterId;
    parameter.type = NodeGraphParamType::Float;
    parameter.floatValue = value;
    return nodeGraphBridge->setNodeParameter(nodeId, parameter);
}

bool writeIntParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, int64_t value) {
    if (!nodeGraphBridge) {
        return false;
    }

    NodeGraphParamValue parameter{};
    parameter.id = parameterId;
    parameter.type = NodeGraphParamType::Int;
    parameter.intValue = value;
    return nodeGraphBridge->setNodeParameter(nodeId, parameter);
}

bool writeStringParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, const std::string& value) {
    if (!nodeGraphBridge) {
        return false;
    }

    NodeGraphParamValue parameter{};
    parameter.id = parameterId;
    parameter.type = NodeGraphParamType::String;
    parameter.stringValue = value;
    return nodeGraphBridge->setNodeParameter(nodeId, parameter);
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

NodeInspectorDialog::NodeInspectorDialog(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(260);

    buildUi();
}

void NodeInspectorDialog::bind(NodeGraphBridge* nodeGraphBridgePtr, const RuntimeQuery* runtimeQueryPtr) {
    nodeGraphBridge = nodeGraphBridgePtr;
    runtimeQuery = runtimeQueryPtr;

    if (isVisible() && currentNodeId.isValid()) {
        setNode(currentNodeId);
    }
}

bool NodeInspectorDialog::setNode(NodeGraphNodeId nodeId) {
    if (!nodeGraphBridge) {
        return false;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(nodeId, node)) {
        return false;
    }

    currentNodeId = nodeId;
    currentNodeTypeId = canonicalNodeTypeId(node.typeId);

    titleLabel->setText(QString::fromStdString(node.title));
    subtitleLabel->setText(nodeTypeDisplayName(currentNodeTypeId));
    statusLabel->clear();

    if (currentNodeTypeId == nodegraphtypes::Model) {
        pageStack->setCurrentWidget(modelPage);
        modelPathLineEdit->setText(QString::fromStdString(readStringParam(node, nodegraphparams::model::Path)));
        heatStatusTimer->stop();
    } else if (currentNodeTypeId == nodegraphtypes::Remesh) {
        pageStack->setCurrentWidget(remeshPage);
        iterationsSpinBox->setValue(readIntParam(node, nodegraphparams::remesh::Iterations, 1));
        minAngleSpinBox->setValue(readFloatParam(node, nodegraphparams::remesh::MinAngleDegrees, 30.0));
        maxEdgeLengthSpinBox->setValue(readFloatParam(node, nodegraphparams::remesh::MaxEdgeLength, 0.1));
        stepSizeSpinBox->setValue(readFloatParam(node, nodegraphparams::remesh::StepSize, 0.25));
        heatStatusTimer->stop();
    } else if (currentNodeTypeId == nodegraphtypes::HeatSolve) {
        pageStack->setCurrentWidget(heatPage);
        updateHeatStatus();
        if (runtimeQuery) {
            heatStatusTimer->start(125);
        } else {
            heatStatusTimer->stop();
        }
    } else {
        pageStack->setCurrentWidget(genericPage);
        heatStatusTimer->stop();
    }

    refreshRuntimeDebugViews();

    return true;
}

void NodeInspectorDialog::hideEvent(QHideEvent* event) {
    if (heatStatusTimer) {
        heatStatusTimer->stop();
    }

    QWidget::hideEvent(event);
}

void NodeInspectorDialog::buildUi() {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(scrollArea, 1);

    QWidget* inspectorContent = new QWidget(scrollArea);
    QVBoxLayout* contentLayout = new QVBoxLayout(inspectorContent);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    scrollArea->setWidget(inspectorContent);

    titleLabel = new QLabel(inspectorContent);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    titleLabel->setFont(titleFont);
    titleLabel->setText("No Node Selected");
    contentLayout->addWidget(titleLabel);

    subtitleLabel = new QLabel(inspectorContent);
    subtitleLabel->setText("Select a node in the graph to inspect parameters and dataflow.");
    contentLayout->addWidget(subtitleLabel);

    pageStack = new QStackedWidget(inspectorContent);

    genericPage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(genericPage);
        QLabel* msg = new QLabel("This node has no editable actions yet.", genericPage);
        msg->setWordWrap(true);
        layout->addWidget(msg);
        layout->addStretch();
    }
    pageStack->addWidget(genericPage);

    modelPage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(modelPage);

        QLabel* pathLabel = new QLabel("Model File:", modelPage);
        layout->addWidget(pathLabel);

        QHBoxLayout* pathRow = new QHBoxLayout();
        modelPathLineEdit = new QLineEdit(modelPage);
        modelPathLineEdit->setReadOnly(true);
        modelPathLineEdit->setPlaceholderText("models/teapot.obj");
        pathRow->addWidget(modelPathLineEdit, 1);

        modelBrowseButton = new QPushButton("Browse...", modelPage);
        pathRow->addWidget(modelBrowseButton);
        layout->addLayout(pathRow);

        modelApplyButton = new QPushButton("Apply Selected Model", modelPage);
        layout->addWidget(modelApplyButton);

        QLabel* hintLabel = new QLabel("Model nodes are independent. Their downstream graph wiring determines how runtime consumes this mesh.", modelPage);
        hintLabel->setWordWrap(true);
        layout->addWidget(hintLabel);
        layout->addStretch();
    }
    pageStack->addWidget(modelPage);

    remeshPage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(remeshPage);

        QHBoxLayout* iterationsRow = new QHBoxLayout();
        iterationsRow->addWidget(new QLabel("Iterations:", remeshPage));
        iterationsSpinBox = new QSpinBox(remeshPage);
        iterationsSpinBox->setMinimum(1);
        iterationsSpinBox->setMaximum(1000);
        iterationsSpinBox->setValue(1);
        iterationsRow->addWidget(iterationsSpinBox, 1);
        layout->addLayout(iterationsRow);

        QHBoxLayout* minAngleRow = new QHBoxLayout();
        minAngleRow->addWidget(new QLabel("Min Angle:", remeshPage));
        minAngleSpinBox = new QDoubleSpinBox(remeshPage);
        minAngleSpinBox->setMinimum(0.0);
        minAngleSpinBox->setMaximum(60.0);
        minAngleSpinBox->setSingleStep(1.0);
        minAngleSpinBox->setValue(30.0);
        minAngleRow->addWidget(minAngleSpinBox, 1);
        layout->addLayout(minAngleRow);

        QHBoxLayout* maxEdgeRow = new QHBoxLayout();
        maxEdgeRow->addWidget(new QLabel("Max Edge Length:", remeshPage));
        maxEdgeLengthSpinBox = new QDoubleSpinBox(remeshPage);
        maxEdgeLengthSpinBox->setMinimum(0.001);
        maxEdgeLengthSpinBox->setMaximum(10.0);
        maxEdgeLengthSpinBox->setDecimals(4);
        maxEdgeLengthSpinBox->setSingleStep(0.01);
        maxEdgeLengthSpinBox->setValue(0.1);
        maxEdgeRow->addWidget(maxEdgeLengthSpinBox, 1);
        layout->addLayout(maxEdgeRow);

        QHBoxLayout* stepRow = new QHBoxLayout();
        stepRow->addWidget(new QLabel("Step Size:", remeshPage));
        stepSizeSpinBox = new QDoubleSpinBox(remeshPage);
        stepSizeSpinBox->setMinimum(0.01);
        stepSizeSpinBox->setMaximum(1.0);
        stepSizeSpinBox->setSingleStep(0.05);
        stepSizeSpinBox->setDecimals(2);
        stepSizeSpinBox->setValue(0.25);
        stepRow->addWidget(stepSizeSpinBox, 1);
        layout->addLayout(stepRow);

        QHBoxLayout* actionRow = new QHBoxLayout();
        remeshApplyButton = new QPushButton("Apply", remeshPage);
        remeshRunButton = new QPushButton("Run Remesh", remeshPage);
        actionRow->addWidget(remeshApplyButton);
        actionRow->addWidget(remeshRunButton);
        layout->addLayout(actionRow);
        layout->addStretch();
    }
    pageStack->addWidget(remeshPage);

    heatPage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(heatPage);

        QHBoxLayout* statusRow = new QHBoxLayout();
        statusRow->addWidget(new QLabel("Status:", heatPage));
        heatStatusValueLabel = new QLabel("Unknown", heatPage);
        statusRow->addWidget(heatStatusValueLabel, 1);
        layout->addLayout(statusRow);

        heatToggleButton = new QPushButton("Start", heatPage);
        heatPauseButton = new QPushButton("Pause", heatPage);
        heatResetButton = new QPushButton("Reset", heatPage);
        layout->addWidget(heatToggleButton);
        layout->addWidget(heatPauseButton);
        layout->addWidget(heatResetButton);
        layout->addStretch();
    }
    pageStack->addWidget(heatPage);

    contentLayout->addWidget(pageStack, 1);

    QLabel* runtimeDataHeader = new QLabel("Runtime Data:", inspectorContent);
    contentLayout->addWidget(runtimeDataHeader);

    dataTabWidget = new QTabWidget(inspectorContent);
    {
        QWidget* dataflowTab = new QWidget(dataTabWidget);
        QVBoxLayout* dataflowLayout = new QVBoxLayout(dataflowTab);
        dataflowRefreshButton = new QPushButton("Refresh Dataflow", dataflowTab);
        dataflowLayout->addWidget(dataflowRefreshButton);

        dataflowTextEdit = new QTextEdit(dataflowTab);
        dataflowTextEdit->setReadOnly(true);
        dataflowTextEdit->setMinimumHeight(140);
        dataflowTextEdit->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        dataflowLayout->addWidget(dataflowTextEdit, 1);
        dataTabWidget->addTab(dataflowTab, "Dataflow");
    }
    {
        QWidget* spreadsheetTab = new QWidget(dataTabWidget);
        QVBoxLayout* spreadsheetLayout = new QVBoxLayout(spreadsheetTab);

        QHBoxLayout* topRow = new QHBoxLayout();
        topRow->addWidget(new QLabel("Output Socket:", spreadsheetTab));
        spreadsheetSocketComboBox = new QComboBox(spreadsheetTab);
        topRow->addWidget(spreadsheetSocketComboBox, 1);
        spreadsheetRefreshButton = new QPushButton("Refresh Spreadsheet", spreadsheetTab);
        topRow->addWidget(spreadsheetRefreshButton);
        spreadsheetLayout->addLayout(topRow);

        spreadsheetSummaryLabel = new QLabel(spreadsheetTab);
        spreadsheetSummaryLabel->setWordWrap(true);
        spreadsheetSummaryLabel->setText("No node selected.");
        spreadsheetLayout->addWidget(spreadsheetSummaryLabel);

        spreadsheetAttributesTable = new QTableWidget(spreadsheetTab);
        spreadsheetAttributesTable->setColumnCount(5);
        spreadsheetAttributesTable->setHorizontalHeaderLabels({"Attribute", "Domain", "Type", "Tuple", "Elements"});
        spreadsheetAttributesTable->horizontalHeader()->setStretchLastSection(true);
        spreadsheetAttributesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        spreadsheetAttributesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        spreadsheetAttributesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        spreadsheetAttributesTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        spreadsheetAttributesTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        spreadsheetAttributesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        spreadsheetAttributesTable->setSelectionMode(QAbstractItemView::NoSelection);
        spreadsheetAttributesTable->setMinimumHeight(130);
        spreadsheetLayout->addWidget(spreadsheetAttributesTable, 1);

        spreadsheetSamplesTable = new QTableView(spreadsheetTab);
        spreadsheetSamplesModel = new AttributeSamplesTableModel(spreadsheetSamplesTable);
        spreadsheetSamplesTable->setModel(spreadsheetSamplesModel);
        spreadsheetSamplesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        spreadsheetSamplesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        spreadsheetSamplesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        spreadsheetSamplesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        spreadsheetSamplesTable->setSelectionMode(QAbstractItemView::NoSelection);
        spreadsheetSamplesTable->setMinimumHeight(130);
        spreadsheetLayout->addWidget(spreadsheetSamplesTable, 1);

        dataTabWidget->addTab(spreadsheetTab, "Spreadsheet");
    }
    contentLayout->addWidget(dataTabWidget, 1);

    statusLabel = new QLabel(inspectorContent);
    statusLabel->setWordWrap(true);
    contentLayout->addWidget(statusLabel);
    contentLayout->addStretch();

    heatStatusTimer = new QTimer(this);
    heatStatusTimer->setInterval(125);

    connect(remeshApplyButton, &QPushButton::clicked, this, [this]() {
        applyRemeshSettings();
    });
    connect(modelApplyButton, &QPushButton::clicked, this, [this]() {
        applyModelSettings();
    });
    connect(modelBrowseButton, &QPushButton::clicked, this, [this]() {
        browseModelFile();
    });
    connect(remeshRunButton, &QPushButton::clicked, this, [this]() {
        executeRemesh();
    });

    connect(heatToggleButton, &QPushButton::clicked, this, [this]() {
        toggleHeatSystem();
    });
    connect(heatPauseButton, &QPushButton::clicked, this, [this]() {
        pauseHeatSystem();
    });
    connect(heatResetButton, &QPushButton::clicked, this, [this]() {
        resetHeatSystem();
    });
    connect(heatStatusTimer, &QTimer::timeout, this, [this]() {
        updateHeatStatus();
    });
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

void NodeInspectorDialog::browseModelFile() {
    if (!nodeGraphBridge || currentNodeTypeId != nodegraphtypes::Model || !modelPathLineEdit) {
        statusLabel->setText("Cannot browse model file for this node.");
        return;
    }

    QString initialPath = modelPathLineEdit->text().trimmed();
    if (initialPath.isEmpty()) {
        initialPath = QDir::currentPath();
    }

    const QString selectedFilePath = QFileDialog::getOpenFileName(
        this,
        "Select Model File",
        initialPath,
        "OBJ Files (*.obj);;All Files (*.*)");
    if (selectedFilePath.isEmpty()) {
        return;
    }

    const QString normalizedPath = QDir::fromNativeSeparators(selectedFilePath);
    modelPathLineEdit->setText(normalizedPath);
    applyModelSettings();
}

void NodeInspectorDialog::applyModelSettings() {
    if (!nodeGraphBridge || currentNodeTypeId != nodegraphtypes::Model || !modelPathLineEdit) {
        statusLabel->setText("Cannot apply model settings for this node.");
        return;
    }

    const std::string modelPath = modelPathLineEdit->text().trimmed().toStdString();
    if (modelPath.empty()) {
        statusLabel->setText("Model path cannot be empty.");
        return;
    }

    if (!writeStringParam(nodeGraphBridge, currentNodeId, nodegraphparams::model::Path, modelPath) ||
        !writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::model::ApplyRequested, true)) {
        statusLabel->setText("Failed to update model settings.");
        return;
    }

    statusLabel->setText("Model path applied.");
}

void NodeInspectorDialog::applyRemeshSettings() {
    if (!nodeGraphBridge || currentNodeTypeId != nodegraphtypes::Remesh) {
        statusLabel->setText("Cannot apply settings for this node.");
        return;
    }

    if (!writeIntParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::Iterations, iterationsSpinBox->value()) ||
        !writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::MinAngleDegrees, minAngleSpinBox->value()) ||
        !writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::MaxEdgeLength, maxEdgeLengthSpinBox->value()) ||
        !writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::StepSize, stepSizeSpinBox->value())) {
        statusLabel->setText("Failed to update remesh settings.");
        return;
    }

    statusLabel->setText("Remesh settings updated.");
}

void NodeInspectorDialog::executeRemesh() {
    if (!nodeGraphBridge || currentNodeTypeId != nodegraphtypes::Remesh) {
        statusLabel->setText("Cannot run remesh for this node.");
        return;
    }

    applyRemeshSettings();
    if (!writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::RunRequested, true)) {
        statusLabel->setText("Failed to request remesh.");
        return;
    }

    statusLabel->setText("Remesh requested through node graph.");
}

void NodeInspectorDialog::toggleHeatSystem() {
    if (!nodeGraphBridge || currentNodeTypeId != nodegraphtypes::HeatSolve) {
        statusLabel->setText("Cannot control heat system for this node.");
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        statusLabel->setText("Failed to read node state.");
        return;
    }

    const bool enable = !readBoolParam(node, nodegraphparams::heatsolve::Enabled, false);
    if (!writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::heatsolve::Enabled, enable)) {
        statusLabel->setText("Failed to update heat node state.");
        return;
    }
    if (!enable) {
        writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::heatsolve::Paused, false);
    }

    updateHeatStatus();
    statusLabel->setText(enable ? "Heat solve enabled through node graph." : "Heat solve disabled through node graph.");
}

void NodeInspectorDialog::pauseHeatSystem() {
    if (!nodeGraphBridge || currentNodeTypeId != nodegraphtypes::HeatSolve) {
        statusLabel->setText("Cannot control heat system for this node.");
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        statusLabel->setText("Failed to read node state.");
        return;
    }

    const bool pause = !readBoolParam(node, nodegraphparams::heatsolve::Paused, false);
    if (!writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::heatsolve::Paused, pause)) {
        statusLabel->setText("Failed to update heat pause state.");
        return;
    }

    updateHeatStatus();
    statusLabel->setText(pause ? "Heat solve pause requested." : "Heat solve resume requested.");
}

void NodeInspectorDialog::resetHeatSystem() {
    if (!nodeGraphBridge || currentNodeTypeId != nodegraphtypes::HeatSolve) {
        statusLabel->setText("Cannot control heat system for this node.");
        return;
    }

    if (!writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::heatsolve::Paused, false) ||
        !writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::heatsolve::ResetRequested, true)) {
        statusLabel->setText("Failed to request heat reset.");
        return;
    }

    updateHeatStatus();
    statusLabel->setText("Heat solve reset requested.");
}

void NodeInspectorDialog::refreshRuntimeDebugViews() {
    updateDataflowView();
    updateSpreadsheetView();
}

void NodeInspectorDialog::updateDataflowView() {
    if (!dataflowTextEdit) {
        return;
    }

    if (!currentNodeId.isValid()) {
        dataflowTextEdit->setPlainText("Select a node to inspect dataflow packets.");
        return;
    }

    NodeGraphRuntimeNodeDebugInfo debugInfo{};
    if (!NodeGraphDebugStore::tryGetLatestNodeDebugInfo(currentNodeId, debugInfo)) {
        dataflowTextEdit->setPlainText("No runtime packet data for this node yet.\nRun the graph for at least one frame.");
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

void NodeInspectorDialog::updateSpreadsheetView() {
    if (!spreadsheetSocketComboBox || !spreadsheetSummaryLabel ||
        !spreadsheetAttributesTable || !spreadsheetSamplesTable || !spreadsheetSamplesModel) {
        return;
    }

    if (!currentNodeId.isValid()) {
        spreadsheetSocketComboBox->blockSignals(true);
        spreadsheetSocketComboBox->clear();
        spreadsheetSocketComboBox->blockSignals(false);
        clearSpreadsheetView("Select a node to inspect geometry attributes.");
        return;
    }

    NodeGraphRuntimeNodeDebugInfo debugInfo{};
    if (!NodeGraphDebugStore::tryGetLatestNodeDebugInfo(currentNodeId, debugInfo)) {
        spreadsheetSocketComboBox->blockSignals(true);
        spreadsheetSocketComboBox->clear();
        spreadsheetSocketComboBox->blockSignals(false);
        clearSpreadsheetView("No runtime geometry data yet. Run the graph for at least one frame.");
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
        clearSpreadsheetView("Selected output socket has no runtime value.");
        return;
    }

    spreadsheetSummaryLabel->setText(
        QString("Data Type: %1 | Lineage: %2 | Attributes: %3")
            .arg(QString::fromStdString(selectedSocketDebug.dataType))
            .arg(QString::fromStdString(formatLineagePath(selectedSocketDebug.lineageNodeIds)))
            .arg(static_cast<int>(selectedSocketDebug.attributes.size())));

    spreadsheetAttributesTable->clearContents();
    spreadsheetAttributesTable->setRowCount(static_cast<int>(selectedSocketDebug.attributes.size()));
    for (int row = 0; row < static_cast<int>(selectedSocketDebug.attributes.size()); ++row) {
        const NodeGraphRuntimeAttributeDebugInfo& attributeDebug =
            selectedSocketDebug.attributes[static_cast<std::size_t>(row)];
        spreadsheetAttributesTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(attributeDebug.name)));
        spreadsheetAttributesTable->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(attributeDebug.domain)));
        spreadsheetAttributesTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(attributeDebug.dataType)));
        spreadsheetAttributesTable->setItem(row, 3, new QTableWidgetItem(QString::number(attributeDebug.tupleSize)));
        spreadsheetAttributesTable->setItem(row, 4, new QTableWidgetItem(QString::number(attributeDebug.elementCount)));
    }

    if (AttributeSamplesTableModel* sampleModel = dynamic_cast<AttributeSamplesTableModel*>(spreadsheetSamplesModel)) {
        sampleModel->setAttributes(selectedSocketDebug.attributes);
    }
}

void NodeInspectorDialog::clearSpreadsheetView(const QString& message) {
    if (spreadsheetSummaryLabel) {
        spreadsheetSummaryLabel->setText(message);
    }
    if (spreadsheetAttributesTable) {
        spreadsheetAttributesTable->clearContents();
        spreadsheetAttributesTable->setRowCount(0);
    }
    if (AttributeSamplesTableModel* sampleModel = dynamic_cast<AttributeSamplesTableModel*>(spreadsheetSamplesModel)) {
        sampleModel->clear();
    }
}

void NodeInspectorDialog::updateHeatStatus() {
    if (!heatStatusValueLabel || !heatToggleButton || !heatPauseButton) {
        return;
    }

    NodeGraphNode node{};
    const bool hasNodeState = nodeGraphBridge && currentNodeId.isValid() && nodeGraphBridge->getNode(currentNodeId, node);
    if (!hasNodeState || canonicalNodeTypeId(node.typeId) != nodegraphtypes::HeatSolve) {
        heatStatusValueLabel->setText("Unavailable");
        heatToggleButton->setEnabled(false);
        heatPauseButton->setEnabled(false);
        heatResetButton->setEnabled(false);
        return;
    }

    const bool desiredEnabled = readBoolParam(node, nodegraphparams::heatsolve::Enabled, false);
    const bool desiredPaused = readBoolParam(node, nodegraphparams::heatsolve::Paused, false);

    heatToggleButton->setEnabled(true);
    heatToggleButton->setText(desiredEnabled ? "Stop" : "Start");
    heatPauseButton->setEnabled(desiredEnabled);
    heatPauseButton->setText(desiredPaused ? "Resume" : "Pause");
    heatResetButton->setEnabled(true);

    if (!runtimeQuery) {
        heatStatusValueLabel->setText(desiredEnabled ? (desiredPaused ? "Queued Paused" : "Queued") : "Stopped");
        return;
    }

    const bool active = runtimeQuery->isSimulationActive();
    const bool paused = runtimeQuery->isSimulationPaused();

    if (!active) {
        if (desiredEnabled) {
            std::string reason;
            if (nodeGraphBridge && !nodeGraphBridge->canExecuteHeatSolve(reason)) {
                heatStatusValueLabel->setText("Blocked");
                return;
            }

            heatStatusValueLabel->setText(desiredPaused ? "Pending Pause" : "Pending Start");
            return;
        }

        heatStatusValueLabel->setText("Stopped");
        return;
    }

    if (paused) {
        heatStatusValueLabel->setText("Paused");
        return;
    }

    heatStatusValueLabel->setText("Running");
}
