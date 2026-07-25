#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include "MeshImporter.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>

constexpr float StlCreaseAngleDegrees = 50.0f;
static const float StlCreaseCosine = std::cos(glm::radians(StlCreaseAngleDegrees));

static void generateStlCreaseNormals(MeshImporter::Mesh& outMesh) {
    const std::size_t vertexCount = outMesh.positions.size() / 3;
    const std::size_t numTriangles = outMesh.triangleCornerIndices.size() / 3;
    if (vertexCount == 0 || numTriangles == 0) return;

    std::vector<glm::vec3> faceNormals(numTriangles, glm::vec3(0.0f, 0.0f, 1.0f));
    std::vector<std::array<float, 3>> cornerAngles(numTriangles, {0.0f, 0.0f, 0.0f});
    std::vector<std::vector<uint32_t>> vertexIncidentFaces(vertexCount);

    for (std::size_t t = 0; t < numTriangles; ++t) {
        const uint32_t c0 = outMesh.triangleCornerIndices[3 * t + 0];
        const uint32_t c1 = outMesh.triangleCornerIndices[3 * t + 1];
        const uint32_t c2 = outMesh.triangleCornerIndices[3 * t + 2];

        if (c0 >= outMesh.corners.size() || c1 >= outMesh.corners.size() || c2 >= outMesh.corners.size()) continue;

        const int32_t v0Idx = outMesh.corners[c0].vertexIndex;
        const int32_t v1Idx = outMesh.corners[c1].vertexIndex;
        const int32_t v2Idx = outMesh.corners[c2].vertexIndex;

        if (v0Idx < 0 || v1Idx < 0 || v2Idx < 0 ||
            static_cast<std::size_t>(v0Idx) >= vertexCount ||
            static_cast<std::size_t>(v1Idx) >= vertexCount ||
            static_cast<std::size_t>(v2Idx) >= vertexCount) continue;

        const glm::vec3 p0(outMesh.positions[3 * v0Idx + 0], outMesh.positions[3 * v0Idx + 1], outMesh.positions[3 * v0Idx + 2]);
        const glm::vec3 p1(outMesh.positions[3 * v1Idx + 0], outMesh.positions[3 * v1Idx + 1], outMesh.positions[3 * v1Idx + 2]);
        const glm::vec3 p2(outMesh.positions[3 * v2Idx + 0], outMesh.positions[3 * v2Idx + 1], outMesh.positions[3 * v2Idx + 2]);

        const glm::vec3 e01 = p1 - p0;
        const glm::vec3 e02 = p2 - p0;
        const glm::vec3 e12 = p2 - p1;

        const glm::vec3 rawFaceNormal = glm::cross(e01, e02);
        const float faceArea2 = glm::length(rawFaceNormal);
        if (faceArea2 < 1e-12f) continue;

        const glm::vec3 unitFaceNormal = rawFaceNormal / faceArea2;
        faceNormals[t] = unitFaceNormal;

        const float l01 = glm::length(e01);
        const float l02 = glm::length(e02);
        const float l12 = glm::length(e12);

        if (l01 > 1e-6f && l02 > 1e-6f) {
            cornerAngles[t][0] = std::acos(std::clamp(glm::dot(e01, e02) / (l01 * l02), -1.0f, 1.0f));
        }
        if (l01 > 1e-6f && l12 > 1e-6f) {
            cornerAngles[t][1] = std::acos(std::clamp(glm::dot(-e01, e12) / (l01 * l12), -1.0f, 1.0f));
        }
        if (l02 > 1e-6f && l12 > 1e-6f) {
            cornerAngles[t][2] = std::acos(std::clamp(glm::dot(-e02, -e12) / (l02 * l12), -1.0f, 1.0f));
        }

        vertexIncidentFaces[v0Idx].push_back(static_cast<uint32_t>(t));
        vertexIncidentFaces[v1Idx].push_back(static_cast<uint32_t>(t));
        vertexIncidentFaces[v2Idx].push_back(static_cast<uint32_t>(t));
    }

    outMesh.normals.clear();
    std::vector<uint32_t> normalIndexByCorner(outMesh.corners.size(), 0);

    for (std::size_t v = 0; v < vertexCount; ++v) {
        const auto& incident = vertexIncidentFaces[v];
        if (incident.empty()) continue;

        std::vector<std::vector<uint32_t>> clusterFaces;
        std::vector<glm::vec3> clusterRepresentatives;

        for (uint32_t faceIdx : incident) {
            const glm::vec3& fNormal = faceNormals[faceIdx];
            bool addedToCluster = false;

            for (std::size_t c = 0; c < clusterFaces.size(); ++c) {
                if (glm::dot(fNormal, clusterRepresentatives[c]) >= StlCreaseCosine) {
                    clusterFaces[c].push_back(faceIdx);
                    glm::vec3 newRep(0.0f);
                    for (uint32_t f : clusterFaces[c]) {
                        newRep += faceNormals[f];
                    }
                    const float len = glm::length(newRep);
                    if (len > 1e-6f) clusterRepresentatives[c] = newRep / len;
                    addedToCluster = true;
                    break;
                }
            }

            if (!addedToCluster) {
                clusterFaces.push_back({faceIdx});
                clusterRepresentatives.push_back(fNormal);
            }
        }

        for (std::size_t c = 0; c < clusterFaces.size(); ++c) {
            glm::vec3 accumulated(0.0f);
            for (uint32_t faceIdx : clusterFaces[c]) {
                const uint32_t c0 = outMesh.triangleCornerIndices[3 * faceIdx + 0];
                const uint32_t c1 = outMesh.triangleCornerIndices[3 * faceIdx + 1];
                const uint32_t c2 = outMesh.triangleCornerIndices[3 * faceIdx + 2];

                float angle = 1.0f;
                if (c0 < outMesh.corners.size() && outMesh.corners[c0].vertexIndex == static_cast<int32_t>(v)) {
                    angle = cornerAngles[faceIdx][0];
                } else if (c1 < outMesh.corners.size() && outMesh.corners[c1].vertexIndex == static_cast<int32_t>(v)) {
                    angle = cornerAngles[faceIdx][1];
                } else if (c2 < outMesh.corners.size() && outMesh.corners[c2].vertexIndex == static_cast<int32_t>(v)) {
                    angle = cornerAngles[faceIdx][2];
                }

                accumulated += faceNormals[faceIdx] * angle;
            }

            glm::vec3 finalNormal = accumulated;
            const float len = glm::length(finalNormal);
            if (len > 1e-6f) finalNormal /= len;
            else finalNormal = glm::vec3(0.0f, 0.0f, 1.0f);

            const uint32_t newNormalIndex = static_cast<uint32_t>(outMesh.normals.size() / 3);
            outMesh.normals.push_back(finalNormal.x);
            outMesh.normals.push_back(finalNormal.y);
            outMesh.normals.push_back(finalNormal.z);

            for (uint32_t faceIdx : clusterFaces[c]) {
                const uint32_t c0 = outMesh.triangleCornerIndices[3 * faceIdx + 0];
                const uint32_t c1 = outMesh.triangleCornerIndices[3 * faceIdx + 1];
                const uint32_t c2 = outMesh.triangleCornerIndices[3 * faceIdx + 2];

                if (c0 < outMesh.corners.size() && outMesh.corners[c0].vertexIndex == static_cast<int32_t>(v)) {
                    normalIndexByCorner[c0] = newNormalIndex;
                }
                if (c1 < outMesh.corners.size() && outMesh.corners[c1].vertexIndex == static_cast<int32_t>(v)) {
                    normalIndexByCorner[c1] = newNormalIndex;
                }
                if (c2 < outMesh.corners.size() && outMesh.corners[c2].vertexIndex == static_cast<int32_t>(v)) {
                    normalIndexByCorner[c2] = newNormalIndex;
                }
            }
        }
    }

    for (std::size_t i = 0; i < outMesh.corners.size(); ++i) {
        outMesh.corners[i].normalIndex = static_cast<int32_t>(normalIndexByCorner[i]);
    }
}

