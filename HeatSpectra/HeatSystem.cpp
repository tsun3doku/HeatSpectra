#include <vulkan/vulkan.h>
#include <omp.h>
#include <unordered_map>
#include <memory>

#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "ResourceManager.hpp"
#include "VulkanImage.hpp"
#include "VulkanBuffer.hpp"
#include "CommandBufferManager.hpp"
#include "UniformBufferManager.hpp"
#include "Camera.hpp"
#include "Model.hpp"
#include "predicates.h"
#include "HeatSource.hpp"
#include "HeatReceiver.hpp"
#include "SurfelRenderer.hpp"
#include "VoronoiRenderer.hpp"
#include "PointRenderer.hpp"
#include "ContactLineRenderer.hpp"
#include "HeatRenderer.hpp"
#include "ContactInterface.hpp"
#include "HeatSystem.hpp"
#include "VoronoiGeoCompute.hpp"
#include "VoronoiCandidateCompute.hpp"
#include "VoronoiIntegrator.hpp"

#include <iostream>
#include <fstream>
#include <cstring>
#include <numeric>
#include <iomanip>
#include <cmath>
#include <cfloat>

#include <algorithm>

#include <limits>

HeatSystem::HeatSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager,
    UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight, CommandPool& cmdPool,
    VkExtent2D extent, VkRenderPass renderPass)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), resourceManager(resourceManager),
      uniformBufferManager(uniformBufferManager), renderCommandPool(cmdPool), maxFramesInFlight(maxFramesInFlight) {
    (void)extent;

    heatSource = std::make_unique<HeatSource>(vulkanDevice, memoryAllocator, resourceManager.getHeatModel(), resourceManager, renderCommandPool);
    
    voronoiSeeder = std::make_unique<VoronoiSeeder>();
    createTimeBuffer();
    createSurfaceDescriptorPool(maxFramesInFlight);
    createSurfaceDescriptorSetLayout();
    createSurfacePipeline();

    createContactDescriptorPool(maxFramesInFlight);
    createContactDescriptorSetLayout();
    createContactPipeline();

    heatRenderer = std::make_unique<HeatRenderer>(vulkanDevice, uniformBufferManager);
    heatRenderer->initialize(renderPass, maxFramesInFlight);
    initializeSurfelRenderers(renderPass, maxFramesInFlight);
    initializeVoronoiRenderer(renderPass, maxFramesInFlight);
    initializePointRenderer(renderPass, maxFramesInFlight);
    initializeContactLineRenderer(renderPass, maxFramesInFlight);

	contactInterface = std::make_unique<ContactInterface>(resourceManager);

    initializeVoronoiGeoCompute();
    initializeVoronoiCandidateCompute();

    createComputeCommandBuffers(maxFramesInFlight);
}

void HeatSystem::createContactDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t MAX_RECEIVERS = 10;
    const uint32_t totalSets = MAX_RECEIVERS * 2;

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = totalSets * 9;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = totalSets * 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = totalSets;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &contactDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create contact descriptor pool");
    }
}

void HeatSystem::createContactDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
    bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;

    std::vector<VkDescriptorBindingFlags> flags(bindings.size(),
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT);

    bindingFlags.bindingCount = static_cast<uint32_t>(flags.size());
    bindingFlags.pBindingFlags = flags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    layoutInfo.pNext = &bindingFlags;

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &contactDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create contact descriptor set layout");
    }
}

void HeatSystem::createContactPipeline() {
    auto computeShaderCode = readFile("shaders/heat_contact_comp.spv");
    VkShaderModule computeShaderModule = createShaderModule(vulkanDevice, computeShaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(HeatSourcePushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &contactDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &contactPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create contact pipeline layout!");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = contactPipelineLayout;

    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &contactPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create contact compute pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), computeShaderModule, nullptr);
}

HeatSystem::~HeatSystem() {
}

void HeatSystem::initializeSurfelRenderers(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    const auto& modelRemeshData = resourceManager.getModelRemeshData();
    for (auto& [model, remeshData] : modelRemeshData) {
        if (remeshData.isRemeshed && remeshData.surfel) {
            remeshData.surfel->initialize(renderPass, maxFramesInFlight);
        }
    }
}

void HeatSystem::initializeVoronoiGeoCompute() {
    voronoiGeoCompute = std::make_unique<VoronoiGeoCompute>(vulkanDevice, renderCommandPool);
}

void HeatSystem::initializeVoronoiCandidateCompute() {
    voronoiCandidateCompute = std::make_unique<VoronoiCandidateCompute>(vulkanDevice, renderCommandPool);
    voronoiCandidateCompute->initialize();
}

void HeatSystem::update(bool upPressed, bool downPressed, bool leftPressed, bool rightPressed, UniformBufferObject& ubo, uint32_t WIDTH, uint32_t HEIGHT) {
    if (isPaused) {
        return;
    }

    // Time calculation
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();

    const float timeScale = 1.0f;
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count() * timeScale;
    lastTime = currentTime;

    // Calculate substep deltaTime for iterative solver
    float subStepDeltaTime = deltaTime / static_cast<float>(NUM_SUBSTEPS);

    // Update GPU time buffer with substep delta time
    if (mappedTimeData) {
        TimeUniform* timeData = static_cast<TimeUniform*>(mappedTimeData);
        timeData->deltaTime = subStepDeltaTime;   
        timeData->totalTime += deltaTime;       
    }

    heatSource->controller(upPressed, downPressed, leftPressed, rightPressed, deltaTime);

    glm::mat4 heatSourceModelMatrix = resourceManager.getHeatModel().getModelMatrix();
    heatSource->setHeatSourcePushConstant(heatSourceModelMatrix);

    (void)ubo;
    (void)WIDTH;
    (void)HEIGHT;
}

void HeatSystem::recreateResources(uint32_t maxFramesInFlight, VkExtent2D extent, VkRenderPass renderPass) {
    (void)extent;
    std::cout << "[HeatSystem] recreateResources() called" << std::endl;

    if (!heatRenderer) {
        heatRenderer = std::make_unique<HeatRenderer>(vulkanDevice, uniformBufferManager);
    }
    if (heatRenderer) {
        heatRenderer->initialize(renderPass, maxFramesInFlight);
    }

    // Use Voronoi if ready
    if (isVoronoiSeederReady) {
        if (voronoiDescriptorPool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(vulkanDevice.getDevice(), voronoiDescriptorPool, 0);
        }
        if (surfaceDescriptorPool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(vulkanDevice.getDevice(), surfaceDescriptorPool, 0);
        }
        createVoronoiDescriptorSetLayout();
        createVoronoiPipeline();
        createVoronoiDescriptorSets(maxFramesInFlight);

        if (surfacePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(vulkanDevice.getDevice(), surfacePipeline, nullptr);
            surfacePipeline = VK_NULL_HANDLE;
        }
        if (surfacePipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(vulkanDevice.getDevice(), surfacePipelineLayout, nullptr);
            surfacePipelineLayout = VK_NULL_HANDLE;
        }
        if (surfaceDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), surfaceDescriptorSetLayout, nullptr);
            surfaceDescriptorSetLayout = VK_NULL_HANDLE;
        }

        createSurfaceDescriptorSetLayout();
        createSurfacePipeline();
        
        uint32_t nodeCount = voronoiNodeCount;
        for (auto& receiver : receivers) {
            receiver->recreateDescriptors(
                surfaceDescriptorSetLayout,
                surfaceDescriptorPool,
                tempBufferA,
                tempBufferAOffset_,
                tempBufferB,
                tempBufferBOffset_,
                timeBuffer,
                timeBufferOffset_,
                nodeCount
            );
        }

        heatRenderer->updateDescriptors(
            resourceManager,
            resourceManager.getHeatModel(),
            *heatSource,
            receivers,
            maxFramesInFlight,
            true);
    }

	if (contactLineRenderer) {
		contactLineRenderer->cleanup();
		contactLineRenderer.reset();
	}
	initializeContactLineRenderer(renderPass, maxFramesInFlight);
	if (contactLineRenderer && contactInterface) {
		const auto& cachedOutlines = contactInterface->getCachedOutlineVertices();
		if (!cachedOutlines.empty()) {
			std::vector<ContactLineRenderer::LineVertex> outlineVerts;
			outlineVerts.reserve(cachedOutlines.size());
			for (size_t i = 0; i < cachedOutlines.size(); ++i) {
				ContactLineRenderer::LineVertex v{};
				v.position = cachedOutlines[i].position;
				v.color = cachedOutlines[i].color;
				outlineVerts.push_back(v);
			}
			contactLineRenderer->uploadOutlines(outlineVerts);
		}
		const auto& cachedCorrespondences = contactInterface->getCachedCorrespondenceVertices();
		if (!cachedCorrespondences.empty()) {
			std::vector<ContactLineRenderer::LineVertex> corrVerts;
			corrVerts.reserve(cachedCorrespondences.size());
			for (size_t i = 0; i < cachedCorrespondences.size(); ++i) {
				ContactLineRenderer::LineVertex v{};
				v.position = cachedCorrespondences[i].position;
				v.color = cachedCorrespondences[i].color;
				corrVerts.push_back(v);
			}
			contactLineRenderer->uploadCorrespondences(corrVerts);
		}
	}

    createComputeCommandBuffers(maxFramesInFlight);
}

