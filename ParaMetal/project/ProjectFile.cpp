#include "ProjectFile.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeGraphUtils.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

constexpr int ProjectVersion = 2;
constexpr const char* ProjectApp = "ParaMetal";

bool ProjectFile::save(const ProjectState& data, const QString& filePath, QString* outError) {
    const QFileInfo projectInfo(filePath);
    const QDir projectDir = projectInfo.absoluteDir();

    QJsonObject root;
    root["version"] = ProjectVersion;
    root["app"] = ProjectApp;

    QJsonObject graph;
    graph["nextNodeId"] = static_cast<int>(data.graph.nextNodeId);
    graph["nextSocketId"] = static_cast<int>(data.graph.nextSocketId);
    graph["nextEdgeId"] = static_cast<int>(data.graph.nextEdgeId);

    QJsonArray nodes;
    std::vector<uint32_t> nodeIds;
    nodeIds.reserve(data.graph.nodes.size());
    for (const auto& [id, node] : data.graph.nodes) {
        nodeIds.push_back(id);
    }
    std::sort(nodeIds.begin(), nodeIds.end());
    for (uint32_t id : nodeIds) {
        nodes.append(nodeToJson(data.graph.nodes.at(id), projectDir));
    }
    graph["nodes"] = nodes;

    QJsonArray edges;
    std::vector<uint32_t> edgeIds;
    edgeIds.reserve(data.graph.edges.size());
    for (const auto& [id, edge] : data.graph.edges) {
        edgeIds.push_back(id);
    }
    std::sort(edgeIds.begin(), edgeIds.end());
    for (uint32_t id : edgeIds) {
        edges.append(edgeToJson(data.graph.edges.at(id)));
    }
    graph["edges"] = edges;
    root["graph"] = graph;
    root["viewport"] = viewportToJson(data.viewport);
    root["worldUnit"] = QString::fromUtf8(units::toString(data.worldUnit));

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(outError, "Failed to open project file for writing: " + file.errorString());
        return false;
    }

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        setError(outError, "Failed to write project file: " + file.errorString());
        return false;
    }
    if (file.error() != QFile::NoError) {
        setError(outError, "Failed to write project file: " + file.errorString());
        return false;
    }
    if (!file.commit()) {
        setError(outError, "Failed to replace project file: " + file.errorString());
        return false;
    }

    return true;
}

bool ProjectFile::load(ProjectState& outData, const QString& filePath, QString* outError) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(outError, "Failed to open project file: " + file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(outError, "Project file is not valid JSON: " + parseError.errorString());
        return false;
    }

    const QJsonObject root = document.object();
    if (!root["version"].isDouble() || !root["app"].isString() || !root["graph"].isObject()) {
        setError(outError, "Project file is missing required root fields.");
        return false;
    }
    if (root["app"].toString() != ProjectApp) {
        setError(outError, "Project file was not created by ParaMetal.");
        return false;
    }
    if (root["version"].toInt() > ProjectVersion) {
        setError(outError, "Project file version is newer than this ParaMetal build supports.");
        return false;
    }

    const QDir projectDir = QFileInfo(filePath).absoluteDir();
    const QJsonObject graph = root["graph"].toObject();
    if (!graph["nextNodeId"].isDouble() || !graph["nextSocketId"].isDouble() ||
        !graph["nextEdgeId"].isDouble() || !graph["nodes"].isArray() || !graph["edges"].isArray()) {
        setError(outError, "Project graph has invalid fields.");
        return false;
    }

    ProjectState loaded;
    loaded.graph.nextNodeId = static_cast<uint32_t>(graph["nextNodeId"].toInt());
    loaded.graph.nextSocketId = static_cast<uint32_t>(graph["nextSocketId"].toInt());
    loaded.graph.nextEdgeId = static_cast<uint32_t>(graph["nextEdgeId"].toInt());

    for (const QJsonValue& nodeValue : graph["nodes"].toArray()) {
        NodeGraphNode node;
        if (!nodeFromJson(nodeValue, node, projectDir, outError)) {
            return false;
        }
        if (!node.id.isValid() || loaded.graph.nodes.find(node.id.value) != loaded.graph.nodes.end()) {
            setError(outError, "Project graph contains duplicate or invalid node ids.");
            return false;
        }
        loaded.graph.nodes[node.id.value] = node;
    }

    for (const QJsonValue& edgeValue : graph["edges"].toArray()) {
        NodeGraphEdge edge;
        if (!edgeFromJson(edgeValue, edge, outError)) {
            return false;
        }
        if (!edge.id.isValid() || loaded.graph.edges.find(edge.id)) {
            setError(outError, "Project graph contains duplicate or invalid edge ids.");
            return false;
        }
        loaded.graph.edges.upsert(edge);
    }

    if (root.contains("viewport") && !viewportFromJson(root["viewport"], loaded.viewport, outError)) {
        return false;
    }
    if (root.contains("worldUnit")) {
        units::LengthUnit parsed = units::defaultLengthUnit();
        if (!root["worldUnit"].isString() ||
            !units::tryParse(root["worldUnit"].toString().toStdString(), parsed)) {
            setError(outError, "Project worldUnit is invalid.");
            return false;
        }
        loaded.worldUnit = parsed;
    }

    outData = loaded;
    return true;
}

