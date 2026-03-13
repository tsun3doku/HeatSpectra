#include "NodeHeatSolverPanel.hpp"

#include "NodeHeatMaterialPresets.hpp"
#include "NodeGraphBridge.hpp"
#include "NodeGraphDebugStore.hpp"
#include "NodePanelUtils.hpp"
#include "heat/HeatContactParams.hpp"
#include "heat/HeatSystemPresets.hpp"
#include "runtime/RuntimeInterfaces.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct HeatContactBindingRow {
    NodeGraphSocketId socketId{};
    std::string socketName;
    std::string pairSummary;
    float thermalConductance = HeatContactParams{}.thermalConductance;
};

void appendReceiverModelNodeIdsFromContactPair(
    const NodeGraphState& state,
    const NodeGraphNode& contactPairNode,
    std::unordered_set<uint32_t>& seenModelNodeIds,
    std::vector<uint32_t>& outModelNodeIds) {
    if (contactPairNode.inputs.size() < 2) {
        return;
    }

    const NodeGraphEdge* receiverEdge = NodePanelUtils::findIncomingEdgeInState(
        state,
        contactPairNode.id,
        contactPairNode.inputs[1].id);
    if (!receiverEdge) {
        return;
    }

    std::unordered_set<uint32_t> visitedNodeIds;
    NodePanelUtils::collectUpstreamModelNodeIds(
        state,
        receiverEdge->fromNode,
        visitedNodeIds,
        seenModelNodeIds,
        outModelNodeIds);
}

std::vector<HeatMaterialBindingEntry> parseHeatMaterialBindingsString(const std::string& serializedBindings) {
    std::vector<HeatMaterialBindingEntry> parsedBindings;
    if (serializedBindings.empty()) {
        return parsedBindings;
    }

    std::stringstream listStream(serializedBindings);
    std::string token;
    std::unordered_set<std::string> seenGroups;
    while (std::getline(listStream, token, ';')) {
        token = NodePanelUtils::trimCopy(token);
        if (token.empty()) {
            continue;
        }

        const std::size_t separatorIndex = token.find('=');
        if (separatorIndex == std::string::npos) {
            continue;
        }

        std::string groupName = NodePanelUtils::trimCopy(token.substr(0, separatorIndex));
        std::string presetName = NodePanelUtils::trimCopy(token.substr(separatorIndex + 1));
        if (groupName.empty() || presetName.empty()) {
            continue;
        }

        std::string normalizedGroupName;
        normalizedGroupName.reserve(groupName.size());
        for (char character : groupName) {
            const unsigned char u = static_cast<unsigned char>(character);
            if (std::isspace(u) != 0) {
                continue;
            }
            normalizedGroupName.push_back(static_cast<char>(std::tolower(u)));
        }
        if (normalizedGroupName.empty() || !seenGroups.insert(normalizedGroupName).second) {
            continue;
        }

        HeatMaterialPresetId presetId = HeatMaterialPresetId::Aluminum;
        if (!tryResolveHeatPresetId(presetName, presetId)) {
            continue;
        }

        HeatMaterialBindingEntry binding{};
        binding.groupName = std::move(groupName);
        binding.presetId = presetId;
        parsedBindings.push_back(std::move(binding));
    }

    return parsedBindings;
}

std::string serializeHeatMaterialBindings(const std::vector<HeatMaterialBindingEntry>& bindings) {
    std::string serialized;
    for (const HeatMaterialBindingEntry& binding : bindings) {
        if (binding.groupName.empty()) {
            continue;
        }

        if (!serialized.empty()) {
            serialized += ";";
        }
        serialized += binding.groupName;
        serialized += "=";
        serialized += heatMaterialPresetName(binding.presetId);
    }
    return serialized;
}

