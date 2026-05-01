#pragma once

#include "NodePanelBase.hpp"
#include "nodegraph/NodeHeatSolveParams.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;
class NodeGraphSliderRow;
class RuntimeQuery;

class NodeHeatSolverPanel final : public NodePanelBase {
public:
    explicit NodeHeatSolverPanel(QWidget* parent = nullptr);

    void bind(NodeGraphBridge* nodeGraphBridge, const RuntimeQuery* runtimeQuery);

    void refreshBindingGroupOptions();
    void updateHeatStatus();

    void startStatusTimer();
    void stopStatusTimer();

protected:
    void refreshFromNode() override;

private:
    bool writeNodeParams(const HeatSolveNodeParams& params);
    bool tryLoadNodeParams(HeatSolveNodeParams& outParams) const;
    void toggleHeatSystem();
    void pauseHeatSystem();
    void resetHeatSystem();
    void applySolveSettings();
    void applyMaterialBindings();

    const RuntimeQuery* runtimeQuery = nullptr;

    QLabel* heatStatusValueLabel = nullptr;
    QPushButton* heatToggleButton = nullptr;
    QPushButton* heatPauseButton = nullptr;
    QPushButton* heatResetButton = nullptr;
    NodeGraphSliderRow* heatCellSizeRow = nullptr;
    NodeGraphSliderRow* heatVoxelResolutionRow = nullptr;
    NodeGraphSliderRow* heatContactThermalConductanceRow = nullptr;
    QCheckBox* heatOverlayCheckBox = nullptr;
    QPushButton* heatSolveSettingsApplyButton = nullptr;
    QComboBox* heatBindingGroupComboBox = nullptr;
    QComboBox* heatBindingPresetComboBox = nullptr;
    QPushButton* heatBindingAddButton = nullptr;
    QPushButton* heatBindingRemoveButton = nullptr;
    QPushButton* heatBindingApplyButton = nullptr;
    QTableWidget* heatBindingsTable = nullptr;
    QTimer* heatStatusTimer = nullptr;
};
