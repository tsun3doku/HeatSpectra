#include "NodeGraphIconRegistry.hpp"

#include "ui/UiIconRegistry.hpp"

#include <QString>

const char* NodeGraphIconRegistry::iconFolderForType(const NodeTypeId& typeId) {
    if (typeId == "heat_solve") return "HeatSystem";
    if (typeId == "model") return "Model";
    if (typeId == "heat_model") return "HeatModel";
    if (typeId == "remesh") return "Remesh";
    if (typeId == "transform") return "Transform";
    if (typeId == "voronoi") return "VoronoiSystem";
    if (typeId == "mesh_points") return "Points";
    return nullptr;
}

QPixmap NodeGraphIconRegistry::nodePixmapForType(const NodeTypeId& typeId,
                                                 qreal targetPixelWidth) {
    const char* folder = iconFolderForType(typeId);
    return folder
        ? ui::IconRegistry::pixmapForFolder(QString::fromUtf8(folder), targetPixelWidth)
        : QPixmap();
}

QPixmap NodeGraphIconRegistry::screenSpaceNodePixmapForType(const NodeTypeId& typeId,
                                                            qreal logicalWidth) {
    const char* folder = iconFolderForType(typeId);
    return folder
        ? ui::IconRegistry::screenSpacePixmapForFolder(QString::fromUtf8(folder), logicalWidth)
        : QPixmap();
}
