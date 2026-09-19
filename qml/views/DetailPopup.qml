import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: popup
    width: 460
    implicitHeight: mainLayout.implicitHeight + 40
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // 对外数据接口
    property string titleText: ""
    property string contentText: ""
    property string iconColor: "#3B82F6"
    property string characterText: ""

    // 控制接口
    property bool editable: true
    property bool isEditing: false // 保留属性以防外部绑定报错

    // 信号定义
    signal saved(string newTitle, string newContent, string newColor, string newCharacter)
    signal deleted()
    signal launched()

    // 颜色合法性校验
    function getValidColor(colorStr, fallback) {
        if (!colorStr) return fallback
        var c = Qt.color(colorStr)
        return (c.a > 0 || colorStr.toLowerCase() === "#000000" || colorStr.toLowerCase() === "black") ? colorStr : fallback
    }

    onAboutToShow: {
        editNameField.text = titleText
        editPathField.text = contentText
        editCharField.text = characterText
        editColorField.text = iconColor
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.45)
    }

    background: Rectangle {
        radius: 12
        color: "#FFFFFF"
        border.color: "#E5E7EB"
        border.width: 1
    }

    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // --- 1. 顶部标题栏 ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: popup.editable ? (popup.titleText ? ("配置 - " + popup.titleText) : "程序配置") : "程序详情"
                font.pixelSize: 16
                font.bold: true
                color: "#111827"
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Rectangle {
                width: 26
                height: 26
                radius: 13
                color: closeMouse.containsMouse ? "#F3F4F6" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: 12
                    color: "#9CA3AF"
                }

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: popup.close()
                }
            }
        }

        // 分割线
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#F3F4F6"
        }

        // --- 2. 核心交互区（程序图标预览 + 字段直接配置） ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Layout.alignment: Qt.AlignTop

            // 图标预览容器（优先显示提取的原生程序图标，失败时展示字符色块徽标）
            Rectangle {
                id: iconContainer
                Layout.preferredWidth: 54
                Layout.preferredHeight: 54
                Layout.alignment: Qt.AlignTop
                radius: 10
                color: "#F9FAFB"
                border.color: "#E5E7EB"
                border.width: 1
                clip: true

                // A. 原生程序图标展示（通过 AppIconProvider 获取）
                Image {
                    id: sysAppIcon
                    anchors.fill: parent
                    anchors.margins: 5
                    source: editPathField.text.trim() !== ""
                            ? ("image://appicon/" + encodeURIComponent(editPathField.text.trim()))
                            : ""
                    sourceSize: Qt.size(64, 64)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    asynchronous: true
                    cache: false
                    visible: status === Image.Ready
                }

                // B. 兜底字符徽标（当路径无效或无法提取原生图标时呈现）
                Rectangle {
                    anchors.fill: parent
                    visible: sysAppIcon.status !== Image.Ready
                    radius: parent.radius
                    color: popup.getValidColor(editColorField.text.trim(), popup.iconColor)

                    Label {
                        anchors.centerIn: parent
                        text: {
                            if (editCharField.text.trim() !== "") {
                                return editCharField.text.trim().toUpperCase()
                            }
                            var name = editNameField.text.trim()
                            return name.length > 0 ? name.charAt(0).toUpperCase() : "A"
                        }
                        font.pixelSize: 18
                        font.bold: true
                        color: "#FFFFFF"
                    }
                }
            }

            // 软件名称输入
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: "程序名称"
                    font.pixelSize: 12
                    font.bold: true
                    color: "#4B5563"
                }

                TextField {
                    id: editNameField
                    Layout.fillWidth: true
                    implicitHeight: 38
                    text: popup.titleText
                    placeholderText: "输入程序名称"
                    font.pixelSize: 13
                    selectByMouse: true
                    readOnly: !popup.editable
                    background: Rectangle {
                        radius: 6
                        border.color: editNameField.activeFocus ? "#3B82F6" : "#D1D5DB"
                        border.width: editNameField.activeFocus ? 1.5 : 1
                        color: popup.editable ? "#FFFFFF" : "#F9FAFB"
                    }
                }
            }
        }

        // 执行路径配置
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Label {
                text: "执行路径 (.exe / .lnk)"
                font.pixelSize: 12
                font.bold: true
                color: "#4B5563"
            }

            TextField {
                id: editPathField
                Layout.fillWidth: true
                implicitHeight: 38
                text: popup.contentText
                placeholderText: "输入可执行文件或快捷方式绝对路径"
                font.pixelSize: 12
                selectByMouse: true
                readOnly: !popup.editable
                background: Rectangle {
                    radius: 6
                    border.color: editPathField.activeFocus ? "#3B82F6" : "#D1D5DB"
                    border.width: editPathField.activeFocus ? 1.5 : 1
                    color: popup.editable ? "#FFFFFF" : "#F9FAFB"
                }
            }
        }

        // 兜底徽标辅助配置（代表字符与背景色）
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            visible: popup.editable

            // 代表字符
            ColumnLayout {
                Layout.preferredWidth: 120
                spacing: 4

                Label {
                    text: "备用代表字符"
                    font.pixelSize: 12
                    font.bold: true
                    color: "#4B5563"
                }

                TextField {
                    id: editCharField
                    Layout.fillWidth: true
                    implicitHeight: 36
                    text: popup.characterText
                    placeholderText: "如：A"
                    maximumLength: 2
                    font.pixelSize: 13
                    selectByMouse: true
                    background: Rectangle {
                        radius: 6
                        border.color: editCharField.activeFocus ? "#3B82F6" : "#D1D5DB"
                        border.width: editCharField.activeFocus ? 1.5 : 1
                        color: "#FFFFFF"
                    }
                }
            }

            // 备用背景色
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: "备用背景色 (#HEX)"
                    font.pixelSize: 12
                    font.bold: true
                    color: "#4B5563"
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: editColorField
                        Layout.fillWidth: true
                        implicitHeight: 36
                        text: popup.iconColor
                        placeholderText: "#3B82F6"
                        font.pixelSize: 13
                        selectByMouse: true
                        background: Rectangle {
                            radius: 6
                            border.color: editColorField.activeFocus ? "#3B82F6" : "#D1D5DB"
                            border.width: editColorField.activeFocus ? 1.5 : 1
                            color: "#FFFFFF"
                        }
                    }

                    // 色块指示
                    Rectangle {
                        width: 36
                        height: 36
                        radius: 6
                        color: popup.getValidColor(editColorField.text.trim(), popup.iconColor)
                        border.color: "#D1D5DB"
                        border.width: 1
                    }
                }
            }
        }

        // --- 3. 底部操作栏 ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            // 删除按钮（仅编辑模式可见）
            Button {
                id: deleteBtn
                text: "删除"
                visible: popup.editable
                implicitHeight: 36
                implicitWidth: 64
                font.pixelSize: 13

                contentItem: Text {
                    text: deleteBtn.text
                    font: deleteBtn.font
                    color: "#EF4444"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: 6
                    border.color: deleteBtn.down ? "#FCA5A5" : "#FECACA"
                    color: deleteBtn.down ? "#FEE2E2" : (deleteBtn.hovered ? "#FEF2F2" : "#FFFFFF")
                }

                onClicked: {
                    popup.deleted()
                    popup.close()
                }
            }

            Item { Layout.fillWidth: true }

            // 立即运行
            Button {
                id: launchBtn
                text: "立即运行"
                implicitHeight: 36
                implicitWidth: 84
                font.pixelSize: 13
                font.bold: true

                contentItem: Text {
                    text: launchBtn.text
                    font: launchBtn.font
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: 6
                    color: launchBtn.down ? "#047857" : (launchBtn.hovered ? "#059669" : "#10B981")
                }

                onClicked: {
                    popup.launched()
                    popup.close()
                }
            }

            // 取消按钮
            // 取消/关闭按钮
            Button {
                id: cancelBtn
                text: popup.editable ? "取消" : "关闭"
                implicitHeight: 36
                implicitWidth: 64
                font.pixelSize: 13
                background: Rectangle {
                    radius: 6
                    border.color: "#D1D5DB"
                    color: cancelBtn.down ? "#E5E7EB" : (cancelBtn.hovered ? "#F9FAFB" : "#FFFFFF")
                }
                onClicked: popup.close()
            }

            // 保存修改（仅编辑模式）
            Button {
                id: saveBtn
                text: "保存修改"
                visible: popup.editable
                implicitHeight: 36
                implicitWidth: 88
                font.pixelSize: 13
                font.bold: true

                contentItem: Text {
                    text: saveBtn.text
                    font: saveBtn.font
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: 6
                    color: saveBtn.down ? "#1D4ED8" : (saveBtn.hovered ? "#2563EB" : "#3B82F6")
                }

                onClicked: {
                    popup.titleText = editNameField.text.trim()
                    popup.contentText = editPathField.text.trim()
                    popup.iconColor = popup.getValidColor(editColorField.text.trim(), popup.iconColor)
                    popup.characterText = editCharField.text.trim().toUpperCase()

                    popup.saved(popup.titleText, popup.contentText, popup.iconColor, popup.characterText)
                    popup.close()
                }
            }
        }
    }
}