#pragma once

#include "nodegraph/NodeGraphTypes.hpp"
#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

namespace nodegraphscene {

constexpr qreal nodeWidth = 95.0;
constexpr qreal nodeHeight = 30.0;
constexpr qreal roundCorners = 5.0;
constexpr qreal nodeOuterBorderWidth = 1.0;
constexpr qreal capBorderWidth = 0.5;
constexpr qreal selectedBorderWidth = 1.0;
constexpr qreal capWidth = 18.0;
constexpr qreal socketRadius = 3.0;
constexpr qreal socketHoverPadding = 3.0;
constexpr qreal socketRowInset = 6.0;
constexpr qreal titleTopOffset = 1.0;
constexpr qreal iconBoxWidth = 37.0;
constexpr qreal iconBoxHeight = 25.0;
constexpr qreal capGlowRadius = 8.0;
constexpr qreal centerGradientDarken = 110;
constexpr qreal capGlowRadiusMultiplier = 0.7;
constexpr int capGlowAlpha = 85;
constexpr qreal capActiveLighten = 120;
constexpr qreal capInactiveDarken = 120;
constexpr qreal capActiveDarken = 100;
constexpr qreal socketBorderWidth = 0.5;
constexpr qreal edgeDefaultWidth = 2.0;
constexpr qreal edgeHoveredWidth = 3.0;
constexpr int edgeHoverLightenFactor = 145;
constexpr qreal edgeMinTangent = 50.0;
constexpr qreal edgeMaxTangent = 210.0;
constexpr qreal edgeTangentVertScale = 0.35;
constexpr qreal edgeTangentHorzScale = 0.1;
constexpr qreal socketDistance = 0.8;
constexpr qreal nodeShadowOffsetX = 2.0;
constexpr qreal nodeShadowOffsetY = 3.0;
constexpr int nodeShadowAlpha = 60;
constexpr qreal simulationRingPadding = 5.0;
constexpr qreal simulationRingWidth = 14.0;
constexpr qreal simulationLabelRadiusInset = 3.0;
constexpr qreal simulationLabelCenterAngle = 270.0;
constexpr qreal simulationLabelMaxSweep = 120.0;
constexpr qreal simulationNodeLabelCenterAngle = 90.0;
constexpr qreal simulationNodeLabelMaxSweep = 90.0;
constexpr qreal simulationLabelBaselineInset = 3.0;

enum class NodeHitRegion : uint8_t {
    None = 0,
    Body = 1,
    LeftCap = 2,
    RightCap = 3
};

QColor valueTypeColor(NodeGraphValueType valueType);
QColor nodeShellColor();
QColor nodeShellOutlineColor();
QColor nodeCenterFillColor();
QColor selectedBorderColor();
QColor titleColor();
QColor frozenCapActiveColor();
QColor frozenCapInactiveColor();
QColor displayCapActiveColor();
QColor displayCapInactiveColor();
QColor socketBorderColor();
QColor dragPreviewColor();
QColor simulationRingColor();
QRectF nodeRect();
QRectF outerFrameRect();
QRectF segmentBoundsRect();
QRectF centerPanelRect();
QRectF leftCapRect();
QRectF rightCapRect();
QRectF iconRect();
QPointF titlePosition(const QRectF& titleBounds);
QPainterPath leftCapPath();
QPainterPath rightCapPath();
QPainterPath centerPanelPath();
qreal socketTopY();
qreal socketBottomY();
QPointF inputSocketPosition(std::size_t index, std::size_t total);
QPointF outputSocketPosition(std::size_t index, std::size_t total);
NodeHitRegion hitRegionAt(const QPointF& localPos);
QPointF inputAnchor(const QRectF& rect);
QPointF outputAnchor(const QRectF& rect);
QPainterPath buildEdgePath(
    const QPointF& src,
    const QPointF& dst,
    NodeGraphSocketDirection srcDirection = NodeGraphSocketDirection::Output,
    NodeGraphSocketDirection dstDirection = NodeGraphSocketDirection::Input);
}