void ProjectFile::setError(QString* outError, const QString& error) {
    if (outError) {
        *outError = error;
    }
}

QString ProjectFile::toRelativePath(const QString& path, const QDir& projectDir) {
    if (path.isEmpty()) {
        return path;
    }

    QFileInfo info(path);
    if (!info.isAbsolute()) {
        return path;
    }

    const QString absolutePath = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    const QString relativePath = projectDir.relativeFilePath(absolutePath);
    if (relativePath.startsWith("..") || QDir::isAbsolutePath(relativePath)) {
        return path;
    }
    return relativePath;
}

QString ProjectFile::toAbsolutePath(const QString& path, const QDir& projectDir) {
    if (path.isEmpty() || QDir::isAbsolutePath(path)) {
        return path;
    }
    return QDir::cleanPath(projectDir.absoluteFilePath(path));
}

QString ProjectFile::valueTypeToString(NodeGraphValueType value) {
    return QString::fromStdString(::valueTypeToString(value));
}

bool ProjectFile::valueTypeFromString(const QString& text, NodeGraphValueType& outValue) {
    if (text == "None") outValue = NodeGraphValueType::None;
    else if (text == "Mesh") outValue = NodeGraphValueType::Mesh;
    else if (text == "Remesh") outValue = NodeGraphValueType::Remesh;
    else if (text == "HeatModel") outValue = NodeGraphValueType::HeatModel;
    else if (text == "Points") outValue = NodeGraphValueType::Points;
    else if (text == "Volume") outValue = NodeGraphValueType::Volume;
    else if (text == "Field") outValue = NodeGraphValueType::Field;
    else if (text == "Vector3") outValue = NodeGraphValueType::Vector3;
    else if (text == "ScalarFloat") outValue = NodeGraphValueType::ScalarFloat;
    else if (text == "ScalarInt") outValue = NodeGraphValueType::ScalarInt;
    else if (text == "ScalarBool") outValue = NodeGraphValueType::ScalarBool;
    else return false;
    return true;
}

QString ProjectFile::directionToString(NodeGraphSocketDirection direction) {
    return direction == NodeGraphSocketDirection::Input ? "Input" : "Output";
}

bool ProjectFile::directionFromString(const QString& text, NodeGraphSocketDirection& outDirection) {
    if (text == "Input") {
        outDirection = NodeGraphSocketDirection::Input;
        return true;
    }
    if (text == "Output") {
        outDirection = NodeGraphSocketDirection::Output;
        return true;
    }
    return false;
}

