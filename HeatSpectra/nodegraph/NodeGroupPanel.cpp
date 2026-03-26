#include "NodeGroupPanel.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphDebugStore.hpp"
#include "NodePanelUtils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

enum class GroupSourceType : int64_t {
    Vertex = nodegraphparams::group::sourcetype::Vertex,
    Object = nodegraphparams::group::sourcetype::Object,
    Material = nodegraphparams::group::sourcetype::Material,
    Smooth = nodegraphparams::group::sourcetype::Smooth,
};

GroupSourceType sanitizeGroupSourceType(int64_t sourceTypeValue) {
    switch (sourceTypeValue) {
    case nodegraphparams::group::sourcetype::Object:
        return GroupSourceType::Object;
    case nodegraphparams::group::sourcetype::Material:
        return GroupSourceType::Material;
    case nodegraphparams::group::sourcetype::Smooth:
        return GroupSourceType::Smooth;
    case nodegraphparams::group::sourcetype::Vertex:
    default:
        return GroupSourceType::Vertex;
    }
}

const char* metadataKeyForSourceType(GroupSourceType sourceType) {
    switch (sourceType) {
    case GroupSourceType::Object:
        return "geometry.group_names.object";
    case GroupSourceType::Material:
        return "geometry.group_names.material";
    case GroupSourceType::Smooth:
        return "geometry.group_names.smooth";
    case GroupSourceType::Vertex:
    default:
        return "geometry.group_names.vertex";
    }
}

void appendGroupNamesFromObjPath(
    const std::string& modelPath,
    GroupSourceType sourceType,
    std::unordered_set<std::string>& seenNames,
    std::vector<std::string>& outNames) {
    for (const std::string& candidatePath : NodePanelUtils::resolveCandidateModelPaths(modelPath)) {
        if (candidatePath.empty() || !std::filesystem::exists(candidatePath)) {
            continue;
        }

        std::ifstream inputStream(candidatePath);
        if (!inputStream.is_open()) {
            continue;
        }

        auto addName = [&](const std::string& name) {
            const std::string trimmedName = NodePanelUtils::trimCopy(name);
            if (trimmedName.empty()) {
                return;
            }
            if (seenNames.insert(trimmedName).second) {
                outNames.push_back(trimmedName);
            }
        };

        std::string rawLine;
        while (std::getline(inputStream, rawLine)) {
            const std::string line = NodePanelUtils::trimCopy(NodePanelUtils::stripLineComment(rawLine));
            if (line.empty()) {
                continue;
            }

            const std::size_t firstWhitespace = line.find_first_of(" \t");
            const std::string keyword = NodePanelUtils::toLowerCopy(
                firstWhitespace == std::string::npos ? line : line.substr(0, firstWhitespace));
            const std::string value = NodePanelUtils::trimCopy(
                firstWhitespace == std::string::npos ? std::string() : line.substr(firstWhitespace + 1));
            if (value.empty()) {
                continue;
            }

            switch (sourceType) {
            case GroupSourceType::Vertex:
                if (keyword == "g") {
                    std::stringstream groupStream(value);
                    std::string groupToken;
                    while (groupStream >> groupToken) {
                        addName(groupToken);
                    }
                }
                break;
            case GroupSourceType::Object:
                if (keyword == "o") {
                    std::stringstream objectStream(value);
                    std::string objectToken;
                    if (objectStream >> objectToken) {
                        addName(objectToken);
                    }
                }
                break;
            case GroupSourceType::Material:
                if (keyword == "usemtl") {
                    std::stringstream materialStream(value);
                    std::string materialToken;
                    if (materialStream >> materialToken) {
                        addName(materialToken);
                    }
                }
                break;
            case GroupSourceType::Smooth:
                if (keyword == "s") {
                    std::stringstream smoothStream(value);
                    std::string smoothToken;
                    if (smoothStream >> smoothToken) {
                        addName(smoothToken);
                    }
                }
                break;
            default:
                break;
            }
        }

        return;
    }
}

} // namespace

