#include "NodePanelBase.hpp"
#include "NodeGraphWidgetStyle.hpp"
#include "NodePanelUtils.hpp"

#include <QVBoxLayout>

NodePanelBase::NodePanelBase(QWidget* parent)
    : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(nodegraphwidgets::panelCardInnerSpacing);
}

void NodePanelBase::bind(NodeGraphBridge* bridgePtr) {
    this->bridgePtr = bridgePtr;
    if (nodeId.isValid()) {
        refreshFromNode();
    }
}

void NodePanelBase::setNode(NodeGraphNodeId newId) {
    nodeId = newId;
    refreshFromNode();
}

void NodePanelBase::setStatusSink(std::function<void(const QString&)> sink) {
    statusSink = std::move(sink);
}

bool NodePanelBase::canEdit() const {
    return bridgePtr && nodeId.isValid();
}

bool NodePanelBase::loadCurrentNode(NodeGraphNode& outNode) {
    return NodePanelUtils::loadNode(bridgePtr, nodeId, outNode);
}

void NodePanelBase::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}

NodeGraphBridge* NodePanelBase::bridge() const {
    return bridgePtr;
}

NodeGraphNodeId NodePanelBase::currentNodeId() const {
    return nodeId;
}

void NodePanelBase::setSyncing(bool value) {
    syncing = value;
}

bool NodePanelBase::isSyncing() const {
    return syncing;
}
