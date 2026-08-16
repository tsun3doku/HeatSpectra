#pragma once

#include "project/ProjectFile.hpp"

#include <QObject>
#include <QString>
#include <QUrl>
#include "util/Units.hpp"

class GraphHost;

class ProjectController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int worldUnit READ worldUnit WRITE setWorldUnit NOTIFY worldUnitChanged)
public:
    explicit ProjectController(GraphHost& graphHost, QObject* parent = nullptr);

    QString path() const { return projectPath; }
    bool modified() const { return projectModified; }
    bool busy() const { return busyState; }
    int worldUnit() const { return static_cast<int>(projectWorldUnit); }

    Q_INVOKABLE void newProject();
    Q_INVOKABLE void newEmptyProject();
    Q_INVOKABLE void open(const QUrl& url);
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveAs(const QUrl& url);
    Q_INVOKABLE void setWorldUnit(int unit);

signals:
    void pathChanged();
    void modifiedChanged();
    void saveAsRequired();
    void error(const QString& message);
    void busyChanged();
    void worldUnitChanged();
    void worldUnitRequested(int unit);
    void viewportStateRequested();
    void viewportStateApplied(const ProjectFile::Viewport& state);

public slots:
    void onGraphStateReady(const NodeGraphState& state);
    void onViewportStateReady(const ProjectFile::Viewport& state);
    void onGraphStateLoaded(bool success, const QString& errorMessage);

private:
    enum class PendingOperation { None, Save, Open };
    void setPath(const QString& path);
    void setModified(bool modified);
    void requestSave(const QString& path);
    void finishSaveIfReady();

    GraphHost& host;
    QString projectPath;
    bool projectModified = false;
    PendingOperation pendingOperation = PendingOperation::None;
    QString pendingPath;
    NodeGraphState pendingGraphState;
    ProjectFile::Viewport pendingViewportState;
    bool graphStatePending = false;
    bool viewportStatePending = false;
    bool busyState = false;
    units::LengthUnit projectWorldUnit = units::defaultLengthUnit();
};