NodeGroupPanel::NodeGroupPanel(QWidget* parent)
    : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    enabledCheckBox = new QCheckBox("Enable Assignment", this);
    enabledCheckBox->setChecked(true);
    layout->addWidget(enabledCheckBox);

    layout->addWidget(new QLabel("Source Type:", this));
    sourceTypeComboBox = new QComboBox(this);
    sourceTypeComboBox->setEditable(false);
    sourceTypeComboBox->addItem("Vertex", static_cast<qlonglong>(nodegraphparams::group::sourcetype::Vertex));
    sourceTypeComboBox->addItem("Object", static_cast<qlonglong>(nodegraphparams::group::sourcetype::Object));
    sourceTypeComboBox->addItem("Material", static_cast<qlonglong>(nodegraphparams::group::sourcetype::Material));
    sourceTypeComboBox->addItem("Smooth", static_cast<qlonglong>(nodegraphparams::group::sourcetype::Smooth));
    layout->addWidget(sourceTypeComboBox);

    layout->addWidget(new QLabel("Source Name:", this));
    sourceNameComboBox = new QComboBox(this);
    sourceNameComboBox->setEditable(false);
    layout->addWidget(sourceNameComboBox);

    layout->addWidget(new QLabel("Target Group Name:", this));
    targetNameLineEdit = new QLineEdit(this);
    targetNameLineEdit->setPlaceholderText("CopperSide");
    layout->addWidget(targetNameLineEdit);

    applyButton = new QPushButton("Apply Group Assignment", this);
    layout->addWidget(applyButton);

    QLabel* hintLabel = new QLabel(
        "Select a source type and source name from incoming mesh data, then assign triangles to a target group name.",
        this);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);
    layout->addStretch();

    connect(applyButton, &QPushButton::clicked, this, [this]() {
        applySettings();
    });
    connect(sourceTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshSourceOptions();
    });
}

void NodeGroupPanel::bind(NodeGraphBridge* nodeGraphBridgePtr) {
    nodeGraphBridge = nodeGraphBridgePtr;
    if (currentNodeId.isValid()) {
        setNode(currentNodeId);
    }
}

void NodeGroupPanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        setStatus("Failed to read Group node.");
        return;
    }

    enabledCheckBox->setChecked(NodePanelUtils::readBoolParam(node, nodegraphparams::group::Enabled, true));
    targetNameLineEdit->setText(QString::fromStdString(NodePanelUtils::readStringParam(node, nodegraphparams::group::TargetName)));

    const GroupSourceType sourceType = sanitizeGroupSourceType(
        NodePanelUtils::readIntParam(node, nodegraphparams::group::SourceType, nodegraphparams::group::sourcetype::Vertex));
    const int sourceTypeIndex = sourceTypeComboBox->findData(static_cast<qlonglong>(static_cast<int64_t>(sourceType)));
    sourceTypeComboBox->setCurrentIndex(sourceTypeIndex >= 0 ? sourceTypeIndex : 0);

    refreshSourceOptions();
}

