#include "NodeGraphUiModel.hpp"

#include "nodegraph/NodeGraphUtils.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeHeatMaterialPresets.hpp"
#include "serial/SerialPort.hpp"

#include <algorithm>
#include <unordered_set>

static QString nodeCategoryName(NodeGraphNodeCategory category) {
    switch (category) {
    case NodeGraphNodeCategory::Geometry: return QStringLiteral("Geometry");
    case NodeGraphNodeCategory::Meshing:  return QStringLiteral("Meshing");
    case NodeGraphNodeCategory::System:   return QStringLiteral("System");
    case NodeGraphNodeCategory::Misc:     return QStringLiteral("Misc");
    }
    return QStringLiteral("Misc");
}

static int nodeCategoryOrder(NodeGraphNodeCategory category) {
    switch (category) {
    case NodeGraphNodeCategory::Geometry: return 0;
    case NodeGraphNodeCategory::Meshing:  return 1;
    case NodeGraphNodeCategory::System:   return 2;
    case NodeGraphNodeCategory::Misc:     return 3;
    }
    return 3;
}

NodeGraphUiModel::NodeGraphUiModel(QObject* parent)
    : QAbstractListModel(parent) {
    refreshSerialPorts();
}

int NodeGraphUiModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(nodes.size());
}

QVariant NodeGraphUiModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(nodes.size())) {
        return {};
    }

    const NodeGraphNode& node = nodes[static_cast<std::size_t>(index.row())];
    switch (role) {
    case NodeIdRole: return static_cast<int>(node.id.value);
    case TitleRole: return QString::fromStdString(node.title);
    case TypeRole: return QString::fromStdString(node.typeId);
    case NodeXRole: return node.x;
    case NodeYRole: return node.y;
    case InputsRole: return socketList(node.id, node.inputs);
    case OutputsRole: return socketList(node.id, node.outputs);
    case DisplayEnabledRole: return node.state.isPrimaryDisplay();
    case FrozenRole: return node.state.isFrozen();
    case SelectedRole:
        return std::find(selectedIds.begin(), selectedIds.end(), node.id.value) != selectedIds.end();
    default: return {};
    }
}

QHash<int, QByteArray> NodeGraphUiModel::roleNames() const {
    return {
        {NodeIdRole, "nodeId"},
        {TitleRole, "title"},
        {TypeRole, "typeId"},
        {NodeXRole, "nodeX"},
        {NodeYRole, "nodeY"},
        {InputsRole, "inputs"},
        {OutputsRole, "outputs"},
        {DisplayEnabledRole, "displayEnabled"},
        {FrozenRole, "frozen"},
        {SelectedRole, "selected"}
    };
}

QVariantList NodeGraphUiModel::edges() const {
    return edgeItems;
}

QVariantList NodeGraphUiModel::nodeCategories() const {
    return categoryItems;
}

int NodeGraphUiModel::selectedNodeId() const {
    return selectedId;
}

bool NodeGraphUiModel::isNodeSelected(int nodeId) const {
    if (nodeId <= 0) return false;
    return std::find(selectedIds.begin(), selectedIds.end(), static_cast<uint32_t>(nodeId)) != selectedIds.end();
}

QString NodeGraphUiModel::selectedNodeTitle() const {
    return selectedTitle;
}

QString NodeGraphUiModel::selectedNodeType() const {
    return selectedType;
}

QString NodeGraphUiModel::selectedNodeDescription() const {
    if (selectedType == QStringLiteral("model")) return QStringLiteral("Choose a 3D model");
    if (selectedType == QStringLiteral("transform")) return QStringLiteral("Adjust placement, orientation and scale of a 3D model");
    if (selectedType == QStringLiteral("group")) return QStringLiteral("Target source groups and write grouped mesh selections");
    if (selectedType == QStringLiteral("remesh")) return QStringLiteral("Intrinsically remesh an underlying 3D model while preserving its shape");
    if (selectedType == QStringLiteral("voronoi")) return QStringLiteral("Generate a volumetric Voronoi domain");
    if (selectedType == QStringLiteral("heat_model")) return QStringLiteral("Assign thermal properties and boundary conditions to a 3D model");
    if (selectedType == QStringLiteral("heat_solve")) return QStringLiteral("Simulate the transient transfer of heat between 3D geometry");
    if (selectedType == QStringLiteral("serial_temperature")) return QStringLiteral("Read a live Celsius value from a serial temperature sensor");
    if (selectedType == QStringLiteral("points")) return QStringLiteral("Generate a point cloud domain");
    if (selectedType == QStringLiteral("mesh_points")) return QStringLiteral("Extract mesh surface vertices as a point cloud");
    return selectedId > 0 ? QStringLiteral("Inspect this node's parameters and generated data") :
                            QStringLiteral("Select a node in the graph to inspect parameters and dataflow");
}

