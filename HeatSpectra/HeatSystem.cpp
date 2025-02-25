#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <omp.h>
#include <unordered_map>
#include <memory>

#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "ResourceManager.hpp"
#include "VulkanImage.hpp"
#include "CommandBufferManager.hpp"
#include "UniformBufferManager.hpp"
#include "Camera.hpp"
#include "Model.hpp"
#include "HeatSource.hpp"
#include "HeatSystem.hpp"


//                                       [ the following code requires
//                                         good commentary to fully
//                                         understand the logic ]
//

HeatSystem::HeatSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), resourceManager(resourceManager), uniformBufferManager(uniformBufferManager) {

    heatSource = std::make_unique<HeatSource>(vulkanDevice, memoryAllocator, resourceManager.getHeatModel(), maxFramesInFlight);

    generateTetrahedralMesh(resourceManager);
    createTetraBuffer(vulkanDevice, maxFramesInFlight);
    createNeighborBuffer(vulkanDevice);
    createCenterBuffer(vulkanDevice);
    createTimeBuffer(vulkanDevice);

    initializeSurfaceBuffer(resourceManager);
    initializeTetra(vulkanDevice);

    createTetraDescriptorPool(vulkanDevice, maxFramesInFlight);
    createTetraDescriptorSetLayout(vulkanDevice);
    createTetraDescriptorSets(vulkanDevice, maxFramesInFlight);
    createTetraPipeline(vulkanDevice);

    createSurfaceDescriptorPool(vulkanDevice, maxFramesInFlight);
    createSurfaceDescriptorSetLayout(vulkanDevice);
    createSurfaceDescriptorSets(vulkanDevice, resourceManager, maxFramesInFlight);
    createSurfacePipeline(vulkanDevice);

    createComputeCommandBuffers(vulkanDevice, maxFramesInFlight);
}

HeatSystem::~HeatSystem() {
}

void HeatSystem::update(VulkanDevice& vulkanDevice, GLFWwindow* window, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager, UniformBufferObject& ubo, uint32_t WIDTH, uint32_t HEIGHT) {
    // Time calculation
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();

    const float timeScale = 5.0f;
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count() * timeScale;
    lastTime = currentTime;

    // Update GPU time buffer
    if (mappedTimeData) {
        mappedTimeData->deltaTime = deltaTime;
        mappedTimeData->totalTime += deltaTime;
    }

    heatSource->controller(window, deltaTime);

    glm::mat4 heatSourceModelMatrix = glm::translate(glm::mat4(1.0f), resourceManager.getHeatModel().getModelPosition());
    resourceManager.getHeatModel().setModelMatrix(heatSourceModelMatrix);
    heatSource->setHeatSourcePushConstant(heatSourceModelMatrix);

    // Use the already mapped memory from UniformBufferManager
    void* mappedMemory = uniformBufferManager.getUniformBuffersMapped()[0];
    memcpy(mappedMemory, &ubo, sizeof(UniformBufferObject));
}

void HeatSystem::recreateResources(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    VkBuffer oldReadBuffer = tetraFrameBuffers.readBuffer;
    VkDeviceMemory oldReadBufferMemory = tetraFrameBuffers.readBufferMemory;
    VkDeviceSize oldReadOffset = tetraFrameBuffers.readBufferOffset_;
    VkBuffer oldWriteBuffer = tetraFrameBuffers.writeBuffer;
    VkDeviceMemory oldWriteBufferMemory = tetraFrameBuffers.writeBufferMemory;
    VkDeviceSize oldWriteOffset = tetraFrameBuffers.writeBufferOffset_;

    // Copy data from old buffers to new buffers
    VkDeviceSize bufferSize = sizeof(float) * feaMesh.elements.size();
    if (oldReadBuffer != VK_NULL_HANDLE &&
        oldWriteBuffer != VK_NULL_HANDLE &&
        oldReadBuffer != tetraFrameBuffers.readBuffer &&
        oldWriteBuffer != tetraFrameBuffers.writeBuffer) {
        VkCommandBuffer copyCmd = beginSingleTimeCommands(vulkanDevice);

        VkBufferCopy copyRegion{};
        copyRegion.size = bufferSize;
        copyRegion.srcOffset = oldReadOffset;
        copyRegion.dstOffset = tetraFrameBuffers.readBufferOffset_;
        vkCmdCopyBuffer(copyCmd, oldReadBuffer, tetraFrameBuffers.readBuffer, 1, &copyRegion);

        copyRegion.srcOffset = oldWriteOffset;
        copyRegion.dstOffset = tetraFrameBuffers.writeBufferOffset_;
        vkCmdCopyBuffer(copyCmd, oldWriteBuffer, tetraFrameBuffers.writeBuffer, 1, &copyRegion);

        endSingleTimeCommands(vulkanDevice, copyCmd);

        memoryAllocator.free(oldReadBuffer, oldReadOffset);
        memoryAllocator.free(oldWriteBuffer, oldWriteOffset);
    }
    createSurfaceDescriptorPool(vulkanDevice, maxFramesInFlight);
    createSurfaceDescriptorSetLayout(vulkanDevice);
    createTetraDescriptorPool(vulkanDevice, maxFramesInFlight);
    createTetraDescriptorSetLayout(vulkanDevice);
    createTetraPipeline(vulkanDevice);
    createSurfacePipeline(vulkanDevice);

    createComputeCommandBuffers(vulkanDevice, maxFramesInFlight);
    createTetraDescriptorSets(vulkanDevice, maxFramesInFlight);
    createSurfaceDescriptorSets(vulkanDevice, resourceManager, maxFramesInFlight);
}

