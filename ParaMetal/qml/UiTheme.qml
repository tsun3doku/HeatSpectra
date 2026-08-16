pragma Singleton

import QtQuick

QtObject {
    readonly property color windowBackground: "#202023"
    readonly property color panelBackground: "#2e2e34"
    readonly property color splitterCenter: windowBackground
    readonly property color splitterBorder: subtleBorder
    readonly property int splitterWidth: 6
    readonly property int splitterBorderWidth: 2
    readonly property color cardBackground: "#2d2c32"
    readonly property color border: "#3a393c"
    readonly property color subtleBorder: "#3c3c45"
    readonly property color inputBackground: "#242429"
    readonly property color checkboxBackground: "#3a3946"
    readonly property color checkboxBorder: "#77758b"

    readonly property color text: "#eceaf6"
    readonly property color headingText: "#f6f5fb"
    readonly property color secondaryText: "#d2d1de"
    readonly property color mutedText: "#c8c6d4"
    readonly property color onAccentText: "#ffffff"
    readonly property color activeTabText: "#f4f2ff"
    readonly property color navigationText: "#9695a2"

    readonly property color accent: "#5e7cff"
    readonly property color interactiveAccent: "#3578ff"
    readonly property color interactiveHover: "#4180ff"
    readonly property color hover: "#484752"
    readonly property color selected: "#504f5c"

    readonly property color toolNormal: "#3a3a3a"
    readonly property color toolHover: "#505050"
    readonly property color toolPressed: "#5a5a5a"
    readonly property color toolSelectedPressed: "#4b67e0"
    readonly property color toolBorder: "#46464e"
    readonly property color sliderTrack: "#696875"
    readonly property color sliderHandle: "#2f2e35"
    readonly property color sliderHandleBorder: "#e6e4ef"
    readonly property color sourceButton: "#3a3950"
    readonly property color sourceButtonHover: "#474664"
    readonly property color sourceButtonBorder: "#585670"
    readonly property int toolButtonSize: 24
    readonly property int toolButtonPadding: 2
    readonly property int toolButtonFrameInset: 1
    readonly property int toolButtonGroupSpacing: 1
    readonly property int toolButtonFrameSize: toolButtonFrameInset * 2

    function toolButtonGroupWidth(buttonCount) {
        return toolButtonSize * buttonCount
                + toolButtonGroupSpacing * Math.max(0, buttonCount - 1)
                + toolButtonFrameSize
    }

    readonly property color menuBarBackground: "#252529"
    readonly property color menuText: "#d2d2d7"
    readonly property color menuDisabledText: "#6e6e76"
    readonly property color menuSeparator: "#414048"

    readonly property color timelineBackground: "#1e1e1e"
    readonly property color timelineTrack: "#41434c"
    readonly property color timelineRecorded: "#416d9c"
    readonly property color timelineTick: "#767780"
    readonly property color timelineText: "#cccccc"
    readonly property color timelinePlayhead: "#2e7eff"
    readonly property color timelineFrameBackground: interactiveAccent
    readonly property color timelineFrameText: "#f5f8ff"
    readonly property color timelineFieldBackground: "#222328"
    readonly property color welcomeBackdrop: Qt.rgba(0.065, 0.065, 0.08, 0.75)
    readonly property color welcomeOptionBackground: "#24242c"
    readonly property color welcomeOptionHover: "#303047"
    readonly property color welcomeCardBorder: "#5b5b69"
    readonly property color welcomeText: "#ffffff"
    readonly property color welcomeSubtext: "#e8e7ef"

    readonly property string fontFamily: UiTypography.fontFamily
    readonly property string monoFamily: UiTypography.monoFamily
    readonly property int titleFontSize: UiTypography.titleFontSize
    readonly property int regularFontSize: UiTypography.regularFontSize
    readonly property int regularFontWeight: UiTypography.regularFontWeight
    readonly property int descriptionFontSize: UiTypography.descriptionFontSize
    readonly property int consoleFontSize: UiTypography.consoleFontSize
    readonly property font regularFont: UiTypography.regularFont
    readonly property font descriptionFont: UiTypography.descriptionFont
}
