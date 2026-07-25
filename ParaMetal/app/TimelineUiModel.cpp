#include "TimelineUiModel.hpp"

#include <algorithm>

void TimelineUiModel::setPlaying(bool value) {
    emit playingRequested(value);
}

void TimelineUiModel::reset() {
    emit resetRequested();
}

void TimelineUiModel::scrub(int frame) {
    emit scrubRequested(static_cast<uint32_t>(std::max(0, frame)));
}

void TimelineUiModel::step(int delta) {
    emit stepRequested(delta);
}

void TimelineUiModel::applyState(const TimelineUiState& updated) {
    if (state == updated) return;
    state = updated;
    emit stateChanged();
}