void HeatSystem::swapBuffers(ResourceManager& resourceManager) {
    // Swap buffer handles
    std::swap(tetraFrameBuffers.readBuffer, tetraFrameBuffers.writeBuffer);
    std::swap(tetraFrameBuffers.readBufferMemory, tetraFrameBuffers.writeBufferMemory);
    std::swap(tetraFrameBuffers.readBufferOffset_, tetraFrameBuffers.writeBufferOffset_);

    // Update ALL descriptors for each frame
    for (size_t i = 0; i < tetraDescriptorSets.size(); ++i) {
        std::array<VkDescriptorBufferInfo, 7> bufferInfos = {
            VkDescriptorBufferInfo{tetraBuffer, tetraBufferOffset_, sizeof(TetrahedralElement) * feaMesh.elements.size()},
            VkDescriptorBufferInfo{tetraFrameBuffers.writeBuffer, tetraFrameBuffers.writeBufferOffset_, sizeof(float) * feaMesh.elements.size()},
            VkDescriptorBufferInfo{tetraFrameBuffers.readBuffer, tetraFrameBuffers.readBufferOffset_, sizeof(float) * feaMesh.elements.size()},
            VkDescriptorBufferInfo{neighborBuffer, neighborBufferOffset_, sizeof(int32_t) * (1 + MAX_NEIGHBORS) * feaMesh.elements.size()},
            VkDescriptorBufferInfo{centerBuffer, centerBufferOffset_, sizeof(glm::vec4) * feaMesh.tetraCenters.size()},
            VkDescriptorBufferInfo{timeBuffer, timeBufferOffset_, sizeof(TimeUniform)},
            VkDescriptorBufferInfo{heatSource->getSourceBuffer(), heatSource->getSourceBufferOffset(), sizeof(HeatSourceVertex) * heatSource->getVertexCount()}
        };

        std::array<VkWriteDescriptorSet, 7> descriptorWrites{};
        for (size_t j = 0; j < descriptorWrites.size(); j++) {
            descriptorWrites[j] = {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                nullptr,
                tetraDescriptorSets[i],
                static_cast<uint32_t>(j),  // binding
                0,
                1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                nullptr,
                &bufferInfos[j],
                nullptr
            };
        }
        // Time buffer uses uniform buffer type
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }

    // Update surface compute descriptor sets
    for (size_t i = 0; i < surfaceDescriptorSets.size(); ++i) {
        std::array<VkDescriptorBufferInfo, 3> surfaceBufferInfos = {
            VkDescriptorBufferInfo{tetraFrameBuffers.readBuffer, tetraFrameBuffers.readBufferOffset_, sizeof(float) * feaMesh.elements.size()},
            VkDescriptorBufferInfo{resourceManager.getVisModel().getSurfaceBuffer(), resourceManager.getVisModel().getSurfaceBufferOffset(), sizeof(SurfaceVertex) * resourceManager.getVisModel().getVertexCount()},
            VkDescriptorBufferInfo{centerBuffer, centerBufferOffset_, sizeof(glm::vec4) * feaMesh.tetraCenters.size()}
        };

        std::array<VkWriteDescriptorSet, 3> surfaceWrites{};
        for (size_t j = 0; j < surfaceWrites.size(); j++) {
            surfaceWrites[j] = {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                nullptr,
                surfaceDescriptorSets[i],
                static_cast<uint32_t>(j),
                0,
                1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                nullptr,
                &surfaceBufferInfos[j],
                nullptr
            };
        }

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), surfaceWrites.size(), surfaceWrites.data(), 0, nullptr);
    }
}