void HeatSystem::processResetRequest() {
    if (needsReset.exchange(false, std::memory_order_acq_rel)) {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
        initializeVoronoi();
    }
}

void HeatSystem::setActive(bool active) {
    if (active && !isActive) {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
    }

	if (active && isVoronoiSeederReady && !isPaused) {
		initializeContactInterface();
		executeBufferTransfers();
	}

	if (active && !isVoronoiSeederReady) {
		iODT* remesher = resourceManager.getRemesherForModel(&resourceManager.getVisModel());
		if (!remesher || !remesher->getSupportingHalfedge()) {
			std::cerr << "[HeatSystem] Cannot seed Voronoi: missing supporting halfedge" << std::endl;
			isActive = active;
			return;
		}

		auto intrinsicMesh = remesher->getSupportingHalfedge()->buildIntrinsicMesh();
		voronoiSeeder->generateSeeds(
			intrinsicMesh,
			resourceManager.getVisModel(),
			0.005f,
			voronoiVoxelGrid,
			128
		);
		voronoiVoxelGridBuilt = (voronoiVoxelGrid.getGridSize() > 0);
		if (debugEnable && voronoiVoxelGridBuilt) {
			voronoiVoxelGrid.exportOccupancyVisualization("voxel_occupancy.ply");
		}

		if (debugEnable) {
			voronoiSeeder->exportSeedsToOBJ("voronoi_seeds.obj");
		}
		isVoronoiSeederReady = true;

		std::cout << "[HeatSystem] Using Voronoi VEM" << std::endl;
		generateVoronoiDiagram();

		Model* heatModelPtr = &resourceManager.getHeatModel();
		for (const auto& [model, remeshData] : resourceManager.getModelRemeshData()) {
			if (model != heatModelPtr && remeshData.isRemeshed) {
				addReceiver(model);

				if (!receivers.empty() && voronoiIntegrator) {
					const auto& surfacePoints = receivers.back()->getIntrinsicSurfacePositions();
					if (!surfacePoints.empty()) {
						std::vector<uint32_t> cellIndices;
						voronoiIntegrator->computeSurfacePointMapping(
							surfacePoints,
							voronoiSeedFlags,
							K_NEIGHBORS,
							cellIndices
						);
						receivers.back()->stageVoronoiSurfaceMapping(cellIndices);
					}
				}
			}
		}

		initializeContactInterface();
		executeBufferTransfers();
		isVoronoiReady = true;
	}
	isActive = active;
}

void HeatSystem::initializeContactInterface() {
	if (!contactInterface) {
		contactInterface = std::make_unique<ContactInterface>(resourceManager);
	}

	ContactInterface::Settings settings{};
	contactInterface->mapSurfacePoints(
		resourceManager.getHeatModel(),
		receivers,
		receiverContactPairs,
        settings
	);

	if (contactLineRenderer) {
		const auto& cachedOutlines = contactInterface->getCachedOutlineVertices();
		if (!cachedOutlines.empty()) {
			std::vector<ContactLineRenderer::LineVertex> outlineVerts;
			outlineVerts.reserve(cachedOutlines.size());
			for (size_t i = 0; i < cachedOutlines.size(); ++i) {
				ContactLineRenderer::LineVertex v{};
				v.position = cachedOutlines[i].position;
				v.color = cachedOutlines[i].color;
				outlineVerts.push_back(v);
			}
			contactLineRenderer->uploadOutlines(outlineVerts);
		}
		const auto& cachedCorrespondences = contactInterface->getCachedCorrespondenceVertices();
		if (!cachedCorrespondences.empty()) {
			std::vector<ContactLineRenderer::LineVertex> corrVerts;
			corrVerts.reserve(cachedCorrespondences.size());
			for (size_t i = 0; i < cachedCorrespondences.size(); ++i) {
				ContactLineRenderer::LineVertex v{};
				v.position = cachedCorrespondences[i].position;
				v.color = cachedCorrespondences[i].color;
				corrVerts.push_back(v);
			}
			contactLineRenderer->uploadCorrespondences(corrVerts);
		}
	}
}

void HeatSystem::initializeContactLineRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
	contactLineRenderer = std::make_unique<ContactLineRenderer>(
		vulkanDevice,
		memoryAllocator,
		uniformBufferManager
	);
	contactLineRenderer->initialize(renderPass, 2, maxFramesInFlight);
}

void HeatSystem::renderContactLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) {
	if (!contactLineRenderer || !isActive) {
		return;
	}

	glm::mat4 modelMatrix(1.0f);
	contactLineRenderer->render(cmdBuffer, frameIndex, modelMatrix, extent);
}

void HeatSystem::requestReset() {
	needsReset.store(true, std::memory_order_release);
}

void HeatSystem::initializeVoronoi() {
	if (!mappedTempBufferA || !mappedTempBufferB)
		return;

	float* tempsA = static_cast<float*>(mappedTempBufferA);
	float* tempsB = static_cast<float*>(mappedTempBufferB);

	for (uint32_t i = 0; i < voronoiNodeCount; i++) {
		tempsA[i] = AMBIENT_TEMPERATURE;
		tempsB[i] = AMBIENT_TEMPERATURE;
	}
}

void HeatSystem::createTimeBuffer() {
	VkDeviceSize bufferSize = sizeof(TimeUniform);

	createUniformBuffer(
		memoryAllocator, vulkanDevice,
		bufferSize,
		timeBuffer, timeBufferOffset_, &mappedTimeData
	);
}

void HeatSystem::createSurfaceDescriptorPool(uint32_t maxFramesInFlight) {
	const uint32_t MAX_HEAT_MODELS = 10;
	const uint32_t totalSets = (MAX_HEAT_MODELS * 2);

	std::array<VkDescriptorPoolSize, 2> poolSizes{};

	poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSizes[0].descriptorCount = totalSets * 3;

	poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[1].descriptorCount = totalSets * 1;

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = totalSets;

	if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr,
		&surfaceDescriptorPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create surface descriptor pool");
	}
}

