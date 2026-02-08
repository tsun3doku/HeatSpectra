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
#include "HashGridRenderer.hpp"
#include "VoronoiRenderer.hpp"
#include "PointRenderer.hpp"
#include "ContactLineRenderer.hpp"
#include "ContactInterface.hpp"
#include "HeatSystem.hpp"
#include "VoronoiGeoCompute.hpp"
#include "VoronoiIntegrator.hpp"
#include "TriangleHashGrid.hpp"

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

    heatSource = std::make_unique<HeatSource>(vulkanDevice, memoryAllocator, resourceManager.getHeatModel(), resourceManager, maxFramesInFlight, renderCommandPool);
    
    voronoiSeeder = std::make_unique<VoronoiSeeder>();
    createTimeBuffer();
    createSurfaceDescriptorPool(maxFramesInFlight);
    createSurfaceDescriptorSetLayout();
    createSurfacePipeline();

    createContactDescriptorPool(maxFramesInFlight);
    createContactDescriptorSetLayout();
    createContactPipeline();
    
    createHeatRenderDescriptorPool(maxFramesInFlight);
    createHeatRenderDescriptorSetLayout();
    createHeatRenderDescriptorSets(maxFramesInFlight);
    createHeatRenderPipeline(extent, renderPass);
    initializeSurfelRenderers(renderPass, maxFramesInFlight);
    initializeHashGridRenderer(renderPass, maxFramesInFlight);
    initializeVoronoiRenderer(renderPass, maxFramesInFlight);
    initializePointRenderer(renderPass, maxFramesInFlight);
    initializeContactLineRenderer(renderPass, maxFramesInFlight);

	contactInterface = std::make_unique<ContactInterface>(resourceManager);

    initializeVoronoiGeoCompute();

    createComputeCommandBuffers(maxFramesInFlight);
}

void HeatSystem::createContactDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t MAX_RECEIVERS = 10;
    const uint32_t totalSets = MAX_RECEIVERS * 2;

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = totalSets * 11;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = totalSets * 2;

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
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
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

void HeatSystem::initializeHashGridRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    hashGridRenderer = std::make_unique<HashGridRenderer>(vulkanDevice, uniformBufferManager);
    hashGridRenderer->initialize(renderPass, 2, maxFramesInFlight);  // Subpass 2 = Grid subpass
    
    // Allocate and update descriptor sets for all models with hash grids
    Model* heatModelPtr = &resourceManager.getHeatModel();
    if (auto* heatGrid = resourceManager.getHashGridForModel(heatModelPtr)) {
        hashGridRenderer->allocateDescriptorSetsForModel(heatModelPtr, maxFramesInFlight);
        hashGridRenderer->updateDescriptorSetsForModel(heatModelPtr, heatGrid, maxFramesInFlight);
    }
    
    Model* visModelPtr = &resourceManager.getVisModel();
    if (auto* visGrid = resourceManager.getHashGridForModel(visModelPtr)) {
        hashGridRenderer->allocateDescriptorSetsForModel(visModelPtr, maxFramesInFlight);
        hashGridRenderer->updateDescriptorSetsForModel(visModelPtr, visGrid, maxFramesInFlight);
    }
}

void HeatSystem::initializeVoronoiGeoCompute() {
    voronoiGeoCompute = std::make_unique<VoronoiGeoCompute>(vulkanDevice, renderCommandPool);
}

void HeatSystem::update(bool upPressed, bool downPressed, bool leftPressed, bool rightPressed, UniformBufferObject& ubo, uint32_t WIDTH, uint32_t HEIGHT) {
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

    // Use the already mapped memory from UniformBufferManager
    void* mappedMemory = uniformBufferManager.getUniformBuffersMapped()[0];
    memcpy(mappedMemory, &ubo, sizeof(UniformBufferObject));
}

