#include "NodeGraphWidgetStyle.hpp"

#include <QFrame>
#include <QLineEdit>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace nodegraphwidgets {

QString px(int value) {
    return QString::number(value) + "px";
}

void styleLineEdit(QLineEdit* edit) {
    if (!edit) {
        return;
    }

    edit->setFrame(false);

    QString stylesheet;
    stylesheet += "QLineEdit {";
    stylesheet += "  background: " + colorInputBackground.name() + ";";
    stylesheet += "  color: " + colorTextInput.name() + ";";
    stylesheet += "  border: 1px solid " + colorInputBorder.name() + ";";
    stylesheet += "  border-radius: " + px(panelWidgetRadius) + ";";
    stylesheet += "  padding: "
        + px(panelFieldVerticalPadding) + " "
        + px(panelFieldHorizontalPadding) + ";";
    stylesheet += "  selection-background-color: " + colorAccent.name() + ";";
    stylesheet += "}";
    stylesheet += "QLineEdit:focus {";
    stylesheet += "  border: 1px solid " + colorAccentFocus.name() + ";";
    stylesheet += "}";
    edit->setStyleSheet(stylesheet);
}

QWidget* buildPanelCardPage(QWidget* parent, QWidget* contentWidget) {
    QWidget* page = new QWidget(parent);
    QVBoxLayout* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    QFrame* card = new QFrame(page);
    card->setObjectName("NodePanelCard");
    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(
        panelMarginLeft,
        panelMarginTop,
        panelMarginRight,
        panelMarginBottom);
    cardLayout->setSpacing(panelCardInnerSpacing);
    cardLayout->addWidget(contentWidget);

    pageLayout->addWidget(card);
    return page;
}

