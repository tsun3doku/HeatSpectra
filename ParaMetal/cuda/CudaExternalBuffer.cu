#define NOMINMAX
#include <Windows.h>

#include "CudaExternalBuffer.hpp"
#include "../vulkan/VulkanExternalBuffer.hpp"

#include <cuda_runtime.h>
#include <iostream>

class CudaExternalBuffer::Implementation {
public:
    bool ensureMapped(const VulkanExternalBuffer& buffer, int requestedDevice) {
        if (pointer && sourceBuffer == buffer.getBuffer()) return true;
        cleanup();

        cudaDevice = requestedDevice;
        if (cudaSetDevice(cudaDevice) != cudaSuccess) return false;

        HANDLE handle = static_cast<HANDLE>(buffer.exportWin32Handle());
        if (!handle) return false;

        cudaExternalMemoryHandleDesc memoryDescription{};
        memoryDescription.type = cudaExternalMemoryHandleTypeOpaqueWin32;
        memoryDescription.handle.win32.handle = handle;
        memoryDescription.size = buffer.getAllocationSize();
        const cudaError_t importResult = cudaImportExternalMemory(&memory, &memoryDescription);
        DWORD handleFlags = 0;
        if (GetHandleInformation(handle, &handleFlags)) CloseHandle(handle);
        if (importResult != cudaSuccess) {
            std::cerr << "[CudaExternalBuffer] import failed: " << cudaGetErrorString(importResult) << std::endl;
            return false;
        }

        cudaExternalMemoryBufferDesc bufferDescription{};
        bufferDescription.size = buffer.getBufferSize();
        const cudaError_t mapResult = cudaExternalMemoryGetMappedBuffer(
            reinterpret_cast<void**>(&pointer), memory, &bufferDescription);
        if (mapResult != cudaSuccess) {
            std::cerr << "[CudaExternalBuffer] map failed: " << cudaGetErrorString(mapResult) << std::endl;
            cleanup();
            return false;
        }
        sourceBuffer = buffer.getBuffer();
        return true;
    }

    void cleanup() {
        if (cudaDevice >= 0) cudaSetDevice(cudaDevice);
        if (pointer) cudaFree(pointer);
        if (memory) cudaDestroyExternalMemory(memory);
        pointer = nullptr;
        memory = nullptr;
        sourceBuffer = VK_NULL_HANDLE;
        cudaDevice = -1;
    }

    float* pointer = nullptr;
    cudaExternalMemory_t memory = nullptr;
    VkBuffer sourceBuffer = VK_NULL_HANDLE;
    int cudaDevice = -1;
};

CudaExternalBuffer::CudaExternalBuffer() : implementation(std::make_unique<Implementation>()) {}
CudaExternalBuffer::~CudaExternalBuffer() = default;
bool CudaExternalBuffer::ensureMapped(const VulkanExternalBuffer& buffer, int cudaDevice) { return implementation->ensureMapped(buffer, cudaDevice); }
void CudaExternalBuffer::cleanup() { implementation->cleanup(); }
float* CudaExternalBuffer::getPointer() const { return implementation->pointer; }