QString ProjectFile::paramTypeToString(NodeGraphParamType type) {
    switch (type) {
    case NodeGraphParamType::Float: return "Float";
    case NodeGraphParamType::Int: return "Int";
    case NodeGraphParamType::Bool: return "Bool";
    case NodeGraphParamType::String: return "String";
    case NodeGraphParamType::Enum: return "Enum";
    case NodeGraphParamType::Struct: return "Struct";
    case NodeGraphParamType::Array: return "Array";
    }
    return "Float";
}

bool ProjectFile::paramTypeFromString(const QString& text, NodeGraphParamType& outType) {
    if (text == "Float") outType = NodeGraphParamType::Float;
    else if (text == "Int") outType = NodeGraphParamType::Int;
    else if (text == "Bool") outType = NodeGraphParamType::Bool;
    else if (text == "String") outType = NodeGraphParamType::String;
    else if (text == "Enum") outType = NodeGraphParamType::Enum;
    else if (text == "Struct") outType = NodeGraphParamType::Struct;
    else if (text == "Array") outType = NodeGraphParamType::Array;
    else return false;
    return true;
}

QJsonArray ProjectFile::vec3ToJson(const glm::vec3& value) {
    QJsonArray array;
    array.append(value.x);
    array.append(value.y);
    array.append(value.z);
    return array;
}

QJsonArray ProjectFile::quatToJson(const glm::quat& value) {
    QJsonArray array;
    array.append(value.x);
    array.append(value.y);
    array.append(value.z);
    array.append(value.w);
    return array;
}

bool ProjectFile::vec3FromJson(const QJsonValue& value, glm::vec3& outValue) {
    if (!value.isArray()) {
        return false;
    }
    const QJsonArray array = value.toArray();
    if (array.size() != 3 || !array[0].isDouble() || !array[1].isDouble() || !array[2].isDouble()) {
        return false;
    }
    outValue = glm::vec3(
        static_cast<float>(array[0].toDouble()),
        static_cast<float>(array[1].toDouble()),
        static_cast<float>(array[2].toDouble()));
    return true;
}

bool ProjectFile::quatFromJson(const QJsonValue& value, glm::quat& outValue) {
    if (!value.isArray()) {
        return false;
    }
    const QJsonArray array = value.toArray();
    if (array.size() != 4 || !array[0].isDouble() || !array[1].isDouble() || !array[2].isDouble() || !array[3].isDouble()) {
        return false;
    }
    outValue = glm::quat(
        static_cast<float>(array[3].toDouble()),
        static_cast<float>(array[0].toDouble()),
        static_cast<float>(array[1].toDouble()),
        static_cast<float>(array[2].toDouble()));
    return true;
}

QJsonObject ProjectFile::socketToJson(const NodeGraphSocket& socket) {
    QJsonObject obj;
    obj["id"] = static_cast<int>(socket.id.value);
    obj["name"] = QString::fromStdString(socket.name);
    obj["valueType"] = valueTypeToString(socket.valueType);
    if (!socket.acceptedValueTypes.empty()) {
        QJsonArray accepted;
        for (NodeGraphValueType vt : socket.acceptedValueTypes) {
            accepted.append(valueTypeToString(vt));
        }
        obj["acceptedValueTypes"] = accepted;
    }
    obj["direction"] = directionToString(socket.direction);
    QJsonObject contract;
    contract["producedPayloadType"] = static_cast<int>(socket.contract.producedPayloadType);
    obj["contract"] = contract;
    obj["variadic"] = socket.variadic;
    obj["required"] = socket.required;
    return obj;
}