void HeatSystem::createSurfaceDescriptorSetLayout() {
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
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

void HeatSystem::createSurfacePipeline() {
    auto computeShaderCode = readFile("shaders/heat_surface_comp.spv");
    VkShaderModule computeShaderModule = createShaderModule(vulkanDevice, computeShaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";
    
    // Add push constants for model matrices
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(HeatSourcePushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &surfaceDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

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

void HeatSystem::createVoronoiGeometryBuffers(const VoronoiIntegrator& integrator, const std::vector<uint32_t>& seedFlags) {
    std::cout << "[HeatSystem] Creating Voronoi geometry buffers..." << std::endl;  
    void* mappedPtr;
    
    // Binding 0: Voronoi nodes 
    std::vector<VoronoiNodeGPU> nodes(voronoiNodeCount);
    // Initialize with aluminum thermal properties
    for (auto& node : nodes) {
        node.temperature = 1.0f;
        node.prevTemperature = 0.0f;
        node.volume = 0.0f;
        node.thermalMass = 0.0f;
        
        // Aluminum thermal properties
        node.density = 2700.0f;        // kg/m^3
        node.specificHeat = 900.0f;    // J/(kg*K)
        node.conductivity = 205.0f;    // W/(m*K)
        
        node.neighborOffset = 0;
        node.neighborCount = 0;
        node.interfaceNeighborCount = 0;
    }
    
    VkDeviceSize bufferSize = sizeof (VoronoiNodeGPU) * voronoiNodeCount;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        nodes.data(), bufferSize,
        voronoiNodeBuffer, voronoiNodeBufferOffset_, &mappedVoronoiNodeData
    );
    std::cout << "  Voronoi node buffer: " << voronoiNodeCount << " nodes ("
              << (bufferSize / 1024.0) << " KB)" << std::endl;
     
    // Binding 17: Neighbor indices 
    auto neighborIndices = integrator.getNeighborIndices();
    if (neighborIndices.empty()) {
        // Fill with sentinel values if empty
        neighborIndices.resize(K_NEIGHBORS, UINT32_MAX);
    }
    
    bufferSize = sizeof(uint32_t) * neighborIndices.size();
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        neighborIndices.data(), bufferSize,
        neighborIndicesBuffer, neighborIndicesBufferOffset_, &mappedPtr
    );
    std::cout << "  Neighbor indices: " << integrator.getNeighborIndices().size() 
              << " (K=" << K_NEIGHBORS << " per cell)" << std::endl;
    
    // Binding 18: Interface areas
    size_t interfaceDataSize = voronoiNodeCount * K_NEIGHBORS;
    std::vector<float> emptyAreas(interfaceDataSize, 0.0f);
    bufferSize = sizeof(float) * interfaceDataSize;
    void* mappedInterfaceAreas = nullptr;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        emptyAreas.data(), bufferSize,
        interfaceAreasBuffer, interfaceAreasBufferOffset_, &mappedInterfaceAreas
    );
    std::cout << "  Interface areas buffer: " << interfaceDataSize << " elements ("
              << (bufferSize / 1024.0) << " KB)" << std::endl;
    
    // Binding 19: Interface neighbor IDs
    std::vector<uint32_t> emptyIds(interfaceDataSize, UINT32_MAX);
    bufferSize = sizeof(uint32_t) * interfaceDataSize;
    void* mappedInterfaceNeighborIds = nullptr;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        emptyIds.data(), bufferSize,
        interfaceNeighborIdsBuffer, interfaceNeighborIdsBufferOffset_, &mappedInterfaceNeighborIds
    );
    
    // Store mapped pointers for later readback
    mappedInterfaceAreasData = mappedInterfaceAreas;
    mappedInterfaceNeighborIdsData = mappedInterfaceNeighborIds;
    
    std::cout << "  Interface neighbor IDs buffer: " << interfaceDataSize << " elements ("
              << (bufferSize / 1024.0) << " KB)" << std::endl;
    
    // Binding 20: Debug cell geometry
    uint32_t numDebugCells = 1u;
    if (debugEnable) {
        numDebugCells = voronoiNodeCount;
    }
    std::vector<DebugCellGeometry> debugCells(numDebugCells);
    for (auto& cell : debugCells) {
        cell.cellID = 0;
        cell.vertexCount = 0;
        cell.triangleCount = 0;
        cell.volume = 0.0f;
    }
    
    bufferSize = sizeof(DebugCellGeometry) * numDebugCells;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        debugCells.data(), bufferSize,
        debugCellGeometryBuffer, debugCellGeometryBufferOffset_, &mappedDebugCellGeometryData
    );

    // Binding 22: VoronoiDumpInfo debug buffer
    uint32_t dumpCount = 1u;
    if (debugEnable) {
        dumpCount = DEBUG_DUMP_CELL_COUNT;
    }
    std::vector<VoronoiDumpInfo> dumpInfos(dumpCount);
    memset(dumpInfos.data(), 0, sizeof(VoronoiDumpInfo) * dumpCount);
    
    bufferSize = sizeof(VoronoiDumpInfo) * dumpCount;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        dumpInfos.data(), bufferSize,
        voronoiDumpBuffer, voronoiDumpBufferOffset_, &mappedVoronoiDumpData
    );

    // Binding 6: Mesh triangles
    auto meshTris = integrator.getMeshTriangles();
    if (meshTris.empty()) meshTris.push_back({});
    
    bufferSize = sizeof(MeshTriangleGPU) * meshTris.size();
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        meshTris.data(), bufferSize,
        meshTriangleBuffer, meshTriangleBufferOffset_, &mappedPtr
    );
    
    // Binding 7: Seed positions
    auto seeds = integrator.getSeedPositions();
    if (seeds.empty()) seeds.push_back(glm::vec4(0.0f));
    
    bufferSize = sizeof(glm::vec4) * seeds.size();
    void* mappedSeeds = nullptr;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        seeds.data(), bufferSize,
        seedPositionBuffer, seedPositionBufferOffset_, &mappedSeeds
    );
    mappedSeedPositionData = mappedSeeds;  
    
    // Binding 21: Seed flags (isGhost, isSurface)
    if (seedFlags.empty()) {
        std::cerr << "[HeatSystem] ERROR: seedFlags is empty" << std::endl;
    }
    
    bufferSize = sizeof(uint32_t) * seedFlags.size();
    void* mappedFlags = nullptr;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        seedFlags.data(), bufferSize,
        seedFlagsBuffer, seedFlagsBufferOffset_, &mappedFlags
    );
    std::cout << "  Seed flags: " << seedFlags.size() << " seeds" << std::endl;
    
    if (voronoiVoxelGridBuilt) {
        const auto& voxelGrid = voronoiVoxelGrid;
        const auto& params = voxelGrid.getParams();
                
        // Binding 5: Voxel grid parameters (uniform buffer)
        bufferSize = sizeof(VoxelGrid::VoxelGridParams);
        createUniformBuffer(
            memoryAllocator, vulkanDevice, bufferSize,
            voxelGridParamsBuffer, voxelGridParamsBufferOffset_, &mappedPtr
        );

        if (mappedPtr) {
            memcpy(mappedPtr, &params, sizeof(VoxelGrid::VoxelGridParams));
        }
        
        // Binding 6: Occupancy data
        auto occupancy8 = voxelGrid.getOccupancyData();

        if (occupancy8.empty()) {
            occupancy8.push_back(0);
        }

        std::vector<uint32_t> occupancy32(occupancy8.size());

        for (size_t i = 0; i < occupancy8.size(); i++) {
            occupancy32[i] = static_cast<uint32_t>(occupancy8[i]);
        }

        bufferSize = sizeof(uint32_t) * occupancy32.size();
        createStorageBuffer(
            memoryAllocator, vulkanDevice,
            occupancy32.data(), bufferSize,
            voxelOccupancyBuffer, voxelOccupancyBufferOffset_, &mappedPtr,
            true // Send data to GPU
        );
        
        // Binding 9: Triangle list
        const auto& trianglesList = voxelGrid.getTrianglesList(); 

        if (trianglesList.empty()) {
            std::cerr << "ERROR: VoxelGrid trianglesList is empty" << std::endl;
        }

        bufferSize = sizeof(int32_t) * trianglesList.size();
        createStorageBuffer(
            memoryAllocator, vulkanDevice,
            trianglesList.data(), bufferSize,
            voxelTrianglesListBuffer, voxelTrianglesListBufferOffset_, &mappedPtr,
            true  // Send data to GPU
        );
        
        // Binding 10: Offsets
        const auto& offsets = voxelGrid.getOffsets();  

        if (offsets.empty()) {
            std::cerr << "ERROR: VoxelGrid offsets is empty" << std::endl;
        }

        bufferSize = sizeof(int32_t) * offsets.size();
        createStorageBuffer(
            memoryAllocator, vulkanDevice,
            offsets.data(), bufferSize,
            voxelOffsetsBuffer, voxelOffsetsBufferOffset_, &mappedPtr,
            true  // Send data to GPU
        );
    }
}

