#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ContactSampling.hpp"
#include "contact/ContactTypes.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"

class ContactInterface {
public:
    struct Settings {
		float contactRadius = 0.01f;
        float minNormalDot = -0.25f;
    };

    struct ContactLineVertex {
        glm::vec3 position;
        glm::vec3 color;

        bool operator==(const ContactLineVertex& other) const {
            return position.x == other.position.x &&
                position.y == other.position.y &&
                position.z == other.position.z &&
                color.x == other.color.x &&
                color.y == other.color.y &&
                color.z == other.color.z;
        }
    };

    void mapSurfacePoints(
        const SupportingHalfedge::IntrinsicMesh& sourceIntrinsicMesh,
        const std::array<float, 16>& sourceLocalToWorld,
        const std::vector<const SupportingHalfedge::IntrinsicMesh*>& receiverIntrinsicMeshes,
        const std::vector<std::array<float, 16>>& receiverLocalToWorld,
        std::vector<std::vector<ContactPair>>& receiverContactPairs,
        std::vector<ContactLineVertex>& outOutlineVertices,
        std::vector<ContactLineVertex>& outCorrespondenceVertices,
        const Settings& settings);
};