QJsonObject ProjectFile::paramToJson(const NodeGraphParamValue& parameter, const NodeGraphNode& node, const QDir& projectDir) {
    QJsonObject obj;
    obj["id"] = static_cast<int>(parameter.id);
    obj["type"] = paramTypeToString(parameter.type);
    switch (parameter.type) {
    case NodeGraphParamType::Float:
        obj["floatValue"] = parameter.floatValue;
        break;
    case NodeGraphParamType::Int:
        obj["intValue"] = static_cast<qint64>(parameter.intValue);
        break;
    case NodeGraphParamType::Bool:
        obj["boolValue"] = parameter.boolValue;
        break;
    case NodeGraphParamType::String: {
        QString value = QString::fromStdString(parameter.stringValue);
        if (node.typeId == nodegraphtypes::Model && parameter.id == nodegraphparams::model::Path) {
            value = toRelativePath(value, projectDir);
        }
        obj["stringValue"] = value;
        break;
    }
    case NodeGraphParamType::Enum:
        obj["enumValue"] = QString::fromStdString(parameter.enumValue);
        obj["intValue"] = static_cast<qint64>(parameter.intValue);
        break;
    case NodeGraphParamType::Struct: {
        QJsonArray fields;
        for (const NodeGraphParamFieldValue& field : parameter.fieldValues) {
            fields.append(fieldValueToJson(field, node, projectDir));
        }
        obj["fields"] = fields;
        break;
    }
    case NodeGraphParamType::Array: {
        QJsonArray values;
        for (const NodeGraphParamValue& value : parameter.arrayValues) {
            values.append(paramToJson(value, node, projectDir));
        }
        obj["values"] = values;
        break;
    }
    }
    return obj;
}

QJsonObject ProjectFile::fieldValueToJson(const NodeGraphParamFieldValue& field, const NodeGraphNode& node, const QDir& projectDir) {
    QJsonObject obj;
    obj["name"] = QString::fromStdString(field.name);
    if (field.value) {
        obj["value"] = paramToJson(*field.value, node, projectDir);
    }
    return obj;
}

QJsonObject ProjectFile::nodeToJson(const NodeGraphNode& node, const QDir& projectDir) {
    QJsonObject obj;
    obj["id"] = static_cast<int>(node.id.value);
    obj["typeId"] = QString::fromStdString(node.typeId);
    obj["title"] = QString::fromStdString(node.title);
    obj["x"] = node.x;
    obj["y"] = node.y;

    QJsonObject flags;
    const NodeGraphNodeState::Flags& stateFlags = node.state.flags();
    flags["displayEnabled"] = stateFlags.displayEnabled;
    flags["frozen"] = stateFlags.frozenEnabled;
    obj["flags"] = flags;

    QJsonArray inputs;
    for (const NodeGraphSocket& socket : node.inputs) {
        inputs.append(socketToJson(socket));
    }
    obj["inputs"] = inputs;

    QJsonArray outputs;
    for (const NodeGraphSocket& socket : node.outputs) {
        outputs.append(socketToJson(socket));
    }
    obj["outputs"] = outputs;

    QJsonArray parameters;
    for (const NodeGraphParamValue& parameter : node.parameters) {
        parameters.append(paramToJson(parameter, node, projectDir));
    }
    obj["parameters"] = parameters;
    return obj;
}

QJsonObject ProjectFile::edgeToJson(const NodeGraphEdge& edge) {
    QJsonObject obj;
    obj["id"] = static_cast<int>(edge.id.value);
    obj["fromNode"] = static_cast<int>(edge.fromNode.value);
    obj["fromSocket"] = static_cast<int>(edge.fromSocket.value);
    obj["toNode"] = static_cast<int>(edge.toNode.value);
    obj["toSocket"] = static_cast<int>(edge.toSocket.value);
    return obj;
}

