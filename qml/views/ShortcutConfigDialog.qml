// qmllint disable unqualified
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    title: "快捷键盘功能映射"
    width: 780
    height: 560
    minimumWidth: 720
    minimumHeight: 520
    visible: false
    color: "#F5F6F8"

    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowCloseButtonHint | Qt.CustomizeWindowHint

    // 当前选中的按键索引（0 ~ 5）
    property int selectedIndex: 0

    // 6 个按键的数据模型
    ListModel {
        id: keyModel
        ListElement { keyId: 1; keyName: "按键 1"; actionType: "组合快捷键"; param: "Ctrl + C"; icon: "📋" }
        ListElement { keyId: 2; keyName: "按键 2"; actionType: "组合快捷键"; param: "Ctrl + V"; icon: "📄" }
        ListElement { keyId: 3; keyName: "按键 3"; actionType: "启动应用"; param: "notepad.exe"; icon: "🚀" }
        ListElement { keyId: 4; keyName: "按键 4"; actionType: "多媒体控制"; param: "静音 / 取消静音"; icon: "🔇" }
        ListElement { keyId: 5; keyName: "按键 5"; actionType: "发送文本"; param: "Thank you!"; icon: "💬" }
        ListElement { keyId: 6; keyName: "按键 6"; actionType: "组合快捷键"; param: "Win + D"; icon: "🖥️" }
    }

    function updateCurrentKey(field, value) {
        if (selectedIndex >= 0 && selectedIndex < keyModel.count) {
            keyModel.setProperty(selectedIndex, field, value)
        }
    }

    function loadSavedKeys() {
        const saved = shortcutController.loadShortcuts()
        keyModel.clear()
        for (let i = 0; i < saved.length; ++i) keyModel.append(saved[i])
    }
    onVisibleChanged: if (visible) loadSavedKeys()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ======================= 顶部导航栏 =======================
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 52
            color: "#FFFFFF"

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: "#E2E4E8"
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 8

                Label {
                    text: "快捷键盘功能映射"
                    font.pixelSize: 15
                    font.bold: true
                    color: "#111827"
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    implicitWidth: tagText.implicitWidth + 16
                    implicitHeight: 22
                    radius: 11
                    color: "#EFF6FF"
                    border.color: "#DBEAFE"
                    border.width: 1

                    Label {
                        id: tagText
                        anchors.centerIn: parent
                        text: "6 键宏键盘配置"
                        font.pixelSize: 11
                        color: "#2563EB"
                        font.bold: true
                    }
                }
            }
        }

        // ======================= 主体双栏区域 =======================
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 18
            spacing: 16

            // ----- 左侧：6 键物理键位矩阵卡片 -----
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 320
                radius: 12
                color: "#FFFFFF"
                border.color: "#E5E7EB"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "硬件按键矩阵"
                            font.pixelSize: 13
                            font.bold: true
                            color: "#1F2937"
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: "点击按键进行配置"
                            font.pixelSize: 11
                            color: "#9CA3AF"
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: "#F3F4F6"
                    }

                    // 键位卡片网格（3 列 x 2 行）
                    GridLayout {
                        columns: 3
                        rowSpacing: 10
                        columnSpacing: 10
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Repeater {
                            model: keyModel

                            delegate: Rectangle {
                                id: keyCard
                                required property int index
                                required property var model

                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 8

                                readonly property bool isSelected: root.selectedIndex === keyCard.index

                                // 浅色选中高亮状态与悬停动效
                                color: keyCard.isSelected ? "#EFF6FF" : (keyMouse.containsMouse ? "#FAFAFA" : "#FFFFFF")
                                border.color: keyCard.isSelected ? "#3B82F6" : (keyMouse.containsMouse ? "#93C5FD" : "#E5E7EB")
                                border.width: keyCard.isSelected ? 1.5 : 1

                                Behavior on color { ColorAnimation { duration: 150 } }
                                Behavior on border.color { ColorAnimation { duration: 150 } }

                                // 键位硬件编码角标 (K1 ~ K6)
                                Rectangle {
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.margins: 6
                                    width: 22
                                    height: 15
                                    radius: 4
                                    color: keyCard.isSelected ? "#3B82F6" : "#F3F4F6"

                                    Label {
                                        anchors.centerIn: parent
                                        text: "K" + keyCard.model.keyId
                                        font.pixelSize: 9
                                        font.bold: true
                                        color: keyCard.isSelected ? "#FFFFFF" : "#6B7280"
                                    }
                                }

                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: 3

                                    Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: keyCard.model.icon
                                        font.pixelSize: 22
                                    }

                                    Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: keyCard.model.keyName
                                        color: keyCard.isSelected ? "#1D4ED8" : "#111827"
                                        font.bold: true
                                        font.pixelSize: 12
                                    }

                                    Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        Layout.maximumWidth: keyCard.width - 14
                                        text: keyCard.model.param
                                        elide: Text.ElideRight
                                        color: keyCard.isSelected ? "#3B82F6" : "#6B7280"
                                        font.pixelSize: 10
                                    }
                                }

                                MouseArea {
                                    id: keyMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.selectedIndex = keyCard.index
                                }
                            }
                        }
                    }
                }
            }

            // ----- 右侧：选定按键参数编辑卡片 -----
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 12
                color: "#FFFFFF"
                border.color: "#E5E7EB"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    // 卡片标题栏
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            implicitWidth: 3
                            implicitHeight: 14
                            radius: 1.5
                            color: "#3B82F6"
                        }

                        Label {
                            text: "配置按键：" + keyModel.get(root.selectedIndex).keyName
                            font.pixelSize: 14
                            font.bold: true
                            color: "#111827"
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            text: "K" + keyModel.get(root.selectedIndex).keyId
                            font.pixelSize: 12
                            font.bold: true
                            color: "#3B82F6"
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: "#F3F4F6"
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true

                        ColumnLayout {
                            width: parent.width
                            spacing: 14

                            // 1. 按键名称输入
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    text: "按键名称 / 别名"
                                    font.pixelSize: 12
                                    font.bold: true
                                    color: "#4B5563"
                                }

                                TextField {
                                    id: nameField
                                    Layout.fillWidth: true
                                    implicitHeight: 38
                                    text: keyModel.get(root.selectedIndex).keyName
                                    font.pixelSize: 13
                                    selectByMouse: true
                                    background: Rectangle {
                                        radius: 6
                                        border.color: nameField.activeFocus ? "#3B82F6" : "#D1D5DB"
                                        border.width: nameField.activeFocus ? 1.5 : 1
                                        color: "#FFFFFF"
                                    }
                                    onTextChanged: {
                                        if (activeFocus) root.updateCurrentKey("keyName", text)
                                    }
                                }
                            }

                            // 2. 快捷图标选择
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    text: "预设图标"
                                    font.pixelSize: 12
                                    font.bold: true
                                    color: "#4B5563"
                                }

                                RowLayout {
                                    spacing: 8

                                    Repeater {
                                        model: ["📋", "📄", "🚀", "🔇", "💬", "🖥️", "⚡", "🎮"]

                                        delegate: Rectangle {
                                            id: iconBtn
                                            required property string modelData

                                            width: 34
                                            height: 34
                                            radius: 6
                                            readonly property bool isCurrent: keyModel.get(root.selectedIndex).icon === iconBtn.modelData

                                            color: isCurrent ? "#EFF6FF" : (iconMouse.containsMouse ? "#F9FAFB" : "#FFFFFF")
                                            border.color: isCurrent ? "#3B82F6" : (iconMouse.containsMouse ? "#93C5FD" : "#D1D5DB")
                                            border.width: isCurrent ? 1.5 : 1

                                            Label {
                                                anchors.centerIn: parent
                                                text: iconBtn.modelData
                                                font.pixelSize: 16
                                            }

                                            MouseArea {
                                                id: iconMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.updateCurrentKey("icon", iconBtn.modelData)
                                            }
                                        }
                                    }
                                }
                            }

                            // 3. 动作类型选择
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    text: "动作类型"
                                    font.pixelSize: 12
                                    font.bold: true
                                    color: "#4B5563"
                                }

                                ComboBox {
                                    id: typeCombo
                                    Layout.fillWidth: true
                                    implicitHeight: 38
                                    model: ["组合快捷键", "启动应用", "发送文本", "多媒体控制"]
                                    currentIndex: model.indexOf(keyModel.get(root.selectedIndex).actionType)

                                    contentItem: Text {
                                        leftPadding: 12
                                        text: typeCombo.displayText
                                        font.pixelSize: 13
                                        color: "#111827"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }

                                    background: Rectangle {
                                        radius: 6
                                        border.color: typeCombo.activeFocus || typeCombo.down ? "#3B82F6" : "#D1D5DB"
                                        border.width: typeCombo.activeFocus ? 1.5 : 1
                                        color: "#FFFFFF"
                                    }

                                    popup: Popup {
                                        y: typeCombo.height + 4
                                        width: typeCombo.width
                                        implicitHeight: contentItem.implicitHeight + 8
                                        padding: 4

                                        contentItem: ListView {
                                            clip: true
                                            implicitHeight: contentHeight
                                            model: typeCombo.popup.visible ? typeCombo.delegateModel : null
                                            currentIndex: typeCombo.highlightedIndex
                                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                        }

                                        background: Rectangle {
                                            radius: 8
                                            color: "#FFFFFF"
                                            border.color: "#E5E7EB"
                                            border.width: 1
                                        }
                                    }

                                    delegate: ItemDelegate {
                                        id: comboDelegate
                                        required property string modelData
                                        required property int index

                                        width: typeCombo.width - 8
                                        implicitHeight: 34

                                        contentItem: Text {
                                            text: comboDelegate.modelData
                                            color: comboDelegate.hovered ? "#2563EB" : "#374151"
                                            font.pixelSize: 13
                                            verticalAlignment: Text.AlignVCenter
                                        }

                                        background: Rectangle {
                                            radius: 4
                                            color: comboDelegate.hovered ? "#F3F4F6" : "transparent"
                                        }
                                    }

                                    onActivated: {
                                        root.updateCurrentKey("actionType", currentText)
                                        if (currentText === "组合快捷键") root.updateCurrentKey("param", "Ctrl + Alt + A")
                                        else if (currentText === "启动应用") root.updateCurrentKey("param", "calc.exe")
                                        else if (currentText === "发送文本") root.updateCurrentKey("param", "快捷回复内容")
                                        else if (currentText === "多媒体控制") root.updateCurrentKey("param", "播放 / 暂停")
                                    }
                                }
                            }

                            // 4. 执行参数配置
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    text: "执行参数"
                                    font.pixelSize: 12
                                    font.bold: true
                                    color: "#4B5563"
                                }

                                TextField {
                                    id: paramField
                                    Layout.fillWidth: true
                                    implicitHeight: 38
                                    text: keyModel.get(root.selectedIndex).param
                                    font.pixelSize: 13
                                    selectByMouse: true
                                    background: Rectangle {
                                        radius: 6
                                        border.color: paramField.activeFocus ? "#3B82F6" : "#D1D5DB"
                                        border.width: paramField.activeFocus ? 1.5 : 1
                                        color: "#FFFFFF"
                                    }
                                    onTextChanged: {
                                        if (activeFocus) root.updateCurrentKey("param", text)
                                    }
                                }
                            }

                            Item { implicitHeight: 4 }
                        }
                    }

                    // 底部操作按钮行
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Item { Layout.fillWidth: true }

                        Button {
                            id: closeBtn
                            text: "关闭"
                            implicitHeight: 36
                            implicitWidth: 76
                            font.pixelSize: 13

                            contentItem: Text {
                                text: closeBtn.text
                                font: closeBtn.font
                                color: "#374151"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 6
                                border.color: closeBtn.down ? "#9CA3AF" : "#D1D5DB"
                                color: closeBtn.down ? "#E5E7EB" : (closeBtn.hovered ? "#F9FAFB" : "#FFFFFF")
                            }

                            onClicked: root.close()
                        }

                        Button {
                            id: applyBtn
                            text: "应用配置"
                            implicitHeight: 36
                            implicitWidth: 96
                            font.pixelSize: 13
                            font.bold: true

                            contentItem: Text {
                                text: applyBtn.text
                                font: applyBtn.font
                                color: "#FFFFFF"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 6
                                color: applyBtn.down ? "#1D4ED8" : (applyBtn.hovered ? "#2563EB" : "#3B82F6")
                            }

                            onClicked: {
                                const values = []
                                for (let i = 0; i < keyModel.count; ++i) {
                                    const item = keyModel.get(i)
                                    values.push({keyId:item.keyId, keyName:item.keyName, actionType:item.actionType, param:item.param, icon:item.icon})
                                }
                                if (shortcutController.saveShortcuts(values)) root.close()
                            }
                        }
                    }
                }
            }
        }
    }
}
