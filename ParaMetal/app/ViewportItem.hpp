#pragma once

#include <QtQuick/QQuickRhiItem>

#include "ViewportMailbox.hpp"
#include "project/ProjectFile.hpp"

class QHoverEvent;
class RuntimeNotifier;
class RuntimeSystems;

class ViewportItem : public QQuickRhiItem {
    Q_OBJECT

public:
    explicit ViewportItem(QQuickItem* parent = nullptr);
    void bindRuntime(RuntimeSystems& runtime, RuntimeNotifier& notifier);

public slots:
    void requestWireframeMode(app::WireframeMode mode);
    void requestGridEnabled(bool enabled);
    void requestTimelinePlaying(bool playing);
    void requestTimelineReset();
    void requestTimelineScrub(uint32_t frame);
    void requestTimelineStep(int delta);
    void requestTimelineRange(uint32_t frameCount, float fps);
    void requestSelection(int nodeId);
    void requestHeatPaletteRange(float minimum, float maximum);
    void requestHeatPalette(int palette);
    void requestWorldUnit(int unit);
    void applyViewportProjectState(const ProjectFile::Viewport& viewport);
    void requestCurrentViewportProjectState();
    void initializeGraph(const NodeGraphState& graphState);
    void queueGraphDelta(const NodeGraphDelta& delta);

signals:
    void projectViewportStateReady(const ProjectFile::Viewport& state);

protected:
    QQuickRhiItemRenderer* createRenderer() override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void updateMousePosition(const QPointF& position);

    ViewportMailbox mailbox;
    RuntimeSystems* runtime = nullptr;
    RuntimeNotifier* notifier = nullptr;
};