QJsonObject ProjectFile::viewportToJson(const Viewport& viewport) {
    QJsonObject obj;
    obj["lookAt"] = vec3ToJson(viewport.lookAt);
    obj["orientation"] = quatToJson(viewport.orientation);
    obj["radius"] = viewport.radius;
    obj["fov"] = viewport.fov;
    obj["projection"] = viewport.projectionMode == CameraProjectionMode::Orthographic
        ? QStringLiteral("orthographic")
        : QStringLiteral("perspective");
    obj["orthographicHeight"] = viewport.orthographicHeight;
    obj["zoomSpeed"] = viewport.zoomSpeed;
    obj["panSpeed"] = viewport.panSpeed;
    obj["background"] = viewport.backgroundMode == app::BackgroundMode::SolidColor
        ? QStringLiteral("solid")
        : QStringLiteral("image");
    obj["navigationCubeVisible"] = viewport.navigationCubeVisible;
    obj["axisLabelsVisible"] = viewport.axisLabelsVisible;
    return obj;
}

bool ProjectFile::socketFromJson(const QJsonValue& value, NodeGraphSocket& outSocket, QString* outError) {
    if (!value.isObject()) {
        setError(outError, "Socket entry is not an object.");
        return false;
    }
    const QJsonObject obj = value.toObject();
    NodeGraphValueType valueType = NodeGraphValueType::None;
    NodeGraphSocketDirection direction = NodeGraphSocketDirection::Input;
    if (!obj["id"].isDouble() || !obj["name"].isString() || !obj["valueType"].isString() ||
        !obj["direction"].isString() || !obj["contract"].isObject() || !obj["variadic"].isBool() ||
        !valueTypeFromString(obj["valueType"].toString(), valueType) ||
        !directionFromString(obj["direction"].toString(), direction)) {
        setError(outError, "Socket entry has invalid fields.");
        return false;
    }

    const QJsonObject contract = obj["contract"].toObject();
    if (!contract["producedPayloadType"].isDouble()) {
        setError(outError, "Socket contract has invalid fields.");
        return false;
    }

    outSocket.id = NodeGraphSocketId{static_cast<uint32_t>(obj["id"].toInt())};
    outSocket.name = obj["name"].toString().toStdString();
    outSocket.valueType = valueType;
    outSocket.direction = direction;
    outSocket.contract.producedPayloadType = static_cast<uint8_t>(contract["producedPayloadType"].toInt());
    outSocket.variadic = obj["variadic"].toBool();
    outSocket.required = obj.contains("required") ? obj["required"].toBool() : true;
    if (obj.contains("acceptedValueTypes") && obj["acceptedValueTypes"].isArray()) {
        for (const QJsonValue& vtValue : obj["acceptedValueTypes"].toArray()) {
            NodeGraphValueType vt = NodeGraphValueType::None;
            if (valueTypeFromString(vtValue.toString(), vt) && vt != NodeGraphValueType::None) {
                outSocket.acceptedValueTypes.push_back(vt);
            }
        }
    }
    return true;
}

