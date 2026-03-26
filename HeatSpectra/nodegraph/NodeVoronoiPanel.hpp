#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

#include <functional>

class NodeGraphBridge;
class QDoubleSpinBox;
class QString;
class QSpinBox;

class NodeVoronoiPanel final : public QWidget {
public:
    explicit NodeVoronoiPanel(QWidget* parent = nullptr);

    void bind(NodeGraphBridge* nodeGraphBridge);
    void setNode(NodeGraphNodeId nodeId);
    void setStatusSink(std::function<void(const QString&)> statusSink);

private:
    bool writeParameters();
    void onParametersEdited();
    void setStatus(const QString& text) const;

    NodeGraphBridge* nodeGraphBridge = nullptr;
    NodeGraphNodeId currentNodeId{};
    bool syncingFromNode = false;

    QDoubleSpinBox* cellSizeSpinBox = nullptr;
    QSpinBox* voxelResolutionSpinBox = nullptr;

    std::function<void(const QString&)> statusSink;
};