void HeatSystem::recreateResources(uint32_t maxFramesInFlight, VkExtent2D extent, VkRenderPass renderPass) {
    std::cout << "[HeatSystem] recreateResources() called" << std::endl;

    // Use Voronoi if ready
    if (isVoronoiSeederReady) {
        if (voronoiDescriptorPool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(vulkanDevice.getDevice(), voronoiDescriptorPool, 0);
        }
        if (surfaceDescriptorPool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(vulkanDevice.getDevice(), surfaceDescriptorPool, 0);
        }
        if (heatRenderDescriptorPool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(vulkanDevice.getDevice(), heatRenderDescriptorPool, 0);
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
                heatRenderDescriptorSetLayout,
                heatRenderDescriptorPool,
                uniformBufferManager,
                tempBufferA,
                tempBufferAOffset_,
                tempBufferB,
                tempBufferBOffset_,
                timeBuffer,
                timeBufferOffset_,
                maxFramesInFlight,
                nodeCount
            );
        }
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
	if (active && !isVoronoiSeederReady) {
		resourceManager.buildCommonSubdivision();

		voronoiSeeder->generateSeeds(
			resourceManager.getIntrinsicTriangles(),
			resourceManager.getCommonSubdivision().getVertices(),
			resourceManager.getVisModel(),
			0.005f
		);

		voronoiSeeder->exportSeedsToOBJ("voronoi_seeds.obj");
		isVoronoiSeederReady = true;

		std::cout << "[HeatSystem] Using Voronoi VEM" << std::endl;
		generateVoronoiDiagram();

		Model* heatModelPtr = &resourceManager.getHeatModel();
		for (const auto& [model, remeshData] : resourceManager.getModelRemeshData()) {
			if (model != heatModelPtr && remeshData.isRemeshed) {
				addReceiver(model);

				if (!receivers.empty() && voronoiIntegrator) {
					auto* iodt = resourceManager.getRemesherForModel(&receivers.back()->getModel());
					if (iodt) {
						auto* sh = iodt->getSupportingHalfedge();
						if (sh) {
							auto intrinsicMesh = sh->buildIntrinsicMesh();
							std::vector<glm::vec3> surfacePoints;
							surfacePoints.reserve(intrinsicMesh.vertices.size());
							for (size_t vi = 0; vi < intrinsicMesh.vertices.size(); ++vi) {
								surfacePoints.push_back(intrinsicMesh.vertices[vi].position);
							}

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
    
    // Binding 0: Voronoi nodes (volumes, thermal properties)
    std::vector<VoronoiNodeGPU> nodes(voronoiNodeCount);
    // Initialize with aluminum thermal properties
    for (auto& node : nodes) {
        node.temperature = 1.0f;
        node.prevTemperature = 0.0f;
        node.volume = 0.0f;
        node.thermalMass = 0.0f;
        
        // Aluminum thermal properties
        node.density = 2700.0f;        // kg/m³
        node.specificHeat = 900.0f;    // J/(kg·K)
        node.conductivity = 205.0f;    // W/(m·K)
        
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
    
    // Binding 19: Interface neighbor IDs (separate buffer)
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
    const uint32_t NUM_DEBUG_CELLS = voronoiNodeCount;  // Export all cells
    std::vector<DebugCellGeometry> debugCells(NUM_DEBUG_CELLS);
    for (auto& cell : debugCells) {
        cell.cellID = 0;
        cell.vertexCount = 0;
        cell.triangleCount = 0;
        cell.volume = 0.0f;
    }
    
    bufferSize = sizeof(DebugCellGeometry) * NUM_DEBUG_CELLS;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        debugCells.data(), bufferSize,
        debugCellGeometryBuffer, debugCellGeometryBufferOffset_, &mappedDebugCellGeometryData
    );
    std::cout << "  Debug cell geometry buffer: " << NUM_DEBUG_CELLS << " cells ("
              << (bufferSize / (1024.0 * 1024.0)) << " MB)" << std::endl;

    // Binding 22: VoronoiDumpInfo debug buffer
    std::vector<VoronoiDumpInfo> dumpInfos(DEBUG_DUMP_CELL_COUNT);
    memset(dumpInfos.data(), 0, sizeof(VoronoiDumpInfo) * DEBUG_DUMP_CELL_COUNT);
    
    bufferSize = sizeof(VoronoiDumpInfo) * DEBUG_DUMP_CELL_COUNT;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        dumpInfos.data(), bufferSize,
        voronoiDumpBuffer, voronoiDumpBufferOffset_, &mappedVoronoiDumpData
    );
    std::cout << "  VoronoiDumpInfo buffer: " << DEBUG_DUMP_CELL_COUNT << " slots ("
              << (bufferSize / 1024.0) << " KB)" << std::endl;

    // Binding 6: Mesh triangles
    auto meshTris = integrator.getMeshTriangles();
    if (meshTris.empty()) meshTris.push_back({});
    
    bufferSize = sizeof(MeshTriangleGPU) * meshTris.size();
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        meshTris.data(), bufferSize,
        meshTriangleBuffer, meshTriangleBufferOffset_, &mappedPtr
    );
    std::cout << "  Mesh triangles: " << integrator.getMeshTriangles().size() << std::endl;
    
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
    mappedSeedPositionData = mappedSeeds;  // Store for distance calculations
    std::cout << "  Seed positions: " << integrator.getSeedPositions().size() << std::endl;
    
    // Binding 21: Seed flags (isGhost, isSurface)
    if (seedFlags.empty()) {
        std::cerr << "[HeatSystem] ERROR: seedFlags is empty!" << std::endl;
    }
    
    bufferSize = sizeof(uint32_t) * seedFlags.size();
    void* mappedFlags = nullptr;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        seedFlags.data(), bufferSize,
        seedFlagsBuffer, seedFlagsBufferOffset_, &mappedFlags
    );
    std::cout << "  Seed flags: " << seedFlags.size() << " seeds" << std::endl;
    
    // VoxelGrid buffers (bindings 5, 6, 9, 10)
    if (integrator.hasVoxelGrid()) {
        const auto& voxelGrid = integrator.getVoxelGrid();
        const auto& params = voxelGrid.getParams();
        
        std::cout << "  Creating VoxelGrid GPU buffers..." << std::endl;
        
        // Binding 5: Voxel grid parameters (uniform buffer)
        bufferSize = sizeof(VoxelGrid::VoxelGridParams);
        createUniformBuffer(
            memoryAllocator, vulkanDevice, bufferSize,
            voxelGridParamsBuffer, voxelGridParamsBufferOffset_, &mappedPtr
        );
        if (mappedPtr) {
            memcpy(mappedPtr, &params, sizeof(VoxelGrid::VoxelGridParams));
        }
        std::cout << "    Grid params: " << params.gridDim.x << "^3" << std::endl;
        
        // Binding 6: Occupancy data
        // Store as uint32_t elements so shaders can safely read `uint occupancy[]`.
        auto occupancy8 = voxelGrid.getOccupancyData();
        if (occupancy8.empty()) occupancy8.push_back(0);
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
        std::cout << "    Occupancy: " << occupancy8.size() << " corners" << std::endl;
        
        // Binding 9: Triangle list
        const auto& trianglesList = voxelGrid.getTrianglesList();  
        if (trianglesList.empty()) {
            std::cerr << "ERROR: VoxelGrid trianglesList is empty!" << std::endl;
        }
        bufferSize = sizeof(int32_t) * trianglesList.size();
        createStorageBuffer(
            memoryAllocator, vulkanDevice,
            trianglesList.data(), bufferSize,
            voxelTrianglesListBuffer, voxelTrianglesListBufferOffset_, &mappedPtr,
            true  // Send data to GPU
        );
        std::cout << "    Triangle list: " << voxelGrid.getTrianglesList().size() << " refs" << std::endl;
        
        // Binding 10: Offsets
        const auto& offsets = voxelGrid.getOffsets();  
        if (offsets.empty()) {
            std::cerr << "ERROR: VoxelGrid offsets is empty!" << std::endl;
        }
        bufferSize = sizeof(int32_t) * offsets.size();
        createStorageBuffer(
            memoryAllocator, vulkanDevice,
            offsets.data(), bufferSize,
            voxelOffsetsBuffer, voxelOffsetsBufferOffset_, &mappedPtr,
            true  // Send data to GPU
        );
        std::cout << "    Offsets: " << voxelGrid.getOffsets().size() << std::endl;
    }
    
    std::cout << "[HeatSystem] Voronoi geometry buffers created" << std::endl;
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
    
    // Export cell index to seed position mapping
    {
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
    
    // Build voxel grid 
    std::cout << "[HeatSystem] Building voxel grid for spatial mapping and shadow cone origins..." << std::endl;
    TriangleHashGrid triangleGrid;
    triangleGrid.build(resourceManager.getVisModel(), gridMin, gridMax, cellSize);
    voronoiIntegrator->buildVoxelGrid(resourceManager.getVisModel(), triangleGrid, 128);
    
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
        voronoiGeoCompute->dispatch();
    }
    exportDebugCellsToOBJ();
    exportCellVolumes();
    exportVoronoiDumpInfo();
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

    std::cout << "[HeatSystem] Created ping pong temp buffers (" << voronoiNodeCount << " floats)" << std::endl;
    
    // Initialize temperatures
    initializeVoronoi();
    
    // Now create all Voronoi heat diffusion resources with valid buffer handles
    std::cout << "[HeatSystem] Creating Voronoi compute pipeline..." << std::endl;
    createVoronoiDescriptorPool(maxFramesInFlight);
    createVoronoiDescriptorSetLayout();
    createVoronoiPipeline();
    createVoronoiDescriptorSets(maxFramesInFlight);
    
    uploadOccupancyPoints(voronoiIntegrator->getVoxelGrid());
    
    std::cout << "[HeatSystem] Geometry precomputation complete" << std::endl;
}

void HeatSystem::exportDebugCellsToOBJ() {    
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

    // Store global sum into each dump slot
    for (uint32_t slot = 0; slot < DEBUG_DUMP_CELL_COUNT; slot++)
    {
        dumpInfos[slot].totalRestrictedVolume = (float)totalRestrictedVolumePos;
    }

    std::ofstream dumpFile("voronoi_debug_dump.txt");
    dumpFile << std::scientific << std::setprecision(10);

    dumpFile << "Total Mesh Volume: " << totalRestrictedVolumePos << "\n";
    dumpFile << "Negative volume cells: " << negativeVolumeCount << " Sum =" << totalRestrictedVolumeNegAbs << ")\n";
    dumpFile << "\n";
    
    uint32_t debugCellIDs[] = {59903};  
    
    for (uint32_t slot = 0; slot < DEBUG_DUMP_CELL_COUNT; slot++) {
        const VoronoiDumpInfo& info = dumpInfos[slot];
        
        // Skip empty slots
        if (info.cellID == 0)
            continue;
        
        dumpFile << "CELL " << info.cellID << " (Slot " << slot << ")\n";
        dumpFile << "Seed: (" << info.seedPos.x << ", " << info.seedPos.y << ", " << info.seedPos.z << ")\n";
        dumpFile << "Flags: isGhost=" << info.isGhost << "\n";
        dumpFile << "inDomain: " << info.inDomain << " (addVolume=" << info.addVolume << ")\n";
        dumpFile << "Origin: (" << info.origin.x << ", " << info.origin.y << ", " << info.origin.z << ") status=" << info.originOccupancy << "\n";
        dumpFile << "FirstInsideCorner: (" << info.firstInsideCorner.x << "," << info.firstInsideCorner.y << "," << info.firstInsideCorner.z << ")\n";
        dumpFile << "FirstOutsideCorner: (" << info.firstOutsideCorner.x << "," << info.firstOutsideCorner.y << "," << info.firstOutsideCorner.z << ")\n\n";
        
        dumpFile << "Volumes:\n";
        dumpFile << "  Unrestricted: " << info.unrestrictedVolume << "\n";
        dumpFile << "  Restricted:   " << info.restrictedVolume << "\n";
        dumpFile << "Volume Chunks:\n";
        
        for (uint32_t i = 0; i < DEBUG_MAX_TRIANGLES; i++) {
            const VolumeChunkInfo& chunk = info.volumeChunks[i];
            
            // Skip empty/unprocessed chunks
            if (chunk.triIndex == 0 && chunk.triDet == 0.0f && chunk.chunkVolume == 0.0f)
                continue;
            
            dumpFile << "  [" << i << "] triId=" << chunk.triIndex;
            dumpFile << " triDet=" << chunk.triDet << ", goOut=" << chunk.goOut;
            
            if (chunk.skipped > 0) {
                const char* skipReasons[] = {"", "plane0", "plane1", "plane2", "cap"};
                if (chunk.skipped < (sizeof(skipReasons) / sizeof(skipReasons[0]))) {
                    dumpFile << " SKIPPED@" << skipReasons[chunk.skipped] << "\n";
                } else {
                    dumpFile << " SKIPPED@unknown(" << chunk.skipped << ")\n";
                }
            } else {
                dumpFile << " chunkVol=" << chunk.chunkVolume;
                dumpFile << " chunkDet=" << chunk.chunkDet;
                dumpFile << " cumulative=" << chunk.cumulativeVolume << "\n";
            }
        }
        
        // Corner occupancy
        dumpFile << "Corner Occupancy (" << info.cornerCount << " total):\n";
        dumpFile << "Range: (" << info.cornerMin.x << "," << info.cornerMin.y << "," << info.cornerMin.z << ") -> ("
                 << info.cornerMax.x << "," << info.cornerMax.y << "," << info.cornerMax.z << ")\n";
        
        uint32_t cIdx = 0;
        const char* domainLabels[] = {"Outside", "Border ", "Inside "};
        
        bool doneCorners = false;
        for (int z = info.cornerMin.z; z <= info.cornerMax.z && !doneCorners; z++) {
            for (int y = info.cornerMin.y; y <= info.cornerMax.y && !doneCorners; y++) {
                for (int x = info.cornerMin.x; x <= info.cornerMax.x; x++) {
                    if (cIdx >= info.cornerCount || cIdx >= 1024)
                    {
                        doneCorners = true;
                        break;
                    }
                    
                    uint32_t status = info.cornerOccupancy[cIdx];
                    const char* label = (status <= 2) ? domainLabels[status] : "Unknown";
                    
                    dumpFile << "  Corner(" << x << "," << y << "," << z << "): " << label << " (" << status << ")\n";
                    cIdx++;
                }
            }
        }
        dumpFile << "\n";
    }
    
    dumpFile.close();
    std::cout << "[HeatSystem] Exported debug dump to: voronoi_debug_dump.txt" << std::endl;
}

void HeatSystem::buildVoronoiNeighborBuffer() {
    std::cout << "[HeatSystem] Building VoronoiNeighborBuffer from GPU data..." << std::endl;
    
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
    
    std::cout << "  Built " << totalNeighbors << " neighbor entries for " << voronoiNodeCount << " cells" << std::endl;
    
    // Create voronoiNeighborBuffer
    if (totalNeighbors > 0) {
        VkDeviceSize bufferSize = sizeof(VoronoiNeighborGPU) * totalNeighbors;
        void* mappedPtr = nullptr;
        
        createStorageBuffer(
            memoryAllocator, vulkanDevice,
            neighbors.data(), bufferSize,
            voronoiNeighborBuffer, voronoiNeighborBufferOffset_, &mappedPtr
        );
        
        std::cout << "  VoronoiNeighborBuffer created (" << totalNeighbors << " neighbors)" << std::endl;
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

void HeatSystem::recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording compute command buffer");
    }

    if (!isActive) {
        vkEndCommandBuffer(commandBuffer);
        return;
    }
    
    // Early return if Voronoi setup not complete yet
    if (!isVoronoiReady) {
        vkEndCommandBuffer(commandBuffer);
        return;
    }

    static int recordCallCount = 0;
    if (recordCallCount == 0) {
        std::cout << "[HeatSystem] recordComputeCommands called, isActive=" << isActive << std::endl;
    }
    recordCallCount++;
    
    // Update hash grid descriptors 
    Model* heatModelPtr = &resourceManager.getHeatModel();
    if (auto* heatGrid = resourceManager.getHashGridForModel(heatModelPtr)) {
        heatGrid->updateBuildDescriptors(
            heatSource->getTriangleCentroidBuffer(),
            heatSource->getTriangleCentroidBufferOffset(),
            currentFrame
        );
    }
    
    // Build heat source hash grid (receiver->source queries only)
    if (auto* heatGrid = resourceManager.getHashGridForModel(heatModelPtr)) {
        heatGrid->buildGrid(
            commandBuffer,
            heatSource->getTriangleCentroidBuffer(),
            heatSource->getTriangleCount(),
            heatSource->getTriangleCentroidBufferOffset(),
            heatModelPtr->getModelMatrix(),
            currentFrame
        );
    }
        
        // Memory barrier after hash grid builds
        VkMemoryBarrier memBarrier{};
        memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        
        vkCmdPipelineBarrier(commandBuffer,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                           0, 1, &memBarrier, 0, nullptr, 0, nullptr);
        
        if (true) { 
            uint32_t nodeCount = voronoiNodeCount;
            uint32_t workGroupSize = 256;
            uint32_t workGroupCount = (nodeCount + workGroupSize - 1) / workGroupSize;
            
            // Build push constant
            HeatSourcePushConstant pushConstant = heatSource->getHeatSourcePushConstant();
            pushConstant.heatSourceModelMatrix = heatModelPtr->getModelMatrix();
            pushConstant.inverseHeatSourceModelMatrix = glm::inverse(pushConstant.heatSourceModelMatrix);
            
            Model* receiverModelPtr = nullptr;
            if (!receivers.empty()) {
                receiverModelPtr = &receivers[0]->getModel();
            }
            if (receiverModelPtr) {
                pushConstant.visModelMatrix = receiverModelPtr->getModelMatrix();
            }
            
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
        
            // Barrier for hash grid buffers 
            VkMemoryBarrier gridMemBarrier{};
            gridMemBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            gridMemBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            gridMemBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            
            vkCmdPipelineBarrier(commandBuffer,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                               0, 1, &gridMemBarrier, 0, nullptr, 0, nullptr);
    }

    vkEndCommandBuffer(commandBuffer);
}

void HeatSystem::renderHashGrids(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (!hashGridRenderer) 
        return;
    
    // Render hash grid for heat source model
    Model* heatModelPtr = &resourceManager.getHeatModel();
    if (auto* heatGrid = resourceManager.getHashGridForModel(heatModelPtr)) {
        glm::vec3 gridColor = glm::vec3(1.0f, 0.0f, 0.0f);  // Red for heat source
        hashGridRenderer->render(cmdBuffer, heatModelPtr, heatGrid, frameIndex, 
                                heatModelPtr->getModelMatrix(), gridColor);
    }
    
    // Render hash grid for receiver models
    for (const auto& receiver : receivers) {
        Model* receiverModelPtr = &receiver->getModel();
        if (auto* receiverGrid = resourceManager.getHashGridForModel(receiverModelPtr)) {
            glm::vec3 gridColor = glm::vec3(0.0f, 0.5f, 1.0f);  // Blue for receivers
            hashGridRenderer->render(cmdBuffer, receiverModelPtr, receiverGrid, frameIndex, 
                                    receiverModelPtr->getModelMatrix(), gridColor);
        }
    }
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
        VkBuffer mappingBuffer = receiver->getVoronoiMappingBuffer();
        VkDeviceSize mappingOffset = receiver->getVoronoiMappingBufferOffset();
        Model& model = receiver->getModel();
        uint32_t vertexCount = static_cast<uint32_t>(model.getVertices().size());
        
        // Only render if mapping buffer exists
        if (mappingBuffer != VK_NULL_HANDLE && vertexCount > 0) {
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

void HeatSystem::initializePointRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    pointRenderer = std::make_unique<PointRenderer>(
        vulkanDevice, 
        memoryAllocator,
        uniformBufferManager
    );
    pointRenderer->initialize(renderPass, 2, maxFramesInFlight);  // Subpass 2 = Grid subpass
}

void HeatSystem::uploadOccupancyPoints(const VoxelGrid& voxelGrid) {
    std::cout << "[HeatSystem] Uploading occupancy points for visualization..." << std::endl;
    
    const auto& occupancy = voxelGrid.getOccupancyData();
    const auto& params = voxelGrid.getParams();
    
    std::vector<PointRenderer::PointVertex> points;
    points.reserve(occupancy.size() / 4);  // Estimate: ~25% will be inside/border
    
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

void HeatSystem::createHeatRenderDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t MAX_HEAT_MODELS = 10;
    const uint32_t totalSets = maxFramesInFlight * (1 + MAX_HEAT_MODELS); // Global + models
    
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    // UBO
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = totalSets;
    
    // Texture buffers (S, A, H, E, T, L, H_input, E_input, T_input, L_input, heatColors = 11)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[1].descriptorCount = totalSets * 11;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = totalSets;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &heatRenderDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat render descriptor pool");
    }
}

void HeatSystem::createHeatRenderDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 12> bindings{};
    
    // Binding 0: UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Bindings 1-10: Supporting halfedge data
    for (int i = 1; i <= 10; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    
    // Binding 11: Heat colors
    bindings[11].binding = 11;
    bindings[11].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[11].descriptorCount = 1;
    bindings[11].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &heatRenderDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat render descriptor set layout");
    }
}

void HeatSystem::createHeatRenderDescriptorSets(uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, heatRenderDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = heatRenderDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    heatRenderDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, heatRenderDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate heat render descriptor sets");
    }
    
    // Write UBO for each frame
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);
        
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = heatRenderDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &uboBufferInfo;
        
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

void HeatSystem::createHeatRenderPipeline(VkExtent2D extent, VkRenderPass renderPass) {
    auto vertShaderCode = readFile("shaders/intrinsic_supporting_vert.spv");
    auto geomShaderCode = readFile("shaders/intrinsic_supporting_geom.spv");
    auto fragShaderCode = readFile("shaders/heat_buffer_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule geomShaderModule = createShaderModule(vulkanDevice, geomShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo geomShaderStageInfo{};
    geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geomShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geomShaderStageInfo.module = geomShaderModule;
    geomShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, geomShaderStageInfo, fragShaderStageInfo };

    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto vertexAttributes = Vertex::getVertexAttributes();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();

    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[3] = {};
    for (int i = 0; i < 3; ++i) {
        colorBlendAttachments[i].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[i].blendEnable = VK_TRUE;
        colorBlendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachments[i].colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachments[i].alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 3;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
         VK_DYNAMIC_STATE_VIEWPORT,
         VK_DYNAMIC_STATE_SCISSOR,
         VK_DYNAMIC_STATE_DEPTH_BIAS,
         VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constants for model matrix
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);  
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &heatRenderDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &heatRenderPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat render pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = heatRenderPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &heatRenderPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat render pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);  
}