void HeatSystem::generateTetrahedralMesh(ResourceManager& resourceManager) {
    const auto& vertices = resourceManager.getSimModel().getVertices();
    const auto& indices = resourceManager.getSimModel().getIndices();

    if (vertices.empty()) {
        throw std::runtime_error("Vertices vector is empty!");
    }
    if (indices.empty()) {
        throw std::runtime_error("Indices vector is empty!");
    }
    for (size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] >= vertices.size()) {
            std::cerr << "Error: indices[" << i << "] = " << indices[i] << ", but vertices.size() = " << vertices.size() << std::endl;
            throw std::runtime_error("Index out of range!");
        }
    }
    std::vector<REAL> points;

    for (size_t i = 0; i < indices.size(); i++) {
        const auto& vertex = vertices[indices[i]];

        // Create hash key for vertex position
        glm::vec3 pos = vertex.pos;

        // If this position hasn't been seen before, add it
        if (vertexMap.find(pos) == vertexMap.end()) {
            int newIndex = points.size() / 3;
            vertexMap[pos] = newIndex;
            points.push_back(pos.x);
            points.push_back(pos.y);
            points.push_back(pos.z);
        }
        remappedIndices.push_back(vertexMap[pos]);

    }

    std::cout << "Number of unique vertices after deduplication: " << points.size() / 3 << std::endl;

    tetgenio in, out;
    in.firstnumber = 0;
    in.numberofholes = 0;
    in.holelist = nullptr;
    in.numberofpoints = points.size() / 3;
    in.pointlist = new REAL[points.size()];
    std::memcpy(in.pointlist, points.data(), points.size() * sizeof(REAL));

    in.numberofregions = 1;
    in.regionlist = new REAL[5];  // x,y,z,attribute,volume
    in.regionlist[0] = 0.0;  // x
    in.regionlist[1] = 0.0;  // y 
    in.regionlist[2] = 0.0;  // z
    in.regionlist[3] = 1.0;  // region attribute
    in.regionlist[4] = 0.001;  // volume constraint

    in.numberoffacets = remappedIndices.size() / 3;
    in.facetlist = new tetgenio::facet[in.numberoffacets];
    in.facetmarkerlist = new int[in.numberoffacets];

    in.pointmarkerlist = new int[in.numberofpoints];
    for (int i = 0; i < in.numberofpoints; i++) {
        in.pointmarkerlist[i] = 1;
    }

    for (int i = 0; i < in.numberoffacets; ++i) {
        int numVertices = 3;
        in.facetmarkerlist[i] = 1;
        tetgenio::facet& f = in.facetlist[i];
        f.numberofpolygons = 1;
        f.polygonlist = new tetgenio::polygon[f.numberofpolygons];

        tetgenio::polygon& p = f.polygonlist[0];
        p.numberofvertices = numVertices;
        p.vertexlist = new int[p.numberofvertices];

        for (int j = 0; j < numVertices; ++j) {
            p.vertexlist[j] = remappedIndices[i * numVertices + j];
        }
    }

    try {
        // Set TetGen behavior
        tetgenbehavior b;
        b.plc = 1;           // Preserve the input surface mesh
        b.quality = 1;       // Generate quality tetrahedral mesh
        b.nobisect = 0;      // Allow splitting
        b.steinerleft = 100;
        b.quiet = 0;         // Enable output for debugging
        b.minratio = 1.5;    // Add quality mesh ratio
        b.mindihedral = 10;  // Lower minimum dihedral angle
        b.coarsen = 0;       // Enable mesh coarsening
        b.verbose = 1;       // Enable verbose output
        b.docheck = 1;       // Check mesh consistency
        b.refine = 0;        // Disable refining tetrahedra mesh
        b.weighted = 0;      // No weighted Delaunay
        b.metric = 1;        // Use metric for size control

        // Generate tetrahedral mesh
        try {
            tetrahedralize(&b, &in, &out);

        }
        catch (int error_code) {
            printf("Detailed mesh information:\n");
            printf("Vertices: %zu\n", vertices.size());
            printf("Indices: %zu\n", indices.size());
            printf("Input faces: %d\n", in.numberoffacets);
            throw std::runtime_error("TetGen mesh generation failed - Check mesh val idity");
        }
        std::cout << "TetGen output: points=" << out.numberofpoints << ", tetrahedra=" << out.numberoftetrahedra << std::endl;

    }
    catch (int error_code) {
        switch (error_code) {
        case 1:
            printf("Self-intersection found in mesh\n");
            break;
        case 2:
            printf("Very small input feature size detected\n");
            break;
        case 3:
            printf("Invalid mesh topology\n");
            break;
        default:
            printf("TetGen error code: %d\n", error_code);
        }
        throw std::runtime_error("TetGen mesh generation failed");
    }

    // Process generated tetrahedra
    feaMesh.nodes.resize(out.numberofpoints);
    for (int i = 0; i < out.numberofpoints; i++) {
        feaMesh.nodes[i] = glm::vec4(
            out.pointlist[i * 3],
            out.pointlist[i * 3 + 1],
            out.pointlist[i * 3 + 2],
            0.0f  // W component for alignment
        );
    }

    feaMesh.elements.resize(out.numberoftetrahedra);
    for (int i = 0; i < out.numberoftetrahedra; i++) {
        TetrahedralElement tetra;
        tetra.vertices[0] = out.tetrahedronlist[i * 4];
        tetra.vertices[1] = out.tetrahedronlist[i * 4 + 1];
        tetra.vertices[2] = out.tetrahedronlist[i * 4 + 2];
        tetra.vertices[3] = out.tetrahedronlist[i * 4 + 3];
        feaMesh.elements[i] = tetra;
    }
    //std::cout << "feaMesh: nodes = " << feaMesh.nodes.size() << ", elements = " << feaMesh.elements.size() << std::endl;
    std::unordered_map<std::string, std::vector<uint32_t>> faceMap;

    auto createFaceKey = [](uint32_t a, uint32_t b, uint32_t c) {
        if (a > b) std::swap(a, b);
        if (b > c) std::swap(b, c);
        if (a > b) std::swap(a, b);
        return std::to_string(a) + "_" + std::to_string(b) + "_" + std::to_string(c);
        };

    // Build face map
    for (uint32_t tid = 0; tid < feaMesh.elements.size(); ++tid) {
        const auto& t = feaMesh.elements[tid];
        uint32_t v[4] = { t.vertices[0], t.vertices[1], t.vertices[2], t.vertices[3] };

        // Create all 4 faces
        const std::array<std::array<int, 3>, 4> faces = { {
            {0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}
        } };

        for (const auto& face : faces) {
            // Sort vertices to ensure consistent face key
            uint32_t sorted[3] = { v[face[0]], v[face[1]], v[face[2]] };
            std::sort(sorted, sorted + 3);
            auto key = createFaceKey(sorted[0], sorted[1], sorted[2]);
            faceMap[key].push_back(tid);
        }
    }

    // Build neighbor list
    feaMesh.neighbors.resize(feaMesh.elements.size());
    for (auto& [key, tets] : faceMap) {
        // Only faces shared by exactly 2 tetras are valid neighbors
        if (tets.size() == 2) {
            feaMesh.neighbors[tets[0]].push_back(tets[1]);
            feaMesh.neighbors[tets[1]].push_back(tets[0]);
        }
    }

    // Remove duplicates (if any)
    for (auto& neighbors : feaMesh.neighbors) {
        std::sort(neighbors.begin(), neighbors.end());
        auto last = std::unique(neighbors.begin(), neighbors.end());
        neighbors.erase(last, neighbors.end());
    }

    feaMesh.tetraCenters.resize(feaMesh.elements.size());
    for (size_t i = 0; i < feaMesh.elements.size(); i++) {
        glm::vec3 center = calculateTetraCenter(feaMesh.elements[i]);
        feaMesh.tetraCenters[i] = glm::vec4(center, 0.0f);
    }

    // First clean up tetgenio input structures
    if (in.pointlist) {
        delete[] in.pointlist;
        in.pointlist = nullptr;
    }

    for (int i = 0; i < in.numberoffacets; i++) {
        if (in.facetlist[i].polygonlist) {
            for (int j = 0; j < in.facetlist[i].numberofpolygons; j++) {
                if (in.facetlist[i].polygonlist[j].vertexlist) {
                    delete[] in.facetlist[i].polygonlist[j].vertexlist;
                }
            }
            delete[] in.facetlist[i].polygonlist;
        }
    }

    if (in.facetlist) {
        delete[] in.facetlist;
        in.facetlist = nullptr;
    }

    if (in.facetmarkerlist) {
        delete[] in.facetmarkerlist;
        in.facetmarkerlist = nullptr;
    }

    // Then clean up tetgenio output structures
    if (out.pointlist) {
        delete[] out.pointlist;
        out.pointlist = nullptr;
    }

    if (out.tetrahedronlist) {
        delete[] out.tetrahedronlist;
        out.tetrahedronlist = nullptr;
    }

    if (out.facetlist) {
        for (int i = 0; i < out.numberoffacets; i++) {
            if (out.facetlist[i].polygonlist) {
                for (int j = 0; j < out.facetlist[i].numberofpolygons; j++) {
                    if (out.facetlist[i].polygonlist[j].vertexlist) {
                        delete[] out.facetlist[i].polygonlist[j].vertexlist;
                    }
                }
                delete[] out.facetlist[i].polygonlist;
            }
        }
        delete[] out.facetlist;
        out.facetlist = nullptr;
    }

    if (out.facetmarkerlist) {
        delete[] out.facetmarkerlist;
        out.facetmarkerlist = nullptr;
    }

    std::cout << "Generated mesh stats:" << std::endl;
    std::cout << "Nodes: " << feaMesh.nodes.size() << std::endl;
    std::cout << "Elements: " << feaMesh.elements.size() << std::endl;
}