void HeatSystem::generateVoronoiDiagram() {
    if (!voronoiSeeder) {
        std::cerr << "[HeatSystem] Error: Voronoi seeder not ready" << std::endl;
        return;
    }
        
    const auto& seeds = voronoiSeeder->getSeeds();
    glm::vec3 gridMin = voronoiSeeder->getGridMin();
    glm::vec3 gridMax = voronoiSeeder->getGridMax();
    float cellSize = voronoiSeeder->getCellSize();
    
    // Collect all seeds and their flags
    std::vector<glm::dvec3> seedPositions;

    // Bit layout: 0=isGhost, 1=isSurface
    voronoiSeedFlags.clear();
    seedPositions.reserve(seeds.size());
    voronoiSeedFlags.reserve(seeds.size());
    
    uint32_t regularSeedCount = 0;
    for (const auto& seed : seeds) {
        seedPositions.push_back(glm::dvec3(seed.pos));
        
        uint32_t flags = 0;
        if (seed.isGhost) 
            flags |= 1;      // bit 0
        if (seed.isSurface) 
            flags |= 2;      // bit 1
        voronoiSeedFlags.push_back(flags);
        
        if (!seed.isGhost) {
            regularSeedCount++;
        }
    }
    
    voronoiNodeCount = static_cast<uint32_t>(seedPositions.size());
    
    // Export cell index to seed position mapping (debug only)
    if (debugEnable) {
        std::ofstream seedMapFile("cell_seed_positions.txt");
        seedMapFile << "# Cell Index -> Seed Position\n";
        seedMapFile << "# Seed positions (cells.size() = " << seedPositions.size() << ")\n";
        for (size_t i = 0; i < seedPositions.size(); i++) {
            const auto& pos = seedPositions[i];
            seedMapFile << "Cell " << i << " -> Seed at (" 
                       << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
        }
        seedMapFile.close();
        std::cout << "[HeatSystem] Exported seed mapping to cell_seed_positions.txt" << std::endl;
    }
    
    voronoiIntegrator = std::make_unique<VoronoiIntegrator>();
    voronoiIntegrator->computeNeighbors(seedPositions, K_NEIGHBORS);
    
    // Extract surface mesh triangles for cell restriction
    voronoiIntegrator->extractMeshTriangles(resourceManager.getVisModel());
    
    createVoronoiGeometryBuffers(*voronoiIntegrator, voronoiSeedFlags);
    
    // Create geometry precompute resources
    std::cout << "[HeatSystem] Creating geometry precompute shader..." << std::endl;
    if (voronoiGeoCompute) {
        voronoiGeoCompute->initialize(voronoiNodeCount);

        VoronoiGeoCompute::Bindings bindings;
        bindings.voronoiNodeBuffer = voronoiNodeBuffer;
        bindings.voronoiNodeBufferOffset = voronoiNodeBufferOffset_;
        bindings.voronoiNodeBufferRange = sizeof(VoronoiNodeGPU) * voronoiNodeCount;

        bindings.meshTriangleBuffer = meshTriangleBuffer;
        bindings.meshTriangleBufferOffset = meshTriangleBufferOffset_;

        bindings.seedPositionBuffer = seedPositionBuffer;
        bindings.seedPositionBufferOffset = seedPositionBufferOffset_;

        bindings.voxelGridParamsBuffer = voxelGridParamsBuffer;
        bindings.voxelGridParamsBufferOffset = voxelGridParamsBufferOffset_;
        bindings.voxelGridParamsBufferRange = sizeof(VoxelGrid::VoxelGridParams);

        bindings.voxelOccupancyBuffer = voxelOccupancyBuffer;
        bindings.voxelOccupancyBufferOffset = voxelOccupancyBufferOffset_;

        bindings.voxelTrianglesListBuffer = voxelTrianglesListBuffer;
        bindings.voxelTrianglesListBufferOffset = voxelTrianglesListBufferOffset_;

        bindings.voxelOffsetsBuffer = voxelOffsetsBuffer;
        bindings.voxelOffsetsBufferOffset = voxelOffsetsBufferOffset_;

        bindings.neighborIndicesBuffer = neighborIndicesBuffer;
        bindings.neighborIndicesBufferOffset = neighborIndicesBufferOffset_;

        bindings.interfaceAreasBuffer = interfaceAreasBuffer;
        bindings.interfaceAreasBufferOffset = interfaceAreasBufferOffset_;

        bindings.interfaceNeighborIdsBuffer = interfaceNeighborIdsBuffer;
        bindings.interfaceNeighborIdsBufferOffset = interfaceNeighborIdsBufferOffset_;

        bindings.debugCellGeometryBuffer = debugCellGeometryBuffer;
        bindings.debugCellGeometryBufferOffset = debugCellGeometryBufferOffset_;

        bindings.seedFlagsBuffer = seedFlagsBuffer;
        bindings.seedFlagsBufferOffset = seedFlagsBufferOffset_;

        bindings.voronoiDumpBuffer = voronoiDumpBuffer;
        bindings.voronoiDumpBufferOffset = voronoiDumpBufferOffset_;

        voronoiGeoCompute->updateDescriptors(bindings);
    }

    // Copy GPU seed positions back into VoronoiSeeder 
    {
        const auto& seedsRef = voronoiSeeder->getSeeds();
        if (mappedSeedPositionData) {
            glm::vec4* seedPos = static_cast<glm::vec4*>(mappedSeedPositionData);
            uint32_t count = voronoiNodeCount;
            if (count > static_cast<uint32_t>(seedsRef.size())) {
                count = static_cast<uint32_t>(seedsRef.size());
            }
            for (uint32_t i = 0; i < count; i++) {
                voronoiSeeder->updateSeedPosition(i, glm::vec3(seedPos[i]));
            }
        }
    }

    if (voronoiGeoCompute) {
        VoronoiGeoCompute::PushConstants geoPushConstants{};
        geoPushConstants.debugEnable = debugEnable ? 1u : 0u;
        voronoiGeoCompute->dispatch(geoPushConstants);
    }
    if (debugEnable) {
        exportDebugCellsToOBJ();
        exportCellVolumes();
        exportVoronoiDumpInfo();
    }
    buildVoronoiNeighborBuffer();

    // Create ping pong temperature buffers before creating descriptor sets
    VkDeviceSize tempBufferSize = sizeof(float) * voronoiNodeCount;
    void* mappedPtr;
    
    createStorageBuffer(memoryAllocator, vulkanDevice,
        nullptr, tempBufferSize,
        tempBufferA, tempBufferAOffset_, &mappedPtr);
    mappedTempBufferA = mappedPtr;
    
    createStorageBuffer(memoryAllocator, vulkanDevice,
        nullptr, tempBufferSize,
        tempBufferB, tempBufferBOffset_, &mappedPtr);
    mappedTempBufferB = mappedPtr;

    VkDeviceSize injectionBufferSize = sizeof(uint32_t) * voronoiNodeCount;
    createStorageBuffer(memoryAllocator, vulkanDevice,
        nullptr, injectionBufferSize,
        injectionKBuffer, injectionKBufferOffset_, &mappedPtr);
    mappedInjectionKBuffer = mappedPtr;

    createStorageBuffer(memoryAllocator, vulkanDevice,
        nullptr, injectionBufferSize,
        injectionKTBuffer, injectionKTBufferOffset_, &mappedPtr);
    mappedInjectionKTBuffer = mappedPtr;

    if (mappedInjectionKBuffer && mappedInjectionKTBuffer)
    {
        std::memset(mappedInjectionKBuffer, 0, static_cast<size_t>(injectionBufferSize));
        std::memset(mappedInjectionKTBuffer, 0, static_cast<size_t>(injectionBufferSize));
    }
    
    // Initialize temperatures
    initializeVoronoi();
    
    createVoronoiDescriptorPool(maxFramesInFlight);
    createVoronoiDescriptorSetLayout();
    createVoronoiPipeline();
    createVoronoiDescriptorSets(maxFramesInFlight);
    
    if (voronoiVoxelGridBuilt) {
        uploadOccupancyPoints(voronoiVoxelGrid);
    }
    
    std::cout << "[HeatSystem] Geometry precomputation complete" << std::endl;
}

void HeatSystem::exportDebugCellsToOBJ() {    
    if (!debugEnable) {
        return;
    }

    if (!mappedDebugCellGeometryData) 
        return;
    DebugCellGeometry* cells = static_cast<DebugCellGeometry*>(mappedDebugCellGeometryData);
    
    std::ofstream obj("voronoi_unrestricted_debug_cells.obj");
    if (!obj) {
        std::cerr << "[HeatSystem] Failed to create OBJ file!" << std::endl;
        return;
    }
    
    char buffer[65536];
    obj.rdbuf()->pubsetbuf(buffer, sizeof(buffer));
    
    obj << "# Unrestricted Voronoi Cells\n";
    obj << "o Voronoi_Cells_Combined\n"; 
    uint32_t offset = 1;
    uint32_t exportCount = 0;
    
    std::cout << "[HeatSystem] Writing OBJ file (checking " << voronoiNodeCount << " cells)..." << std::endl;
    
    for (uint32_t i = 0; i < voronoiNodeCount; i++) {
        if (cells[i].vertexCount == 0) {
            continue;  // Skip empty cells 
        }
        
        // Export vertices and faces without creating separate objects
        for (uint32_t v = 0; v < cells[i].vertexCount; v++)
            obj << "v " << cells[i].vertices[v].x << " " << cells[i].vertices[v].y << " " << cells[i].vertices[v].z << "\n";
        for (uint32_t t = 0; t < cells[i].triangleCount; t++)
            obj << "f " << (offset + cells[i].triangles[t].x) << " " 
                << (offset + cells[i].triangles[t].y) << " " 
                << (offset + cells[i].triangles[t].z) << "\n";
        offset += cells[i].vertexCount;
        exportCount++;
    }
    
    obj.close();
    std::cout << "Exported " << exportCount << " cells to: voronoi_unrestricted_debug_cells.obj\n";
}

void HeatSystem::exportCellVolumes() {
    if (!debugEnable) {
        return;
    }

    std::cout << "[HeatSystem] Exporting cell volumes..." << std::endl;
    
    if (!mappedVoronoiNodeData) {
        std::cerr << "[HeatSystem] Error: VoronoiNode buffer not mapped" << std::endl;
        return;
    }
    
    VoronoiNodeGPU* nodes = static_cast<VoronoiNodeGPU*>(mappedVoronoiNodeData);
    
    std::ofstream volumeFile("cell_volumes.txt");
    volumeFile << "# Cell Index -> Restricted Volume\n";
    volumeFile << "# NOTE: Using GPU filtered seed positions (cells.size() = " << voronoiNodeCount << ")\n";
    for (uint32_t i = 0; i < voronoiNodeCount; i++) {
        volumeFile << "Cell " << i << " -> Volume: " << nodes[i].volume << "\n";
    }
    
    volumeFile.close();
    std::cout << "[HeatSystem] Exported " << voronoiNodeCount 
              << " cell volumes to: cell_volumes.txt" << std::endl;
}

