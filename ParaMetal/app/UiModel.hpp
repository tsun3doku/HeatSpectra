#pragma once

#include "ViewportUiModel.hpp"
#include "HeatPaletteUiModel.hpp"
#include "TimelineUiModel.hpp"
#include "ConsoleUiModel.hpp"
#include "NodeGraphUiModel.hpp"

#include <QObject>
#include <QUrl>
#include <QString>

class UiModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(ViewportUiModel* viewport READ viewport CONSTANT)
    Q_PROPERTY(HeatPaletteUiModel* heatPalette READ heatPalette CONSTANT)
    Q_PROPERTY(TimelineUiModel* timeline READ timeline CONSTANT)
    Q_PROPERTY(NodeGraphUiModel* nodeGraph READ nodeGraph CONSTANT)
    Q_PROPERTY(ConsoleUiModel* console READ console CONSTANT)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectPathChanged)

public:
    explicit UiModel(QObject* parent = nullptr);

    ViewportUiModel* viewport() { return &viewportModel; }
    HeatPaletteUiModel* heatPalette() { return &heatPaletteModel; }
    TimelineUiModel* timeline() { return &timelineModel; }
    NodeGraphUiModel* nodeGraph() { return &graphModel; }
    ConsoleUiModel* console() { return &consoleModel; }

    QString projectPath() const { return currentProjectPath; }
    Q_INVOKABLE void newProject();
    Q_INVOKABLE void openProjectUrl(const QUrl& url);
    Q_INVOKABLE void saveProject();
    Q_INVOKABLE void saveProjectAsUrl(const QUrl& url);

public slots:
    void setProjectPath(const QString& path);

signals:
    void newProjectRequested();
    void openProjectRequested(const QString& path);
    void saveProjectRequested();
    void saveProjectAsRequested(const QString& path);
    void saveAsDialogRequested();
    void projectPathChanged();
    void projectError(const QString& message);

private:
    ViewportUiModel viewportModel;
    HeatPaletteUiModel heatPaletteModel;
    TimelineUiModel timelineModel;
    NodeGraphUiModel graphModel;
    ConsoleUiModel consoleModel;
    QString currentProjectPath;
};