void HeatSystem::initializeSurfaceBuffer(ResourceManager& resourceManager) {
    VkDeviceSize bufferSize = sizeof(SurfaceVertex) * resourceManager.getVisModel().getVertexCount();

    // Create staging buffer
    auto [stagingBuffer, stagingOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    // Initialize surface vertices on the CPU
    std::vector<SurfaceVertex> surfaceVertices(resourceManager.getVisModel().getVertexCount());
    const auto& modelVertices = resourceManager.getVisModel().getVertices();
    for (size_t i = 0; i < resourceManager.getVisModel().getVertexCount(); i++) {
        surfaceVertices[i].position = modelVertices[i].pos;
        surfaceVertices[i].color = glm::vec3(0.0f);
    }

    // Copy to staging buffer
    void* stagingData = memoryAllocator.getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, surfaceVertices.data(), bufferSize);

    // Copy to destination buffers
    VkCommandBuffer copyCmd = beginSingleTimeCommands(vulkanDevice);
    VkBufferCopy copyRegion = { stagingOffset, resourceManager.getVisModel().getSurfaceBufferOffset(), bufferSize };
    vkCmdCopyBuffer(copyCmd, stagingBuffer, resourceManager.getVisModel().getSurfaceBuffer(), 1, &copyRegion);
    endSingleTimeCommands(vulkanDevice, copyCmd);

    memoryAllocator.free(stagingBuffer, stagingOffset);
}

void HeatSystem::createTetraBuffer(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    if (feaMesh.elements.empty()) {
        throw std::runtime_error("No tetrahedral elements to create buffer");
    }

    VkDeviceSize bufferSize = sizeof(TetrahedralElement) * feaMesh.elements.size();
    VkDeviceSize tempBufferSize = sizeof(float) * feaMesh.elements.size();

    // Create tetra buffer
    auto [tetraBufferHandle, tetraBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment
    );
    tetraBuffer = tetraBufferHandle;
    tetraBufferOffset_ = tetraBufferOffset;

    // Map the tetra buffer
    mappedTetraData = static_cast<TetrahedralElement*>(
        memoryAllocator.getMappedPointer(tetraBuffer, tetraBufferOffset_)
        );

    // Create read buffer
    auto [readBufferHandle, readOffset] = memoryAllocator.allocate(
        tempBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment
    );
    tetraFrameBuffers.readBuffer = readBufferHandle;
    tetraFrameBuffers.readBufferOffset_ = readOffset;

    // Create write buffer
    auto [writeBufferHandle, writeOffset] = memoryAllocator.allocate(
        tempBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment
    );
    tetraFrameBuffers.writeBuffer = writeBufferHandle;
    tetraFrameBuffers.writeBufferOffset_ = writeOffset;
}

void HeatSystem::createNeighborBuffer(VulkanDevice& vulkanDevice) {
    std::vector<int32_t> neighborData;

    for (const auto& neighbors : feaMesh.neighbors) {
        uint32_t count = std::min(static_cast<uint32_t>(neighbors.size()), MAX_NEIGHBORS);
        neighborData.push_back(count);
        for (uint32_t i = 0; i < count; ++i) {
            neighborData.push_back(neighbors[i]);
        }

        for (uint32_t i = count; i < MAX_NEIGHBORS; ++i) {
            neighborData.push_back(-1);
        }
    }

    VkDeviceSize bufferSize = neighborData.size() * sizeof(int32_t);

    // Create neighbor buffer
    auto [neighborBufferHandle, neighborBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment
    );
    neighborBuffer = neighborBufferHandle;
    neighborBufferOffset_ = neighborBufferOffset;

    // Map and copy data
    void* data = memoryAllocator.getMappedPointer(neighborBuffer, neighborBufferOffset_);
    memcpy(data, neighborData.data(), bufferSize);
}