bool ProjectFile::paramFromJson(const QJsonValue& value, NodeGraphParamValue& outParameter, const NodeGraphNode& node, const QDir& projectDir, QString* outError) {
    if (!value.isObject()) {
        setError(outError, "Parameter entry is not an object.");
        return false;
    }
    const QJsonObject obj = value.toObject();
    NodeGraphParamType type = NodeGraphParamType::Float;
    if (!obj["id"].isDouble() || !obj["type"].isString() || !paramTypeFromString(obj["type"].toString(), type)) {
        setError(outError, "Parameter entry has invalid fields.");
        return false;
    }

    outParameter = {};
    outParameter.id = static_cast<uint32_t>(obj["id"].toInt());
    outParameter.type = type;
    switch (type) {
    case NodeGraphParamType::Float:
        if (!obj["floatValue"].isDouble()) {
            setError(outError, "Float parameter is missing floatValue.");
            return false;
        }
        outParameter.floatValue = obj["floatValue"].toDouble();
        break;
    case NodeGraphParamType::Int:
        if (!obj["intValue"].isString() && !obj["intValue"].isDouble()) {
            setError(outError, "Int parameter is missing intValue.");
            return false;
        }
        if (obj["intValue"].isString()) {
            bool ok = false;
            outParameter.intValue = obj["intValue"].toString().toLongLong(&ok);
            if (!ok) {
                setError(outError, "Int parameter has an invalid intValue.");
                return false;
            }
        } else {
            outParameter.intValue = static_cast<int64_t>(obj["intValue"].toDouble());
        }
        break;
    case NodeGraphParamType::Bool:
        if (!obj["boolValue"].isBool()) {
            setError(outError, "Bool parameter is missing boolValue.");
            return false;
        }
        outParameter.boolValue = obj["boolValue"].toBool();
        break;
    case NodeGraphParamType::String: {
        if (!obj["stringValue"].isString()) {
            setError(outError, "String parameter is missing stringValue.");
            return false;
        }
        QString stringValue = obj["stringValue"].toString();
        if (node.typeId == nodegraphtypes::Model && outParameter.id == nodegraphparams::model::Path) {
            stringValue = toAbsolutePath(stringValue, projectDir);
        }
        outParameter.stringValue = stringValue.toStdString();
        break;
    }
    case NodeGraphParamType::Enum:
        if (!obj["enumValue"].isString()) {
            setError(outError, "Enum parameter is missing enumValue.");
            return false;
        }
        outParameter.enumValue = obj["enumValue"].toString().toStdString();
        if (obj["intValue"].isDouble()) {
            outParameter.intValue = static_cast<int64_t>(obj["intValue"].toDouble());
        } else if (obj["intValue"].isString()) {
            bool ok = false;
            outParameter.intValue = obj["intValue"].toString().toLongLong(&ok);
            if (!ok) {
                setError(outError, "Enum parameter has an invalid intValue.");
                return false;
            }
        }
        break;
    case NodeGraphParamType::Struct: {
        if (!obj["fields"].isArray()) {
            setError(outError, "Struct parameter is missing fields.");
            return false;
        }
        for (const QJsonValue& fieldValue : obj["fields"].toArray()) {
            NodeGraphParamFieldValue field;
            if (!fieldValueFromJson(fieldValue, field, node, projectDir, outError)) {
                return false;
            }
            outParameter.fieldValues.push_back(field);
        }
        break;
    }
    case NodeGraphParamType::Array: {
        if (!obj["values"].isArray()) {
            setError(outError, "Array parameter is missing values.");
            return false;
        }
        for (const QJsonValue& itemValue : obj["values"].toArray()) {
            NodeGraphParamValue item;
            if (!paramFromJson(itemValue, item, node, projectDir, outError)) {
                return false;
            }
            outParameter.arrayValues.push_back(item);
        }
        break;
    }
    }
    return true;
}

bool ProjectFile::fieldValueFromJson(const QJsonValue& value, NodeGraphParamFieldValue& outField, const NodeGraphNode& node, const QDir& projectDir, QString* outError) {
    if (!value.isObject()) {
        setError(outError, "Parameter field entry is not an object.");
        return false;
    }
    const QJsonObject obj = value.toObject();
    if (!obj["name"].isString() || !obj["value"].isObject()) {
        setError(outError, "Parameter field entry has invalid fields.");
        return false;
    }

    outField.name = obj["name"].toString().toStdString();
    outField.value = std::make_shared<NodeGraphParamValue>();
    return paramFromJson(obj["value"], *outField.value, node, projectDir, outError);
}

