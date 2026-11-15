import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 920
    height: 560
    minimumWidth: 700
    minimumHeight: 460
    visible: true
    title: "6 Music"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    readonly property color dim: "#9a9aa2"

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.toggle() }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; onActivated: backend.muted = !backend.muted }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; onActivated: backend.nudgeVolume(0.05) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; onActivated: backend.nudgeVolume(-0.05) }

    // Status line under the artwork: what the stream is doing right now.
    readonly property string status: backend.streamError !== "" ? qsTr("Can't play the stream: %1").arg(backend.streamError)
        : backend.buffering ? qsTr("Connecting…")
        : backend.playing ? qsTr("Live")
        : qsTr("Stopped")

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            anchors.fill: parent
            anchors.margins: 32
            spacing: 32

            // Left: artwork and the transport.
            ColumnLayout {
                Layout.preferredWidth: 300
                Layout.maximumWidth: 300
                Layout.alignment: Qt.AlignTop
                spacing: 18

                Rectangle {
                    Layout.preferredWidth: 300
                    Layout.preferredHeight: 300
                    radius: theme.radius
                    color: "#1a1a1e"
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: backend.onMusic && backend.artwork !== "" ? backend.artwork : backend.showImage
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 250 } }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: backend.artwork === "" && backend.showImage === ""
                        text: "6"
                        color: theme.accent
                        font.pixelSize: 120
                        font.bold: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    RoundButton {
                        id: playButton
                        Layout.preferredWidth: 56
                        Layout.preferredHeight: 56
                        radius: 28
                        flat: false
                        Material.background: theme.accent
                        Material.foreground: theme.accentForeground
                        text: backend.playing ? "■" : "▶"
                        font.pixelSize: 22
                        onClicked: backend.toggle()
                        ToolTip.visible: hovered
                        ToolTip.text: backend.playing ? qsTr("Stop") : qsTr("Play")
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: win.status
                            color: backend.streamError !== "" ? "#ff6b6b" : win.dim
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap
                        }
                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            value: backend.muted ? 0 : backend.volume
                            onMoved: { backend.muted = false; backend.volume = value }
                        }
                    }
                }
            }

            // Right: what is playing, who is on, and what just played.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6

                Label {
                    text: backend.onMusic ? qsTr("Now playing") : qsTr("On air")
                    color: theme.accent
                    font.pixelSize: 13
                    font.bold: true
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1.5
                }
                Label {
                    Layout.fillWidth: true
                    text: backend.onMusic ? backend.track : backend.showTitle
                    font.pixelSize: 30
                    font.bold: true
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: backend.onMusic ? backend.artist : qsTr("Speech on 6 Music")
                    color: win.dim
                    font.pixelSize: 20
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                // The show, when a track is the headline.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 16
                    Layout.preferredHeight: showColumn.implicitHeight + 24
                    visible: backend.onMusic && backend.showTitle !== ""
                    radius: theme.radius
                    color: "#17171a"

                    ColumnLayout {
                        id: showColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 2
                        Label {
                            text: qsTr("On air")
                            color: win.dim
                            font.pixelSize: 11
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 1.2
                        }
                        Label {
                            Layout.fillWidth: true
                            text: backend.showTitle
                            font.pixelSize: 16
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: {
                                translator.language
                                const start = backend.showStart, end = backend.showEnd
                                const times = isNaN(start.getTime()) ? ""
                                    : Qt.formatTime(start, Locale.ShortFormat) + " – " + Qt.formatTime(end, Locale.ShortFormat)
                                return [backend.showSubtitle, times].filter(s => s !== "").join("  ·  ")
                            }
                            color: win.dim
                            elide: Text.ElideRight
                        }
                    }
                }

                Label {
                    Layout.topMargin: 18
                    text: qsTr("Recently played")
                    color: win.dim
                    font.pixelSize: 11
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1.2
                }

                ListView {
                    id: history
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: backend.recent
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Item {
                        width: history.width
                        height: 44
                        RowLayout {
                            anchors.fill: parent
                            spacing: 12
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.track
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.artist
                                    color: win.dim
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                            Label {
                                text: modelData.when
                                color: win.dim
                                font.pixelSize: 12
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: history.count === 0
                        text: qsTr("Nothing played yet")
                        color: win.dim
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: backend.infoError !== ""
                    text: qsTr("Can't load what's playing: %1").arg(backend.infoError)
                    color: "#ff6b6b"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }

        // Shortcut overlay; click anywhere or press ? or Esc to close.
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: "#d0000000"
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Rectangle {
                anchors.centerIn: parent
                width: 380
                height: helpColumn.implicitHeight + 48
                radius: theme.radius
                color: "#17171a"
                border.color: "#2a2a30"

                ColumnLayout {
                    id: helpColumn
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 10

                    Label { text: qsTr("Keyboard shortcuts"); font.pixelSize: 18; font.bold: true }
                    Repeater {
                        model: [
                            { key: "Space", what: qsTr("Play or stop the radio") },
                            { key: "↑", what: qsTr("Volume up") },
                            { key: "↓", what: qsTr("Volume down") },
                            { key: "M", what: qsTr("Mute") },
                            { key: "?", what: qsTr("Show or hide this help") },
                            { key: "Q", what: qsTr("Quit") }
                        ]
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: modelData.key
                                color: theme.accent
                                font.bold: true
                                Layout.preferredWidth: 60
                            }
                            Label { text: modelData.what; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                    }
                }
            }
        }
    }
}
