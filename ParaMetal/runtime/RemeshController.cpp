#include "RemeshController.hpp"

#include "hash/HashProduct.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "util/GeometryUtils.hpp"
#include "vulkan/MemoryAllocator.hpp"

RemeshController::OperatingScope::OperatingScope(std::atomic<bool>& isOperating)
    : isOperating(isOperating) {
    previousState = isOperating.exchange(true, std::memory_order_acq_rel);
}

RemeshController::OperatingScope::~OperatingScope() {
    isOperating.store(previousState, std::memory_order_release);
}

RemeshController::RemeshController(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    ModelRegistry& modelRegistry,
    std::atomic<bool>& isOperating)
    : vulkanDevice(vulkanDevice),
      modelRegistry(modelRegistry),
      isOperating(isOperating),
      remesher(vulkanDevice, memoryAllocator) {
}

void RemeshController::apply(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    auto& system = activeSystems[socketKey];
    if (!system) {
        system = std::make_unique<RemeshSystem>(remesher, vulkanDevice, modelRegistry);
    }

    system->setSourceGeometry(config.pointPositions, config.triangleIndices);
    system->setParams(config.iterations, config.minAngleDegrees, config.maxEdgeLength, config.stepSize);
    system->setRuntimeModelId(config.runtimeModelId);

    OperatingScope operatingScope(isOperating);
    if (!system->remesh()) {
        activeSystems.erase(socketKey);
    }
}

bool RemeshController::buildProduct(uint64_t socketKey, RemeshProduct& product) {
    if (socketKey == 0) {
        return false;
    }

    const auto it = activeSystems.find(socketKey);
    if (it == activeSystems.end() || !it->second) {
        return false;
    }
    RemeshSystem& system = *it->second;

    product = {};

    // CPU data
    product.runtimeModelId = system.runtimeModelId();
    product.geometryPositions = toVec3Array(system.sourcePositions());
    product.geometryTriangleIndices = system.sourceTriangles();
    const SupportingHalfedge::IntrinsicMesh& mesh = system.intrinsicMesh();
    product.surfacePositions.reserve(mesh.vertices.size());
    product.surfaceNormals.reserve(mesh.vertices.size());
    for (const SupportingHalfedge::IntrinsicVertex& vertex : mesh.vertices) {
        product.surfacePositions.push_back(vertex.position);
        product.surfaceNormals.push_back(vertex.normal);
    }
    product.surfaceTriangleIndices = mesh.indices;

    // GPU resources
    const SupportingHalfedge::GPUResources gpu = system.takeIntrinsicGpuResources();

    product.intrinsicTriangleCount = gpu.triangleCount;
    product.intrinsicVertexCount = gpu.vertexCount;
    product.averageTriangleArea = gpu.averageTriangleArea;

    product.intrinsicTriangleBuffer = gpu.intrinsicTriangleBuffer;
    product.intrinsicTriangleBufferOffset = gpu.triangleGeometryOffset;
    product.intrinsicVertexBuffer = gpu.intrinsicVertexBuffer;
    product.intrinsicVertexBufferOffset = gpu.vertexGeometryOffset;

    product.supportingHalfedgeBuffer = gpu.bufferS;
    product.supportingHalfedgeOffset = gpu.offsetS;
    product.supportingHalfedgeView = gpu.viewS;
    product.supportingAngleBuffer = gpu.bufferA;
    product.supportingAngleOffset = gpu.offsetA;
    product.supportingAngleView = gpu.viewA;
    product.halfedgeBuffer = gpu.bufferH;
    product.halfedgeOffset = gpu.offsetH;
    product.halfedgeView = gpu.viewH;
    product.edgeBuffer = gpu.bufferE;
    product.edgeOffset = gpu.offsetE;
    product.edgeView = gpu.viewE;
    product.triangleBuffer = gpu.bufferT;
    product.triangleOffset = gpu.offsetT;
    product.triangleView = gpu.viewT;
    product.lengthBuffer = gpu.bufferL;
    product.lengthOffset = gpu.offsetL;
    product.lengthView = gpu.viewL;
    product.inputHalfedgeBuffer = gpu.bufferHInput;
    product.inputHalfedgeOffset = gpu.offsetHInput;
    product.inputHalfedgeView = gpu.viewHInput;
    product.inputEdgeBuffer = gpu.bufferEInput;
    product.inputEdgeOffset = gpu.offsetEInput;
    product.inputEdgeView = gpu.viewEInput;
    product.inputTriangleBuffer = gpu.bufferTInput;
    product.inputTriangleOffset = gpu.offsetTInput;
    product.inputTriangleView = gpu.viewTInput;
    product.inputLengthBuffer = gpu.bufferLInput;
    product.inputLengthOffset = gpu.offsetLInput;
    product.inputLengthView = gpu.viewLInput;

    HashProduct::seal(product);
    return product.isValid();
}

void RemeshController::remove(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    auto it = activeSystems.find(socketKey);
    if (it != activeSystems.end()) {
        if (it->second) {
            it->second->disable();
        }
        activeSystems.erase(it);
    }
}

void RemeshController::disableAll() {
    for (auto& [socketKey, system] : activeSystems) {
        (void)socketKey;
        if (system) {
            system->disable();
        }
    }
    activeSystems.clear();
}
