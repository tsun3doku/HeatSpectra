#include "ViewportUiModel.hpp"

void ViewportUiModel::setWireframeMode(int mode) {
    Q_ASSERT(mode >= static_cast<int>(app::WireframeMode::Off));
    Q_ASSERT(mode <= static_cast<int>(app::WireframeMode::Shaded));
    if (mode < static_cast<int>(app::WireframeMode::Off) ||
        mode > static_cast<int>(app::WireframeMode::Shaded)) return;
    emit wireframeModeRequested(static_cast<app::WireframeMode>(mode));
}

void ViewportUiModel::setGridEnabled(bool enabled) {
    emit gridEnabledRequested(enabled);
}

void ViewportUiModel::applyState(const ViewportUiState& updated) {
    if (state == updated) return;
    state = updated;
    emit stateChanged();
}
