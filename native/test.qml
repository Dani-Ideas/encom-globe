// Prueba fuera de Plasma:
//   QT_FORCE_STDERR_LOGGING=1 /usr/lib/qt6/bin/qml -I build/qml test.qml
// Con "-- --shot=/ruta.png [--at=8000]" guarda una captura a los N ms y sale.
import QtQuick
import QtQuick.Window
import org.daniideas.encomglobe

Window {
    id: win
    width: 1280
    height: 720
    visible: true
    color: "black"
    title: "Encom Globe (nativo)"

    function arg(name, fallback) {
        var args = Qt.application.arguments;
        for (var i = 0; i < args.length; i++)
            if (args[i].indexOf("--" + name + "=") === 0) return args[i].substring(name.length + 3);
        return fallback;
    }

    Item {
        id: root
        anchors.fill: parent

        Globe {
            id: globe
            anchors.fill: parent
            baseColor: "#c6c7f5"
            viewAngle: 0.210
            dayLength: 28000
            introDuration: 5000
            maxPins: 120
            maxMarkers: 2
            maxFps: 30
            sampleCount: 4
            Component.onCompleted: addIpPin(19.43, -99.13)
        }
    }

    // Mismos tiempos que plasma-wallpaper/contents/web/wallpaper.html
    Timer {
        interval: 3000
        running: true
        onTriggered: {
            globe.addMarker(49.25, -123.1, "Vancouver");
            globe.addMarker(35.6895, 139.6917, "Tokyo", true);
        }
    }

    // Con "-- --retract=1" agrega un tercer marcador a los 8 s: Vancouver se quita
    // y el arco Vancouver -> Tokyo se recoge.
    Timer {
        interval: 8000
        running: win.arg("retract", "") !== ""
        onTriggered: globe.addMarker(-33.87, 151.21, "Sydney", true)
    }

    Timer {
        interval: 5000
        running: true
        onTriggered: globe.addConstellation([
            { lat:  45, lon: -120, altitude: 1.4 }, { lat:  45, lon:    0, altitude: 1.4 },
            { lat:  45, lon:  120, altitude: 1.4 }, { lat: -30, lon:  -60, altitude: 1.4 },
            { lat: -30, lon:   60, altitude: 1.4 }, { lat: -30, lon:  180, altitude: 1.4 }
        ], { coreColor: "#e31b1b", numWaves: 8 })
    }

    Timer {
        property string shot: win.arg("shot", "")
        interval: parseInt(win.arg("at", "8000"))
        running: shot !== ""
        onTriggered: root.grabToImage(function(r) { r.saveToFile(shot); Qt.quit(); })
    }
}
