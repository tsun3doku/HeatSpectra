# Model Selection System - Implementation Summary

## ✅ Completed Work

### **1. Shaders Created**
- `shaders/outline.vert` - Vertex shader that expands geometry along normals
- `shaders/outline.frag` - Fragment shader for outline color

### **2. Selection Utilities**
- `ModelSelection.hpp/.cpp` - Ray casting for mouse picking (AABB and sphere tests)

### **3. Data Structures**
- Added `OutlinePushConstant` to `Structs.hpp` with:
  - `float outlineThickness`
  - `glm::vec3 outlineColor`

### **4. GBuffer Integration**
✅ **GBuffer.hpp:**
- Added outline pipeline members
- Added selection state (`modelSelected`, `outlineColor`, `outlineThickness`)
- Added API: `setModelSelected()`, `isModelSelected()`

✅ **GBuffer.cpp:**
- Implemented `createOutlinePipeline()` - full Vulkan pipeline creation
- Added outline draw call in `recordCommandBuffer()` (after wireframe, before heat source)
- Added cleanup for outline pipeline
- Called `createOutlinePipeline()` in constructor

---

## 📋 What You Need to Do

### **Step 1: Compile Shaders** ⚠️ REQUIRED
```bash
glslc shaders/outline.vert -o shaders/outline_vert.spv
glslc shaders/outline.frag -o shaders/outline_frag.spv
```

### **Step 2: Add Mouse Click Handler**
You need to wire up mouse events to toggle selection. Here's example code for `App.cpp`:

```cpp
#include "ModelSelection.hpp"

void App::handleMouseClick(float mouseX, float mouseY) {
    // Get camera matrices
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix();
    
    // Cast ray from mouse position
    Ray ray = ModelSelection::screenToWorldRay(mouseX, mouseY, WIDTH, HEIGHT, view, proj);
    
    // Test against model bounding box
    glm::vec3 modelMin = resourceManager->getVisModel().getBoundingBoxMin();
    glm::vec3 modelMax = resourceManager->getVisModel().getBoundingBoxMax();
    
    bool hit = ModelSelection::rayIntersectsAABB(ray, modelMin, modelMax);
    
    if (hit) {
        // Toggle selection
        bool currentlySelected = gbuffer->isModelSelected();
        gbuffer->setModelSelected(!currentlySelected);
        std::cout << "[App] Model " << (currentlySelected ? "deselected" : "selected") << std::endl;
    } else {
        // Clicked empty space
        gbuffer->setModelSelected(false);
    }
}
```

### **Step 3: Connect Qt Mouse Events**
In your `VulkanWindow` or `MainQt.cpp`, connect mouse press events:

```cpp
void VulkanWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && app) {
        float mouseX = event->pos().x();
        float mouseY = event->pos().y();
        app->handleMouseClick(mouseX, mouseY);
    }
}
```

### **Step 4: (Optional) Adjust Outline Settings**
You can change outline appearance in `GBuffer.hpp`:
```cpp
glm::vec3 outlineColor = glm::vec3(1.0f, 0.6f, 0.0f);  // Orange
float outlineThickness = 0.02f;                         // Thickness
```

Or add setter methods to change them at runtime.

---

## 🎨 How It Works

1. **Click Detection**: Mouse click → 3D ray → AABB intersection test
2. **Selection State**: `GBuffer::modelSelected` tracks if model is selected
3. **Outline Rendering**:
   - Vertices expanded along normals by `outlineThickness`
   - Front faces culled → only "back" (outline) visible
   - Blended on top of geometry with `outlineColor`
4. **Visual Result**: Orange glow/border around the selected model

---

## 📁 Files Modified/Created

**Created:**
- `ModelSelection.hpp`
- `ModelSelection.cpp`
- `shaders/outline.vert`
- `shaders/outline.frag`

**Modified:**
- `Structs.hpp` - Added `OutlinePushConstant`
- `GBuffer.hpp` - Added pipeline, state, API
- `GBuffer.cpp` - Pipeline creation, draw call, cleanup

**Still Need:**
- Add mouse event handling in your Qt window/app code
- Compile shaders
- Test!

---

## 🔧 Testing

1. Compile shaders
2. Build and run
3. Click on the model → Should see orange outline
4. Click again → Outline disappears
5. Click empty space → Deselects

**Adjust outline thickness/color as needed for your scene!**
