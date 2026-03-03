#pragma once

#include <QSplitter>

#include <string>

namespace ui {

inline constexpr int SplitterVisualThickness = 7;
inline constexpr const char* SplitterBaseColor = "#8e8e8e";
inline constexpr const char* SplitterHoverColor = "#b6b6b6";
inline constexpr const char* SplitterBorderColor = "#5a5a5a";

inline std::string splitterStyleSheet() {
    const std::string thickness = std::to_string(SplitterVisualThickness);
    return
        "QSplitter::handle:horizontal { "
        "width: " + thickness + "px; "
        "background: " + std::string(SplitterBaseColor) + "; "
        "border-left: 1px solid " + std::string(SplitterBorderColor) + "; "
        "border-right: 1px solid " + std::string(SplitterBorderColor) + "; "
        "}"
        "QSplitter::handle:horizontal:hover { "
        "background: " + std::string(SplitterHoverColor) + "; "
        "}"
        "QSplitter::handle:vertical { "
        "height: " + thickness + "px; "
        "background: " + std::string(SplitterBaseColor) + "; "
        "border-top: 1px solid " + std::string(SplitterBorderColor) + "; "
        "border-bottom: 1px solid " + std::string(SplitterBorderColor) + "; "
        "}"
        "QSplitter::handle:vertical:hover { "
        "background: " + std::string(SplitterHoverColor) + "; "
        "}"
        "QMainWindow::separator { "
        "background: " + std::string(SplitterBaseColor) + "; "
        "}"
        "QMainWindow::separator:hover { "
        "background: " + std::string(SplitterHoverColor) + "; "
        "}"
        "QMainWindow::separator:horizontal { "
        "background: " + std::string(SplitterBaseColor) + "; "
        "width: " + thickness + "px; "
        "border-left: 1px solid " + std::string(SplitterBorderColor) + "; "
        "border-right: 1px solid " + std::string(SplitterBorderColor) + "; "
        "}"
        "QMainWindow::separator:horizontal:hover { "
        "background: " + std::string(SplitterHoverColor) + "; "
        "}"
        "QMainWindow::separator:vertical { "
        "background: " + std::string(SplitterBaseColor) + "; "
        "height: " + thickness + "px; "
        "border-top: 1px solid " + std::string(SplitterBorderColor) + "; "
        "border-bottom: 1px solid " + std::string(SplitterBorderColor) + "; "
        "}"
        "QMainWindow::separator:vertical:hover { "
        "background: " + std::string(SplitterHoverColor) + "; "
        "}"
        "QDockWidget { "
        "border: 0px; "
        "}"
        "QDockWidget > QWidget { "
        "border: 0px; "
        "}"
        "";
}

inline void configureSplitter(QSplitter& splitter) {
    splitter.setHandleWidth(SplitterVisualThickness);
    splitter.setOpaqueResize(true);
}

}