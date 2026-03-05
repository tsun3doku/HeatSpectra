#include "FrameSync.hpp"

#include <array>

bool FrameSync::initialize(VkDevice deviceHandle, uint32_t maxFramesInFlight) {
    shutdown();

    device = deviceHandle;
    frameCount = maxFramesInFlight;
    currentFrame = 0;

    if (device == VK_NULL_HANDLE || frameCount == 0) {
        return false;
    }

    imageAvailableSemaphores.resize(frameCount);
    renderFinishedSemaphores.resize(frameCount);
    computeFinishedSemaphores.resize(frameCount);
    inFlightFences.resize(frameCount);
    computeInFlightFences.resize(frameCount);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    const auto failCreate = [this]() {
        shutdown();
        return false;
    };

    for (uint32_t index = 0; index < frameCount; ++index) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[index]) != VK_SUCCESS) {
            return failCreate();
        }
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[index]) != VK_SUCCESS) {
            return failCreate();
        }
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &computeFinishedSemaphores[index]) != VK_SUCCESS) {
            return failCreate();
        }
        if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[index]) != VK_SUCCESS) {
            return failCreate();
        }
        if (vkCreateFence(device, &fenceInfo, nullptr, &computeInFlightFences[index]) != VK_SUCCESS) {
            return failCreate();
        }
    }

    return true;
}

void FrameSync::shutdown() {
    if (device != VK_NULL_HANDLE) {
        for (VkSemaphore semaphore : renderFinishedSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
        }
        for (VkSemaphore semaphore : imageAvailableSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
        }
        for (VkSemaphore semaphore : computeFinishedSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
        }
        for (VkFence fence : inFlightFences) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(device, fence, nullptr);
            }
        }
        for (VkFence fence : computeInFlightFences) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(device, fence, nullptr);
            }
        }
    }

    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    computeFinishedSemaphores.clear();
    inFlightFences.clear();
    computeInFlightFences.clear();
    frameCount = 0;
    currentFrame = 0;
    device = VK_NULL_HANDLE;
}

void FrameSync::waitForCurrentFrameFence() const {
    if (device == VK_NULL_HANDLE || currentFrame >= inFlightFences.size()) {
        return;
    }

    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    if (currentFrame < computeInFlightFences.size()) {
        vkWaitForFences(device, 1, &computeInFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    }
}

uint32_t FrameSync::beginFrame() const {
    waitForCurrentFrameFence();
    return currentFrame;
}

void FrameSync::waitForAllFrameFences() const {
    if (device == VK_NULL_HANDLE || inFlightFences.empty()) {
        return;
    }

    vkWaitForFences(device, static_cast<uint32_t>(inFlightFences.size()), inFlightFences.data(), VK_TRUE, UINT64_MAX);
    if (!computeInFlightFences.empty()) {
        vkWaitForFences(device, static_cast<uint32_t>(computeInFlightFences.size()), computeInFlightFences.data(), VK_TRUE, UINT64_MAX);
    }
}

void FrameSync::prepareGraphicsSubmit() const {
    if (device == VK_NULL_HANDLE || currentFrame >= inFlightFences.size()) {
        return;
    }

    vkResetFences(device, 1, &inFlightFences[currentFrame]);
}

void FrameSync::prepareComputeSubmit() const {
    if (device == VK_NULL_HANDLE || currentFrame >= computeInFlightFences.size()) {
        return;
    }

    vkResetFences(device, 1, &computeInFlightFences[currentFrame]);
}

VkResult FrameSync::acquireNextImage(VkSwapchainKHR swapChain, uint32_t& imageIndex) const {
    imageIndex = 0;
    if (device == VK_NULL_HANDLE ||
        swapChain == VK_NULL_HANDLE ||
        currentFrame >= imageAvailableSemaphores.size()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    return vkAcquireNextImageKHR(
        device,
        swapChain,
        UINT64_MAX,
        imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE,
        &imageIndex);
}

VkResult FrameSync::submitCompute(VkQueue computeQueue, VkCommandBuffer commandBuffer, bool signalComputeFinished) const {
    if (computeQueue == VK_NULL_HANDLE || commandBuffer == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkSubmitInfo computeSubmitInfo{};
    computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    computeSubmitInfo.commandBufferCount = 1;
    computeSubmitInfo.pCommandBuffers = &commandBuffer;
    if (signalComputeFinished) {
        const VkSemaphore signalSemaphore = computeFinishedSemaphores[currentFrame];
        computeSubmitInfo.signalSemaphoreCount = 1;
        computeSubmitInfo.pSignalSemaphores = &signalSemaphore;
    }

    return vkQueueSubmit(computeQueue, 1, &computeSubmitInfo, computeInFlightFences[currentFrame]);
}

VkResult FrameSync::submitGraphics(VkQueue graphicsQueue, VkCommandBuffer commandBuffer, bool waitForCompute, VkPipelineStageFlags computeWaitStageMask) const {
    if (graphicsQueue == VK_NULL_HANDLE || commandBuffer == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::array<VkSemaphore, 2> waitSemaphores{};
    std::array<VkPipelineStageFlags, 2> waitStages{};
    uint32_t waitCount = 1;
    waitSemaphores[0] = imageAvailableSemaphores[currentFrame];
    waitStages[0] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    if (waitForCompute) {
        waitSemaphores[waitCount] = computeFinishedSemaphores[currentFrame];
        waitStages[waitCount] = computeWaitStageMask;
        ++waitCount;
    }

    const VkSemaphore signalSemaphore = renderFinishedSemaphores[currentFrame];

    VkSubmitInfo graphicsSubmitInfo{};
    graphicsSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    graphicsSubmitInfo.waitSemaphoreCount = waitCount;
    graphicsSubmitInfo.pWaitSemaphores = waitSemaphores.data();
    graphicsSubmitInfo.pWaitDstStageMask = waitStages.data();
    graphicsSubmitInfo.commandBufferCount = 1;
    graphicsSubmitInfo.pCommandBuffers = &commandBuffer;
    graphicsSubmitInfo.signalSemaphoreCount = 1;
    graphicsSubmitInfo.pSignalSemaphores = &signalSemaphore;

    return vkQueueSubmit(graphicsQueue, 1, &graphicsSubmitInfo, inFlightFences[currentFrame]);
}

VkResult FrameSync::present(VkQueue presentQueue, VkSwapchainKHR swapChain, uint32_t imageIndex) const {
    if (presentQueue == VK_NULL_HANDLE || swapChain == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const VkSemaphore waitSemaphore = renderFinishedSemaphores[currentFrame];
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(presentQueue, &presentInfo);
}

void FrameSync::advanceFrame() {
    if (frameCount == 0) {
        return;
    }

    currentFrame = (currentFrame + 1) % frameCount;
}

void FrameSync::resetFrameIndex() {
    currentFrame = 0;
}