QVariantList NodeGraphUiModel::selectedNodeParameters() const {
    return selectedParameters;
}

QVariantList NodeGraphUiModel::serialPorts() const {
    return serialPortItems;
}

void NodeGraphUiModel::initializeGraph(
    const NodeGraphState& state,
    const std::vector<NodeTypeDefinition>& definitions) {
    typeDefinitions = definitions;
    graphState = state;
    rebuildNodeCategories();
    revision = UINT64_MAX;
    refresh();
}

void NodeGraphUiModel::replaceGraphState(const NodeGraphState& state) {
    graphState = state;
    revision = UINT64_MAX;
    refresh();
}

void NodeGraphUiModel::applyDelta(const NodeGraphDelta& delta) {
    const bool applied = applyNodeGraphDelta(graphState, delta);
    Q_ASSERT(applied);
    if (!applied) return;
    refresh();
}

void NodeGraphUiModel::handleNodesPasted(const std::vector<NodeGraphNodeId>& nodeIds) {
    selectedIds.clear();
    for (NodeGraphNodeId id : nodeIds) selectedIds.push_back(id.value);
    notifySelectionChanged(true);
}

void NodeGraphUiModel::setRuntimeSelectedNodeId(int nodeId) {
    const uint32_t runtimeNodeId = nodeId > 0 ? static_cast<uint32_t>(nodeId) : 0;
    if ((runtimeNodeId == 0 && selectedIds.empty()) ||
        (runtimeNodeId != 0 && selectedIds.size() == 1 && selectedIds.front() == runtimeNodeId)) {
        return;
    }
    selectedIds.clear();
    if (runtimeNodeId != 0) {
        selectedIds.push_back(runtimeNodeId);
    }
    notifySelectionChanged(false);
}

void NodeGraphUiModel::refresh() {
    const NodeGraphState& updatedState = graphState;
    const uint64_t updatedRevision = graphState.revision;
    if (updatedRevision == revision) {
        return;
    }

    std::vector<NodeGraphNode> updatedNodes;
    updatedNodes.reserve(updatedState.nodes.size());
    for (const auto& entry : updatedState.nodes) {
        updatedNodes.push_back(entry.second);
    }
    std::sort(updatedNodes.begin(), updatedNodes.end(), [](const NodeGraphNode& lhs, const NodeGraphNode& rhs) {
        return lhs.id.value < rhs.id.value;
    });

    QVariantList updatedEdges;
    updatedEdges.reserve(static_cast<qsizetype>(updatedState.edges.size()));
    for (const auto& entry : updatedState.edges) {
        const NodeGraphEdge& edge = entry.second;
        QVariantMap item;
        item.insert(QStringLiteral("edgeId"), static_cast<int>(edge.id.value));
        item.insert(QStringLiteral("fromNode"), static_cast<int>(edge.fromNode.value));
        item.insert(QStringLiteral("fromSocket"), static_cast<int>(edge.fromSocket.value));
        item.insert(QStringLiteral("toNode"), static_cast<int>(edge.toNode.value));
        item.insert(QStringLiteral("toSocket"), static_cast<int>(edge.toSocket.value));
        item.insert(QStringLiteral("valueType"), static_cast<int>(
            socketType(updatedState, edge.fromNode, edge.fromSocket)));
        updatedEdges.push_back(item);
    }

    beginResetModel();
    nodes = updatedNodes;
    edgeItems = updatedEdges;
    revision = updatedRevision;
    endResetModel();
    selectedIds.erase(std::remove_if(selectedIds.begin(), selectedIds.end(), [this](uint32_t nodeId) {
        return graphState.nodes.find(nodeId) == graphState.nodes.end();
    }), selectedIds.end());
    if (std::find(selectedIds.begin(), selectedIds.end(), static_cast<uint32_t>(selectedId)) == selectedIds.end()) {
        selectedId = selectedIds.empty() ? 0 : static_cast<int>(selectedIds.back());
    }
    rebuildSelectedNode();
    emit edgesChanged();
}

