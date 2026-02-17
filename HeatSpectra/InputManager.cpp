#include "InputManager.hpp"
#include "VulkanWindow.h"
#include "Camera.hpp"
#include "Gizmo.hpp"
#include "ModelSelection.hpp"
#include "ResourceManager.hpp"
#include "Model.hpp" 

InputManager::InputManager(Camera& camera, Gizmo& gizmo, ModelSelection& modelSelection, 
                           ResourceManager& resourceManager, VkExtent2D& swapChainExtent,
                           VulkanWindow& window)
    : camera(camera), gizmo(gizmo), modelSelection(modelSelection), 
      resourceManager(resourceManager), swapChainExtent(swapChainExtent) {
          
    // Register callbacks directly with the window
    window.setScrollCallback([this](double x, double y) {
        handleScrollInput(x, y);
    });
    
    window.setKeyCallback([this](Qt::Key key, bool pressed) {
        handleKeyInput(key, pressed);
    });
    
    window.setMouseMoveCallback([this](float x, float y) {
        handleMouseMove(x, y);
    });
    
    window.setMouseReleaseCallback([this](int button, float x, float y) {
        handleMouseRelease(button, x, y);
    });
    
    window.setMouseClickCallback([this](int button, float x, float y, bool shift) {
        handleMouseButton(button, x, y, shift);
    });
}

void InputManager::handleScrollInput(double xOffset, double yOffset) {
    camera.processMouseScroll(xOffset, yOffset);
}

void InputManager::handleKeyInput(Qt::Key key, bool pressed) {
    // Only handle key press, not release
    if (!pressed) 
        return;
    
    if (key == Qt::Key_H) {
        if (onWireframeToggled) onWireframeToggled(); 
    }
    else if (key == Qt::Key_C) {
        if (onIntrinsicOverlayToggled) onIntrinsicOverlayToggled();
    }
    else if (key == Qt::Key_V) {
        if (onHeatOverlayToggled) onHeatOverlayToggled();
    }
    else if (key == Qt::Key_AsciiTilde) {
        if (onTimingOverlayToggled) onTimingOverlayToggled();
    }
    else if (key == Qt::Key_Space) {
        if (onToggleHeatSystem) onToggleHeatSystem();
    }
    else if (key == Qt::Key_P) {
        if (onPauseHeatSystem) onPauseHeatSystem();
    }
    else if (key == Qt::Key_R) {
        if (onResetHeatSystem) onResetHeatSystem();
    }
    else if (key == Qt::Key_F) {
        if (modelSelection.getSelected()) {
            uint32_t selectedID = modelSelection.getSelectedModelID();
            Model* model = resourceManager.getModelByID(selectedID);
            if (model) {
                glm::vec3 localCenter = model->getBoundingBoxCenter();
                glm::vec3 worldCenter = glm::vec3(model->getModelMatrix() * glm::vec4(localCenter, 1.0f));
                camera.setLookAt(worldCenter);
            }
        }
    }
    else if (key == Qt::Key_G) {
        camera.setLookAt(glm::vec3(0.0f));
        camera.resetRadius();
    }
}

void InputManager::handleMouseMove(float mouseX, float mouseY) {
    // Handle gizmo dragging 
    if (isDraggingGizmo) {
        // Cast ray from current mouse position
        glm::vec3 rayOrigin = camera.getPosition();
        glm::vec3 rayDir = camera.screenToWorldRay(mouseX, mouseY, swapChainExtent.width, swapChainExtent.height);
        
        // Auto detect based on active mode
        if (gizmo.getMode() == GizmoMode::Translate) {
            glm::vec3 newTranslation = gizmo.calculateTranslationDelta(rayOrigin, rayDir, cachedGizmoPosition, gizmo.getActiveAxis());
            accumulatedTranslation = newTranslation;
        } 
        else if (gizmo.getMode() == GizmoMode::Rotate) {
            float angle = gizmo.calculateRotationDelta(rayOrigin, rayDir, cachedGizmoPosition, gizmo.getActiveAxis());
            accumulatedRotation = angle;
        }
    }
}

void InputManager::handleMouseRelease(int button, float mouseX, float mouseY) {
    if (button != static_cast<int>(Qt::LeftButton)) return;
    
    if (isDraggingGizmo) {        
        isDraggingGizmo = false;
        gizmo.endDrag();
        accumulatedTranslation = glm::vec3(0.0f);
        lastAppliedTranslation = glm::vec3(0.0f);
        accumulatedRotation = 0.0f;
        lastAppliedRotation = 0.0f;
    }
}