void HeatSystem::createCenterBuffer(VulkanDevice& vulkanDevice) {
    VkDeviceSize bufferSize = sizeof(glm::vec4) * feaMesh.tetraCenters.size();

    auto [centerBufferHandle, centerBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment
    );
    centerBuffer = centerBufferHandle;
    centerBufferOffset_ = centerBufferOffset;

    // Map and copy tetra center data
    void* data = memoryAllocator.getMappedPointer(centerBuffer, centerBufferOffset_);
    memcpy(data, feaMesh.tetraCenters.data(), bufferSize);
}

void HeatSystem::createTimeBuffer(VulkanDevice& vulkanDevice) {
    VkDeviceSize bufferSize = sizeof(TimeUniform);

    // Create time buffer
    auto [timeBufferHandle, timeBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment
    );
    timeBuffer = timeBufferHandle;
    timeBufferOffset_ = timeBufferOffset;

    // Get mapped pointer via allocator
    mappedTimeData = static_cast<TimeUniform*>(
        memoryAllocator.getMappedPointer(timeBuffer, timeBufferOffset_)
        );
}

void HeatSystem::initializeTetra(VulkanDevice& vulkanDevice) {
    std::cout << "\n=== Initial Tetra Values ===\n";

    // First set temperatures for each tetrahedron
    for (size_t i = 0; i < feaMesh.elements.size(); i++) {
        feaMesh.elements[i].temperature = 1.0f;
        feaMesh.elements[i].volume = calculateTetraVolume(feaMesh.elements[i]);
        feaMesh.elements[i].density = 2710.0f;
        feaMesh.elements[i].specificHeat = 903.0f;
        feaMesh.elements[i].conductivity = 237.0f;
        feaMesh.elements[i].coolingRate = 0.1f;

        std::cout << "Tetra " << i << ": temp = " << feaMesh.elements[i].temperature
            << ", volume = " << feaMesh.elements[i].volume  
            << ", vertices = ["
            << feaMesh.elements[i].vertices[0] << ", "
            << feaMesh.elements[i].vertices[1] << ", "
            << feaMesh.elements[i].vertices[2] << ", "
            << feaMesh.elements[i].vertices[3] << "]\n";
    }

    // Map and copy the updated elements to GPU buffer
    memcpy(mappedTetraData, feaMesh.elements.data(), sizeof(TetrahedralElement) * feaMesh.elements.size());

    // Copy to temperature buffers using memory allocator
    float* tempData = static_cast<float*>(
        memoryAllocator.getMappedPointer(tetraFrameBuffers.readBuffer, tetraFrameBuffers.readBufferOffset_)
        );

    for (size_t i = 0; i < feaMesh.elements.size(); i++) {
        tempData[i] = feaMesh.elements[i].temperature;
    }

    // Copy readBuffer -> writeBuffer
    VkCommandBuffer copyCmd = beginSingleTimeCommands(vulkanDevice);
    VkBufferCopy copyRegion{};
    copyRegion.size = sizeof(float) * feaMesh.elements.size();
    copyRegion.srcOffset = tetraFrameBuffers.readBufferOffset_;
    copyRegion.dstOffset = tetraFrameBuffers.writeBufferOffset_;
    vkCmdCopyBuffer(copyCmd, tetraFrameBuffers.readBuffer, tetraFrameBuffers.writeBuffer, 1, &copyRegion);
    endSingleTimeCommands(vulkanDevice, copyCmd);
}

glm::vec3 HeatSystem::calculateTetraCenter(const TetrahedralElement& tetra) {
    // Get tetra vertices
    glm::vec3 v0 = feaMesh.nodes[tetra.vertices[0]];
    glm::vec3 v1 = feaMesh.nodes[tetra.vertices[1]];
    glm::vec3 v2 = feaMesh.nodes[tetra.vertices[2]];
    glm::vec3 v3 = feaMesh.nodes[tetra.vertices[3]];

    // Return center of tetrahedron
    return (v0 + v1 + v2 + v3) * 0.25f;
}

float HeatSystem::calculateTetraVolume(const TetrahedralElement& tetra) {
    // Get vertex positions
    glm::vec3 v0 = feaMesh.nodes[tetra.vertices[0]];
    glm::vec3 v1 = feaMesh.nodes[tetra.vertices[1]];
    glm::vec3 v2 = feaMesh.nodes[tetra.vertices[2]];
    glm::vec3 v3 = feaMesh.nodes[tetra.vertices[3]];

    // Calculate scaled edges
    glm::vec3 edge1 = (v1 - v0);
    glm::vec3 edge2 = (v2 - v0);
    glm::vec3 edge3 = (v3 - v0);

    // Return volume using scalar triple product
    return glm::abs(glm::dot(edge1, glm::cross(edge2, edge3))) / 6.0f;
}

void HeatSystem::createTetraDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};

    // Storage buffers
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * 6;  // Storage buffer count

    // Uniform buffer
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight;  // Uniform buffer count

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &tetraDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat descriptor pool");
    }
}

void HeatSystem::createTetraDescriptorSetLayout(const VulkanDevice& vulkanDevice) {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // Static tetra binding
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Temperature write binding
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Temperature read binbing
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Neighbor binding
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Center binding
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Time binding
        {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Heat source binding
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
    bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;

    std::vector<VkDescriptorBindingFlags> flags(bindings.size(),
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT);

    bindingFlags.bindingCount = flags.size();
    bindingFlags.pBindingFlags = flags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    layoutInfo.pNext = &bindingFlags;

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &tetraDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat descriptor set layout");
    }
}