void NodeGraphUiModel::moveNode(int nodeId, qreal x, qreal y) {
    if (nodeId > 0) emit moveNodeRequested(NodeGraphNodeId{static_cast<uint32_t>(nodeId)}, static_cast<float>(x), static_cast<float>(y));
}

void NodeGraphUiModel::removeNode(int nodeId) {
    if (nodeId > 0) {
        selectedIds.erase(std::remove(selectedIds.begin(), selectedIds.end(), static_cast<uint32_t>(nodeId)), selectedIds.end());
        notifySelectionChanged(true);
        emit removeNodeRequested(NodeGraphNodeId{static_cast<uint32_t>(nodeId)});
    }
}

void NodeGraphUiModel::removeSelectedNodes() {
    const std::vector<uint32_t> ids = selectedIds;
    for (uint32_t nodeId : ids) {
        emit removeNodeRequested(NodeGraphNodeId{nodeId});
    }
    selectedIds.clear();
    notifySelectionChanged(true);
}

void NodeGraphUiModel::resetToDefaultGraph() {
    emit resetRequested();
}

int NodeGraphUiModel::addNode(const QString& typeId, qreal x, qreal y) {
    emit addNodeRequested(typeId, static_cast<float>(x), static_cast<float>(y));
    return 0;
}

void NodeGraphUiModel::toggleNodeDisplay(int nodeId) {
    if (nodeId > 0) emit toggleNodeDisplayRequested(NodeGraphNodeId{static_cast<uint32_t>(nodeId)});
}

void NodeGraphUiModel::toggleNodeFrozen(int nodeId) {
    if (nodeId > 0) emit toggleNodeFrozenRequested(NodeGraphNodeId{static_cast<uint32_t>(nodeId)});
}

void NodeGraphUiModel::setSelectedNodeId(int nodeId) {
    setNodeSelected(nodeId, false);
}

void NodeGraphUiModel::setNodeSelected(int nodeId, bool additive) {
    const uint32_t id = nodeId > 0 ? static_cast<uint32_t>(nodeId) : 0;
    if (!additive) {
        selectedIds.clear();
        if (id != 0) {
            selectedIds.push_back(id);
        }
    } else if (id != 0) {
        const auto it = std::find(selectedIds.begin(), selectedIds.end(), id);
        if (it == selectedIds.end()) {
            selectedIds.push_back(id);
        } else {
            selectedIds.erase(it);
        }
    }
    if (id != 0 && std::find(selectedIds.begin(), selectedIds.end(), id) != selectedIds.end()) {
        selectedId = static_cast<int>(id);
    } else if (selectedId == static_cast<int>(id)) {
        selectedId = selectedIds.empty() ? 0 : static_cast<int>(selectedIds.back());
    }
    notifySelectionChanged(true);
}

void NodeGraphUiModel::copySelectedNodes() {
    copiedNodes.clear();
    copiedEdges.clear();
    if (selectedIds.empty()) {
        return;
    }

    const std::unordered_set<uint32_t> selectedSet(selectedIds.begin(), selectedIds.end());
    for (uint32_t nodeId : selectedIds) {
        const auto nodeIt = graphState.nodes.find(nodeId);
        if (nodeIt == graphState.nodes.end()) {
            continue;
        }
        const NodeGraphNode& node = nodeIt->second;
        NodeGraphEditor::CopiedNode copy{};
        copy.sourceNodeId = node.id;
        copy.typeId = node.typeId;
        copy.title = node.title;
        copy.x = node.x;
        copy.y = node.y;
        copy.parameters = node.parameters;
        for (const NodeGraphSocket& socket : node.outputs) {
            copy.outputSocketIds.push_back(socket.id);
        }
        copiedNodes.push_back(copy);
    }

    for (const auto& entry : graphState.edges) {
        const NodeGraphEdge& edge = entry.second;
        if (selectedSet.find(edge.fromNode.value) == selectedSet.end() ||
            selectedSet.find(edge.toNode.value) == selectedSet.end()) {
            continue;
        }
        copiedEdges.push_back({edge.fromNode, edge.fromSocket, edge.toNode, edge.toSocket});
    }
}

void NodeGraphUiModel::pasteCopiedNodes() {
    if (copiedNodes.empty()) return;
    GraphPastePayload payload{};
    payload.nodes = copiedNodes;
    payload.edges = copiedEdges;
    emit pasteRequested(payload);
}

