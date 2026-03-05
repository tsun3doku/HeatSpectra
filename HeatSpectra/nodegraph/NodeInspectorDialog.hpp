#pragma once

#include "NodeGraphTypes.hpp"

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
class QString;
class NodeGroupPanel;
class NodeHeatSolverPanel;
class NodeModelPanel;
class NodeRemeshPanel;

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

    QStackedWidget* pageStack = nullptr;
    QWidget* genericPage = nullptr;
    QWidget* modelPage = nullptr;
    QWidget* groupPage = nullptr;
    QWidget* remeshPage = nullptr;
    QWidget* heatPage = nullptr;

    NodeModelPanel* modelPanel = nullptr;
    NodeGroupPanel* groupPanel = nullptr;
    NodeRemeshPanel* remeshPanel = nullptr;
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