bool ProjectFile::nodeFromJson(const QJsonValue& value, NodeGraphNode& outNode, const QDir& projectDir, QString* outError) {
    if (!value.isObject()) {
        setError(outError, "Node entry is not an object.");
        return false;
    }
    const QJsonObject obj = value.toObject();
    const QJsonObject flags = obj["flags"].toObject();
    if (!obj["id"].isDouble() || !obj["typeId"].isString() || !obj["title"].isString() ||
        !obj["x"].isDouble() || !obj["y"].isDouble() || !obj["flags"].isObject() ||
        !flags["displayEnabled"].isBool() || !flags["frozen"].isBool() ||
        !obj["inputs"].isArray() || !obj["outputs"].isArray() || !obj["parameters"].isArray()) {
        setError(outError, "Node entry has invalid fields.");
        return false;
    }

    outNode = {};
    outNode.id = NodeGraphNodeId{static_cast<uint32_t>(obj["id"].toInt())};
    outNode.typeId = obj["typeId"].toString().toStdString();
    outNode.title = obj["title"].toString().toStdString();
    outNode.x = static_cast<float>(obj["x"].toDouble());
    outNode.y = static_cast<float>(obj["y"].toDouble());

    NodeGraphNodeState::Flags stateFlags{};
    stateFlags.displayEnabled = flags["displayEnabled"].toBool();
    stateFlags.frozenEnabled = flags["frozen"].toBool();
    outNode.state.restore(stateFlags);

    for (const QJsonValue& socketValue : obj["inputs"].toArray()) {
        NodeGraphSocket socket;
        if (!socketFromJson(socketValue, socket, outError)) {
            return false;
        }
        outNode.inputs.push_back(socket);
    }
    for (const QJsonValue& socketValue : obj["outputs"].toArray()) {
        NodeGraphSocket socket;
        if (!socketFromJson(socketValue, socket, outError)) {
            return false;
        }
        outNode.outputs.push_back(socket);
    }
    for (const QJsonValue& parameterValue : obj["parameters"].toArray()) {
        NodeGraphParamValue parameter;
        if (!paramFromJson(parameterValue, parameter, outNode, projectDir, outError)) {
            return false;
        }
        outNode.parameters.push_back(parameter);
    }
    return true;
}

bool ProjectFile::edgeFromJson(const QJsonValue& value, NodeGraphEdge& outEdge, QString* outError) {
    if (!value.isObject()) {
        setError(outError, "Edge entry is not an object.");
        return false;
    }
    const QJsonObject obj = value.toObject();
    if (!obj["id"].isDouble() || !obj["fromNode"].isDouble() || !obj["fromSocket"].isDouble() ||
        !obj["toNode"].isDouble() || !obj["toSocket"].isDouble()) {
        setError(outError, "Edge entry has invalid fields.");
        return false;
    }

    outEdge.id = NodeGraphEdgeId{static_cast<uint32_t>(obj["id"].toInt())};
    outEdge.fromNode = NodeGraphNodeId{static_cast<uint32_t>(obj["fromNode"].toInt())};
    outEdge.fromSocket = NodeGraphSocketId{static_cast<uint32_t>(obj["fromSocket"].toInt())};
    outEdge.toNode = NodeGraphNodeId{static_cast<uint32_t>(obj["toNode"].toInt())};
    outEdge.toSocket = NodeGraphSocketId{static_cast<uint32_t>(obj["toSocket"].toInt())};
    return true;
}

