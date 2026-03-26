#include "NodeInspectorDialog.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphDebugStore.hpp"
#include "NodeContactPanel.hpp"
#include "NodeHeatSourcePanel.hpp"
#include "NodeGroupPanel.hpp"
#include "NodeHeatSolverPanel.hpp"
#include "NodeModelPanel.hpp"
#include "NodeTransformPanel.hpp"
#include "NodeRemeshPanel.hpp"
#include "NodeVoronoiPanel.hpp"
#include "runtime/RuntimeInterfaces.hpp"

#include <QAbstractScrollArea>
#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QComboBox>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
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
    const NodeTypeDefinition* definition = NodeGraphRegistry::findNodeById(typeId);
    if (definition) {
        return QString::fromStdString(definition->displayName);
    }

    return QString::fromStdString(typeId.empty() ? std::string("Unknown") : typeId);
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
    if (groupPanel) {
        groupPanel->bind(nodeGraphBridgePtr);
    }
    if (modelPanel) {
        modelPanel->bind(nodeGraphBridgePtr);
    }
    if (transformPanel) {
        transformPanel->bind(nodeGraphBridgePtr);
    }
    if (remeshPanel) {
        remeshPanel->bind(nodeGraphBridgePtr);
    }
    if (voronoiPanel) {
        voronoiPanel->bind(nodeGraphBridgePtr);
    }
    if (contactPanel) {
        contactPanel->bind(nodeGraphBridgePtr);
    }
    if (heatSourcePanel) {
        heatSourcePanel->bind(nodeGraphBridgePtr);
    }
    if (heatSolverPanel) {
        heatSolverPanel->bind(nodeGraphBridgePtr, runtimeQueryPtr);
    }

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
    currentNodeTypeId = getNodeTypeId(node.typeId);

    titleLabel->setText(QString::fromStdString(node.title));
    subtitleLabel->setText(nodeTypeDisplayName(currentNodeTypeId));
    statusLabel->clear();

    if (currentNodeTypeId == nodegraphtypes::Model) {
        pageStack->setCurrentWidget(modelPage);
        if (modelPanel) {
            modelPanel->setNode(currentNodeId);
        }
        if (heatSolverPanel) { heatSolverPanel->stopStatusTimer(); }
    } else if (currentNodeTypeId == nodegraphtypes::Transform) {
        pageStack->setCurrentWidget(transformPage);
        if (transformPanel) {
            transformPanel->setNode(currentNodeId);
        }
        if (heatSolverPanel) { heatSolverPanel->stopStatusTimer(); }
    } else if (currentNodeTypeId == nodegraphtypes::Group) {
        pageStack->setCurrentWidget(groupPage);
        if (groupPanel) {
            groupPanel->setNode(currentNodeId);
        }
        if (heatSolverPanel) { heatSolverPanel->stopStatusTimer(); }
    } else if (currentNodeTypeId == nodegraphtypes::Remesh) {
        pageStack->setCurrentWidget(remeshPage);
        if (remeshPanel) {
            remeshPanel->setNode(currentNodeId);
        }
        if (heatSolverPanel) { heatSolverPanel->stopStatusTimer(); }
    } else if (currentNodeTypeId == nodegraphtypes::Voronoi) {
        pageStack->setCurrentWidget(voronoiPage);
        if (voronoiPanel) {
            voronoiPanel->setNode(currentNodeId);
        }
        if (heatSolverPanel) { heatSolverPanel->stopStatusTimer(); }
    } else if (currentNodeTypeId == nodegraphtypes::Contact) {
        pageStack->setCurrentWidget(contactPage);
        if (contactPanel) {
            contactPanel->setNode(currentNodeId);
        }
        if (heatSolverPanel) { heatSolverPanel->stopStatusTimer(); }
    } else if (currentNodeTypeId == nodegraphtypes::HeatSource) {
        pageStack->setCurrentWidget(heatSourcePage);
        if (heatSourcePanel) {
            heatSourcePanel->setNode(currentNodeId);
        }
        if (heatSolverPanel) { heatSolverPanel->stopStatusTimer(); }
    } else if (currentNodeTypeId == nodegraphtypes::HeatSolve) {
        pageStack->setCurrentWidget(heatPage);
        if (heatSolverPanel) {
            heatSolverPanel->setNode(currentNodeId);
            if (runtimeQuery) {
                heatSolverPanel->startStatusTimer();
            } else {
                heatSolverPanel->stopStatusTimer();
            }
        }
    } else {
        pageStack->setCurrentWidget(genericPage);
        if (heatSolverPanel) { heatSolverPanel->stopStatusTimer(); }
    }

    refreshRuntimeDebugViews();

    return true;
}