std::unordered_map<uint32_t, float> parseHeatContactBindingsString(const std::string& serializedBindings) {
    std::unordered_map<uint32_t, float> parsedBindings;
    if (serializedBindings.empty()) {
        return parsedBindings;
    }

    std::stringstream listStream(serializedBindings);
    std::string token;
    while (std::getline(listStream, token, ';')) {
        token = NodePanelUtils::trimCopy(token);
        if (token.empty()) {
            continue;
        }

        const std::size_t separatorIndex = token.find('=');
        if (separatorIndex == std::string::npos) {
            continue;
        }

        uint32_t socketId = 0;
        if (!NodePanelUtils::tryParseUint32Id(token.substr(0, separatorIndex), socketId) || socketId == 0) {
            continue;
        }

        try {
            parsedBindings[socketId] = std::stof(NodePanelUtils::trimCopy(token.substr(separatorIndex + 1)));
        } catch (...) {
            continue;
        }
    }

    return parsedBindings;
}

std::string serializeHeatContactBindings(const std::vector<HeatContactBindingRow>& bindings) {
    std::string serialized;
    for (const HeatContactBindingRow& binding : bindings) {
        if (!binding.socketId.isValid()) {
            continue;
        }

        if (!serialized.empty()) {
            serialized += ";";
        }

        serialized += std::to_string(binding.socketId.value);
        serialized += "=";
        serialized += std::to_string(binding.thermalConductance);
    }

    return serialized;
}

std::optional<uint32_t> findFirstUpstreamModelNodeId(
    const NodeGraphState& state,
    NodeGraphNodeId startNodeId) {
    std::unordered_set<uint32_t> visitedNodeIds;
    std::unordered_set<uint32_t> seenModelNodeIds;
    std::vector<uint32_t> modelNodeIds;
    NodePanelUtils::collectUpstreamModelNodeIds(
        state,
        startNodeId,
        visitedNodeIds,
        seenModelNodeIds,
        modelNodeIds);

    if (modelNodeIds.empty() || modelNodeIds.front() == 0) {
        return std::nullopt;
    }

    return modelNodeIds.front();
}

std::string summarizeContactPairNode(
    const NodeGraphState& state,
    const NodeGraphNode& contactPairNode) {
    std::optional<uint32_t> emitterModelId;
    std::optional<uint32_t> receiverModelId;
    if (!contactPairNode.inputs.empty()) {
        if (const NodeGraphEdge* emitterEdge = NodePanelUtils::findIncomingEdgeInState(
                state, contactPairNode.id, contactPairNode.inputs[0].id)) {
            emitterModelId = findFirstUpstreamModelNodeId(state, emitterEdge->fromNode);
        }
    }
    if (contactPairNode.inputs.size() >= 2) {
        if (const NodeGraphEdge* receiverEdge = NodePanelUtils::findIncomingEdgeInState(
                state, contactPairNode.id, contactPairNode.inputs[1].id)) {
            receiverModelId = findFirstUpstreamModelNodeId(state, receiverEdge->fromNode);
        }
    }

    if (emitterModelId.has_value() && receiverModelId.has_value()) {
        return "Emitter " + std::to_string(*emitterModelId) + " -> Receiver " + std::to_string(*receiverModelId);
    }

    if (!contactPairNode.title.empty()) {
        return contactPairNode.title;
    }

    return "Contact Pair " + std::to_string(contactPairNode.id.value);
}

} // namespace

