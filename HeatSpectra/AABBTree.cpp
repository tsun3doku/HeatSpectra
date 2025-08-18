#include <algorithm>
#include <numeric>

#include "AABBTree.hpp"

void AABBTree::build(int maxDepth, int minTrianglesPerNode) {
    // Make sure we have valid data
    const auto& indices = model.getIndices();

    if (indices.empty()) {
        throw std::runtime_error("Cannot build AABB Tree with empty indices");
        return;
    }

    // Create triangle indices 
    std::vector<uint32_t> allTriangles(indices.size() / 3);
    std::iota(allTriangles.begin(), allTriangles.end(), 0);

    root = std::make_unique<AABBNode>();
    buildRecursive(root, allTriangles, 0, maxDepth, minTrianglesPerNode);
}

void AABBTree::buildRecursive(std::unique_ptr<AABBNode>& node, const std::vector<uint32_t>& triangles, int depth, int maxDepth, int minTriangles) {
    const auto& vertices = model.getVertices();
    const auto& indices = model.getIndices();

    // Compute AABB for this node
    for (uint32_t triIdx : triangles) {
        const auto& vertices = model.getVertices();
        uint32_t base = triIdx * 3;
        node->bounds.expand(vertices[model.getIndices()[base]].pos);
        node->bounds.expand(vertices[model.getIndices()[base + 1]].pos);
        node->bounds.expand(vertices[model.getIndices()[base + 2]].pos);
    }

    // Stop splitting if conditions met
    if (triangles.size() <= minTriangles || depth >= maxDepth) {
        node->triangleIndices = triangles;
        node->isLeaf = true;
        return;
    }

    // Split along longest axis
    glm::vec3 extent = node->bounds.max - node->bounds.min;
    int axis = (extent.x > extent.y && extent.x > extent.z) ? 0 :
        (extent.y > extent.z) ? 1 : 2;
    float splitPos = node->bounds.center()[axis];

    // Partition triangles
    std::vector<uint32_t> leftTriangles, rightTriangles;
    for (uint32_t triIdx : triangles) {
        uint32_t base = triIdx * 3;

        if (base + 2 >= indices.size()) {
            throw std::runtime_error("Triangle index out of range");
            continue;
        }

        glm::vec3 triCenter = (
            model.getVertices()[model.getIndices()[base]].pos +
            model.getVertices()[model.getIndices()[base + 1]].pos +
            model.getVertices()[model.getIndices()[base + 2]].pos
            ) / 3.0f;

        if (triCenter[axis] <= splitPos) 
            leftTriangles.push_back(triIdx);
        else 
            rightTriangles.push_back(triIdx);
    }

    // Create child nodes
    node->left = std::make_unique<AABBNode>();
    node->right = std::make_unique<AABBNode>();
    buildRecursive(node->left, leftTriangles, depth + 1, maxDepth, minTriangles);
    buildRecursive(node->right, rightTriangles, depth + 1, maxDepth, minTriangles);
}

void AABBTree::query(const AABB& range, std::vector<uint32_t>& outTriangles) const {
    std::queue<const AABBNode*> queue;
    queue.push(root.get());

    while (!queue.empty()) {
        const AABBNode* node = queue.front();
        queue.pop();

        if (!node->bounds.intersects(range)) continue;

        if (node->isLeaf) {
            for (uint32_t triIdx : node->triangleIndices) {
                outTriangles.push_back(triIdx);
            }
        }
        else {
            queue.push(node->left.get());
            queue.push(node->right.get());
        }
    }
}