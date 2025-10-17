# Model Selection - Usage Guide

## ✅ Better Design - Selection State in ModelSelection

The selection state now lives in `ModelSelection` (where it belongs), not in `GBuffer`. 
- **ModelSelection** = manages selection logic & state
- **GBuffer** = just renders based on selection state

---

## 📦 What You Have Available

### **Option 1: Bounding Sphere (Simpler, Faster)**
Use your existing `Model::calculateBoundingBox()` to get min/max, then test against a sphere:

```cpp
#include "ModelSelection.hpp"

// In App.hpp - add member
ModelSelection modelSelection;

// In App.cpp - handle mouse click
void App::handleMouseClick(float mouseX, float mouseY) {
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix();
    
    // Cast ray from mouse
    Ray ray = ModelSelection::screenToWorldRay(mouseX, mouseY, WIDTH, HEIGHT, view, proj);
    
    // Get model bounds (you already have this!)
    glm::vec3 minBound, maxBound;
    resourceManager->getVisModel().calculateBoundingBox(
        resourceManager->getVisModel().getVertices(), 
        minBound, 
        maxBound
    );
    
    // Test against bounding sphere (fast & simple)
    bool hit = ModelSelection::rayIntersectsBoundingSphere(ray, minBound, maxBound);
    
    if (hit) {
        modelSelection.toggleSelection();
        std::cout << "[Selection] Model " << (modelSelection.getSelected() ? "selected" : "deselected") << std::endl;
    } else {
        modelSelection.setSelected(false);  // Deselect on empty click
    }
}
```

### **Option 2: AABB (More Accurate)**
If you want pixel-perfect selection:

```cpp
void App::handleMouseClick(float mouseX, float mouseY) {
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix();
    
    Ray ray = ModelSelection::screenToWorldRay(mouseX, mouseY, WIDTH, HEIGHT, view, proj);
    
    glm::vec3 minBound, maxBound;
    resourceManager->getVisModel().calculateBoundingBox(
        resourceManager->getVisModel().getVertices(), 
        minBound, 
        maxBound
    );
    
    // Use AABB test (more accurate than sphere)
    bool hit = ModelSelection::rayIntersectsAABB(ray, minBound, maxBound);
    
    if (hit) {
        modelSelection.toggleSelection();
    } else {
        modelSelection.setSelected(false);
    }
}
```

### **Option 3: Use Your Existing Ray Intersection**
You already have `Model::rayIntersect()` - you can use that instead!

```cpp
void App::handleMouseClick(float mouseX, float mouseY) {
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix();
    
    Ray ray = ModelSelection::screenToWorldRay(mouseX, mouseY, WIDTH, HEIGHT, view, proj);
    
    // Use your existing Model::rayIntersect
    HitResult result = resourceManager->getVisModel().rayIntersect(ray.origin, ray.direction);
    
    if (result.hit) {
        modelSelection.toggleSelection();
    } else {
        modelSelection.setSelected(false);
    }
}
```

---

## 🎨 Updating GBuffer Record Call

Update your App.cpp where you call `gbuffer->recordCommandBuffer()`:

```cpp
// OLD:
gbuffer->recordCommandBuffer(resourceManager, heatSystem, swapChainImageViews, 
                             imageIndex, MAXFRAMESINFLIGHT, extent, wireframe, subdivision);

// NEW (add modelSelection parameter):
gbuffer->recordCommandBuffer(resourceManager, heatSystem, modelSelection, swapChainImageViews, 
                             imageIndex, MAXFRAMESINFLIGHT, extent, wireframe, subdivision);
```

---

## 🎯 Customizing Outline Appearance

```cpp
// Change outline color
modelSelection.setOutlineColor(glm::vec3(0.0f, 1.0f, 1.0f));  // Cyan

// Adjust thickness
modelSelection.setOutlineThickness(0.05f);  // Thicker outline

// Or just use defaults:
// Orange outline: glm::vec3(1.0f, 0.6f, 0.0f)
// Thickness: 0.02f
```

---

## 🔌 Wire Up Qt Mouse Events

In `VulkanWindow.cpp` or wherever you handle events:

```cpp
void VulkanWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && app) {
        float mouseX = event->pos().x();
        float mouseY = event->pos().y();
        app->handleMouseClick(mouseX, mouseY);
    }
}
```

---

## 🎊 Summary

**YOU DON'T NEED AABB!** You have 3 options:
1. ✅ **Bounding Sphere** - Fast & simple (recommended)
2. ✅ **AABB** - Available if you want it
3. ✅ **Your existing Model::rayIntersect()** - Most accurate!

Choose whichever fits your needs. The selection system works with all three!
