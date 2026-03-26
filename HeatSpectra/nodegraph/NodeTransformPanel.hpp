#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

#include <array>
#include <functional>

class NodeGraphBridge;
class QDoubleSpinBox;
class QString;

class NodeTransformPanel final : public QWidget {
public:
    explicit NodeTransformPanel(QWidget* parent = nullptr);

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

    std::array<QDoubleSpinBox*, 3> translateSpinBoxes{};
    std::array<QDoubleSpinBox*, 3> rotateSpinBoxes{};
    std::array<QDoubleSpinBox*, 3> scaleSpinBoxes{};

    std::function<void(const QString&)> statusSink;
};