bool ProjectFile::viewportFromJson(const QJsonValue& value, Viewport& outViewport, QString* outError) {
    if (!value.isObject()) {
        setError(outError, "Viewport entry is not an object.");
        return false;
    }
    const QJsonObject obj = value.toObject();
    if (!vec3FromJson(obj["lookAt"], outViewport.lookAt) ||
        !quatFromJson(obj["orientation"], outViewport.orientation) ||
        !obj["radius"].isDouble() ||
        !obj["fov"].isDouble()) {
        setError(outError, "Viewport entry has invalid fields.");
        return false;
    }
    outViewport.radius = static_cast<float>(obj["radius"].toDouble());
    outViewport.fov = static_cast<float>(obj["fov"].toDouble());
    const QJsonValue projectionValue = obj["projection"];
    if (projectionValue.isUndefined()) {
        outViewport.projectionMode = CameraProjectionMode::Perspective;
    } else if (!projectionValue.isString()) {
        setError(outError, "Viewport projection must be a string.");
        return false;
    } else if (projectionValue.toString() == QStringLiteral("perspective")) {
        outViewport.projectionMode = CameraProjectionMode::Perspective;
    } else if (projectionValue.toString() == QStringLiteral("orthographic")) {
        outViewport.projectionMode = CameraProjectionMode::Orthographic;
    } else {
        setError(outError, "Viewport projection is not recognized.");
        return false;
    }

    const QJsonValue orthographicHeightValue = obj["orthographicHeight"];
    if (orthographicHeightValue.isUndefined()) {
        outViewport.orthographicHeight = 2.0f * outViewport.radius *
            std::tan(glm::radians(outViewport.fov) * 0.5f);
    } else if (!orthographicHeightValue.isDouble() ||
               !std::isfinite(orthographicHeightValue.toDouble()) ||
               orthographicHeightValue.toDouble() <= 0.0) {
        setError(outError, "Viewport orthographic height is invalid.");
        return false;
    } else {
        outViewport.orthographicHeight = static_cast<float>(orthographicHeightValue.toDouble());
    }

    const QJsonValue zoomSpeedValue = obj["zoomSpeed"];
    if (zoomSpeedValue.isUndefined()) {
        outViewport.zoomSpeed = Camera::DefaultZoomSpeed;
    } else if (!zoomSpeedValue.isDouble() ||
               !std::isfinite(zoomSpeedValue.toDouble()) ||
               zoomSpeedValue.toDouble() < Camera::MinZoomSpeed ||
               zoomSpeedValue.toDouble() > Camera::MaxZoomSpeed) {
        setError(outError, "Viewport zoom speed is invalid.");
        return false;
    } else {
        outViewport.zoomSpeed = static_cast<float>(zoomSpeedValue.toDouble());
    }

    const QJsonValue panSpeedValue = obj["panSpeed"];
    if (panSpeedValue.isUndefined()) {
        outViewport.panSpeed = Camera::DefaultPanSpeed;
    } else if (!panSpeedValue.isDouble() ||
               !std::isfinite(panSpeedValue.toDouble()) ||
               panSpeedValue.toDouble() < Camera::MinPanSpeed ||
               panSpeedValue.toDouble() > Camera::MaxPanSpeed) {
        setError(outError, "Viewport pan speed is invalid.");
        return false;
    } else {
        outViewport.panSpeed = static_cast<float>(panSpeedValue.toDouble());
    }

    const QJsonValue backgroundValue = obj["background"];
    if (backgroundValue.isUndefined() || backgroundValue.toString() == QStringLiteral("image")) {
        outViewport.backgroundMode = app::BackgroundMode::Image;
    } else if (backgroundValue.isString() && backgroundValue.toString() == QStringLiteral("solid")) {
        outViewport.backgroundMode = app::BackgroundMode::SolidColor;
    } else {
        setError(outError, "Viewport background is not recognized.");
        return false;
    }

    const QJsonValue navigationCubeValue = obj["navigationCubeVisible"];
    if (!navigationCubeValue.isUndefined() && !navigationCubeValue.isBool()) {
        setError(outError, "Viewport navigation cube visibility is invalid.");
        return false;
    }
    outViewport.navigationCubeVisible = navigationCubeValue.isUndefined()
        ? app::RenderSettings{}.navigationCubeVisible
        : navigationCubeValue.toBool();

    const QJsonValue axisLabelsValue = obj["axisLabelsVisible"];
    if (!axisLabelsValue.isUndefined() && !axisLabelsValue.isBool()) {
        setError(outError, "Viewport axis label visibility is invalid.");
        return false;
    }
    outViewport.axisLabelsVisible = axisLabelsValue.isUndefined()
        ? app::RenderSettings{}.axisLabelsVisible
        : axisLabelsValue.toBool();
    return true;
}