void HeatSystem::exportVoronoiDumpInfo() {
    if (!debugEnable) {
        return;
    }

    std::cout << "[HeatSystem] Exporting Voronoi debug dump..." << std::endl;
    
    if (!mappedVoronoiDumpData) {
        std::cerr << "[HeatSystem] Error: VoronoiDumpInfo buffer not mapped" << std::endl;
        return;
    }
    
    VoronoiDumpInfo* dumpInfos = static_cast<VoronoiDumpInfo*>(mappedVoronoiDumpData);

    double totalRestrictedVolumePos = 0.0;
    double totalRestrictedVolumeNegAbs = 0.0;
    uint32_t negativeVolumeCount = 0;
    if (mappedVoronoiNodeData)
    {
        const VoronoiNodeGPU* nodes = static_cast<const VoronoiNodeGPU*>(mappedVoronoiNodeData);
        for (uint32_t i = 0; i < voronoiNodeCount; i++)
        {
            const double v = (double)nodes[i].volume;
            if (v > 0.0)
            {
                totalRestrictedVolumePos += v;
            }
            else if (v < 0.0)
            {
                negativeVolumeCount++;
                totalRestrictedVolumeNegAbs += -v;
            }
        }
    }

    // Store global aggregates into each dump slot for easy GPU->CPU inspection
    for (uint32_t slot = 0; slot < DEBUG_DUMP_CELL_COUNT; slot++)
    {
        dumpInfos[slot].totalMeshVolume = static_cast<float>(totalRestrictedVolumePos);
        dumpInfos[slot].negativeVolumeCellCount = negativeVolumeCount;
        dumpInfos[slot].negativeVolumeSumAbs = static_cast<float>(totalRestrictedVolumeNegAbs);
    }

    std::ofstream dumpFile("voronoi_debug_dump.txt");
    dumpFile << std::scientific << std::setprecision(10);

    dumpFile << "Total Mesh Volume: " << totalRestrictedVolumePos << "\n";
    dumpFile << "Negative volume cells: " << negativeVolumeCount << " Sum =" << totalRestrictedVolumeNegAbs << ")\n";
    dumpFile << "\n";
    
    for (uint32_t slot = 0; slot < DEBUG_DUMP_CELL_COUNT; slot++) {
        const VoronoiDumpInfo& info = dumpInfos[slot];
        
        // Skip empty slots
        if (info.cellID == 0)
            continue;
        
        dumpFile << "CELL " << info.cellID << " (Slot " << slot << ")\n";
        dumpFile << "Seed: (" << info.seedPos.x << ", " << info.seedPos.y << ", " << info.seedPos.z << ")\n";
        dumpFile << "Volumes:\n";
        dumpFile << "  Unrestricted: " << info.unrestrictedVolume << "\n";
        dumpFile << "  Restricted:   " << info.restrictedVolume << "\n";
        dumpFile << "Plane Areas (" << info.planeAreaCount << "):\n";
        for (uint32_t i = 0; i < info.planeAreaCount && i < DEBUG_MAX_PLANE_AREAS; ++i) {
            const DebugPlaneArea& planeArea = info.planeAreas[i];
            dumpFile << "  Plane " << planeArea.planeIndex
                     << " -> Neighbor " << planeArea.neighborCellID
                     << ", Area: " << planeArea.area << "\n";
        }
        dumpFile << "\n";
    }
    
    dumpFile.close();
    std::cout << "[HeatSystem] Exported debug dump to: voronoi_debug_dump.txt" << std::endl;
}

void HeatSystem::buildVoronoiNeighborBuffer() {    
    if (voronoiNodeCount == 0) {
        std::cerr << "[HeatSystem] Error: No Voronoi nodes to build neighbor buffer" << std::endl;
        return;
    }
    
    // Persistently mapped pointers
    float* areas = static_cast<float*>(mappedInterfaceAreasData);
    uint32_t* neighborIds = static_cast<uint32_t*>(mappedInterfaceNeighborIdsData);
    
    if (!areas || !neighborIds) {
        std::cerr << "[HeatSystem] Error: Interface buffers not mapped" << std::endl;
        return;
    }
    
    // Each cell has K_NEIGHBORS entries in the interface buffers
    std::vector<VoronoiNeighborGPU> neighbors;
    neighbors.reserve(voronoiNodeCount * K_NEIGHBORS);
    
    uint32_t totalNeighbors = 0;
    
    for (uint32_t cellIdx = 0; cellIdx < voronoiNodeCount; cellIdx++) {
        uint32_t neighborOffset = totalNeighbors;
        uint32_t validNeighborCount = 0;
        
        for (int k = 0; k < K_NEIGHBORS; k++) {
            uint32_t idx = cellIdx * K_NEIGHBORS + k;
            uint32_t neighborIdx = neighborIds[idx];
            float area = areas[idx];
            
            // Skip invalid neighbors
            if (neighborIdx == UINT32_MAX || area <= 0.0f) {
                continue;
            }
            
            VoronoiNeighborGPU neighbor;
            neighbor.neighborIndex = neighborIdx;
            neighbor.interfaceArea = area;
            
            // Calculate distance between seeds
            if (mappedSeedPositionData) {
                glm::vec4* seeds = static_cast<glm::vec4*>(mappedSeedPositionData);
                glm::vec3 seedA(seeds[cellIdx].x, seeds[cellIdx].y, seeds[cellIdx].z);
                glm::vec3 seedB(seeds[neighborIdx].x, seeds[neighborIdx].y, seeds[neighborIdx].z);
                neighbor.distance = glm::distance(seedA, seedB);
            } else {
                neighbor.distance = 0.1f;  // Fallback
            }
            neighbor.interfaceFaceID = 0;  // Not used
            
            neighbors.push_back(neighbor);
            validNeighborCount++;
            totalNeighbors++;
        }
        
        // Update node's neighbor offset and count in voronoiNodeBuffer
        VoronoiNodeGPU* nodes = static_cast<VoronoiNodeGPU*>(mappedVoronoiNodeData);
        if (nodes) {
            nodes[cellIdx].neighborOffset = neighborOffset;
            nodes[cellIdx].neighborCount = validNeighborCount;
        }
    }
        
    // Create voronoiNeighborBuffer
    if (totalNeighbors > 0) {
        VkDeviceSize bufferSize = sizeof(VoronoiNeighborGPU) * totalNeighbors;
        void* mappedPtr = nullptr;
        
        createStorageBuffer(
            memoryAllocator, vulkanDevice,
            neighbors.data(), bufferSize,
            voronoiNeighborBuffer, voronoiNeighborBufferOffset_, &mappedPtr
        );        
    }
}

void HeatSystem::createVoronoiDescriptorPool(uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    // Storage buffers
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * 2 * 7; 
    
    // Uniform buffers
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * 2;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * 2;  // 2 sets per frame (ping pong)
    
    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &voronoiDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Voronoi descriptor pool");
    }
}

void HeatSystem::createVoronoiDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // Binding 0: Voronoi nodes data (thermal mass, conductivity, etc.)
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Binding 1: Voronoi neighbors
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Binding 2: Time uniform
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Binding 3: Temperature READ buffer (ping pong)
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Binding 4: Temperature WRITE buffer (ping pong)
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Binding 5: Seed flags (isGhost, isSurface)
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Binding 6: Injection K accumulation buffer
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Binding 7: Injection K*T accumulation buffer
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
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
    
    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &voronoiDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Voronoi descriptor set layout");
    }
}