NodeHeatSolverPanel::NodeHeatSolverPanel(QWidget* parent)
    : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    QHBoxLayout* statusRow = new QHBoxLayout();
    statusRow->addWidget(new QLabel("Status:", this));
    heatStatusValueLabel = new QLabel("Unknown", this);
    statusRow->addWidget(heatStatusValueLabel, 1);
    layout->addLayout(statusRow);

    heatToggleButton = new QPushButton("Start", this);
    heatPauseButton = new QPushButton("Pause", this);
    heatResetButton = new QPushButton("Reset", this);
    layout->addWidget(heatToggleButton);
    layout->addWidget(heatPauseButton);
    layout->addWidget(heatResetButton);

    layout->addWidget(new QLabel("Solver Settings:", this));

    QHBoxLayout* cellSizeRow = new QHBoxLayout();
    cellSizeRow->addWidget(new QLabel("Cell Size:", this));
    heatCellSizeSpinBox = new QDoubleSpinBox(this);
    heatCellSizeSpinBox->setMinimum(0.0001);
    heatCellSizeSpinBox->setMaximum(1.0);
    heatCellSizeSpinBox->setDecimals(4);
    heatCellSizeSpinBox->setSingleStep(0.001);
    heatCellSizeSpinBox->setValue(0.005);
    cellSizeRow->addWidget(heatCellSizeSpinBox, 1);
    layout->addLayout(cellSizeRow);

    QHBoxLayout* voxelResolutionRow = new QHBoxLayout();
    voxelResolutionRow->addWidget(new QLabel("Voxel Resolution:", this));
    heatVoxelResolutionSpinBox = new QSpinBox(this);
    heatVoxelResolutionSpinBox->setMinimum(8);
    heatVoxelResolutionSpinBox->setMaximum(1024);
    heatVoxelResolutionSpinBox->setSingleStep(8);
    heatVoxelResolutionSpinBox->setValue(128);
    voxelResolutionRow->addWidget(heatVoxelResolutionSpinBox, 1);
    layout->addLayout(voxelResolutionRow);

    heatSolveSettingsApplyButton = new QPushButton("Apply Solver Settings", this);
    layout->addWidget(heatSolveSettingsApplyButton);

    layout->addWidget(new QLabel("Receiver Material Bindings:", this));

    QHBoxLayout* bindingSourceRow = new QHBoxLayout();
    bindingSourceRow->addWidget(new QLabel("Receiver Model Node ID:", this));
    heatBindingGroupComboBox = new QComboBox(this);
    heatBindingGroupComboBox->setEditable(true);
    if (QLineEdit* comboEdit = heatBindingGroupComboBox->lineEdit()) {
        comboEdit->setPlaceholderText("e.g. 12");
    }
    bindingSourceRow->addWidget(heatBindingGroupComboBox, 1);
    layout->addLayout(bindingSourceRow);

    QHBoxLayout* bindingPresetRow = new QHBoxLayout();
    bindingPresetRow->addWidget(new QLabel("Preset:", this));
    heatBindingPresetComboBox = new QComboBox(this);
    heatBindingPresetComboBox->addItem("Aluminum");
    heatBindingPresetComboBox->addItem("Copper");
    heatBindingPresetComboBox->addItem("Custom");
    heatBindingPresetComboBox->addItem("Iron");
    heatBindingPresetComboBox->addItem("Ceramic");
    bindingPresetRow->addWidget(heatBindingPresetComboBox, 1);
    layout->addLayout(bindingPresetRow);

    QHBoxLayout* bindingActionRow = new QHBoxLayout();
    heatBindingAddButton = new QPushButton("Add/Update Receiver", this);
    heatBindingRemoveButton = new QPushButton("Remove Selected", this);
    bindingActionRow->addWidget(heatBindingAddButton);
    bindingActionRow->addWidget(heatBindingRemoveButton);
    layout->addLayout(bindingActionRow);

    heatBindingsTable = new QTableWidget(this);
    heatBindingsTable->setColumnCount(2);
    heatBindingsTable->setHorizontalHeaderLabels({"Receiver Model Node ID", "Preset"});
    heatBindingsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    heatBindingsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    heatBindingsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    heatBindingsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    heatBindingsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    heatBindingsTable->setMinimumHeight(120);
    layout->addWidget(heatBindingsTable);

    heatBindingApplyButton = new QPushButton("Apply Material Bindings", this);
    layout->addWidget(heatBindingApplyButton);

    layout->addWidget(new QLabel("Contact Parameters:", this));

    heatContactBindingsTable = new QTableWidget(this);
    heatContactBindingsTable->setColumnCount(3);
    heatContactBindingsTable->setHorizontalHeaderLabels({"Socket", "Contact Pair", "Conductance"});
    heatContactBindingsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    heatContactBindingsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    heatContactBindingsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    heatContactBindingsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    heatContactBindingsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    heatContactBindingsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    heatContactBindingsTable->setMinimumHeight(120);
    layout->addWidget(heatContactBindingsTable);

    heatContactBindingApplyButton = new QPushButton("Apply Contact Parameters", this);
    layout->addWidget(heatContactBindingApplyButton);
    layout->addStretch();

    heatStatusTimer = new QTimer(this);
    heatStatusTimer->setInterval(125);

    connect(heatToggleButton, &QPushButton::clicked, this, [this]() {
        toggleHeatSystem();
    });
    connect(heatPauseButton, &QPushButton::clicked, this, [this]() {
        pauseHeatSystem();
    });
    connect(heatResetButton, &QPushButton::clicked, this, [this]() {
        resetHeatSystem();
    });
    connect(heatSolveSettingsApplyButton, &QPushButton::clicked, this, [this]() {
        applySolveSettings();
    });
    connect(heatBindingAddButton, &QPushButton::clicked, this, [this]() {
        if (!heatBindingGroupComboBox || !heatBindingPresetComboBox || !heatBindingsTable) {
            return;
        }

        const QString receiverKey = heatBindingGroupComboBox->currentText().trimmed();
        const QString presetName = heatBindingPresetComboBox->currentText().trimmed();
        if (receiverKey.isEmpty() || presetName.isEmpty()) {
            setStatus("Receiver model ID and preset are required.");
            return;
        }

        uint32_t receiverModelId = 0;
        if (!NodePanelUtils::tryParseUint32Id(receiverKey.toStdString(), receiverModelId) || receiverModelId == 0) {
            setStatus("Receiver model ID must be a positive integer.");
            return;
        }

        int existingRow = -1;
        for (int row = 0; row < heatBindingsTable->rowCount(); ++row) {
            QTableWidgetItem* groupItem = heatBindingsTable->item(row, 0);
            if (groupItem && groupItem->text().compare(receiverKey, Qt::CaseInsensitive) == 0) {
                existingRow = row;
                break;
            }
        }

        const int targetRow = (existingRow >= 0) ? existingRow : heatBindingsTable->rowCount();
        if (existingRow < 0) {
            heatBindingsTable->insertRow(targetRow);
        }
        heatBindingsTable->setItem(targetRow, 0, new QTableWidgetItem(receiverKey));
        heatBindingsTable->setItem(targetRow, 1, new QTableWidgetItem(presetName));
    });
    connect(heatBindingRemoveButton, &QPushButton::clicked, this, [this]() {
        if (!heatBindingsTable) {
            return;
        }
        const int row = heatBindingsTable->currentRow();
        if (row >= 0) {
            heatBindingsTable->removeRow(row);
        }
    });
    connect(heatBindingApplyButton, &QPushButton::clicked, this, [this]() {
        applyMaterialBindings();
    });
    connect(heatContactBindingApplyButton, &QPushButton::clicked, this, [this]() {
        applyContactBindings();
    });
    connect(heatStatusTimer, &QTimer::timeout, this, [this]() {
        updateHeatStatus();
    });
}

