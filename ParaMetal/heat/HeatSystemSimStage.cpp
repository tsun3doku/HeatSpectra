#include "HeatSystemSimStage.hpp"

#include "heat/HeatModelRuntime.hpp"
#include "heat/HeatSystemPlayback.hpp"
#include "HeatSystemDiffusionStage.hpp"

bool HeatSystemSimStage::recordComputeCommands(
    VkCommandBuffer commandBuffer,
    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& activeModels,
    const HeatSystemDiffusionStage& diffusionStage,
    bool steppingPhysics,
    bool captureFrame,
    bool temperatureBufferAIsCurrent,
    uint32_t diffusionSubsteps) const {
    if (!steppingPhysics || activeModels.empty() || diffusionSubsteps == 0) {
        return temperatureBufferAIsCurrent;
    }

    const bool finalA = recordSim(
        commandBuffer,
        activeModels,
        diffusionStage,
        temperatureBufferAIsCurrent,
        diffusionSubsteps);

    if (captureFrame) {
        recordHistoryCapture(commandBuffer, activeModels, !finalA);
    }

    return finalA;
}

bool HeatSystemSimStage::recordSim(
    VkCommandBuffer commandBuffer,
    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& activeModels,
    const HeatSystemDiffusionStage& diffusionStage,
    bool temperatureBufferAIsCurrent,
    uint32_t diffusionSubsteps) const {
    const uint32_t workGroupSize = 256;

    for (const auto& [_, modelPtr] : activeModels) {
        if (modelPtr) {
            modelPtr->uploadRuntimeLoads(commandBuffer);
        }
    }

    bool readBufferA = temperatureBufferAIsCurrent;

    for (uint32_t substepIndex = 0; substepIndex < diffusionSubsteps; ++substepIndex) {

        // Diffusion reads the current buffer and writes the opposite
        for (const auto& [modelId, modelPtr] : activeModels) {
            if (!modelPtr || modelPtr->getSimNodeCount() == 0) continue;

            heat::HeatModelPushConstant pushConstant{
                modelPtr->getSimNodeCount(),
                modelPtr->getMetersPerWorldUnit()};
            VkDescriptorSet modelSet =
                readBufferA ? modelPtr->getVoronoiDescriptorSetA() : modelPtr->getVoronoiDescriptorSetB();

            if (modelSet != VK_NULL_HANDLE) {
                diffusionStage.dispatchDiffusionSubstep(commandBuffer, modelSet, pushConstant, (modelPtr->getSimNodeCount() + workGroupSize - 1) / workGroupSize);
            }
        }

        // Flip read for the next substep
        readBufferA = !readBufferA;

        if (substepIndex + 1 < diffusionSubsteps) {
            std::vector<VkBufferMemoryBarrier> substepBarriers;
            substepBarriers.reserve(activeModels.size());

            for (const auto& [modelId, modelPtr] : activeModels) {
                if (!modelPtr || modelPtr->getSimNodeCount() == 0) continue;

                VkBufferMemoryBarrier tempBarrier{};
                tempBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                tempBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                tempBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                tempBarrier.buffer = readBufferA ? modelPtr->getTempBufferA() : modelPtr->getTempBufferB();
                tempBarrier.offset = 0;
                tempBarrier.size = modelPtr->getSimNodeCount() * sizeof(float);
                substepBarriers.push_back(tempBarrier);

            }

            if (!substepBarriers.empty()) {
                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,
                    0,
                    nullptr,
                    static_cast<uint32_t>(substepBarriers.size()),
                    substepBarriers.data(),
                    0,
                    nullptr);
            }
        }
    }

    for (const auto& [modelId, heatModel] : activeModels) {
        if (!heatModel || heatModel->getSimNodeCount() == 0) continue;

        VkBufferMemoryBarrier finalTempBarrier{};
        finalTempBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        finalTempBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        finalTempBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        finalTempBarrier.buffer = readBufferA ? heatModel->getTempBufferA() : heatModel->getTempBufferB();
        finalTempBarrier.offset = 0;
        finalTempBarrier.size = heatModel->getSimNodeCount() * sizeof(float);
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr,
            1, &finalTempBarrier,
            0, nullptr);
    }

    return readBufferA;
}

void HeatSystemSimStage::recordHistoryCapture(
    VkCommandBuffer commandBuffer,
    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& activeModels,
    bool finalWritesBufferB) const {

    std::vector<VkBufferMemoryBarrier> snapshotBarriers;
    for (const auto& [modelId, heatModel] : activeModels) {
        if (!heatModel || heatModel->getSimNodeCount() == 0) continue;
        auto* playback = heatModel->getPlayback();
        if (!playback) continue;

        VkBuffer finalBuf = finalWritesBufferB ? heatModel->getTempBufferB() : heatModel->getTempBufferA();

        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.buffer = finalBuf;
        barrier.offset = 0;
        barrier.size = heatModel->getSimNodeCount() * sizeof(float);
        snapshotBarriers.push_back(barrier);
    }
    if (!snapshotBarriers.empty()) {
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            static_cast<uint32_t>(snapshotBarriers.size()), snapshotBarriers.data(),
            0, nullptr);
    }

    for (const auto& [modelId, heatModel] : activeModels) {
        if (!heatModel || heatModel->getSimNodeCount() == 0) continue;
        auto* playback = heatModel->getPlayback();
        if (!playback) continue;

        VkBuffer finalBuf = finalWritesBufferB ? heatModel->getTempBufferB() : heatModel->getTempBufferA();
        playback->recordFrame(commandBuffer, finalBuf, 0);
    }
}