bool NodeGraphUiModel::connectSockets(int fromNode, int fromSocket, int toNode, int toSocket) {
    if (fromNode <= 0 || fromSocket <= 0 || toNode <= 0 || toSocket <= 0) {
        return false;
    }
    emit connectSocketsRequested(
        NodeGraphNodeId{static_cast<uint32_t>(fromNode)},
        NodeGraphSocketId{static_cast<uint32_t>(fromSocket)},
        NodeGraphNodeId{static_cast<uint32_t>(toNode)},
        NodeGraphSocketId{static_cast<uint32_t>(toSocket)});
    return true;
}

void NodeGraphUiModel::removeConnection(int edgeId) {
    if (edgeId > 0) emit removeConnectionRequested(NodeGraphEdgeId{static_cast<uint32_t>(edgeId)});
}

void NodeGraphUiModel::setParameterValue(int parameterId, const QVariant& value) {
    if (selectedId <= 0 || parameterId <= 0) {
        return;
    }

    const NodeGraphNodeId nodeId{static_cast<uint32_t>(selectedId)};
    const NodeGraphNode* selectedNode = graphState.node(nodeId);
    if (!selectedNode) return;

    for (NodeGraphParamValue parameter : selectedNode->parameters) {
        if (parameter.id != static_cast<uint32_t>(parameterId)) {
            continue;
        }
        switch (parameter.type) {
        case NodeGraphParamType::Float: parameter.floatValue = value.toDouble(); break;
        case NodeGraphParamType::Int: parameter.intValue = value.toLongLong(); break;
        case NodeGraphParamType::Bool: parameter.boolValue = value.toBool(); break;
        case NodeGraphParamType::String: parameter.stringValue = value.toString().toStdString(); break;
        case NodeGraphParamType::Enum: parameter.enumValue = value.toString().toStdString(); break;
        default: return;
        }
        emit setParameterRequested(nodeId, parameter);
        return;
    }
}

void NodeGraphUiModel::setHeatMaterialPreset(const QString& presetName) {
    if (selectedType != QStringLiteral("heat_model")) return;
    HeatMaterialPresetId presetId = HeatMaterialPresetId::Custom;
    if (presetName == QStringLiteral("Aluminum")) presetId = HeatMaterialPresetId::Aluminum;
    else if (presetName == QStringLiteral("Copper")) presetId = HeatMaterialPresetId::Copper;
    else if (presetName == QStringLiteral("Iron")) presetId = HeatMaterialPresetId::Iron;
    else if (presetName == QStringLiteral("Ceramic")) presetId = HeatMaterialPresetId::Ceramic;

    setParameterValue(nodegraphparams::heatmodel::MaterialPreset, presetName);
    if (presetId == HeatMaterialPresetId::Custom) return;
    const HeatMaterialPreset& preset = heatMaterialPresetById(presetId);
    setParameterValue(nodegraphparams::heatmodel::Density, preset.density);
    setParameterValue(nodegraphparams::heatmodel::SpecificHeat, preset.specificHeat);
    setParameterValue(nodegraphparams::heatmodel::Conductivity, preset.conductivity);
}

void NodeGraphUiModel::refreshSerialPorts() {
    QVariantList updated;
    for (const SerialPortInfo& port : SerialPort::enumeratePorts()) {
        QVariantMap item;
        item.insert(QStringLiteral("text"), QString::fromStdString(port.displayName));
        item.insert(QStringLiteral("value"), QString::fromStdString(port.portName));
        updated.push_back(item);
    }
    if (updated != serialPortItems) {
        serialPortItems = updated;
        emit serialPortsChanged();
    }
}

QVariantList NodeGraphUiModel::socketList(NodeGraphNodeId nodeId, const std::vector<NodeGraphSocket>& sockets) const {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(sockets.size()));
    for (const NodeGraphSocket& socket : sockets) {
        QVariantMap item;
        item.insert(QStringLiteral("socketId"), static_cast<int>(socket.id.value));
        item.insert(QStringLiteral("name"), QString::fromStdString(socket.name));
        item.insert(QStringLiteral("valueType"), static_cast<int>(socketType(graphState, nodeId, socket.id)));
        QVariantList acceptedValueTypes;
        acceptedValueTypes.reserve(static_cast<qsizetype>(socket.acceptedValueTypes.size()));
        for (NodeGraphValueType acceptedType : socket.acceptedValueTypes) {
            acceptedValueTypes.push_back(static_cast<int>(acceptedType));
        }
        item.insert(QStringLiteral("acceptedValueTypes"), acceptedValueTypes);
        result.push_back(item);
    }
    return result;
}