void NodeHeatSolverPanel::bind(NodeGraphBridge* nodeGraphBridgePtr, const RuntimeQuery* runtimeQueryPtr) {
    nodeGraphBridge = nodeGraphBridgePtr;
    runtimeQuery = runtimeQueryPtr;
    if (currentNodeId.isValid()) {
        setNode(currentNodeId);
    }
}

void NodeHeatSolverPanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        setStatus("Failed to read HeatSolve node.");
        return;
    }

    refreshBindingGroupOptions();
    refreshContactBindingRows(node);
    if (heatCellSizeSpinBox) {
        heatCellSizeSpinBox->setValue(NodePanelUtils::readFloatParam(
            node,
            nodegraphparams::heatsolve::CellSize,
            0.005));
    }
    if (heatVoxelResolutionSpinBox) {
        heatVoxelResolutionSpinBox->setValue(NodePanelUtils::readIntParam(
            node,
            nodegraphparams::heatsolve::VoxelResolution,
            128));
    }

    const std::vector<HeatMaterialBindingEntry> bindings =
        parseHeatMaterialBindingsString(NodePanelUtils::readStringParam(node, nodegraphparams::heatsolve::MaterialBindings));
    if (heatBindingsTable) {
        heatBindingsTable->setRowCount(0);
        for (const HeatMaterialBindingEntry& binding : bindings) {
            const int row = heatBindingsTable->rowCount();
            heatBindingsTable->insertRow(row);
            heatBindingsTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(binding.groupName)));
            heatBindingsTable->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(heatMaterialPresetName(binding.presetId))));
        }
    }

    updateHeatStatus();
}