void NodeInspectorDialog::hideEvent(QHideEvent* event) {
    if (heatSolverPanel) {
        heatSolverPanel->stopStatusTimer();
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
        modelPanel = new NodeModelPanel(modelPage);
        modelPanel->setStatusSink([this](const QString& text) {
            if (statusLabel) {
                statusLabel->setText(text);
            }
        });
        layout->addWidget(modelPanel);
    }
    pageStack->addWidget(modelPage);

    transformPage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(transformPage);
        transformPanel = new NodeTransformPanel(transformPage);
        transformPanel->setStatusSink([this](const QString& text) {
            if (statusLabel) {
                statusLabel->setText(text);
            }
        });
        layout->addWidget(transformPanel);
    }
    pageStack->addWidget(transformPage);

    groupPage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(groupPage);
        groupPanel = new NodeGroupPanel(groupPage);
        groupPanel->setStatusSink([this](const QString& text) {
            if (statusLabel) {
                statusLabel->setText(text);
            }
        });
        layout->addWidget(groupPanel);
    }
    pageStack->addWidget(groupPage);

    remeshPage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(remeshPage);
        remeshPanel = new NodeRemeshPanel(remeshPage);
        remeshPanel->setStatusSink([this](const QString& text) {
            if (statusLabel) {
                statusLabel->setText(text);
            }
        });
        layout->addWidget(remeshPanel);
    }
    pageStack->addWidget(remeshPage);

    voronoiPage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(voronoiPage);
        voronoiPanel = new NodeVoronoiPanel(voronoiPage);
        voronoiPanel->setStatusSink([this](const QString& text) {
            if (statusLabel) {
                statusLabel->setText(text);
            }
        });
        layout->addWidget(voronoiPanel);
    }
    pageStack->addWidget(voronoiPage);

    contactPage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(contactPage);
        contactPanel = new NodeContactPanel(contactPage);
        contactPanel->setStatusSink([this](const QString& text) {
            if (statusLabel) {
                statusLabel->setText(text);
            }
        });
        layout->addWidget(contactPanel);
    }
    pageStack->addWidget(contactPage);

    heatSourcePage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(heatSourcePage);
        heatSourcePanel = new NodeHeatSourcePanel(heatSourcePage);
        heatSourcePanel->setStatusSink([this](const QString& text) {
            if (statusLabel) {
                statusLabel->setText(text);
            }
        });
        layout->addWidget(heatSourcePanel);
    }
    pageStack->addWidget(heatSourcePage);

    heatPage = new QWidget(inspectorContent);
    {
        QVBoxLayout* layout = new QVBoxLayout(heatPage);
        heatSolverPanel = new NodeHeatSolverPanel(heatPage);
        heatSolverPanel->setStatusSink([this](const QString& text) {
            if (statusLabel) {
                statusLabel->setText(text);
            }
        });
        layout->addWidget(heatSolverPanel);
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

        mainTabWidget->addTab(spreadsheetTab, "Spreadsheet");
    }
    {
        QWidget* dataflowTab = new QWidget(mainTabWidget);
        QVBoxLayout* dataflowLayout = new QVBoxLayout(dataflowTab);
        dataflowRefreshButton = new QPushButton("Refresh Dataflow", dataflowTab);
        dataflowLayout->addWidget(dataflowRefreshButton);

        dataflowTextEdit = new QTextEdit(dataflowTab);
        dataflowTextEdit->setReadOnly(true);
        dataflowTextEdit->setMinimumHeight(140);
        dataflowTextEdit->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        dataflowLayout->addWidget(dataflowTextEdit, 1);
        mainTabWidget->addTab(dataflowTab, "Dataflow");
    }
    contentLayout->addWidget(mainTabWidget, 1);

    statusLabel = new QLabel(inspectorContent);
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

void NodeInspectorDialog::refreshRuntimeDebugViews() {
    if (currentNodeTypeId == nodegraphtypes::Group && groupPanel) {
        groupPanel->refreshSourceOptions();
    }
    if (currentNodeTypeId == nodegraphtypes::HeatSolve && heatSolverPanel) {
        heatSolverPanel->refreshBindingGroupOptions();
    }
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

    QString revisionSuffix;
    if (selectedSocketDebug.dataType == "geometry" ||
        selectedSocketDebug.dataType == "heat_receiver" ||
        selectedSocketDebug.dataType == "heat_source") {
        const auto revisionIt = selectedSocketDebug.metadata.find("geometry.revision");
        if (revisionIt != selectedSocketDebug.metadata.end()) {
            revisionSuffix = QString(" | Revision: %1").arg(QString::fromStdString(revisionIt->second));
        }
    }

    spreadsheetSummaryLabel->setText(
        QString("Data Type: %1 | Lineage: %2 | Attributes: %3%4")
            .arg(QString::fromStdString(selectedSocketDebug.dataType))
            .arg(QString::fromStdString(formatLineagePath(selectedSocketDebug.lineageNodeIds)))
            .arg(static_cast<int>(selectedSocketDebug.attributes.size()))
            .arg(revisionSuffix));

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
