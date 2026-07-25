#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class MeshImporter {
public:
    struct Corner {
        int32_t vertexIndex = -1;
        int32_t texcoordIndex = -1;
        int32_t normalIndex = -1;
    };

    struct Group {
        uint32_t id = 0;
        std::string name;
        std::string source;
    };

    struct Mesh {
        std::vector<float> positions;                 // 3 floats per vertex (x, y, z)
        std::vector<float> normals;                   // 3 floats per normal (nx, ny, nz)
        std::vector<float> texcoords;                 // 2 floats per UV (u, v)
        std::vector<Corner> corners;
        std::vector<uint32_t> triangleCornerIndices;   // index into corners (3 per triangle)
        std::vector<uint32_t> triangleGroupIds;         // 1 group ID per triangle
        std::vector<Group> groups;

        bool isValid() const {
            if (positions.empty() || positions.size() % 3 != 0) return false;
            if (normals.size() % 3 != 0) return false;
            if (texcoords.size() % 2 != 0) return false;
            if (triangleCornerIndices.empty() || triangleCornerIndices.size() % 3 != 0) return false;
            if (triangleCornerIndices.size() / 3 != triangleGroupIds.size()) return false;

            const std::size_t numPositions = positions.size() / 3;
            const std::size_t numTexcoords = texcoords.size() / 2;
            const std::size_t numNormals = normals.size() / 3;

            for (uint32_t cornerIdx : triangleCornerIndices) {
                if (cornerIdx >= corners.size()) return false;
                const auto& corner = corners[cornerIdx];
                if (corner.vertexIndex < 0 || static_cast<std::size_t>(corner.vertexIndex) >= numPositions) return false;
                if (corner.texcoordIndex >= 0 && static_cast<std::size_t>(corner.texcoordIndex) >= numTexcoords) return false;
                if (corner.normalIndex >= 0 && static_cast<std::size_t>(corner.normalIndex) >= numNormals) return false;
            }

            for (uint32_t groupId : triangleGroupIds) {
                if (groupId >= groups.size()) return false;
            }

            return true;
        }
    };

    struct Array3Hash {
        std::size_t operator()(const std::array<float, 3>& arr) const noexcept {
            const std::size_t h1 = std::hash<float>{}(arr[0]);
            const std::size_t h2 = std::hash<float>{}(arr[1]);
            const std::size_t h3 = std::hash<float>{}(arr[2]);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    static bool loadMesh(const std::string& filePath, Mesh& outMesh);

private:
    static bool loadObj(const std::string& filePath, Mesh& outMesh);
    static bool loadStl(const std::string& filePath, Mesh& outMesh);
    static bool loadStlBinary(const std::string& filePath, std::size_t fileSize, Mesh& outMesh);
    static bool loadStlAscii(const std::string& filePath, Mesh& outMesh);
};
