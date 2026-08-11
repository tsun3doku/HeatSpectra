#pragma once

#include "nodegraph/NodeGraphState.hpp"
#include "nodegraph/NodeGraphTypes.hpp"
#include "scene/Camera.hpp"
#include "util/Units.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QMetaType>

class ProjectFile {
public:
    struct Viewport {
        glm::vec3 lookAt{0.0f};
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
        float radius = 2.0f;
        float fov = 45.0f;
        CameraProjectionMode projectionMode = CameraProjectionMode::Perspective;
        float orthographicHeight = 2.0f;
    };

    struct ProjectState {
        NodeGraphState graph;
        Viewport viewport;
        units::LengthUnit worldUnit = units::defaultLengthUnit();
    };

    static bool save(const ProjectState& data, const QString& filePath, QString* outError = nullptr);
    static bool load(ProjectState& outData, const QString& filePath, QString* outError = nullptr);

private:
    static void setError(QString* outError, const QString& error);

    static QString valueTypeToString(NodeGraphValueType value);
    static bool valueTypeFromString(const QString& text, NodeGraphValueType& outValue);
    static QString directionToString(NodeGraphSocketDirection direction);
    static bool directionFromString(const QString& text, NodeGraphSocketDirection& outDirection);
    static QString paramTypeToString(NodeGraphParamType type);
    static bool paramTypeFromString(const QString& text, NodeGraphParamType& outType);

    static QJsonArray vec3ToJson(const glm::vec3& value);
    static QJsonArray quatToJson(const glm::quat& value);
    static bool vec3FromJson(const QJsonValue& value, glm::vec3& outValue);
    static bool quatFromJson(const QJsonValue& value, glm::quat& outValue);

    static QString toRelativePath(const QString& path, const QDir& projectDir);
    static QString toAbsolutePath(const QString& path, const QDir& projectDir);

    static QJsonObject socketToJson(const NodeGraphSocket& socket);
    static QJsonObject nodeToJson(const NodeGraphNode& node, const QDir& projectDir);
    static QJsonObject edgeToJson(const NodeGraphEdge& edge);
    static QJsonObject viewportToJson(const Viewport& viewport);
    static QJsonObject paramToJson(const NodeGraphParamValue& parameter, const NodeGraphNode& node, const QDir& projectDir);
    static QJsonObject fieldValueToJson(const NodeGraphParamFieldValue& field, const NodeGraphNode& node, const QDir& projectDir);

    static bool socketFromJson(const QJsonValue& value, NodeGraphSocket& outSocket, QString* outError);
    static bool nodeFromJson(const QJsonValue& value, NodeGraphNode& outNode, const QDir& projectDir, QString* outError);
    static bool edgeFromJson(const QJsonValue& value, NodeGraphEdge& outEdge, QString* outError);
    static bool viewportFromJson(const QJsonValue& value, Viewport& outViewport, QString* outError);
    static bool paramFromJson(const QJsonValue& value, NodeGraphParamValue& outParameter, const NodeGraphNode& node, const QDir& projectDir, QString* outError);
    static bool fieldValueFromJson(const QJsonValue& value, NodeGraphParamFieldValue& outField, const NodeGraphNode& node, const QDir& projectDir, QString* outError);
};

Q_DECLARE_METATYPE(ProjectFile::Viewport)
Q_DECLARE_METATYPE(ProjectFile::ProjectState)