bool MeshImporter::loadMesh(const std::string& filePath, Mesh& outMesh) {
    outMesh = {};
    if (filePath.empty() || !std::filesystem::exists(filePath)) {
        return false;
    }

    std::string ext = std::filesystem::path(filePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (ext == ".obj") {
        return loadObj(filePath, outMesh) && outMesh.isValid();
    }
    if (ext == ".stl") {
        return loadStl(filePath, outMesh) && outMesh.isValid();
    }

    return false;
}

bool MeshImporter::loadObj(const std::string& filePath, Mesh& outMesh) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filePath.c_str())) {
        return false;
    }

    if (attrib.vertices.empty()) {
        return false;
    }

    outMesh.positions = attrib.vertices;
    outMesh.normals = attrib.normals;
    outMesh.texcoords = attrib.texcoords;

    std::unordered_map<std::string, uint32_t> groupIdByKey;

    const auto getGroupIdForFace = [&](const std::string& shapeName, int materialId) -> uint32_t {
        std::string materialName;
        if (materialId >= 0 && static_cast<std::size_t>(materialId) < materials.size()) {
            materialName = materials[static_cast<std::size_t>(materialId)].name;
        }

        std::string groupKey = shapeName + ":" + materialName;
        auto it = groupIdByKey.find(groupKey);
        if (it != groupIdByKey.end()) {
            return it->second;
        }

        const uint32_t newGroupId = static_cast<uint32_t>(outMesh.groups.size());
        outMesh.groups.push_back(Group{newGroupId, shapeName, materialName});
        groupIdByKey[groupKey] = newGroupId;
        return newGroupId;
    };

    std::size_t totalIndices = 0;
    for (const auto& shape : shapes) {
        totalIndices += shape.mesh.indices.size();
    }

    outMesh.triangleCornerIndices.reserve(totalIndices);
    outMesh.corners.reserve(totalIndices);
    outMesh.triangleGroupIds.reserve(totalIndices / 3);

    for (const auto& shape : shapes) {
        const auto& mesh = shape.mesh;
        std::size_t indexOffset = 0;

        for (std::size_t f = 0; f < mesh.num_face_vertices.size(); ++f) {
            const int fv = mesh.num_face_vertices[f];
            if (fv != 3) {
                indexOffset += fv;
                continue;
            }

            const int materialId = (f < mesh.material_ids.size()) ? mesh.material_ids[f] : -1;
            const uint32_t groupId = getGroupIdForFace(shape.name, materialId);

            const uint32_t c0 = static_cast<uint32_t>(outMesh.corners.size());

            for (int v = 0; v < 3; ++v) {
                const tinyobj::index_t idx = mesh.indices[indexOffset + v];
                outMesh.corners.push_back(Corner{
                    idx.vertex_index,
                    idx.texcoord_index,
                    idx.normal_index
                });
            }

            outMesh.triangleCornerIndices.push_back(c0);
            outMesh.triangleCornerIndices.push_back(c0 + 1);
            outMesh.triangleCornerIndices.push_back(c0 + 2);
            outMesh.triangleGroupIds.push_back(groupId);

            indexOffset += 3;
        }
    }

    if (outMesh.groups.empty()) {
        outMesh.groups.push_back(Group{0, "default", ""});
    }

    return true;
}

