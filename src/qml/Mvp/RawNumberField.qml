import QtQuick
import QtQuick.Controls
import "."

TextField {
    id: root

    property var controller: null
    property string parameterKey: ""
    property int listIndex: -1
    property string editingPath: ""
    property real parameterValue: 0
    property int decimals: 0
    property real minimumValue: 0
    property real maximumValue: 1000000000

    function formattedValue() {
        return decimals > 0 ? Number(parameterValue).toFixed(decimals)
                            : String(Math.round(Number(parameterValue)))
    }
    function commit() {
        if (!acceptableInput || !controller ||
                (editingPath.length > 0 && editingPath !== controller.path))
            return
        const number = Number(text)
        if (listIndex >= 0)
            controller.setListValue(parameterKey, listIndex, number)
        else
            controller.setValue(parameterKey, number)
    }

    text: formattedValue()
    implicitHeight: 32
    height: implicitHeight
    leftPadding: 8
    rightPadding: 8
    selectByMouse: true
    color: Theme.graphiteInk
    font.family: Theme.uiFont
    font.pixelSize: 12
    validator: decimals > 0 ? doubleValidator : integerValidator
    onActiveFocusChanged: if (activeFocus) editingPath = controller ? controller.path : ""
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
        locale: "C"
        bottom: root.minimumValue
        top: root.maximumValue
        decimals: root.decimals
        notation: DoubleValidator.StandardNotation
    }
}