void HeatSystem::createVoronoiDescriptorSets(uint32_t maxFramesInFlight) {    
    // Allocate ping pong descriptor sets (2 per frame)
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight * 2, voronoiDescriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = voronoiDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight * 2;
    allocInfo.pSetLayouts = layouts.data();
    
    std::vector<VkDescriptorSet> allSets(maxFramesInFlight * 2);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, allSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Voronoi descriptor sets");
    }
    
    // Store ping-pong sets 
    voronoiDescriptorSets.resize(maxFramesInFlight);
    voronoiDescriptorSetsB.resize(maxFramesInFlight);
    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        voronoiDescriptorSets[i] = allSets[i * 2];      // SetA
        voronoiDescriptorSetsB[i] = allSets[i * 2 + 1]; // SetB
    }
    
    uint32_t nodeCount = voronoiNodeCount;
    
    // Update descriptor sets for all frames
    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        // SET A: Read from tempBufferA, write to tempBufferB
        {
            std::vector<VkDescriptorBufferInfo> bufferInfos = {
                VkDescriptorBufferInfo{voronoiNodeBuffer, voronoiNodeBufferOffset_, sizeof(VoronoiNodeGPU) * nodeCount},
                VkDescriptorBufferInfo{voronoiNeighborBuffer, voronoiNeighborBufferOffset_, VK_WHOLE_SIZE},  
                VkDescriptorBufferInfo{timeBuffer, timeBufferOffset_, sizeof(TimeUniform)},
                VkDescriptorBufferInfo{tempBufferA, tempBufferAOffset_, sizeof(float) * nodeCount},  // READ
                VkDescriptorBufferInfo{tempBufferB, tempBufferBOffset_, sizeof(float) * nodeCount},  // WRITE
                VkDescriptorBufferInfo{seedFlagsBuffer, seedFlagsBufferOffset_, sizeof(uint32_t) * nodeCount}, 
                VkDescriptorBufferInfo{injectionKBuffer, injectionKBufferOffset_, sizeof(uint32_t) * nodeCount},
                VkDescriptorBufferInfo{injectionKTBuffer, injectionKTBufferOffset_, sizeof(uint32_t) * nodeCount},
            };
            
            std::vector<VkWriteDescriptorSet> descriptorWrites(8);
            for (int j = 0; j < 8; j++) {
                descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[j].dstSet = voronoiDescriptorSets[i];
                descriptorWrites[j].dstBinding = j;
                descriptorWrites[j].descriptorCount = 1;
                descriptorWrites[j].descriptorType = (j == 2) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWrites[j].pBufferInfo = &bufferInfos[j];
            }
            vkUpdateDescriptorSets(vulkanDevice.getDevice(), 8, descriptorWrites.data(), 0, nullptr);
        }
        
        // SET B: Read from tempBufferB, write to tempBufferA  
        {
            std::vector<VkDescriptorBufferInfo> bufferInfos = {
                VkDescriptorBufferInfo{voronoiNodeBuffer, voronoiNodeBufferOffset_, sizeof(VoronoiNodeGPU) * nodeCount},
                VkDescriptorBufferInfo{voronoiNeighborBuffer, voronoiNeighborBufferOffset_, VK_WHOLE_SIZE}, 
                VkDescriptorBufferInfo{timeBuffer, timeBufferOffset_, sizeof(TimeUniform)},
                VkDescriptorBufferInfo{tempBufferB, tempBufferBOffset_, sizeof(float) * nodeCount},  // READ
                VkDescriptorBufferInfo{tempBufferA, tempBufferAOffset_, sizeof(float) * nodeCount},  // WRITE
                VkDescriptorBufferInfo{seedFlagsBuffer, seedFlagsBufferOffset_, sizeof(uint32_t) * nodeCount},  
                VkDescriptorBufferInfo{injectionKBuffer, injectionKBufferOffset_, sizeof(uint32_t) * nodeCount},
                VkDescriptorBufferInfo{injectionKTBuffer, injectionKTBufferOffset_, sizeof(uint32_t) * nodeCount},
            };
            
            std::vector<VkWriteDescriptorSet> descriptorWrites(8);
            for (int j = 0; j < 8; j++) {
                descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[j].dstSet = voronoiDescriptorSetsB[i];
                descriptorWrites[j].dstBinding = j;
                descriptorWrites[j].descriptorCount = 1;
                descriptorWrites[j].descriptorType = (j == 2) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWrites[j].pBufferInfo = &bufferInfos[j];
            }
            vkUpdateDescriptorSets(vulkanDevice.getDevice(), 8, descriptorWrites.data(), 0, nullptr);
        }
    }
}

void HeatSystem::createVoronoiPipeline() {
    auto computeShaderCode = readFile("shaders/heat_voronoi_comp.spv");
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
    pipelineLayoutInfo.pSetLayouts = &voronoiDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &voronoiPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Voronoi pipeline layout!");
    }
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = voronoiPipelineLayout;
    
    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &voronoiPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Voronoi compute pipeline!");
    }  

    vkDestroyShaderModule(vulkanDevice.getDevice(), computeShaderModule, nullptr);    
}

void HeatSystem::recordComputeCommands(
    VkCommandBuffer commandBuffer,
    uint32_t currentFrame,
    VkQueryPool timingQueryPool,
    uint32_t timingQueryBase) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording compute command buffer");
    }

    if (timingQueryPool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(commandBuffer, timingQueryPool, timingQueryBase, 2);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timingQueryPool, timingQueryBase);
    }

    if (!isActive || isPaused) {
        if (timingQueryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timingQueryPool, timingQueryBase + 1);
        }
        vkEndCommandBuffer(commandBuffer);
        return;
    }
    
    if (!isVoronoiReady) {
        if (timingQueryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timingQueryPool, timingQueryBase + 1);
        }
        vkEndCommandBuffer(commandBuffer);
        return;
    }

    static int recordCallCount = 0;
    if (recordCallCount == 0) {
        std::cout << "[HeatSystem] recordComputeCommands called, isActive=" << isActive << std::endl;
    }
    recordCallCount++;

    Model* heatModelPtr = &resourceManager.getHeatModel();
    uint32_t nodeCount = voronoiNodeCount;
    uint32_t workGroupSize = 256;
    uint32_t workGroupCount = (nodeCount + workGroupSize - 1) / workGroupSize;
        
    // Build push constant
    HeatSourcePushConstant pushConstant = heatSource->getHeatSourcePushConstant();
    pushConstant.heatSourceModelMatrix = heatModelPtr->getModelMatrix();
    pushConstant.inverseHeatSourceModelMatrix = glm::inverse(pushConstant.heatSourceModelMatrix);
    pushConstant.maxNodeNeighbors = K_NEIGHBORS;

    static int frameCount = 0;
    if (frameCount == 0) {
        std::cout << "[HeatSystem] Frame 0: Starting heat simulation (" << NUM_SUBSTEPS << " substeps, " << workGroupCount << " workgroups)" << std::endl;
    }

    // Substeps
    for (int i = 0; i < NUM_SUBSTEPS; i++) {
        pushConstant.substepIndex = i;
        
        bool isEven = (i % 2 == 0);
        VkDescriptorSet voronoiSet = isEven ? voronoiDescriptorSets[currentFrame] : voronoiDescriptorSetsB[currentFrame];
        
        // Decide buffer layout for the current substep
        VkBuffer writeBuffer = isEven ? tempBufferB : tempBufferA;
        VkDeviceSize writeOffset = isEven ? tempBufferBOffset_ : tempBufferAOffset_;
        
        if (frameCount == 0 && i < 2) {
            std::cout << "  Substep " << i << ": isEven=" << isEven << ", using Set" << (isEven ? "A" : "B") << std::endl;
        }
        
        // INJECTION: receiver triangle centroids -> source triangles -> Voronoi cells (3-cell weighted)
        for (auto& receiver : receivers) {
            uint32_t triCount = static_cast<uint32_t>(receiver->getIntrinsicTriangleCount());
            if (triCount == 0) {
                continue;
            }

            uint32_t groupSize = 256;
            uint32_t groupCount = (triCount + groupSize - 1) / groupSize;

            VkDescriptorSet contactSet = isEven ? receiver->getContactComputeSetA() : receiver->getContactComputeSetB();
            if (contactSet == VK_NULL_HANDLE) {
                continue;
            }

            pushConstant.visModelMatrix = receiver->getModel().getModelMatrix();

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, contactPipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, contactPipelineLayout,
                                   0, 1, &contactSet, 0, nullptr);
            vkCmdPushConstants(commandBuffer, contactPipelineLayout,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HeatSourcePushConstant), &pushConstant);
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);
        }

        VkBufferMemoryBarrier injectionBarriers[2]{};
        injectionBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        injectionBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        injectionBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        injectionBarriers[0].buffer = injectionKBuffer;
        injectionBarriers[0].offset = injectionKBufferOffset_;
        injectionBarriers[0].size = VK_WHOLE_SIZE;

        injectionBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        injectionBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        injectionBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        injectionBarriers[1].buffer = injectionKTBuffer;
        injectionBarriers[1].offset = injectionKTBufferOffset_;
        injectionBarriers[1].size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                           0, nullptr, 2, injectionBarriers, 0, nullptr);

        // DIFFUSION: Voronoi cell heat diffusion
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, voronoiPipeline);
        vkCmdPushConstants(commandBuffer, voronoiPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                         0, sizeof(HeatSourcePushConstant), &pushConstant);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                voronoiPipelineLayout, 0, 1, &voronoiSet, 0, nullptr);
        vkCmdDispatch(commandBuffer, workGroupCount, 1, 1);
        
        if (i < NUM_SUBSTEPS - 1) {
            VkBufferMemoryBarrier barrier2{};
            barrier2.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            barrier2.buffer = writeBuffer;
            barrier2.offset = writeOffset;
            barrier2.size = VK_WHOLE_SIZE;

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                               0, nullptr, 1, &barrier2, 0, nullptr);
        }
    }

    {
        bool finalIsEven = ((NUM_SUBSTEPS - 1) % 2 == 0);
        VkBuffer finalTempBuffer = finalIsEven ? tempBufferB : tempBufferA;
        VkDeviceSize finalTempOffset = finalIsEven ? tempBufferBOffset_ : tempBufferAOffset_;

        VkBufferMemoryBarrier finalTempBarrier{};
        finalTempBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        finalTempBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        finalTempBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        finalTempBarrier.buffer = finalTempBuffer;
        finalTempBarrier.offset = finalTempOffset;
        finalTempBarrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            1, &finalTempBarrier,
            0, nullptr);
    }

    // Viz: update receiver surface temps from final ping pong buffer
    {
        bool finalIsEven = ((NUM_SUBSTEPS - 1) % 2 == 0);
        for (auto& receiver : receivers) {
            uint32_t vertexCount = static_cast<uint32_t>(receiver->getIntrinsicVertexCount());
            if (vertexCount == 0) {
                continue;
            }

            uint32_t groupSize = 256;
            uint32_t groupCount = (vertexCount + groupSize - 1) / groupSize;

            VkDescriptorSet surfaceSet = finalIsEven ? receiver->getSurfaceComputeSetB() : receiver->getSurfaceComputeSetA();
            if (surfaceSet == VK_NULL_HANDLE) {
                continue;
            }

            HeatSourcePushConstant visPC = pushConstant;
            visPC.substepIndex = 0;
            visPC.visModelMatrix = receiver->getModel().getModelMatrix();

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, surfacePipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, surfacePipelineLayout,
                                   0, 1, &surfaceSet, 0, nullptr);
            vkCmdPushConstants(commandBuffer, surfacePipelineLayout,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HeatSourcePushConstant), &visPC);
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);
        }
    }
    
    frameCount++;
    
    if (timingQueryPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timingQueryPool, timingQueryBase + 1);
    }

    vkEndCommandBuffer(commandBuffer);
}

