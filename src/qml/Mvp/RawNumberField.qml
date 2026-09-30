import QtQuick
import QtQuick.Controls
import "."

TextField {
    id: root

    property var controller: null
    property string parameterKey: ""
    property int listIndex: -1
    property real parameterValue: 0
    property int decimals: 0
    property real minimumValue: 0
    property real maximumValue: 1000000000

    function formattedValue() {
        return decimals > 0 ? Number(parameterValue).toFixed(decimals)
                            : String(Math.round(Number(parameterValue)))
    }
    function commit() {
        if (!acceptableInput || !controller)
            return
        const number = Number(text)
        if (listIndex >= 0)
            controller.setListValue(parameterKey, listIndex, number)
        else
            controller.setValue(parameterKey, number)
    }

    text: formattedValue()
    height: 28
    leftPadding: 8
    rightPadding: 8
    selectByMouse: true
    color: Theme.graphiteInk
    font.family: Theme.monoFont
    font.pixelSize: 10
    validator: decimals > 0 ? doubleValidator : integerValidator
    onEditingFinished: commit()
    onAccepted: commit()
    background: Rectangle {
        radius: 5
        color: Theme.paperWhite
        border.width: root.activeFocus ? 2 : 1
        border.color: root.acceptableInput
                      ? (root.activeFocus ? Theme.probeBlue : Theme.opticalGray)
                      : Theme.danger
    }

    IntValidator {
        id: integerValidator
        bottom: Math.round(root.minimumValue)
        top: Math.round(root.maximumValue)
    }
    DoubleValidator {
        id: doubleValidator
        bottom: root.minimumValue
        top: root.maximumValue
        decimals: root.decimals
        notation: DoubleValidator.StandardNotation
    }
}
