#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>
#include <string>

class NodeGraphBridge;
class RuntimeQuery;
class QLabel;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QStackedWidget;
class QTimer;
class QHideEvent;
class QLineEdit;
class QTextEdit;
class QTabWidget;
class QComboBox;
class QTableWidget;
class QTableView;
class QAbstractTableModel;
class QString;

class NodeInspectorDialog : public QWidget {
public:
    explicit NodeInspectorDialog(QWidget* parent = nullptr);

    void bind(NodeGraphBridge* nodeGraphBridge, const RuntimeQuery* runtimeQuery);
    bool setNode(NodeGraphNodeId nodeId);

protected:
    void hideEvent(QHideEvent* event) override;

private:
    void buildUi();
    void browseModelFile();
    void applyModelSettings();
    void applyRemeshSettings();
    void executeRemesh();
    void toggleHeatSystem();
    void pauseHeatSystem();
    void resetHeatSystem();
    void updateHeatStatus();
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
    QWidget* remeshPage = nullptr;
    QWidget* heatPage = nullptr;

    QLineEdit* modelPathLineEdit = nullptr;
    QPushButton* modelBrowseButton = nullptr;
    QPushButton* modelApplyButton = nullptr;

    QSpinBox* iterationsSpinBox = nullptr;
    QDoubleSpinBox* minAngleSpinBox = nullptr;
    QDoubleSpinBox* maxEdgeLengthSpinBox = nullptr;
    QDoubleSpinBox* stepSizeSpinBox = nullptr;
    QPushButton* remeshApplyButton = nullptr;
    QPushButton* remeshRunButton = nullptr;

    QLabel* heatStatusValueLabel = nullptr;
    QPushButton* heatToggleButton = nullptr;
    QPushButton* heatPauseButton = nullptr;
    QPushButton* heatResetButton = nullptr;
    QTimer* heatStatusTimer = nullptr;

    QTabWidget* dataTabWidget = nullptr;
    QTextEdit* dataflowTextEdit = nullptr;
    QPushButton* dataflowRefreshButton = nullptr;
    QComboBox* spreadsheetSocketComboBox = nullptr;
    QLabel* spreadsheetSummaryLabel = nullptr;
    QTableWidget* spreadsheetAttributesTable = nullptr;
    QTableView* spreadsheetSamplesTable = nullptr;
    QAbstractTableModel* spreadsheetSamplesModel = nullptr;
    QPushButton* spreadsheetRefreshButton = nullptr;
};