void applyNodePanelStyle(QWidget* panel) {
    panel->setObjectName("NodePanelRoot");

    QString stylesheet;
    stylesheet += "QWidget#NodePanelRoot {";
    stylesheet += "  background: " + colorPanelBackground.name() + ";";
    stylesheet += "  color: " + colorTextPrimary.name() + ";";
    stylesheet += "}";
    stylesheet += "QScrollArea {";
    stylesheet += "  border: none;";
    stylesheet += "  background: transparent;";
    stylesheet += "}";
    stylesheet += "QWidget#NodePanelContent {";
    stylesheet += "  background: " + colorPanelBackground.name() + ";";
    stylesheet += "}";
    stylesheet += "QFrame#NodePanelCard {";
    stylesheet += "  background: " + colorPanelCardBackground.name() + ";";
    stylesheet += "  border: 1px solid " + colorPanelCardBorder.name() + ";";
    stylesheet += "  border-radius: " + px(panelCardRadius) + ";";
    stylesheet += "}";
    stylesheet += "QLabel#NodePanelTitle {";
    stylesheet += "  color: " + colorTextHeading.name() + ";";
    stylesheet += "  font-size: " + px(panelTitleFontSize) + ";";
    stylesheet += "  font-weight: " + QString::number(panelTitleFontWeight) + ";";
    stylesheet += "}";
    stylesheet += "QLabel#NodePanelSubtitle {";
    stylesheet += "  color: " + colorTextSecondary.name() + ";";
    stylesheet += "  font-size: " + px(panelSubtitleFontSize) + ";";
    stylesheet += "}";
    stylesheet += "QLabel#NodePanelStatus {";
    stylesheet += "  color: " + colorStatusAccent.name() + ";";
    stylesheet += "  padding-top: " + px(panelStatusTopPadding) + ";";
    stylesheet += "}";
    stylesheet += "QTabWidget::pane {";
    stylesheet += "  border: none;";
    stylesheet += "  background: transparent;";
    stylesheet += "  top: -1px;";
    stylesheet += "}";
    stylesheet += "QTabBar::tab {";
    stylesheet += "  background: transparent;";
    stylesheet += "  color: " + colorTextMuted.name() + ";";
    stylesheet += "  border: none;";
    stylesheet += "  padding: "
        + px(panelTabTopPadding) + " "
        + px(panelTabHorizontalPadding) + " "
        + px(panelTabBottomPadding) + " "
        + px(panelTabHorizontalPadding) + ";";
    stylesheet += "  margin-right: " + px(panelTabRightMargin) + ";";
    stylesheet += "  font-size: " + px(panelTabFontSize) + ";";
    stylesheet += "}";
    stylesheet += "QTabBar::tab:selected {";
    stylesheet += "  color: " + colorTextTabSelected.name() + ";";
    stylesheet += "  border-bottom: "
        + px(panelTabSelectedBorderWidth) + " solid "
        + colorAccent.name() + ";";
    stylesheet += "}";
    stylesheet += "QTabBar::tab:hover:!selected {";
    stylesheet += "  color: " + colorTextHover.name() + ";";
    stylesheet += "}";
    stylesheet += "QLabel {";
    stylesheet += "  color: " + colorTextPrimary.name() + ";";
    stylesheet += "}";
    stylesheet += "QCheckBox {";
    stylesheet += "  color: " + colorTextPrimary.name() + ";";
    stylesheet += "  spacing: " + px(panelCheckboxSpacing) + ";";
    stylesheet += "}";
    stylesheet += "QCheckBox::indicator {";
    stylesheet += "  width: " + px(panelCheckboxIndicatorSize) + ";";
    stylesheet += "  height: " + px(panelCheckboxIndicatorSize) + ";";
    stylesheet += "  border-radius: " + px(panelCheckboxIndicatorRadius) + ";";
    stylesheet += "  border: 1px solid " + colorCheckboxBorder.name() + ";";
    stylesheet += "  background: " + colorCheckboxBackground.name() + ";";
    stylesheet += "}";
    stylesheet += "QCheckBox::indicator:checked {";
    stylesheet += "  background: " + colorAccent.name() + ";";
    stylesheet += "  border: 1px solid " + colorAccent.name() + ";";
    stylesheet += "}";
    stylesheet += "QLineEdit, QComboBox, QTextEdit {";
    stylesheet += "  background: " + colorInputBackground.name() + ";";
    stylesheet += "  color: " + colorTextInput.name() + ";";
    stylesheet += "  border: 1px solid " + colorInputBorder.name() + ";";
    stylesheet += "  border-radius: " + px(panelWidgetRadius) + ";";
    stylesheet += "  padding: "
        + px(panelFieldVerticalPadding) + " "
        + px(panelFieldHorizontalPadding) + ";";
    stylesheet += "  selection-background-color: " + colorAccent.name() + ";";
    stylesheet += "}";
    stylesheet += "QLineEdit:focus, QComboBox:focus, QTextEdit:focus {";
    stylesheet += "  border: 1px solid " + colorAccentFocus.name() + ";";
    stylesheet += "}";
    stylesheet += "QComboBox {";
    stylesheet += "  padding-right: 24px;";
    stylesheet += "}";
    stylesheet += "QComboBox::drop-down {";
    stylesheet += "  width: 24px;";
    stylesheet += "  border: none;";
    stylesheet += "}";
    stylesheet += "QComboBox::down-arrow {";
    stylesheet += "  width: 8px;";
    stylesheet += "  height: 8px;";
    stylesheet += "}";
    stylesheet += "QPushButton {";
    stylesheet += "  background: " + colorButtonBackground.name() + ";";
    stylesheet += "  color: " + colorTextHeading.name() + ";";
    stylesheet += "  border: 1px solid " + colorButtonBorder.name() + ";";
    stylesheet += "  border-radius: " + px(panelWidgetRadius) + ";";
    stylesheet += "  padding: "
        + px(panelButtonVerticalPadding) + " "
        + px(panelButtonHorizontalPadding) + ";";
    stylesheet += "}";
    stylesheet += "QPushButton:hover {";
    stylesheet += "  background: " + colorButtonHover.name() + ";";
    stylesheet += "}";
    stylesheet += "QPushButton:pressed {";
    stylesheet += "  background: " + colorButtonPressed.name() + ";";
    stylesheet += "}";
    panel->setStyleSheet(stylesheet);
}

}