void NodeHeatSolverPanel::refreshBindingGroupOptions() {
    if (!heatBindingGroupComboBox || !currentNodeId.isValid()) {
        return;
    }

    std::vector<std::string> receiverModelIds;
    std::unordered_set<std::string> seenReceiverModelIds;

    NodeGraphRuntimeNodeDebugInfo debugInfo{};
    if (NodeGraphDebugStore::tryGetLatestNodeDebugInfo(currentNodeId, debugInfo)) {
        for (const NodeGraphRuntimeSocketDebugInfo& inputSocket : debugInfo.inputs) {
            if (!inputSocket.hasValue || inputSocket.dataType != "heat_receiver") {
                continue;
            }

            const auto modelIdIt = inputSocket.metadata.find("geometry.model_id");
            if (modelIdIt == inputSocket.metadata.end()) {
                continue;
            }

            uint32_t modelId = 0;
            if (!NodePanelUtils::tryParseUint32Id(modelIdIt->second, modelId) || modelId == 0) {
                continue;
            }

            const std::string modelIdText = std::to_string(modelId);
            if (seenReceiverModelIds.insert(modelIdText).second) {
                receiverModelIds.push_back(modelIdText);
            }
        }
    }

    if (nodeGraphBridge) {
        const NodeGraphState state = nodeGraphBridge->state();
        const NodeGraphNode* currentNode = NodePanelUtils::findNodeInState(state, currentNodeId);
        if (currentNode) {
            std::unordered_set<uint32_t> seenModelNodeIds;
            std::vector<uint32_t> modelNodeIds;
            for (const NodeGraphSocket& inputSocket : currentNode->inputs) {
                const NodeGraphEdge* inputEdge = NodePanelUtils::findIncomingEdgeInState(state, currentNode->id, inputSocket.id);
                if (!inputEdge) {
                    continue;
                }

                const NodeGraphNode* upstreamNode = NodePanelUtils::findNodeInState(state, inputEdge->fromNode);
                if (!upstreamNode) {
                    continue;
                }

                const std::string upstreamType = canonicalNodeTypeId(upstreamNode->typeId);
                if (upstreamType == nodegraphtypes::ContactPair) {
                    appendReceiverModelNodeIdsFromContactPair(
                        state,
                        *upstreamNode,
                        seenModelNodeIds,
                        modelNodeIds);
                    continue;
                }

                if (upstreamType != nodegraphtypes::HeatReceiver) {
                    continue;
                }

                std::unordered_set<uint32_t> visitedNodeIds;
                NodePanelUtils::collectUpstreamModelNodeIds(
                    state,
                    upstreamNode->id,
                    visitedNodeIds,
                    seenModelNodeIds,
                    modelNodeIds);
            }

            for (uint32_t modelNodeId : modelNodeIds) {
                if (modelNodeId == 0) {
                    continue;
                }
                const std::string modelNodeIdText = std::to_string(modelNodeId);
                if (seenReceiverModelIds.insert(modelNodeIdText).second) {
                    receiverModelIds.push_back(modelNodeIdText);
                }
            }
        }
    }

    if (heatBindingsTable) {
        for (int row = 0; row < heatBindingsTable->rowCount(); ++row) {
            QTableWidgetItem* groupItem = heatBindingsTable->item(row, 0);
            if (!groupItem) {
                continue;
            }
            uint32_t modelId = 0;
            if (!NodePanelUtils::tryParseUint32Id(groupItem->text().toStdString(), modelId) || modelId == 0) {
                continue;
            }

            const std::string modelIdText = std::to_string(modelId);
            if (seenReceiverModelIds.insert(modelIdText).second) {
                receiverModelIds.push_back(modelIdText);
            }
        }
    }

    const std::string currentGroupText = heatBindingGroupComboBox->currentText().trimmed().toStdString();
    uint32_t currentModelId = 0;
    if (NodePanelUtils::tryParseUint32Id(currentGroupText, currentModelId) && currentModelId != 0) {
        const std::string currentModelText = std::to_string(currentModelId);
        if (seenReceiverModelIds.insert(currentModelText).second) {
            receiverModelIds.push_back(currentModelText);
        }
    }

    std::sort(receiverModelIds.begin(), receiverModelIds.end(), [](const std::string& lhs, const std::string& rhs) {
        uint32_t leftId = 0;
        uint32_t rightId = 0;
        const bool leftOk = NodePanelUtils::tryParseUint32Id(lhs, leftId);
        const bool rightOk = NodePanelUtils::tryParseUint32Id(rhs, rightId);
        if (leftOk && rightOk) {
            return leftId < rightId;
        }
        return lhs < rhs;
    });

    heatBindingGroupComboBox->blockSignals(true);
    heatBindingGroupComboBox->clear();
    for (const std::string& receiverModelIdText : receiverModelIds) {
        heatBindingGroupComboBox->addItem(QString::fromStdString(receiverModelIdText));
    }
    if (!currentGroupText.empty()) {
        const int index = heatBindingGroupComboBox->findText(QString::fromStdString(currentGroupText));
        if (index >= 0) {
            heatBindingGroupComboBox->setCurrentIndex(index);
        } else {
            heatBindingGroupComboBox->setEditText(QString::fromStdString(currentGroupText));
        }
    } else if (heatBindingGroupComboBox->count() > 0) {
        heatBindingGroupComboBox->setCurrentIndex(0);
    }
    heatBindingGroupComboBox->blockSignals(false);
}