void HeatSystem::initializeVoronoiRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    voronoiRenderer = std::make_unique<VoronoiRenderer>(vulkanDevice, uniformBufferManager);
    voronoiRenderer->initialize(renderPass, maxFramesInFlight);
}

void HeatSystem::renderVoronoiSurface(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (!voronoiRenderer || !isActive) 

        return;
    
    // Render Voronoi surface visualization for all receivers
    for (const auto& receiver : receivers) {
        Model& model = receiver->getModel();
        uint32_t vertexCount = static_cast<uint32_t>(receiver->getIntrinsicVertexCount());
        VkBuffer candidateBuffer = receiver->getVoronoiCandidateBuffer();
        
        // Only render if candidate buffer exists
        if (candidateBuffer != VK_NULL_HANDLE && vertexCount > 0) {
            voronoiRenderer->render(
                cmdBuffer, 
                model.getVertexBuffer(), 
                model.getVertexBufferOffset(),
                model.getIndexBuffer(), 
                model.getIndexBufferOffset(), 
                static_cast<uint32_t>(model.getIndices().size()),
                frameIndex,
                model.getModelMatrix()  
            );
        }
    }
}

void HeatSystem::renderHeatOverlay(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (!heatRenderer || (!isActive && !isPaused)) {
        return;
    }

    heatRenderer->render(
        cmdBuffer,
        frameIndex,
        resourceManager.getHeatModel(),
        receivers);
}

void HeatSystem::initializePointRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    pointRenderer = std::make_unique<PointRenderer>(
        vulkanDevice, 
        memoryAllocator,
        uniformBufferManager
    );
    pointRenderer->initialize(renderPass, 2, maxFramesInFlight);  // Subpass 2 = Grid subpass
}

void HeatSystem::uploadOccupancyPoints(const VoxelGrid& voxelGrid) {    
    const auto& occupancy = voxelGrid.getOccupancyData();
    const auto& params = voxelGrid.getParams();
    
    std::vector<PointRenderer::PointVertex> points;
    points.reserve(occupancy.size() / 4); 
    
    int dimX = params.gridDim.x;
    int dimY = params.gridDim.y;
    int dimZ = params.gridDim.z;
    int stride = dimX + 1;
    
    for (int z = 0; z <= dimZ; z++) {
        for (int y = 0; y <= dimY; y++) {
            for (int x = 0; x <= dimX; x++) {
                size_t idx = z * stride * stride + y * stride + x;
                uint8_t occ = occupancy[idx];
                
                // Skip outside points 
                if (occ == 0) 
                    continue;
                
                glm::vec3 pos = voxelGrid.getCornerPosition(x, y, z);
                glm::vec3 color;
                
                if (occ == 1) {
                    color = glm::vec3(1.0f, 0.2f, 0.2f);  // Red for border
                } else {
                    color = glm::vec3(0.2f, 1.0f, 0.2f);  // Green for inside
                }
                
                points.push_back({pos, color});
            }
        }
    }
    
    pointRenderer->uploadPoints(points);
    std::cout << "[HeatSystem] Uploaded " << points.size() << " occupancy points to PointRenderer" << std::endl;
}

void HeatSystem::renderOccupancy(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) {
    if (!pointRenderer || !isActive) 
        return;
    
    glm::mat4 modelMatrix = resourceManager.getVisModel().getModelMatrix();
    pointRenderer->render(cmdBuffer, frameIndex, modelMatrix, extent);
}

void HeatSystem::renderSurfels(VkCommandBuffer cmdBuffer, uint32_t frameIndex, const glm::mat4& heatSourceModel, int radius) {
    // Render surfels for all remeshed models
    const auto& modelRemeshData = resourceManager.getModelRemeshData();
    
    for (const auto& [model, remeshData] : modelRemeshData) {
        if (!remeshData.surfel || !remeshData.isRemeshed) 
            continue;
        
        const SurfelParams& gpuParams = remeshData.surfel->getParams();
        
        SurfelRenderer::Surfel surfel{};
        surfel.modelMatrix = model->getModelMatrix();
        surfel.surfelRadius = radius;

        VkBuffer surfaceBuffer = VK_NULL_HANDLE;
        uint32_t surfelCount = 0;
        VkDeviceSize surfaceBufferOffset = 0;
        
        if (model == &resourceManager.getHeatModel() && heatSource) {
            // Heat source model
            surfaceBuffer = heatSource->getSourceBuffer();
            surfaceBufferOffset = heatSource->getSourceBufferOffset();
            surfelCount = static_cast<uint32_t>(heatSource->getIntrinsicVertexCount());
        } else {
            // Receiver model
            bool found = false;
            for (auto& receiver : receivers) {
                if (&receiver->getModel() == model) {
                    surfaceBuffer = receiver->getSurfaceBuffer();
                    surfaceBufferOffset = receiver->getSurfaceBufferOffset();
                    surfelCount = static_cast<uint32_t>(receiver->getIntrinsicVertexCount());
                    found = true;
                    break;
                }
            }
            if (!found) 
                continue;
        }
        
        if (surfelCount > 0) {
            remeshData.surfel->render(cmdBuffer, surfaceBuffer, surfaceBufferOffset, surfelCount, surfel, frameIndex);
        }
    }
}

void HeatSystem::createComputeCommandBuffers(uint32_t maxFramesInFlight) {
    computeCommandBuffers.resize(maxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = renderCommandPool.getHandle();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(computeCommandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, computeCommandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate compute command buffers!");
    }
}

void HeatSystem::addReceiver(Model* model) {
    if (!model) 
        return;
    
    auto receiver = std::make_unique<HeatReceiver>(vulkanDevice, memoryAllocator, *model, resourceManager, renderCommandPool);
    
    receiver->createReceiverBuffers();
    receiver->initializeReceiverBuffer();
    
    receivers.push_back(std::move(receiver));   
}

void HeatSystem::executeBufferTransfers() {    
    VkCommandBuffer copyCmd = renderCommandPool.beginCommands();
    
    for (auto& receiver : receivers) {
        receiver->executeBufferTransfers(copyCmd);
    }
    
    renderCommandPool.endCommands(copyCmd);
    
    for (auto& receiver : receivers) {
        receiver->cleanupStagingBuffers();
    }

    if (voronoiCandidateCompute && voronoiNodeCount > 0) {
        for (const auto& receiver : receivers) {
            uint32_t faceCount = static_cast<uint32_t>(receiver->getIntrinsicTriangleCount());
            if (faceCount == 0 || receiver->getVoronoiCandidateBuffer() == VK_NULL_HANDLE) {
                continue;
            }

            VoronoiCandidateCompute::Bindings bindings{};
            bindings.vertexBuffer = receiver->getSurfaceBuffer();
            bindings.vertexBufferOffset = receiver->getSurfaceBufferOffset();
            bindings.faceIndexBuffer = receiver->getTriangleIndicesBuffer();
            bindings.faceIndexBufferOffset = receiver->getTriangleIndicesBufferOffset();
            bindings.seedPositionBuffer = seedPositionBuffer;
            bindings.seedPositionBufferOffset = seedPositionBufferOffset_;
            bindings.candidateBuffer = receiver->getVoronoiCandidateBuffer();
            bindings.candidateBufferOffset = receiver->getVoronoiCandidateBufferOffset();

            voronoiCandidateCompute->updateDescriptors(bindings);
            voronoiCandidateCompute->dispatch(faceCount, voronoiNodeCount);
        }
    }

    if (!heatRenderer) {
        std::cerr << "[HeatSystem] HeatRenderer is not initialized; skipping heat descriptor updates" << std::endl;
        return;
    }
    
    // Update descriptors for all receivers
    uint32_t nodeCount = voronoiNodeCount;
    for (auto& receiver : receivers) {
        receiver->updateDescriptors(
            surfaceDescriptorSetLayout, surfaceDescriptorPool,
            tempBufferA, tempBufferAOffset_, tempBufferB, tempBufferBOffset_,
            timeBuffer, timeBufferOffset_, nodeCount
        );

        receiver->updateContactDescriptors(
            contactDescriptorSetLayout,
            contactDescriptorPool,
            *heatSource,
            tempBufferA,
            tempBufferAOffset_,
            tempBufferB,
            tempBufferBOffset_,
            injectionKBuffer,
            injectionKBufferOffset_,
            injectionKTBuffer,
            injectionKTBufferOffset_,
            nodeCount
        );
    }
    heatRenderer->updateDescriptors(
        resourceManager,
        resourceManager.getHeatModel(),
        *heatSource,
        receivers,
        maxFramesInFlight,
        false);

    // Update Voronoi renderer descriptors
    if (voronoiRenderer) {
        for (const auto& receiver : receivers) {
            Model& model = receiver->getModel();
            uint32_t vertexCount = static_cast<uint32_t>(receiver->getIntrinsicVertexCount());

            if (vertexCount == 0) {
                continue;
            }

            auto* iodt = resourceManager.getRemesherForModel(&model);
            if (!iodt) {
                continue;
            }
            auto* supportingHalfedge = iodt->getSupportingHalfedge();
            if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
                continue;
            }

            for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex) {
                voronoiRenderer->updateDescriptors(
                    frameIndex,
                    vertexCount,
                    getSeedPositionBuffer(),
                    getSeedPositionBufferOffset(),
                    getVoronoiNeighborBuffer(),
                    getVoronoiNeighborBufferOffset(),
                    supportingHalfedge->getSupportingHalfedgeView(),
                    supportingHalfedge->getSupportingAngleView(),
                    supportingHalfedge->getHalfedgeView(),
                    supportingHalfedge->getEdgeView(),
                    supportingHalfedge->getTriangleView(),
                    supportingHalfedge->getLengthView(),
                    receiver->getVoronoiCandidateBuffer(),
                    receiver->getVoronoiCandidateBufferOffset()
                );
            }
        }
    }
}

