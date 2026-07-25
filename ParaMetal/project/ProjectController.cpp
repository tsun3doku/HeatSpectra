#include "ProjectController.hpp"

#include "app/GraphHost.hpp"

#include <QFileInfo>
#include <QMetaObject>

ProjectController::ProjectController(GraphHost& graphHost, QObject* parent)
    : QObject(parent), host(graphHost) {
    connect(&host, &GraphHost::graphStateReady,
            this, &ProjectController::onGraphStateReady, Qt::QueuedConnection);
    connect(&host, &GraphHost::graphStateLoaded,
            this, &ProjectController::onGraphStateLoaded, Qt::QueuedConnection);
    connect(&host, &GraphHost::graphChanged, this, [this](const NodeGraphDelta&) {
        setModified(true);
    }, Qt::QueuedConnection);
}

void ProjectController::newProject() {
    if (busyState) { emit error(QStringLiteral("A project operation is already in progress.")); return; }
    QMetaObject::invokeMethod(&host, "newProject", Qt::QueuedConnection);
    emit viewportStateApplied(ProjectFile::Viewport{});
    setPath({});
    setModified(false);
}

void ProjectController::open(const QUrl& url) {
    if (busyState) { emit error(QStringLiteral("A project operation is already in progress.")); return; }
    if (!url.isLocalFile()) {
        emit error(QStringLiteral("The selected project is not a local file."));
        return;
    }
    const QString absolutePath = QFileInfo(url.toLocalFile()).absoluteFilePath();
    if (!QFileInfo(absolutePath).isFile()) {
        emit error(QStringLiteral("Project file does not exist: %1").arg(absolutePath));
        return;
    }
    ProjectFile::ProjectState state{};
    QString loadError;
    if (!ProjectFile::load(state, absolutePath, &loadError)) {
        emit error(loadError);
        return;
    }
    pendingOperation = PendingOperation::Open;
    busyState = true;
    emit busyChanged();
    pendingPath = absolutePath;
    emit viewportStateApplied(state.viewport);
    QMetaObject::invokeMethod(&host, "loadGraphState", Qt::QueuedConnection,
                              Q_ARG(NodeGraphState, state.graph));
}

void ProjectController::save() {
    if (busyState) { emit error(QStringLiteral("A project operation is already in progress.")); return; }
    if (projectPath.isEmpty()) {
        emit saveAsRequired();
        return;
    }
    requestSave(projectPath);
}

void ProjectController::saveAs(const QUrl& url) {
    if (busyState) { emit error(QStringLiteral("A project operation is already in progress.")); return; }
    if (!url.isLocalFile()) {
        emit error(QStringLiteral("The selected save location is not a local file."));
        return;
    }
    QString savePath = QFileInfo(url.toLocalFile()).absoluteFilePath();
    if (QFileInfo(savePath).suffix().isEmpty()) savePath += QStringLiteral(".pm");
    requestSave(savePath);
}

void ProjectController::requestSave(const QString& path) {
    pendingOperation = PendingOperation::Save;
    busyState = true;
    emit busyChanged();
    pendingPath = path;
    graphStatePending = true;
    viewportStatePending = true;
    emit viewportStateRequested();
    QMetaObject::invokeMethod(&host, "requestGraphState", Qt::QueuedConnection);
}

void ProjectController::onGraphStateReady(const NodeGraphState& state) {
    if (pendingOperation != PendingOperation::Save) return;
    pendingGraphState = state;
    graphStatePending = false;
    finishSaveIfReady();
}

void ProjectController::onViewportStateReady(const ProjectFile::Viewport& state) {
    if (pendingOperation != PendingOperation::Save) return;
    pendingViewportState = state;
    viewportStatePending = false;
    finishSaveIfReady();
}

void ProjectController::finishSaveIfReady() {
    if (pendingOperation != PendingOperation::Save || graphStatePending || viewportStatePending) return;
    ProjectFile::ProjectState projectState{};
    projectState.graph = pendingGraphState;
    projectState.viewport = pendingViewportState;
    QString saveError;
    if (!ProjectFile::save(projectState, pendingPath, &saveError) ||
        !QFileInfo(pendingPath).isFile() || QFileInfo(pendingPath).size() == 0) {
        if (saveError.isEmpty()) saveError = QStringLiteral("Project save did not produce a valid file.");
        pendingOperation = PendingOperation::None;
        busyState = false;
        emit busyChanged();
        emit error(saveError);
        return;
    }
    setPath(pendingPath);
    setModified(false);
    pendingOperation = PendingOperation::None;
    busyState = false;
    emit busyChanged();
}

void ProjectController::onGraphStateLoaded(bool success, const QString& errorMessage) {
    if (pendingOperation != PendingOperation::Open) return;
    if (!success) {
        pendingOperation = PendingOperation::None;
        busyState = false;
        emit busyChanged();
        emit error(errorMessage);
        return;
    }
    setPath(pendingPath);
    setModified(false);
    pendingPath.clear();
    pendingOperation = PendingOperation::None;
    busyState = false;
    emit busyChanged();
}

void ProjectController::setPath(const QString& path) {
    if (projectPath == path) return;
    projectPath = path;
    emit pathChanged();
}

void ProjectController::setModified(bool modified) {
    if (projectModified == modified) return;
    projectModified = modified;
    emit modifiedChanged();
}