void NodeHeatSolverPanel::refreshContactBindingRows(const NodeGraphNode& node) {
    if (!heatContactBindingsTable) {
        return;
    }

    heatContactBindingsTable->setRowCount(0);
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        return;
    }

    const std::unordered_map<uint32_t, float> serializedBindings =
        parseHeatContactBindingsString(NodePanelUtils::readStringParam(node, nodegraphparams::heatsolve::ContactBindings));
    const NodeGraphState state = nodeGraphBridge->state();

    for (const NodeGraphSocket& inputSocket : node.inputs) {
        const NodeGraphEdge* inputEdge = NodePanelUtils::findIncomingEdgeInState(state, node.id, inputSocket.id);
        if (!inputEdge) {
            continue;
        }

        const NodeGraphNode* upstreamNode = NodePanelUtils::findNodeInState(state, inputEdge->fromNode);
        if (!upstreamNode || canonicalNodeTypeId(upstreamNode->typeId) != nodegraphtypes::ContactPair) {
            continue;
        }

        const int row = heatContactBindingsTable->rowCount();
        heatContactBindingsTable->insertRow(row);

        QTableWidgetItem* socketItem = new QTableWidgetItem(QString::fromStdString(inputSocket.name));
        socketItem->setData(Qt::UserRole, static_cast<qulonglong>(inputSocket.id.value));
        socketItem->setFlags(socketItem->flags() & ~Qt::ItemIsEditable);
        heatContactBindingsTable->setItem(row, 0, socketItem);

        QTableWidgetItem* pairItem = new QTableWidgetItem(QString::fromStdString(summarizeContactPairNode(state, *upstreamNode)));
        pairItem->setFlags(pairItem->flags() & ~Qt::ItemIsEditable);
        heatContactBindingsTable->setItem(row, 1, pairItem);

        const auto bindingIt = serializedBindings.find(inputSocket.id.value);
        const float thermalConductance =
            (bindingIt != serializedBindings.end()) ? bindingIt->second : HeatContactParams{}.thermalConductance;

        QLineEdit* conductanceEdit = new QLineEdit(QString::number(thermalConductance), heatContactBindingsTable);
        conductanceEdit->setPlaceholderText(QString::number(HeatContactParams{}.thermalConductance));
        heatContactBindingsTable->setCellWidget(row, 2, conductanceEdit);
    }
}