void HeatSystem::addReceiver(Model* model) {
    if (!model) 
        return;
    
    auto receiver = std::make_unique<HeatReceiver>(vulkanDevice, memoryAllocator, *model, resourceManager, renderCommandPool, maxFramesInFlight);
    
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
    
    // Update descriptors for all receivers
    uint32_t nodeCount = voronoiNodeCount;
    for (auto& receiver : receivers) {
        receiver->updateDescriptors(
            surfaceDescriptorSetLayout, heatRenderDescriptorSetLayout, surfaceDescriptorPool, heatRenderDescriptorPool,
            uniformBufferManager, tempBufferA, tempBufferAOffset_, tempBufferB, tempBufferBOffset_,
            timeBuffer, timeBufferOffset_, maxFramesInFlight,
            nodeCount
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
    // Update heat source render descriptors for viz
    heatSource->updateHeatRenderDescriptors(heatRenderDescriptorSetLayout, heatRenderDescriptorPool, uniformBufferManager, maxFramesInFlight);

    // Update Voronoi renderer descriptors once after buffers are ready.
    if (voronoiRenderer) {
        for (const auto& receiver : receivers) {
            VkBuffer mappingBuffer = receiver->getVoronoiMappingBuffer();
            VkDeviceSize mappingOffset = receiver->getVoronoiMappingBufferOffset();
            Model& model = receiver->getModel();
            uint32_t vertexCount = static_cast<uint32_t>(model.getVertices().size());

            if (mappingBuffer == VK_NULL_HANDLE || vertexCount == 0) {
                continue;
            }

            for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex) {
                voronoiRenderer->updateDescriptors(
                    frameIndex,
                    mappingBuffer,
                    mappingOffset,
                    vertexCount,
                    getSeedPositionBuffer(),
                    getSeedPositionBufferOffset(),
                    getVoronoiNodeBuffer(),
                    getVoronoiNodeBufferOffset(),
                    getVoronoiNeighborBuffer(),
                    getVoronoiNeighborBufferOffset()
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

    if (heatRenderPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), heatRenderPipeline, nullptr);
        heatRenderPipeline = VK_NULL_HANDLE;
    }
    if (heatRenderPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), heatRenderPipelineLayout, nullptr);
        heatRenderPipelineLayout = VK_NULL_HANDLE;
    }
    if (heatRenderDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), heatRenderDescriptorPool, nullptr);
        heatRenderDescriptorPool = VK_NULL_HANDLE;
    }
    if (heatRenderDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), heatRenderDescriptorSetLayout, nullptr);
        heatRenderDescriptorSetLayout = VK_NULL_HANDLE;
    }


    
    if (hashGridRenderer) {
        hashGridRenderer->cleanup();
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
}

void HeatSystem::cleanup() {
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

    for (auto& receiver : receivers) {
        if (receiver) {
            receiver->cleanup();
        }
    }
    
    if (heatSource) {
        heatSource->cleanup();
    }
}
