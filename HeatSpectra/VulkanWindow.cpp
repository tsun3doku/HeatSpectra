#include "VulkanWindow.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>

#ifdef _WIN32
#include <Windows.h>
#endif

VulkanWindow::VulkanWindow(QWindow* parent) : QWindow(parent) {
    setSurfaceType(QSurface::VulkanSurface);
    setTitle("HeatSpectra");
    resize(960, 540);
    
    // Enable mouse tracking
    setMouseGrabEnabled(false);
}

VulkanWindow::~VulkanWindow() = default;

void* VulkanWindow::getNativeWindowHandle() {
#ifdef _WIN32
    return reinterpret_cast<void*>(winId());
#else
    return reinterpret_cast<void*>(winId());
#endif
}

HINSTANCE VulkanWindow::getNativeInstance() {
#ifdef _WIN32
    return GetModuleHandle(nullptr);
#else
    return nullptr;
#endif
}

bool VulkanWindow::isKeyPressed(Qt::Key key) const {
    return pressedKeys.contains(key);
}

void VulkanWindow::getMousePosition(double& x, double& y) const {
    x = mousePos.x();
    y = mousePos.y();
}

bool VulkanWindow::isMiddleButtonPressed() const {
    return middleButtonPressed;
}

void VulkanWindow::setScrollCallback(void (*callback)(void*, double, double), void* userPtr) {
    scrollCb = callback;
    scrollUserPtr = userPtr;
}

void VulkanWindow::setKeyCallback(void (*callback)(void*, Qt::Key, bool), void* userPtr) {
    keyCb = callback;
    keyUserPtr = userPtr;
}

void VulkanWindow::setMouseClickCallback(void (*callback)(void*, int, float, float, bool), void* userPtr) {
    mouseClickCb = callback;
    mouseClickUserPtr = userPtr;
}

void VulkanWindow::setMouseMoveCallback(void (*callback)(void*, float, float), void* userPtr) {
    mouseMoveCb = callback;
    mouseMoveUserPtr = userPtr;
}

void VulkanWindow::setMouseReleaseCallback(void (*callback)(void*, int, float, float), void* userPtr) {
    mouseReleaseCb = callback;
    mouseReleaseUserPtr = userPtr;
}

void VulkanWindow::resizeEvent(QResizeEvent* event) {
    QWindow::resizeEvent(event);
}

void VulkanWindow::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        return;
    }
    
    Qt::Key key = static_cast<Qt::Key>(event->key());
    pressedKeys.insert(key);
    
    if (keyCb && keyUserPtr) {
        keyCb(keyUserPtr, key, true);
    }
    
    QWindow::keyPressEvent(event);
}

void VulkanWindow::keyReleaseEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        return;
    }
    
    Qt::Key key = static_cast<Qt::Key>(event->key());
    pressedKeys.remove(key);
    
    if (keyCb && keyUserPtr) {
        keyCb(keyUserPtr, key, false);
    }
    
    QWindow::keyReleaseEvent(event);
}

void VulkanWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        middleButtonPressed = true;
    }
    
    // Trigger mouse click callback for left clicks
    if (event->button() == Qt::LeftButton && mouseClickCb && mouseClickUserPtr) {
        float mouseX = event->position().x();
        float mouseY = event->position().y();
        bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
        mouseClickCb(mouseClickUserPtr, static_cast<int>(event->button()), mouseX, mouseY, shiftPressed);
    }
    
    QWindow::mousePressEvent(event);
}

void VulkanWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        middleButtonPressed = false;
    }
    
    // Trigger mouse release callback
    if (mouseReleaseCb && mouseReleaseUserPtr) {
        float mouseX = event->position().x();
        float mouseY = event->position().y();
        mouseReleaseCb(mouseReleaseUserPtr, static_cast<int>(event->button()), mouseX, mouseY);
    }
    
    QWindow::mouseReleaseEvent(event);
}

void VulkanWindow::mouseMoveEvent(QMouseEvent* event) {
    mousePos = event->position();
    
    // Trigger mouse move callback
    if (mouseMoveCb && mouseMoveUserPtr) {
        float mouseX = event->position().x();
        float mouseY = event->position().y();
        mouseMoveCb(mouseMoveUserPtr, mouseX, mouseY);
    }
    
    QWindow::mouseMoveEvent(event);
}

void VulkanWindow::wheelEvent(QWheelEvent* event) {
    if (scrollCb && scrollUserPtr) {
        double xOffset = event->angleDelta().x() / 120.0;
        double yOffset = event->angleDelta().y() / 120.0;
        scrollCb(scrollUserPtr, xOffset, yOffset);
    }
    
    QWindow::wheelEvent(event);
}