bool MeshImporter::loadStl(const std::string& filePath, Mesh& outMesh) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    const std::size_t fileSize = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (fileSize < 84) {
        return loadStlAscii(filePath, outMesh);
    }

    char header[80];
    file.read(header, 80);
    uint32_t numTriangles = 0;
    file.read(reinterpret_cast<char*>(&numTriangles), sizeof(uint32_t));
    if (!file) return loadStlAscii(filePath, outMesh);

    const std::size_t maxPossibleTriangles = (fileSize - 84) / 50;
    if (numTriangles > maxPossibleTriangles || numTriangles == 0) {
        return loadStlAscii(filePath, outMesh);
    }

    const std::size_t expectedBinarySize = 80 + 4 + static_cast<std::size_t>(numTriangles) * 50;
    if (fileSize == expectedBinarySize) {
        return loadStlBinary(filePath, fileSize, outMesh);
    }

    return loadStlAscii(filePath, outMesh);
}

bool MeshImporter::loadStlBinary(const std::string& filePath, std::size_t fileSize, Mesh& outMesh) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    char header[80];
    file.read(header, 80);
    uint32_t numTriangles = 0;
    file.read(reinterpret_cast<char*>(&numTriangles), sizeof(uint32_t));
    if (!file || numTriangles == 0) return false;

    outMesh.groups.push_back(Group{0, "stl_mesh", "stl"});

    std::unordered_map<std::array<float, 3>, uint32_t, MeshImporter::Array3Hash> vertexMap;

    const auto getOrAddVertex = [&](float x, float y, float z) -> uint32_t {
        std::array<float, 3> pos = {x, y, z};
        auto it = vertexMap.find(pos);
        if (it != vertexMap.end()) {
            return it->second;
        }
        const uint32_t newIndex = static_cast<uint32_t>(outMesh.positions.size() / 3);
        outMesh.positions.push_back(x);
        outMesh.positions.push_back(y);
        outMesh.positions.push_back(z);
        vertexMap[pos] = newIndex;
        return newIndex;
    };

    outMesh.triangleCornerIndices.reserve(numTriangles * 3);
    outMesh.corners.reserve(numTriangles * 3);
    outMesh.triangleGroupIds.reserve(numTriangles);

    for (uint32_t i = 0; i < numTriangles; ++i) {
        float facetNormal[3];
        float v0[3], v1[3], v2[3];
        uint16_t attrByteCount = 0;

        file.read(reinterpret_cast<char*>(facetNormal), sizeof(float) * 3);
        file.read(reinterpret_cast<char*>(v0), sizeof(float) * 3);
        file.read(reinterpret_cast<char*>(v1), sizeof(float) * 3);
        file.read(reinterpret_cast<char*>(v2), sizeof(float) * 3);
        file.read(reinterpret_cast<char*>(&attrByteCount), sizeof(uint16_t));

        if (!file) return false;

        const uint32_t idx0 = getOrAddVertex(v0[0], v0[1], v0[2]);
        const uint32_t idx1 = getOrAddVertex(v1[0], v1[1], v1[2]);
        const uint32_t idx2 = getOrAddVertex(v2[0], v2[1], v2[2]);

        const uint32_t c0 = static_cast<uint32_t>(outMesh.corners.size());
        outMesh.corners.push_back(Corner{static_cast<int32_t>(idx0), -1, static_cast<int32_t>(idx0)});
        outMesh.corners.push_back(Corner{static_cast<int32_t>(idx1), -1, static_cast<int32_t>(idx1)});
        outMesh.corners.push_back(Corner{static_cast<int32_t>(idx2), -1, static_cast<int32_t>(idx2)});

        outMesh.triangleCornerIndices.push_back(c0);
        outMesh.triangleCornerIndices.push_back(c0 + 1);
        outMesh.triangleCornerIndices.push_back(c0 + 2);

        outMesh.triangleGroupIds.push_back(0);
    }

    generateStlCreaseNormals(outMesh);
    return true;
}

