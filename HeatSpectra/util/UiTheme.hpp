#pragma once

#include <QColor>
#include <QSplitter>

#include <string>

namespace ui {

inline constexpr int SplitterVisualThickness = 7;
inline constexpr QColor SplitterBaseColor = QColor(100, 100, 110);
inline constexpr QColor SplitterHoverColor = QColor(180, 180, 180);
inline constexpr QColor SplitterBorderColor = QColor(70, 70, 80);
inline constexpr int ScrollBarThickness = 12;
inline constexpr int ScrollBarMargin = 2;
inline constexpr int ScrollBarRadius = 4;
inline constexpr int ScrollBarMinHandleLength = 28;
inline constexpr QColor ScrollBarTrackColor = QColor(70, 70, 80);
inline constexpr QColor ScrollBarHandleColor = QColor(100, 100, 110);
inline constexpr QColor ScrollBarHandleHoverColor = QColor(180, 180, 180);

inline std::string appStyleSheet() {
    const std::string splitterThickness = std::to_string(SplitterVisualThickness);
    const std::string scrollBarThickness = std::to_string(ScrollBarThickness);
    const std::string scrollBarMargin = std::to_string(ScrollBarMargin);
    const std::string scrollBarRadius = std::to_string(ScrollBarRadius);
    const std::string scrollBarMinHandleLength = std::to_string(ScrollBarMinHandleLength);
    return
        "QSplitter::handle:horizontal { "
        "width: " + splitterThickness + "px; "
        "background: " + SplitterBaseColor.name().toStdString() + "; "
        "border-left: 1px solid " + SplitterBorderColor.name().toStdString() + "; "
        "border-right: 1px solid " + SplitterBorderColor.name().toStdString() + "; "
        "}"
        "QSplitter::handle:horizontal:hover { "
        "background: " + SplitterHoverColor.name().toStdString() + "; "
        "}"
        "QSplitter::handle:vertical { "
        "height: " + splitterThickness + "px; "
        "background: " + SplitterBaseColor.name().toStdString() + "; "
        "border-top: 1px solid " + SplitterBorderColor.name().toStdString() + "; "
        "border-bottom: 1px solid " + SplitterBorderColor.name().toStdString() + "; "
        "}"
        "QSplitter::handle:vertical:hover { "
        "background: " + SplitterHoverColor.name().toStdString() + "; "
        "}"
        "QMainWindow::separator { "
        "background: " + SplitterBaseColor.name().toStdString() + "; "
        "}"
        "QMainWindow::separator:hover { "
        "background: " + SplitterHoverColor.name().toStdString() + "; "
        "}"
        "QMainWindow::separator:horizontal { "
        "background: " + SplitterBaseColor.name().toStdString() + "; "
        "width: " + splitterThickness + "px; "
        "border-left: 1px solid " + SplitterBorderColor.name().toStdString() + "; "
        "border-right: 1px solid " + SplitterBorderColor.name().toStdString() + "; "
        "}"
        "QMainWindow::separator:horizontal:hover { "
        "background: " + SplitterHoverColor.name().toStdString() + "; "
        "}"
        "QMainWindow::separator:vertical { "
        "background: " + SplitterBaseColor.name().toStdString() + "; "
        "height: " + splitterThickness + "px; "
        "border-top: 1px solid " + SplitterBorderColor.name().toStdString() + "; "
        "border-bottom: 1px solid " + SplitterBorderColor.name().toStdString() + "; "
        "}"
        "QMainWindow::separator:vertical:hover { "
        "background: " + SplitterHoverColor.name().toStdString() + "; "
        "}"
        "QDockWidget { "
        "border: 0px; "
        "}"
        "QDockWidget > QWidget { "
        "border: 0px; "
        "}"
        "QScrollBar:vertical { "
        "background: " + ScrollBarTrackColor.name().toStdString() + "; "
        "width: " + scrollBarThickness + "px; "
        "margin: " + scrollBarMargin + "px 0px " + scrollBarMargin + "px 0px; "
        "border: none; "
        "}"
        "QScrollBar::handle:vertical { "
        "background: " + ScrollBarHandleColor.name().toStdString() + "; "
        "min-height: " + scrollBarMinHandleLength + "px; "
        "border-radius: " + scrollBarRadius + "px; "
        "}"
        "QScrollBar::handle:vertical:hover { "
        "background: " + ScrollBarHandleHoverColor.name().toStdString() + "; "
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
        "height: 0px; "
        "background: transparent; "
        "border: none; "
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { "
        "background: transparent; "
        "}"
        "QScrollBar:horizontal { "
        "background: " + ScrollBarTrackColor.name().toStdString() + "; "
        "height: " + scrollBarThickness + "px; "
        "margin: 0px " + scrollBarMargin + "px 0px " + scrollBarMargin + "px; "
        "border: none; "
        "}"
        "QScrollBar::handle:horizontal { "
        "background: " + ScrollBarHandleColor.name().toStdString() + "; "
        "min-width: " + scrollBarMinHandleLength + "px; "
        "border-radius: " + scrollBarRadius + "px; "
        "}"
        "QScrollBar::handle:horizontal:hover { "
        "background: " + ScrollBarHandleHoverColor.name().toStdString() + "; "
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { "
        "width: 0px; "
        "background: transparent; "
        "border: none; "
        "}"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { "
        "background: transparent; "
        "}";
}

inline void configureSplitter(QSplitter& splitter) {
    splitter.setHandleWidth(SplitterVisualThickness);
    splitter.setOpaqueResize(true);
}

}