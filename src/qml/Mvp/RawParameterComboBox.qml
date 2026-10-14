import QtQuick
import "."

SettingsComboBox {
    font.family: Theme.uiFont
    font.pixelSize: 12
    height: 32
    onVisibleChanged: if (!visible) popup.close()
}
