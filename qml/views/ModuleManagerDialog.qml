// qmllint disable unqualified
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    title: "智能部分"
    width: 620
    height: 350
    minimumWidth: 620
    minimumHeight: 350
    visible: false // 初始不显示，等待触发
    modality: Qt.ApplicationModal
    flags: Qt.Dialog

    // 默认绑定上下文中的 moduleModel，支持外部动态赋值
    property var targetModel: typeof moduleModel !== "undefined" ? moduleModel : null

    // 常用预设调色板
    readonly property var presetColors: [
        "#3B82F6", // 经典蓝
        "#10B981", // 翡翠绿
        "#F59E0B", // 活力橙
        "#EF4444", // 珊瑚红
        "#8B5CF6", // 优雅紫
        "#6B7280" // 中性灰
    ]

    function clearInputs() {
        nameInput.text = ""
        charInput.text = ""
        colorInput.text = presetColors[0]
    }

    // 颜色合法性校验辅助函数，防止非法字符串导致 QML 控制台报错
    function getValidColor(colorStr, fallback) {
        const c = Qt.color(colorStr)
        return (c.a > 0 || colorStr.toLowerCase() === "#000000" || colorStr.toLowerCase() === "black") ? colorStr : fallback
    }

    onVisibleChanged:clearInputs()

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // --- 1. 顶部标题与关闭按钮 ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: "添加新模块"
                font.pixelSize: 16
                font.bold: true
                color: "#111827"
                Layout.fillWidth: true
            }
        }

        // 分割线
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: "#F3F4F6"
        }

        // --- 2. 实时效果预览卡片 ---
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: previewLayout.implicitHeight + 20
            radius: 10
            color: "#F9FAFB"
            border.color: "#E5E7EB"
            border.width: 1

            RowLayout {
                id: previewLayout
                anchors.fill: parent
                anchors.margins: 12
                spacing: 14

                // 模块徽标预览
                Rectangle {
                    implicitWidth: 44
                    implicitHeight: 44
                    radius: 22
                    color: root.getValidColor(colorInput.text, "#3B82F6")
                    border.color: Qt.rgba(0, 0, 0, 0.06)
                    border.width: 1

                    Label {
                        anchors.centerIn: parent
                        text: {
                            const customChar = charInput.text.trim()
                            if (customChar !== "") return customChar.toUpperCase()
                            const nameText = nameInput.text.trim()
                            return nameText !== "" ? nameText.charAt(0).toUpperCase() : "模"
                        }
                        font.pixelSize: 18
                        font.bold: true
                        color: "#FFFFFF"
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    Label {
                        text: nameInput.text.trim() !== "" ? nameInput.text.trim() : "模块名称"
                        font.pixelSize: 14
                        font.bold: true
                        color: "#111827"
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    Label {
                        text: "主界面卡片实时效果预览"
                        font.pixelSize: 11
                        color: "#6B7280"
                    }
                }
            }
        }

        // --- 3. 表单输入区 ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            // 模块名称
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: "名称"
                    font.pixelSize: 12
                    font.bold: true
                    color: "#374151"
                }

                TextField {
                    id: nameInput
                    Layout.fillWidth: true
                    implicitHeight: 38
                    placeholderText: "例如：状态监控"
                    font.pixelSize: 13
                    selectByMouse: true
                    background: Rectangle {
                        radius: 6
                        border.color: nameInput.activeFocus ? "#3B82F6" : "#D1D5DB"
                        border.width: nameInput.activeFocus ? 1.5 : 1
                        color: "#FFFFFF"
                    }
                }
            }

            // 单字标识
            ColumnLayout {
                Layout.preferredWidth: 120
                spacing: 6

                Label {
                    text: "NFC标识"
                    font.pixelSize: 12
                    font.bold: true
                    color: "#374151"
                }

                TextField {
                    id: charInput
                    Layout.fillWidth: true
                    implicitHeight: 38
                    maximumLength: 2
                    placeholderText: "默认取首字"
                    font.pixelSize: 13
                    selectByMouse: true
                    background: Rectangle {
                        radius: 6
                        border.color: charInput.activeFocus ? "#3B82F6" : "#D1D5DB"
                        border.width: charInput.activeFocus ? 1.5 : 1
                        color: "#FFFFFF"
                    }
                }
            }
        }

        // 颜色选择
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: "主题颜色"
                font.pixelSize: 12
                font.bold: true
                color: "#374151"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                // Hex 输入框
                TextField {
                    id: colorInput
                    text: root.presetColors[0]
                    Layout.preferredWidth: 100
                    implicitHeight: 36
                    font.pixelSize: 12
                    selectByMouse: true
                    background: Rectangle {
                        radius: 6
                        border.color: colorInput.activeFocus ? "#3B82F6" : "#D1D5DB"
                        border.width: colorInput.activeFocus ? 1.5 : 1
                        color: "#FFFFFF"
                    }
                }

                // 预设色块选取器
                RowLayout {
                    spacing: 8
                    Layout.alignment: Qt.AlignVCenter

                    Repeater {
                        model: root.presetColors

                        delegate: Rectangle {
                            id: swatch
                            required property string modelData

                            width: 26
                            height: 26
                            radius: 13
                            color: swatch.modelData

                            // 选中高亮外圈
                            border.width: colorInput.text.trim().toUpperCase() === swatch.modelData.toUpperCase() ? 2 : 1
                            border.color: colorInput.text.trim().toUpperCase() === swatch.modelData.toUpperCase() ? "#111827" : Qt.rgba(0, 0, 0, 0.12)

                            // 选中内部小白点
                            Rectangle {
                                anchors.centerIn: parent
                                width: 6
                                height: 6
                                radius: 3
                                color: "#FFFFFF"
                                visible: colorInput.text.trim().toUpperCase() === swatch.modelData.toUpperCase()
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: colorInput.text = swatch.modelData
                            }
                        }
                    }
                }
            }
        }

        // --- 4. 底部操作按钮 ---
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10

            Item { Layout.fillWidth: true }

            Button {
                id: cancelBtn
                text: "取消"
                implicitHeight: 36
                implicitWidth: 72
                font.pixelSize: 13

                contentItem: Text {
                    text: cancelBtn.text
                    font: cancelBtn.font
                    color: "#374151"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: 6
                    border.color: cancelBtn.down ? "#9CA3AF" : "#D1D5DB"
                    color: cancelBtn.down ? "#E5E7EB" : (cancelBtn.hovered ? "#F9FAFB" : "#FFFFFF")
                }

                onClicked: root.close()
            }

            Button {
                id: addBtn
                text: "确定添加"
                implicitHeight: 36
                implicitWidth: 90
                font.pixelSize: 13
                font.bold: true

                contentItem: Text {
                    text: addBtn.text
                    font: addBtn.font
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: 6
                    color: addBtn.down ? "#1D4ED8" : (addBtn.hovered ? "#2563EB" : "#3B82F6")
                }

                onClicked: {
                    const finalName = nameInput.text.trim()
                    if (finalName === "") {
                        console.warn("[QML] 模块名称为必填项")
                        return
                    }

                    // 字符自动回退策略：未填写时自动取名称首字符
                    let finalChar = charInput.text.trim().toUpperCase()
                    if (finalChar === "") {
                        finalChar = finalName.charAt(0).toUpperCase()
                    }

                    const finalColor = root.getValidColor(colorInput.text.trim(), root.presetColors[0])

                    if (root.targetModel && root.targetModel.addModule) {
                        // 路径传空字符串 ""，保证 C++ 端 addModule(name, path, color, character) 签名兼容
                        root.targetModel.addModule(finalName, "", finalColor, finalChar)
                        root.clearInputs()
                        root.close()
                    } else {
                        console.error("[QML] targetModel 未指定或未提供 addModule 接口")
                    }
                }
            }
        }
    }
}