void HeatSystem::createTetraDescriptorSets(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, tetraDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = tetraDescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(maxFramesInFlight);
    allocInfo.pSetLayouts = layouts.data();

    tetraDescriptorSets.resize(maxFramesInFlight);
    if (tetraDescriptorPool == VK_NULL_HANDLE) {
        std::cerr << "ERROR: tetraDescriptorPool is VK_NULL_HANDLE!" << std::endl;
        throw std::runtime_error("Invalid descriptor pool");
    }
    if (tetraDescriptorSetLayout == VK_NULL_HANDLE) {
        std::cerr << "ERROR: tetraDescriptorSetLayout is VK_NULL_HANDLE!" << std::endl;
        throw std::runtime_error("Invalid descriptor set layout");
    }

    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, tetraDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate heat descriptor sets");
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkDescriptorBufferInfo, 7> bufferInfos = {
            VkDescriptorBufferInfo{tetraBuffer, tetraBufferOffset_, sizeof(TetrahedralElement) * feaMesh.elements.size()},
            VkDescriptorBufferInfo{tetraFrameBuffers.writeBuffer, tetraFrameBuffers.writeBufferOffset_, sizeof(float) * feaMesh.elements.size()},
            VkDescriptorBufferInfo{tetraFrameBuffers.readBuffer, tetraFrameBuffers.readBufferOffset_, sizeof(float) * feaMesh.elements.size()},
            VkDescriptorBufferInfo{neighborBuffer, neighborBufferOffset_, sizeof(int32_t) * (1 + MAX_NEIGHBORS) * feaMesh.elements.size()},
            VkDescriptorBufferInfo{centerBuffer, centerBufferOffset_, sizeof(glm::vec4) * feaMesh.tetraCenters.size()},
            VkDescriptorBufferInfo{timeBuffer, timeBufferOffset_, sizeof(TimeUniform)},
            VkDescriptorBufferInfo{heatSource->getSourceBuffer(), heatSource->getSourceBufferOffset(), sizeof(HeatSourceVertex) * heatSource->getVertexCount()},
        };

        std::array<VkWriteDescriptorSet, 7> descriptorWrites{};

        VkWriteDescriptorSet& tetraWrite = descriptorWrites[0];
        tetraWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        tetraWrite.dstSet = tetraDescriptorSets[i];
        tetraWrite.dstBinding = 0;
        tetraWrite.descriptorCount = 1;
        tetraWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        tetraWrite.pBufferInfo = &bufferInfos[0];

        VkWriteDescriptorSet& writeWrite = descriptorWrites[1];
        writeWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeWrite.dstSet = tetraDescriptorSets[i];
        writeWrite.dstBinding = 1;
        writeWrite.descriptorCount = 1;
        writeWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writeWrite.pBufferInfo = &bufferInfos[1];

        VkWriteDescriptorSet& readWrite = descriptorWrites[2];
        readWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        readWrite.dstSet = tetraDescriptorSets[i];
        readWrite.dstBinding = 2;
        readWrite.descriptorCount = 1;
        readWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        readWrite.pBufferInfo = &bufferInfos[2];

        VkWriteDescriptorSet& neighborWrite = descriptorWrites[3];
        neighborWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        neighborWrite.dstSet = tetraDescriptorSets[i];
        neighborWrite.dstBinding = 3;
        neighborWrite.descriptorCount = 1;
        neighborWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        neighborWrite.pBufferInfo = &bufferInfos[3];

        VkWriteDescriptorSet& centerWrite = descriptorWrites[4];
        centerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        centerWrite.dstSet = tetraDescriptorSets[i];
        centerWrite.dstBinding = 4;
        centerWrite.descriptorCount = 1;
        centerWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        centerWrite.pBufferInfo = &bufferInfos[4];

        VkWriteDescriptorSet& timeWrite = descriptorWrites[5];
        timeWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        timeWrite.dstSet = tetraDescriptorSets[i];
        timeWrite.dstBinding = 5;
        timeWrite.descriptorCount = 1;
        timeWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        timeWrite.pBufferInfo = &bufferInfos[5];

        VkWriteDescriptorSet& sourceWrite = descriptorWrites[6];
        sourceWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        sourceWrite.dstSet = tetraDescriptorSets[i];
        sourceWrite.dstBinding = 6;
        sourceWrite.descriptorCount = 1;
        sourceWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sourceWrite.pBufferInfo = &bufferInfos[6];

        vkUpdateDescriptorSets(vulkanDevice.getDevice(),
            static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(), 0, nullptr);
    }
}

void HeatSystem::createTetraPipeline(const VulkanDevice& vulkanDevice) {
    auto computeShaderCode = readFile("shaders/heat_tetra_comp.spv"); //change
    VkShaderModule computeShaderModule = createShaderModule(vulkanDevice, computeShaderCode);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(HeatSourcePushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &tetraDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &tetraPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline layout!");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = tetraPipelineLayout;

    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &tetraPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), computeShaderModule, nullptr);
}

void HeatSystem::createSurfaceDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 1> poolSizes{};

    // Storage buffers
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * 3; // Storage buffers

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr,
        &surfaceDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create surface descriptor pool");
    }
}

void HeatSystem::createSurfaceDescriptorSetLayout(const VulkanDevice& vulkanDevice) {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // Tetra data 
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},

        // Surface vertices 
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},

        // Tetra centers 
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},

    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
    bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;

    std::vector<VkDescriptorBindingFlags> flags(bindings.size(),
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT);

    bindingFlags.bindingCount = flags.size();
    bindingFlags.pBindingFlags = flags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    layoutInfo.pNext = &bindingFlags;

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr,
        &surfaceDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create surface descriptor set layout");
    }
}

