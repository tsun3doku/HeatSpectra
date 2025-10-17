# Selection System Integration Guide

## ✅ Files Created
1. ✅ `shaders/outline.vert` - Outline vertex shader (scales along normals)
2. ✅ `shaders/outline.frag` - Outline fragment shader (solid color)
3. ✅ `ModelSelection.hpp` - Ray casting utilities (renamed from MousePicker)
4. ✅ `ModelSelection.cpp` - Ray casting implementation

## ✅ Files Modified
1. ✅ `Structs.hpp` - Added `OutlinePushConstant` struct
2. ✅ `GBuffer.hpp` - Added outline pipeline, selection state, and API methods
3. ✅ `GBuffer.cpp` - Implemented `createOutlinePipeline()`, added draw call, added cleanup

## 🔨 Remaining Steps

### 1. ⚠️ Compile Shaders (REQUIRED)
```bash
# Run in your project directory:
glslc shaders/outline.vert -o shaders/outline_vert.spv
glslc shaders/outline.frag -o shaders/outline_frag.spv
```

### 2. ✅ DONE - Implemented `createOutlinePipeline()` in GBuffer.cpp

Add this method (similar to wireframe pipeline but with different shaders):

```cpp
void GBuffer::createOutlinePipeline(VkExtent2D extent) {
    // Load outline shaders
    auto vertShaderCode = readFile("shaders/outline.vert.spv");
    auto fragShaderCode = readFile("shaders/outline.frag.spv");
    
    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice.getDevice(), vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice.getDevice(), fragShaderCode);
    
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    
    // Vertex input (same as geometry pass)
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;  // Cull front faces (show only back)
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  // Don't write to depth buffer
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Push constant range for outline color and thickness
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) + sizeof(glm::vec3);  // thickness + color
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &geometryDescriptorSetLayout;  // Reuse geometry layout
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &outlinePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create outline pipeline layout!");
    }
    
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = outlinePipelineLayout;
    pipelineInfo.renderPass = deferredRenderer.getDeferredRenderPass();
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outlinePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create outline pipeline!");
    }
    
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
}
```

### 3. Call `createOutlinePipeline()` in GBuffer constructor
Add after other pipeline creations:
```cpp
createOutlinePipeline(swapchainExtent);
```

### 4. Add outline rendering in `recordCommandBuffer()`

After rendering the main model, add:
```cpp
// Render outline if model is selected
if (modelSelected) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, outlinePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                            outlinePipelineLayout, 0, 1, &geometryDescriptorSets[currentFrame], 0, nullptr);
    
    // Push constants for outline
    struct OutlinePushConstants {
        float thickness;
        glm::vec3 color;
    } outlinePC;
    outlinePC.thickness = outlineThickness;
    outlinePC.color = outlineColor;
    
    vkCmdPushConstants(commandBuffer, outlinePipelineLayout, 
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(OutlinePushConstants), &outlinePC);
    
    // Draw model with outline
    VkBuffer vertexBuffers[] = {resourceManager.getVisModel().getVertexBuffer()};
    VkDeviceSize offsets[] = {resourceManager.getVisModel().getVertexBufferOffset()};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, resourceManager.getVisModel().getIndexBuffer(), 
                         resourceManager.getVisModel().getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, resourceManager.getVisModel().getIndexCount(), 1, 0, 0, 0);
}
```

### 5. Cleanup in `cleanup()` method
Add:
```cpp
if (outlinePipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(vulkanDevice.getDevice(), outlinePipeline, nullptr);
    outlinePipeline = VK_NULL_HANDLE;
}
if (outlinePipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), outlinePipelineLayout, nullptr);
    outlinePipelineLayout = VK_NULL_HANDLE;
}
```

### 6. Add Mouse Click Handler in App.cpp

```cpp
void App::handleMouseClick(int button, float mouseX, float mouseY) {
    if (button == Qt::LeftButton) {
        // Get view and projection matrices from camera/UBO
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 proj = camera.getProjectionMatrix();
        
        // Cast ray from mouse position
        Ray ray = MousePicker::screenToWorldRay(mouseX, mouseY, WIDTH, HEIGHT, view, proj);
        
        // Test against model bounding box
        glm::vec3 modelMin = resourceManager->getVisModel().getBoundingBoxMin();
        glm::vec3 modelMax = resourceManager->getVisModel().getBoundingBoxMax();
        
        bool hit = MousePicker::rayIntersectsAABB(ray, modelMin, modelMax);
        
        if (hit) {
            // Toggle selection
            bool currentlySelected = gbuffer->isModelSelected();
            gbuffer->setModelSelected(!currentlySelected);
            std::cout << "[App] Model " << (currentlySelected ? "deselected" : "selected") << std::endl;
        } else {
            // Clicked empty space, deselect
            gbuffer->setModelSelected(false);
        }
    }
}
```

### 7. Connect Qt Mouse Events in VulkanWindow or MainQt

Add mouse press event handler that calls `App::handleMouseClick()`.

## How It Works

1. **Outline Rendering**: The outline shader scales vertices along their normals, creating a slightly larger silhouette
2. **Culling**: Front faces are culled, so only the "back" (outline) is visible around the edges
3. **Depth Test**: Uses LESS_OR_EQUAL so outline renders slightly behind the model
4. **Mouse Picking**: Converts screen coordinates to a 3D ray and tests intersection with model bounding box
5. **Selection State**: GBuffer tracks if model is selected and renders outline accordingly

## Customization

- **Outline Color**: Modify `outlineColor` (default: orange)
- **Outline Thickness**: Modify `outlineThickness` (default: 0.02)
- **Hit Detection**: Use `rayIntersectsSphere()` for faster but less accurate picking

## Next Steps

1. Compile shaders
2. Implement the createOutlinePipeline method
3. Add outline rendering call
4. Wire up mouse click events
5. Test and adjust thickness/color as needed