void InputManager::handleMouseButton(int button, float mouseX, float mouseY, bool shiftPressed) {
    if (button != static_cast<int>(Qt::LeftButton)) 
        return;
    
    int x = static_cast<int>(mouseX);
    int y = static_cast<int>(mouseY);
    
    // Clamp to screen bounds 
    x = (std::max)(0, (std::min)(x, static_cast<int>(swapChainExtent.width) - 1));
    y = (std::max)(0, (std::min)(y, static_cast<int>(swapChainExtent.height) - 1));
    
    // Queue pick request for both model and gizmo selection (processed on render thread)
    modelSelection.queuePickRequest(x, y, shiftPressed, mouseX, mouseY);
}

void InputManager::processInput(bool shiftPressed, bool middleButtonPressed, 
                                double mouseX, double mouseY, float deltaTime) {
    
    camera.processMouseMovement(middleButtonPressed, mouseX, mouseY, shiftPressed);
}

void InputManager::updateGizmo() {
    // Check if gizmo was picked and initiate drag
    if (!isDraggingGizmo) {
        PickedResult lastPick = modelSelection.getLastPickedResult();
        if (lastPick.isGizmo() && modelSelection.getSelected()) {
            // Convert PickedGizmoAxis to GizmoAxis
            GizmoAxis hitAxis = GizmoAxis::None;
            if (lastPick.gizmoAxis == PickedGizmoAxis::X) hitAxis = GizmoAxis::X;
            else if (lastPick.gizmoAxis == PickedGizmoAxis::Y) hitAxis = GizmoAxis::Y;
            else if (lastPick.gizmoAxis == PickedGizmoAxis::Z) hitAxis = GizmoAxis::Z;
            
            if (hitAxis != GizmoAxis::None) {
                // Auto detect mode based on stencil value: 3-5 = translate, 6-8 = rotate
                if (lastPick.stencilValue >= 3 && lastPick.stencilValue <= 5) {
                    gizmo.setMode(GizmoMode::Translate);
                } else if (lastPick.stencilValue >= 6 && lastPick.stencilValue <= 8) {
                    gizmo.setMode(GizmoMode::Rotate);
                }
                
                // Start drag using original click position
                PickingRequest pickReq = modelSelection.getLastPickRequest();
                glm::vec3 gizmoPosition = gizmo.calculateGizmoPosition(resourceManager, modelSelection);
                glm::vec3 rayOrigin = camera.getPosition();
                glm::vec3 rayDir = camera.screenToWorldRay(pickReq.mouseX, pickReq.mouseY, swapChainExtent.width, swapChainExtent.height);
                
                isDraggingGizmo = true;
                cachedGizmoPosition = gizmoPosition;
                gizmo.startDrag(hitAxis, rayOrigin, rayDir, cachedGizmoPosition);
                modelStartPosition = cachedGizmoPosition;
                accumulatedTranslation = glm::vec3(0.0f);
                accumulatedRotation = 0.0f;
                
                modelSelection.clearLastPickedResult();
            }
        }
    }
    
    if (!isDraggingGizmo)
        return;

    const auto& selectedIDs = modelSelection.getSelectedModelIDsRenderThread();
    
    if (gizmo.getMode() == GizmoMode::Translate) {
        glm::vec3 currentTranslation = accumulatedTranslation; 
        glm::vec3 deltaTranslation = currentTranslation - lastAppliedTranslation;
        
        if (glm::length(deltaTranslation) < 1e-6) 
            return;
        
        // Apply incremental translation to all selected models
        for (uint32_t id : selectedIDs) {
            if (id == 1) {
                resourceManager.getVisModel().translate(deltaTranslation);
            } else if (id == 2) {
                resourceManager.getHeatModel().translate(deltaTranslation);
            }
        }
        
        lastAppliedTranslation = currentTranslation;
    }
    else if (gizmo.getMode() == GizmoMode::Rotate) {
        float currentRotation = accumulatedRotation;
        float deltaRotation = currentRotation - lastAppliedRotation;
        
        if (fabs(deltaRotation) < 0.01f) 
            return;
        
        // Get rotation axis
        glm::vec3 rotationAxis;
        GizmoAxis activeAxis = gizmo.getActiveAxis();
        if (activeAxis == GizmoAxis::X) rotationAxis = glm::vec3(1, 0, 0);
        else if (activeAxis == GizmoAxis::Y) rotationAxis = glm::vec3(0, 1, 0);
        else if (activeAxis == GizmoAxis::Z) rotationAxis = glm::vec3(0, 0, 1);
        else return;
        
        // Apply incremental rotation to all selected models around gizmo center
        for (uint32_t id : selectedIDs) {
            if (id == 1) {
                resourceManager.getVisModel().rotate(glm::radians(deltaRotation), rotationAxis, cachedGizmoPosition);
            } else if (id == 2) {
                resourceManager.getHeatModel().rotate(glm::radians(deltaRotation), rotationAxis, cachedGizmoPosition);
            }
        }
        
        lastAppliedRotation = currentRotation;
    }
}