void HeatSystem::createSurfaceDescriptorSets(const VulkanDevice& vulkanDevice, ResourceManager& resourceManager, uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, surfaceDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = surfaceDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    surfaceDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo,
        surfaceDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate surface descriptor sets");
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkDescriptorBufferInfo, 3> bufferInfos = {
            VkDescriptorBufferInfo{tetraFrameBuffers.readBuffer, tetraFrameBuffers.readBufferOffset_, sizeof(float) * feaMesh.elements.size()},
            VkDescriptorBufferInfo{resourceManager.getVisModel().getSurfaceBuffer(), resourceManager.getVisModel().getSurfaceBufferOffset(), sizeof(SurfaceVertex) * resourceManager.getVisModel().getVertexCount()},
            VkDescriptorBufferInfo{centerBuffer, centerBufferOffset_, sizeof(glm::vec4) * feaMesh.tetraCenters.size()},
        };

        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};

        VkWriteDescriptorSet& tetraWrite = descriptorWrites[0];
        tetraWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        tetraWrite.dstSet = surfaceDescriptorSets[i];
        tetraWrite.dstBinding = 0;
        tetraWrite.descriptorCount = 1;
        tetraWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        tetraWrite.pBufferInfo = &bufferInfos[0];

        VkWriteDescriptorSet& surfaceWrite = descriptorWrites[1];
        surfaceWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        surfaceWrite.dstSet = surfaceDescriptorSets[i];
        surfaceWrite.dstBinding = 1;
        surfaceWrite.descriptorCount = 1;
        surfaceWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        surfaceWrite.pBufferInfo = &bufferInfos[1];

        VkWriteDescriptorSet& centerWrite = descriptorWrites[2];
        centerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        centerWrite.dstSet = surfaceDescriptorSets[i];
        centerWrite.dstBinding = 2;
        centerWrite.descriptorCount = 1;
        centerWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        centerWrite.pBufferInfo = &bufferInfos[2];

        vkUpdateDescriptorSets(vulkanDevice.getDevice(),
            static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(), 0, nullptr);
    }
}

void HeatSystem::createSurfacePipeline(const VulkanDevice& vulkanDevice) {
    auto computeShaderCode = readFile("shaders/heat_surface_comp.spv");
    VkShaderModule computeShaderModule = createShaderModule(vulkanDevice, computeShaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &surfaceDescriptorSetLayout;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr,
        &surfacePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create surface pipeline layout!");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = surfacePipelineLayout;

    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1,
        &pipelineInfo, nullptr, &surfacePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create surface compute pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), computeShaderModule, nullptr);
}

void HeatSystem::dispatchTetraCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    uint32_t elementCount = feaMesh.elements.size();
    uint32_t workGroupSize = 256;
    uint32_t workGroupCount = (elementCount + workGroupSize - 1) / workGroupSize;

    const auto& pushConstant = heatSource->getHeatSourcePushConstant();

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, tetraPipeline);
    vkCmdPushConstants(
        commandBuffer,
        tetraPipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(HeatSourcePushConstant),
        &pushConstant);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, tetraPipelineLayout, 0, 1, &tetraDescriptorSets[currentFrame], 0, nullptr);
    vkCmdDispatch(commandBuffer, workGroupCount, 1, 1);
}

void HeatSystem::dispatchSurfaceCompute(VkCommandBuffer commandBuffer, ResourceManager& resourceManager, uint32_t currentFrame) {
    uint32_t vertexCount = resourceManager.getVisModel().getVertexCount();
    uint32_t workGroupSize = 256;
    uint32_t workGroupCount = (vertexCount + workGroupSize - 1) / workGroupSize;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, surfacePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, surfacePipelineLayout, 0, 1, &surfaceDescriptorSets[currentFrame], 0, nullptr);
    vkCmdDispatch(commandBuffer, workGroupCount, 1, 1);
}