void NodeGroupPanel::refreshSourceOptions() {
    if (!sourceTypeComboBox || !sourceNameComboBox || !nodeGraphBridge || !currentNodeId.isValid()) {
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        return;
    }

    const std::string preferredSourceName = NodePanelUtils::readStringParam(node, nodegraphparams::group::SourceName);
    const GroupSourceType selectedSourceType = sanitizeGroupSourceType(sourceTypeComboBox->currentData().toLongLong());
    const std::string sourceTypeMetadataKey = metadataKeyForSourceType(selectedSourceType);

    std::vector<std::string> groupNames;
    std::unordered_set<std::string> seenGroupNames;

    NodeGraphRuntimeNodeDebugInfo debugInfo{};
    if (NodeGraphDebugStore::tryGetLatestNodeDebugInfo(currentNodeId, debugInfo)) {
        for (const NodeGraphRuntimeSocketDebugInfo& inputSocket : debugInfo.inputs) {
            if (!inputSocket.hasValue) {
                continue;
            }

            const auto namesIt = inputSocket.metadata.find(sourceTypeMetadataKey);
            if (namesIt == inputSocket.metadata.end()) {
                if (selectedSourceType != GroupSourceType::Vertex) {
                    continue;
                }

                const auto legacyNamesIt = inputSocket.metadata.find("geometry.group_names");
                if (legacyNamesIt == inputSocket.metadata.end()) {
                    continue;
                }
                NodePanelUtils::appendDelimitedNames(legacyNamesIt->second, seenGroupNames, groupNames);
                continue;
            }
            NodePanelUtils::appendDelimitedNames(namesIt->second, seenGroupNames, groupNames);
        }
    }

    if (groupNames.empty()) {
        const NodeGraphState state = nodeGraphBridge->state();
        std::unordered_set<uint32_t> visitedNodeIds;
        std::unordered_set<std::string> seenModelPaths;
        std::vector<std::string> upstreamModelPaths;
        NodePanelUtils::collectUpstreamModelPaths(
            state,
            currentNodeId,
            visitedNodeIds,
            seenModelPaths,
            upstreamModelPaths);

        for (const std::string& modelPath : upstreamModelPaths) {
            appendGroupNamesFromObjPath(modelPath, selectedSourceType, seenGroupNames, groupNames);
        }
    }

    if (groupNames.empty() && seenGroupNames.insert("Default").second) {
        groupNames.push_back("Default");
    }

    std::sort(groupNames.begin(), groupNames.end());
    if (!preferredSourceName.empty() && seenGroupNames.insert(preferredSourceName).second) {
        groupNames.push_back(preferredSourceName);
        std::sort(groupNames.begin(), groupNames.end());
    }

    sourceNameComboBox->blockSignals(true);
    sourceNameComboBox->clear();
    for (const std::string& groupName : groupNames) {
        sourceNameComboBox->addItem(QString::fromStdString(groupName));
    }

    if (!preferredSourceName.empty()) {
        const int preferredIndex = sourceNameComboBox->findText(QString::fromStdString(preferredSourceName));
        if (preferredIndex >= 0) {
            sourceNameComboBox->setCurrentIndex(preferredIndex);
        }
    } else if (sourceNameComboBox->count() > 0) {
        sourceNameComboBox->setCurrentIndex(0);
    }
    sourceNameComboBox->blockSignals(false);
}

void NodeGroupPanel::setStatusSink(std::function<void(const QString&)> statusSinkFn) {
    statusSink = std::move(statusSinkFn);
}

void NodeGroupPanel::applySettings() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot apply group settings for this node.");
        return;
    }

    const bool enabled = enabledCheckBox->isChecked();
    const int64_t sourceTypeValue = sourceTypeComboBox->currentData().toLongLong();
    const std::string sourceName = sourceNameComboBox->currentText().trimmed().toStdString();
    const std::string targetName = targetNameLineEdit->text().trimmed().toStdString();
    if (enabled && sourceName.empty()) {
        setStatus("Pick a source name.");
        return;
    }
    if (enabled && targetName.empty()) {
        setStatus("Target group name cannot be empty.");
        return;
    }

    if (!NodePanelUtils::writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::group::Enabled, enabled) ||
        !NodePanelUtils::writeIntParam(nodeGraphBridge, currentNodeId, nodegraphparams::group::SourceType, sourceTypeValue) ||
        !NodePanelUtils::writeStringParam(nodeGraphBridge, currentNodeId, nodegraphparams::group::SourceName, sourceName) ||
        !NodePanelUtils::writeStringParam(nodeGraphBridge, currentNodeId, nodegraphparams::group::TargetName, targetName)) {
        setStatus("Failed to update group settings.");
        return;
    }

    setStatus("Group settings applied.");
}

void NodeGroupPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