void NodeHeatSolverPanel::updateHeatStatus() {
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

    const bool desiredEnabled = NodePanelUtils::readBoolParam(node, nodegraphparams::heatsolve::Enabled, false);
    const bool desiredPaused = NodePanelUtils::readBoolParam(node, nodegraphparams::heatsolve::Paused, false);

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

void NodeHeatSolverPanel::setStatusSink(std::function<void(const QString&)> statusSinkFn) {
    statusSink = std::move(statusSinkFn);
}

void NodeHeatSolverPanel::startStatusTimer() {
    if (heatStatusTimer && runtimeQuery) {
        heatStatusTimer->start(125);
    }
}

void NodeHeatSolverPanel::stopStatusTimer() {
    if (heatStatusTimer) {
        heatStatusTimer->stop();
    }
}

void NodeHeatSolverPanel::toggleHeatSystem() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot control heat system for this node.");
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        setStatus("Failed to read node state.");
        return;
    }

    const bool enable = !NodePanelUtils::readBoolParam(node, nodegraphparams::heatsolve::Enabled, false);
    if (!NodePanelUtils::writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::heatsolve::Enabled, enable)) {
        setStatus("Failed to update heat node state.");
        return;
    }
    if (!enable) {
        NodePanelUtils::writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::heatsolve::Paused, false);
    }

    updateHeatStatus();
    setStatus(enable ? "Heat solve enabled through node graph." : "Heat solve disabled through node graph.");
}

void NodeHeatSolverPanel::pauseHeatSystem() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot control heat system for this node.");
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        setStatus("Failed to read node state.");
        return;
    }

    const bool pause = !NodePanelUtils::readBoolParam(node, nodegraphparams::heatsolve::Paused, false);
    if (!NodePanelUtils::writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::heatsolve::Paused, pause)) {
        setStatus("Failed to update heat pause state.");
        return;
    }

    updateHeatStatus();
    setStatus(pause ? "Heat solve pause requested." : "Heat solve resume requested.");
}

void NodeHeatSolverPanel::resetHeatSystem() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot control heat system for this node.");
        return;
    }

    if (!NodePanelUtils::writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::heatsolve::Paused, false) ||
        !NodePanelUtils::writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::heatsolve::ResetRequested, true)) {
        setStatus("Failed to request heat reset.");
        return;
    }

    updateHeatStatus();
    setStatus("Heat solve reset requested.");
}

void NodeHeatSolverPanel::applySolveSettings() {
    if (!nodeGraphBridge || !currentNodeId.isValid() || !heatCellSizeSpinBox || !heatVoxelResolutionSpinBox) {
        setStatus("Cannot apply solver settings for this node.");
        return;
    }

    if (!NodePanelUtils::writeFloatParam(
            nodeGraphBridge,
            currentNodeId,
            nodegraphparams::heatsolve::CellSize,
            heatCellSizeSpinBox->value()) ||
        !NodePanelUtils::writeIntParam(
            nodeGraphBridge,
            currentNodeId,
            nodegraphparams::heatsolve::VoxelResolution,
            heatVoxelResolutionSpinBox->value())) {
        setStatus("Failed to update solver settings.");
        return;
    }

    setStatus("Solver settings applied.");
}

