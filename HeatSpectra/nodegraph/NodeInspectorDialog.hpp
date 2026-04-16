#pragma once

#include "NodeGraphTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include <QWidget>

class NodeGraphBridge;
class RuntimeQuery;
class QLabel;
class QPushButton;
class QStackedWidget;
class QHideEvent;
class QTextEdit;
class QTabWidget;
class QComboBox;
class QTableWidget;
class QTableView;
class QAbstractTableModel;
class QCheckBox;
class QString;
class NodeGroupPanel;
class NodeHeatSolverPanel;
class NodeHeatSourcePanel;
class NodeModelPanel;
class NodeTransformPanel;
class NodeRemeshPanel;
class NodeContactPanel;
class NodeVoronoiPanel;

class NodeInspectorDialog : public QWidget {
public:
    explicit NodeInspectorDialog(QWidget* parent = nullptr);

    void bind(NodeGraphBridge* nodeGraphBridge, const RuntimeQuery* runtimeQuery);
    bool setNode(NodeGraphNodeId nodeId);

protected:
    void hideEvent(QHideEvent* event) override;

private:
    void buildUi();

    void refreshRuntimeDebugViews();
    void updateDataflowView();
    void updateSpreadsheetView();
    void clearSpreadsheetView(const QString& message);

    NodeGraphBridge* nodeGraphBridge = nullptr;
    const RuntimeQuery* runtimeQuery = nullptr;
    NodeGraphNodeId currentNodeId{};
    NodeTypeId currentNodeTypeId = nodegraphtypes::Custom;

    QLabel* titleLabel = nullptr;
    QLabel* subtitleLabel = nullptr;
    QLabel* statusLabel = nullptr;
    QCheckBox* displayCheckBox = nullptr;

    QStackedWidget* pageStack = nullptr;
    QWidget* genericPage = nullptr;
    QWidget* modelPage = nullptr;
    QWidget* transformPage = nullptr;
    QWidget* groupPage = nullptr;
    QWidget* remeshPage = nullptr;
    QWidget* voronoiPage = nullptr;
    QWidget* heatSourcePage = nullptr;
    QWidget* contactPage = nullptr;
    QWidget* heatPage = nullptr;

    NodeModelPanel* modelPanel = nullptr;
    NodeTransformPanel* transformPanel = nullptr;
    NodeGroupPanel* groupPanel = nullptr;
    NodeRemeshPanel* remeshPanel = nullptr;
    NodeVoronoiPanel* voronoiPanel = nullptr;
    NodeContactPanel* contactPanel = nullptr;
    NodeHeatSourcePanel* heatSourcePanel = nullptr;
    NodeHeatSolverPanel* heatSolverPanel = nullptr;

    QTabWidget* mainTabWidget = nullptr;
    QTextEdit* dataflowTextEdit = nullptr;
    QPushButton* dataflowRefreshButton = nullptr;
    QComboBox* spreadsheetSocketComboBox = nullptr;
    QLabel* spreadsheetSummaryLabel = nullptr;
    QTableWidget* spreadsheetAttributesTable = nullptr;
    QTableView* spreadsheetSamplesTable = nullptr;
    QAbstractTableModel* spreadsheetSamplesModel = nullptr;
    QPushButton* spreadsheetRefreshButton = nullptr;
};