bool MeshImporter::loadStlAscii(const std::string& filePath, Mesh& outMesh) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    outMesh.groups.push_back(Group{0, "stl_mesh", "stl"});

    std::unordered_map<std::array<float, 3>, uint32_t, MeshImporter::Array3Hash> vertexMap;

    const auto getOrAddVertex = [&](float x, float y, float z) -> uint32_t {
        std::array<float, 3> pos = {x, y, z};
        auto it = vertexMap.find(pos);
        if (it != vertexMap.end()) {
            return it->second;
        }
        const uint32_t newIndex = static_cast<uint32_t>(outMesh.positions.size() / 3);
        outMesh.positions.push_back(x);
        outMesh.positions.push_back(y);
        outMesh.positions.push_back(z);
        vertexMap[pos] = newIndex;
        return newIndex;
    };

    std::string line;
    std::vector<uint32_t> currentFaceIndices;
    currentFaceIndices.reserve(3);

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (token == "vertex") {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (iss >> x >> y >> z) {
                currentFaceIndices.push_back(getOrAddVertex(x, y, z));
            }
        } else if (token == "endfacet") {
            if (currentFaceIndices.size() == 3) {
                const uint32_t c0 = static_cast<uint32_t>(outMesh.corners.size());
                for (uint32_t idx : currentFaceIndices) {
                    outMesh.corners.push_back(Corner{static_cast<int32_t>(idx), -1, static_cast<int32_t>(idx)});
                }
                outMesh.triangleCornerIndices.push_back(c0);
                outMesh.triangleCornerIndices.push_back(c0 + 1);
                outMesh.triangleCornerIndices.push_back(c0 + 2);
                outMesh.triangleGroupIds.push_back(0);
            }
            currentFaceIndices.clear();
        }
    }

    generateStlCreaseNormals(outMesh);
    return true;
}
