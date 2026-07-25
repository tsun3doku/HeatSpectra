#include "UiModel.hpp"

UiModel::UiModel(QObject* parent)
    : QObject(parent),
      viewportModel(nullptr), heatPaletteModel(nullptr), timelineModel(nullptr), graphModel(nullptr),
      consoleModel(nullptr) {
}

void UiModel::newProject() {
    emit newProjectRequested();
}

void UiModel::openProjectUrl(const QUrl& url) {
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        emit projectError(QStringLiteral("The selected project does not resolve to a local file."));
        return;
    }
    emit openProjectRequested(path);
}

void UiModel::saveProject() {
    emit saveProjectRequested();
}

void UiModel::saveProjectAsUrl(const QUrl& url) {
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        emit projectError(QStringLiteral("The selected save location does not resolve to a local file."));
        return;
    }
    emit saveProjectAsRequested(path);
}

void UiModel::setProjectPath(const QString& path) {
    if (currentProjectPath == path) return;
    currentProjectPath = path;
    emit projectPathChanged();
}