void NodeHeatSolverPanel::applyMaterialBindings() {
    if (!nodeGraphBridge || !currentNodeId.isValid() || !heatBindingsTable) {
        setStatus("Cannot apply material bindings for this node.");
        return;
    }

    std::vector<HeatMaterialBindingEntry> bindings;
    bindings.reserve(static_cast<std::size_t>(heatBindingsTable->rowCount()));
    bool fallbackAssigned = false;
    uint32_t validReceiverBindingCount = 0;
    for (int row = 0; row < heatBindingsTable->rowCount(); ++row) {
        QTableWidgetItem* groupItem = heatBindingsTable->item(row, 0);
        QTableWidgetItem* presetItem = heatBindingsTable->item(row, 1);
        if (!groupItem || !presetItem) {
            continue;
        }

        const std::string receiverKey = NodePanelUtils::trimCopy(groupItem->text().toStdString());
        const std::string presetName = NodePanelUtils::trimCopy(presetItem->text().toStdString());
        if (receiverKey.empty() || presetName.empty()) {
            continue;
        }

        HeatMaterialPresetId presetId = HeatMaterialPresetId::Aluminum;
        if (!tryResolveHeatPresetId(presetName, presetId)) {
            continue;
        }

        uint32_t receiverModelId = 0;
        const bool validReceiverModelId = NodePanelUtils::tryParseUint32Id(receiverKey, receiverModelId) && receiverModelId != 0;
        if (!validReceiverModelId && fallbackAssigned) {
            continue;
        }

        HeatMaterialBindingEntry binding{};
        binding.groupName = validReceiverModelId ? std::to_string(receiverModelId) : receiverKey;
        binding.presetId = presetId;
        bindings.push_back(std::move(binding));
        if (!validReceiverModelId) {
            fallbackAssigned = true;
        } else {
            ++validReceiverBindingCount;
        }
    }

    if (bindings.empty()) {
        setStatus("No valid receiver material bindings to apply.");
        return;
    }

    const std::string serializedBindings = serializeHeatMaterialBindings(bindings);
    if (!NodePanelUtils::writeStringParam(
            nodeGraphBridge,
            currentNodeId,
            nodegraphparams::heatsolve::MaterialBindings,
            serializedBindings)) {
        setStatus("Failed to update material bindings.");
        return;
    }

    if (validReceiverBindingCount == 0 && fallbackAssigned) {
        setStatus("Applied fallback material binding only (no valid receiver IDs found).");
    } else {
        setStatus("Receiver material bindings applied.");
    }
}

void NodeHeatSolverPanel::applyContactBindings() {
    if (!nodeGraphBridge || !currentNodeId.isValid() || !heatContactBindingsTable) {
        setStatus("Cannot apply contact parameters for this node.");
        return;
    }

    std::vector<HeatContactBindingRow> bindings;
    bindings.reserve(static_cast<std::size_t>(heatContactBindingsTable->rowCount()));
    for (int row = 0; row < heatContactBindingsTable->rowCount(); ++row) {
        QTableWidgetItem* socketItem = heatContactBindingsTable->item(row, 0);
        QLineEdit* conductanceEdit = qobject_cast<QLineEdit*>(heatContactBindingsTable->cellWidget(row, 2));
        if (!socketItem || !conductanceEdit) {
            continue;
        }

        bool ok = false;
        const float thermalConductance = conductanceEdit->text().trimmed().toFloat(&ok);
        if (!ok || thermalConductance <= 0.0f) {
            setStatus("Contact conductance must be a positive number.");
            return;
        }

        const qulonglong socketValue = socketItem->data(Qt::UserRole).toULongLong(&ok);
        if (!ok || socketValue == 0) {
            continue;
        }

        HeatContactBindingRow binding{};
        binding.socketId.value = static_cast<uint32_t>(socketValue);
        binding.thermalConductance = thermalConductance;
        bindings.push_back(binding);
    }

    const std::string serializedBindings = serializeHeatContactBindings(bindings);
    if (!NodePanelUtils::writeStringParam(
            nodeGraphBridge,
            currentNodeId,
            nodegraphparams::heatsolve::ContactBindings,
            serializedBindings)) {
        setStatus("Failed to update contact parameters.");
        return;
    }

    setStatus("Contact parameters applied.");
}

void NodeHeatSolverPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
