#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

#include "util/Structs.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"

class HeatReceiver;
class HeatSource;
class Model;
class Remesher;

struct Quadrature {
    static constexpr uint32_t count = 7u;
    static constexpr glm::vec3 bary[count] = {
        glm::vec3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f),
        glm::vec3(0.0597158717f, 0.4701420641f, 0.4701420641f),
        glm::vec3(0.4701420641f, 0.0597158717f, 0.4701420641f),
        glm::vec3(0.4701420641f, 0.4701420641f, 0.0597158717f),
        glm::vec3(0.7974269853f, 0.1012865073f, 0.1012865073f),
        glm::vec3(0.1012865073f, 0.7974269853f, 0.1012865073f),
        glm::vec3(0.1012865073f, 0.1012865073f, 0.7974269853f),
    };
    static constexpr float weights[count] = {
        0.2250000000f,
        0.1323941527f,
        0.1323941527f,
        0.1323941527f,
        0.1259391805f,
        0.1259391805f,
        0.1259391805f,
    };
};

class ContactInterface {
public:
    struct Settings {
		float contactRadius = 0.01f;
        float minNormalDot = -0.25f;
    };

    struct ContactLineVertex {
        glm::vec3 position;
        glm::vec3 color;
    };

    explicit ContactInterface(Remesher& remesher);
    void mapSurfacePoints(
        Model& sourceModel,
        const std::vector<std::unique_ptr<HeatReceiver>>& receivers,
        std::vector<std::vector<ContactPairGPU>>& receiverContactPairs,
        std::vector<ContactLineVertex>& outOutlineVertices,
        std::vector<ContactLineVertex>& outCorrespondenceVertices,
        const Settings& settings);

private:
    Remesher& remesher;
};
