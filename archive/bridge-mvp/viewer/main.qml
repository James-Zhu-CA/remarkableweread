import QtQuick 2.15
import QtQuick.Window 2.15

Window {
    visible: true
    width: Screen.width
    height: Screen.height
    color: "white"
    title: "Shm Epaper Viewer"

    // If screen is portrait (954x1696), rotate the image to fill.
    property bool isPortrait: Screen.height > Screen.width

    Rectangle {
        anchors.fill: parent
        color: "white"

        Image {
            id: frame
            anchors.centerIn: parent
            width: parent.width
            height: parent.height
            fillMode: Image.PreserveAspectFit
            rotation: 0
            transformOrigin: Item.Center
            source: "image://shmframe/frame?gen=" + shmWatcher.gen
            cache: false
        }

        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            color: "#66000000"
            radius: 4
            border.color: "red"
            border.width: 1
            Text {
                anchors.centerIn: parent
                color: "red"
                text: "gen: " + shmWatcher.gen
                font.pixelSize: 24
                padding: 8
            }
            anchors.margins: 12
            width: implicitWidth + 8
            height: implicitHeight + 8
        }

        // Bottom swipe-up gesture to exit backend (keeps frontend running)
        MultiPointTouchArea {
            id: exitSwipeArea
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 80
            minimumTouchPoints: 1
            maximumTouchPoints: 1

            property real startY: -1
            property bool active: false
            readonly property real triggerDelta: 300                    // swipe up distance
            onPressed: {
                if (touchPoints.length === 1) {
                    startY = touchPoints[0].y
                    active = true
                }
            }
            onReleased: {
                if (!active || touchPoints.length === 0) {
                    startY = -1
                    active = false
                    return
                }
                var endY = touchPoints[0].y
                var delta = startY - endY
                if (delta >= triggerDelta) {
                    if (typeof exitHelper !== 'undefined' && exitHelper.exitToXochitl) {
                        exitHelper.exitToXochitl()
                    }
                }
                startY = -1
                active = false
            }
        }
    }
}