void HeatSystem::recordComputeCommands(VkCommandBuffer commandBuffer, ResourceManager& resourceManager, uint32_t currentFrame) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording compute command buffer");
    }

    // --- Heat Source Compute Pass ---
    heatSource->dispatchSourceCompute(commandBuffer, currentFrame);

    // Compute write -> transfer read
    VkBufferMemoryBarrier heatSourceTransferBarrier{};
    heatSourceTransferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    heatSourceTransferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    heatSourceTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    heatSourceTransferBarrier.buffer = resourceManager.getHeatModel().getSurfaceBuffer();
    heatSourceTransferBarrier.offset = resourceManager.getHeatModel().getSurfaceBufferOffset();
    heatSourceTransferBarrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        1, &heatSourceTransferBarrier,
        0, nullptr
    );

    // Copy surfaceBuffer -> surfaceVertexBuffer 
    VkBufferCopy heatSourceCopyRegion{};
    heatSourceCopyRegion.srcOffset = resourceManager.getHeatModel().getSurfaceBufferOffset();
    heatSourceCopyRegion.dstOffset = resourceManager.getHeatModel().getSurfaceVertexBufferOffset();
    heatSourceCopyRegion.size = sizeof(SurfaceVertex) * resourceManager.getHeatModel().getVertexCount();
    vkCmdCopyBuffer(commandBuffer, resourceManager.getHeatModel().getSurfaceBuffer(), resourceManager.getHeatModel().getSurfaceVertexBuffer(), 1, &heatSourceCopyRegion);

    // Transfer write -> vertex input read
    VkBufferMemoryBarrier heatSourceVertexBarrier{};
    heatSourceVertexBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    heatSourceVertexBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    heatSourceVertexBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    heatSourceVertexBarrier.buffer = resourceManager.getHeatModel().getSurfaceVertexBuffer();
    heatSourceVertexBarrier.offset = resourceManager.getHeatModel().getSurfaceVertexBufferOffset();
    heatSourceVertexBarrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0,
        0, nullptr,
        1, &heatSourceVertexBarrier,
        0, nullptr
    );

    VkBuffer currentWriteBuffer = tetraFrameBuffers.writeBuffer;

    // --- Tetra Compute Pass ---
    dispatchTetraCompute(commandBuffer, currentFrame);

    // Barrier between tetra compute and surface compute
    VkBufferMemoryBarrier tetraToSurfaceBarrier{};
    tetraToSurfaceBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    tetraToSurfaceBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    tetraToSurfaceBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    tetraToSurfaceBarrier.buffer = tetraFrameBuffers.writeBuffer;
    tetraToSurfaceBarrier.offset = tetraFrameBuffers.writeBufferOffset_;
    tetraToSurfaceBarrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        1, &tetraToSurfaceBarrier,
        0, nullptr
    );

    // --- Surface Compute Pass ---
    dispatchSurfaceCompute(commandBuffer, resourceManager, currentFrame);

    // Pre-copy barrier: Ensure previous vertex reads complete before transfer
    VkBufferMemoryBarrier preCopyBarrier{};
    preCopyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    preCopyBarrier.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT; // From previous frame
    preCopyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;        // To current transfer
    preCopyBarrier.buffer = resourceManager.getVisModel().getSurfaceVertexBuffer();
    preCopyBarrier.offset = resourceManager.getVisModel().getSurfaceVertexBufferOffset();
    preCopyBarrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,  // Previous vertex shader
        VK_PIPELINE_STAGE_TRANSFER_BIT,      // Current transfer
        0,
        0, nullptr,
        1, &preCopyBarrier,
        0, nullptr
    );

    // Transfer barrier: Surface compute -> Transfer
    VkBufferMemoryBarrier surfaceTransferBarrier{};
    surfaceTransferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    surfaceTransferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    surfaceTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    surfaceTransferBarrier.buffer = resourceManager.getVisModel().getSurfaceBuffer();
    surfaceTransferBarrier.offset = resourceManager.getVisModel().getSurfaceBufferOffset();
    surfaceTransferBarrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        1, &surfaceTransferBarrier,
        0, nullptr
    );

    // Copy the compute shader written surface buffer to the surface vertex buffer readable by the vertex shader per frame
    VkBufferCopy surfaceCopyRegion{};
    surfaceCopyRegion.srcOffset = resourceManager.getVisModel().getSurfaceBufferOffset();
    surfaceCopyRegion.dstOffset = resourceManager.getVisModel().getSurfaceVertexBufferOffset();
    surfaceCopyRegion.size = sizeof(SurfaceVertex) * resourceManager.getVisModel().getVertexCount();
    vkCmdCopyBuffer(commandBuffer, resourceManager.getVisModel().getSurfaceBuffer(), resourceManager.getVisModel().getSurfaceVertexBuffer(), 1, &surfaceCopyRegion);

    // Final barrier: Transfer -> Vertex input
    VkBufferMemoryBarrier surfaceVertexBufferBarrier{};
    surfaceVertexBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    surfaceVertexBufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    surfaceVertexBufferBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    surfaceVertexBufferBarrier.buffer = resourceManager.getVisModel().getSurfaceVertexBuffer();
    surfaceVertexBufferBarrier.offset = resourceManager.getVisModel().getSurfaceVertexBufferOffset();
    surfaceVertexBufferBarrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0,
        0, nullptr,
        1, &surfaceVertexBufferBarrier,
        0, nullptr
    );

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record compute command buffer");
    }
}

void HeatSystem::createComputeCommandBuffers(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    // Free existing command buffers
    if (!computeCommandBuffers.empty()) {
        vkFreeCommandBuffers(vulkanDevice.getDevice(), vulkanDevice.getCommandPool(),
            static_cast<uint32_t>(computeCommandBuffers.size()), computeCommandBuffers.data());
    }

    computeCommandBuffers.resize(maxFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vulkanDevice.getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(computeCommandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, computeCommandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers for compute shader");
    }
}

void HeatSystem::cleanupResources(VulkanDevice& vulkanDevice) {
    vkDestroyPipeline(vulkanDevice.getDevice(), tetraPipeline, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), surfacePipeline, nullptr);

    vkDestroyPipelineLayout(vulkanDevice.getDevice(), tetraPipelineLayout, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), surfacePipelineLayout, nullptr);

    vkDestroyDescriptorPool(vulkanDevice.getDevice(), tetraDescriptorPool, nullptr);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), surfaceDescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), tetraDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), surfaceDescriptorSetLayout, nullptr);

    heatSource->cleanup(vulkanDevice);
}

void HeatSystem::cleanup(VulkanDevice& vulkanDevice) {
    if (mappedTetraData) {
        vkUnmapMemory(vulkanDevice.getDevice(), tetraBufferMemory);
        mappedTetraData = nullptr;
    }

    if (mappedTimeData) {
        vkUnmapMemory(vulkanDevice.getDevice(), timeBufferMemory);
        mappedTimeData = nullptr;
    }

    memoryAllocator.free(tetraFrameBuffers.readBuffer, tetraFrameBuffers.readBufferOffset_);
    memoryAllocator.free(tetraFrameBuffers.writeBuffer, tetraFrameBuffers.writeBufferOffset_);
    memoryAllocator.free(tetraBuffer, tetraBufferOffset_);
    memoryAllocator.free(timeBuffer, timeBufferOffset_);
    memoryAllocator.free(centerBuffer, centerBufferOffset_);
    memoryAllocator.free(neighborBuffer, neighborBufferOffset_);

}