void NodeGraphUiModel::rebuildNodeCategories() {
    categoryItems.clear();
    std::vector<NodeTypeDefinition> definitions = typeDefinitions;
    std::sort(definitions.begin(), definitions.end(), [](const NodeTypeDefinition& lhs, const NodeTypeDefinition& rhs) {
        const int lhsCategory = nodeCategoryOrder(lhs.category);
        const int rhsCategory = nodeCategoryOrder(rhs.category);
        return lhsCategory == rhsCategory ? lhs.displayName < rhs.displayName : lhsCategory < rhsCategory;
    });

    for (int order = 0; order <= 3; ++order) {
        QVariantList types;
        NodeGraphNodeCategory category = NodeGraphNodeCategory::Misc;
        for (const NodeTypeDefinition& definition : definitions) {
            if (nodeCategoryOrder(definition.category) != order) {
                continue;
            }
            category = definition.category;
            QVariantMap type;
            type.insert(QStringLiteral("typeId"), QString::fromStdString(definition.id));
            type.insert(QStringLiteral("name"), QString::fromStdString(definition.displayName));
            types.push_back(type);
        }
        if (!types.empty()) {
            QVariantMap categoryItem;
            categoryItem.insert(QStringLiteral("name"), nodeCategoryName(category));
            categoryItem.insert(QStringLiteral("types"), types);
            categoryItems.push_back(categoryItem);
        }
    }
    emit nodeCategoriesChanged();
}

void NodeGraphUiModel::notifySelectionChanged(bool requestRuntimeSelection) {
    if (std::find(selectedIds.begin(), selectedIds.end(), static_cast<uint32_t>(selectedId)) == selectedIds.end()) {
        selectedId = selectedIds.empty() ? 0 : static_cast<int>(selectedIds.back());
    }
    rebuildSelectedNode();
    if (!nodes.empty()) {
        emit dataChanged(index(0, 0), index(static_cast<int>(nodes.size()) - 1, 0), {SelectedRole});
    }
    if (requestRuntimeSelection) {
        emit selectionRequested(selectedId);
    }
}

void NodeGraphUiModel::rebuildSelectedNode() {
    selectedTitle.clear();
    selectedType.clear();
    selectedParameters.clear();

    if (selectedId > 0) {
        const NodeGraphNode* selectedNode = graphState.node(NodeGraphNodeId{static_cast<uint32_t>(selectedId)});
        if (selectedNode) {
            const NodeGraphNode& node = *selectedNode;
            selectedTitle = QString::fromStdString(node.title);
            selectedType = QString::fromStdString(node.typeId);
            const NodeTypeDefinition* definition = nullptr;
            for (const NodeTypeDefinition& candidate : typeDefinitions) {
                if (candidate.id == node.typeId) { definition = &candidate; break; }
            }
            for (const NodeGraphParamValue& parameter : node.parameters) {
                QVariantMap item;
                item.insert(QStringLiteral("id"), static_cast<int>(parameter.id));
                item.insert(QStringLiteral("type"), static_cast<int>(parameter.type));
                QString name = QStringLiteral("Parameter %1").arg(parameter.id);
                QVariant value;
                QStringList options;
                if (definition) {
                    for (const NodeGraphParamDefinition& parameterDefinition : definition->parameters) {
                        if (parameterDefinition.id == parameter.id) {
                            name = QString::fromStdString(parameterDefinition.name);
                            for (const std::string& option : parameterDefinition.enumOptions) {
                                options.push_back(QString::fromStdString(option));
                            }
                            break;
                        }
                    }
                }
                switch (parameter.type) {
                case NodeGraphParamType::Float: value = parameter.floatValue; break;
                case NodeGraphParamType::Int: value = static_cast<qlonglong>(parameter.intValue); break;
                case NodeGraphParamType::Bool: value = parameter.boolValue; break;
                case NodeGraphParamType::String: value = QString::fromStdString(parameter.stringValue); break;
                case NodeGraphParamType::Enum: value = QString::fromStdString(parameter.enumValue); break;
                default: continue;
                }
                item.insert(QStringLiteral("name"), name);
                item.insert(QStringLiteral("value"), value);
                item.insert(QStringLiteral("options"), options);
                selectedParameters.push_back(item);
            }
        }
    }

    emit selectedNodeChanged();
}
