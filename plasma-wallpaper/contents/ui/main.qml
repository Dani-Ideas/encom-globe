import QtQuick
import QtWebEngine
import org.kde.plasma.plasmoid

WallpaperItem {
    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    WebEngineView {
        anchors.fill: parent

        url: Qt.resolvedUrl("../web/wallpaper.html")

        focus: false
        activeFocusOnPress: false

        settings.javascriptEnabled: true
        settings.localContentCanAccessFileUrls: true
        settings.localContentCanAccessRemoteUrls: true
        settings.webGLEnabled: true
        settings.accelerated2dCanvasEnabled: true

        onLoadingChanged: function(request) {
            if (request.status === WebEngineView.LoadFailedStatus) {
                console.error("EncomGlobe: failed to load", request.url, request.errorString)
            }
        }

        onJavaScriptConsoleMessage: function(level, message, line, source) {
            console.log("EncomGlobe JS [" + line + "]:", message)
        }
    }
}
