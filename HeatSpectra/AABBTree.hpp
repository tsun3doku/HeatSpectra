#pragma once

#include <memory>
#include <vector>
#include <queue>
#include <numeric>
#include "Model.hpp"

class AABBTree {
public:
    AABBTree(Model& model) : model(model) {}
    void build(int maxDepth = 5, int minTrianglesPerNode = 5);
    void query(const AABB& range, std::vector<uint32_t>& outTriangles) const;

private:
    void buildRecursive(std::unique_ptr<AABBNode>& node, const std::vector<uint32_t>& triangles, int depth, int maxDepth, int minTriangles);

    Model& model;
    std::unique_ptr<AABBNode> root;
};