void HeatSystem::cleanupResources() {    
    if (surfacePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), surfacePipeline, nullptr);
        surfacePipeline = VK_NULL_HANDLE;
    }

    if (surfacePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), surfacePipelineLayout, nullptr);
        surfacePipelineLayout = VK_NULL_HANDLE;
    }
    
    if (surfaceDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), surfaceDescriptorPool, nullptr);
        surfaceDescriptorPool = VK_NULL_HANDLE;
    }

    if (surfaceDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), surfaceDescriptorSetLayout, nullptr);
        surfaceDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (contactPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), contactPipeline, nullptr);
        contactPipeline = VK_NULL_HANDLE;
    }
    if (contactPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), contactPipelineLayout, nullptr);
        contactPipelineLayout = VK_NULL_HANDLE;
    }
    if (contactDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), contactDescriptorPool, nullptr);
        contactDescriptorPool = VK_NULL_HANDLE;
    }
    if (contactDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), contactDescriptorSetLayout, nullptr);
        contactDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (heatRenderer) {
        heatRenderer->cleanup();
    }

    if (voronoiRenderer) {
        voronoiRenderer->cleanup();
        voronoiRenderer.reset();
    }

    if (pointRenderer) {
        pointRenderer->cleanup();
        pointRenderer.reset();
    }

	if (contactLineRenderer) {
		contactLineRenderer->cleanup();
		contactLineRenderer.reset();
	}

    if (voronoiPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), voronoiPipeline, nullptr);
        voronoiPipeline = VK_NULL_HANDLE;
    }
    if (voronoiPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), voronoiPipelineLayout, nullptr);
        voronoiPipelineLayout = VK_NULL_HANDLE;
    }
    if (voronoiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), voronoiDescriptorPool, nullptr);
        voronoiDescriptorPool = VK_NULL_HANDLE;
    }
    if (voronoiDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), voronoiDescriptorSetLayout, nullptr);
        voronoiDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (voronoiGeoCompute) {
        voronoiGeoCompute->cleanupResources();
    }

    if (voronoiCandidateCompute) {
        voronoiCandidateCompute->cleanupResources();
    }
}

void HeatSystem::cleanup() {
    if (timeBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(timeBuffer, timeBufferOffset_);
        timeBuffer = VK_NULL_HANDLE;
        mappedTimeData = nullptr;
    }

    if (tempBufferA != VK_NULL_HANDLE) {
        memoryAllocator.free(tempBufferA, tempBufferAOffset_);
        tempBufferA = VK_NULL_HANDLE;
        mappedTempBufferA = nullptr;
    }
    if (tempBufferB != VK_NULL_HANDLE) {
        memoryAllocator.free(tempBufferB, tempBufferBOffset_);
        tempBufferB = VK_NULL_HANDLE;
        mappedTempBufferB = nullptr;
    }

    if (injectionKBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(injectionKBuffer, injectionKBufferOffset_);
        injectionKBuffer = VK_NULL_HANDLE;
        mappedInjectionKBuffer = nullptr;
    }
    if (injectionKTBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(injectionKTBuffer, injectionKTBufferOffset_);
        injectionKTBuffer = VK_NULL_HANDLE;
        mappedInjectionKTBuffer = nullptr;
    }

    if (voronoiNodeBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiNodeBuffer, voronoiNodeBufferOffset_);
        voronoiNodeBuffer = VK_NULL_HANDLE;
        mappedVoronoiNodeData = nullptr;
    }
    if (voronoiNeighborBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiNeighborBuffer, voronoiNeighborBufferOffset_);
        voronoiNeighborBuffer = VK_NULL_HANDLE;
        voronoiNeighborBufferOffset_ = 0;
    }
    if (neighborIndicesBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(neighborIndicesBuffer, neighborIndicesBufferOffset_);
        neighborIndicesBuffer = VK_NULL_HANDLE;
        neighborIndicesBufferOffset_ = 0;
    }

    if (interfaceAreasBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(interfaceAreasBuffer, interfaceAreasBufferOffset_);
        interfaceAreasBuffer = VK_NULL_HANDLE;
        interfaceAreasBufferOffset_ = 0;
        mappedInterfaceAreasData = nullptr;
    }
    if (interfaceNeighborIdsBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(interfaceNeighborIdsBuffer, interfaceNeighborIdsBufferOffset_);
        interfaceNeighborIdsBuffer = VK_NULL_HANDLE;
        interfaceNeighborIdsBufferOffset_ = 0;
        mappedInterfaceNeighborIdsData = nullptr;
    }

    if (meshTriangleBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(meshTriangleBuffer, meshTriangleBufferOffset_);
        meshTriangleBuffer = VK_NULL_HANDLE;
        meshTriangleBufferOffset_ = 0;
    }
    if (seedPositionBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(seedPositionBuffer, seedPositionBufferOffset_);
        seedPositionBuffer = VK_NULL_HANDLE;
        seedPositionBufferOffset_ = 0;
        mappedSeedPositionData = nullptr;
    }
    if (seedFlagsBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(seedFlagsBuffer, seedFlagsBufferOffset_);
        seedFlagsBuffer = VK_NULL_HANDLE;
        seedFlagsBufferOffset_ = 0;
    }

    if (debugCellGeometryBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(debugCellGeometryBuffer, debugCellGeometryBufferOffset_);
        debugCellGeometryBuffer = VK_NULL_HANDLE;
        debugCellGeometryBufferOffset_ = 0;
        mappedDebugCellGeometryData = nullptr;
    }
    if (voronoiDumpBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiDumpBuffer, voronoiDumpBufferOffset_);
        voronoiDumpBuffer = VK_NULL_HANDLE;
        voronoiDumpBufferOffset_ = 0;
        mappedVoronoiDumpData = nullptr;
    }

    if (voxelGridParamsBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voxelGridParamsBuffer, voxelGridParamsBufferOffset_);
        voxelGridParamsBuffer = VK_NULL_HANDLE;
        voxelGridParamsBufferOffset_ = 0;
    }
    if (voxelOccupancyBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voxelOccupancyBuffer, voxelOccupancyBufferOffset_);
        voxelOccupancyBuffer = VK_NULL_HANDLE;
        voxelOccupancyBufferOffset_ = 0;
    }
    if (voxelTrianglesListBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voxelTrianglesListBuffer, voxelTrianglesListBufferOffset_);
        voxelTrianglesListBuffer = VK_NULL_HANDLE;
        voxelTrianglesListBufferOffset_ = 0;
    }
    if (voxelOffsetsBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voxelOffsetsBuffer, voxelOffsetsBufferOffset_);
        voxelOffsetsBuffer = VK_NULL_HANDLE;
        voxelOffsetsBufferOffset_ = 0;
    }

    for (auto& receiver : receivers) {
        if (receiver) {
            receiver->cleanup();
        }
    }
    
    if (heatSource) {
        heatSource->cleanup();
    }
}
