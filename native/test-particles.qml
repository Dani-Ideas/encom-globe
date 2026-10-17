// Prueba de ParticleGlobe fuera de Plasma:
//   QT_FORCE_STDERR_LOGGING=1 /usr/lib/qt6/bin/qml -I build/qml test-particles.qml
// Con "-- --shots=/carpeta" guarda capturas (globo, mapa, mouse encima) y sale.
import QtQuick
import QtQuick.Window
import QtTest
import org.daniideas.encomglobe

Window {
    id: win
    width: 1280
    height: 720
    visible: true
    color: "black"
    title: "Encom Globe · Partículas (nativo)"
    // En modo capturas, ignora el mouse real para que no compita con el simulado.
    flags: shots !== "" ? (Qt.Window | Qt.WindowTransparentForInput) : Qt.Window

    function arg(name, fallback) {
        var args = Qt.application.arguments;
        for (var i = 0; i < args.length; i++)
            if (args[i].indexOf("--" + name + "=") === 0) return args[i].substring(name.length + 3);
        return fallback;
    }
    property string shots: arg("shots", "")

    Item {
        id: root
        anchors.fill: parent

        ParticleGlobe {
            id: globe
            anchors.fill: parent
            baseColor: "#c6c7f5"
            introDuration: 5000
            maxPins: 120
            maxMarkers: 2
            sampleCount: 4
        }
    }

    // Mismos tiempos que particles.html: los pins de data.js salen solos
    // (initialPins), los marcadores a los 3 s y la constelación a los 5 s.
    Timer {
        interval: 3000
        running: true
        onTriggered: {
            globe.addMarker(49.25, -123.1, "Vancouver");
            globe.addMarker(35.6895, 139.6917, "Tokyo", true);
        }
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

    // Solo para las capturas automáticas: mueve el "mouse" sin uno real.
    TestCase { id: input; when: false }

    function shot(name) { root.grabToImage(function(r) { r.saveToFile(shots + "/native_" + name + ".png"); }); }

    SequentialAnimation {
        running: shots !== ""
        PauseAnimation { duration: 9000 }  ScriptAction { script: shot("overlays") }
        ScriptAction { script: input.mouseDrag(globe, 640, 360, -500, 0) }
        PauseAnimation { duration: 1500 }  ScriptAction { script: shot("dragged") }
        ScriptAction { script: input.mouseMove(globe, 640, 360) }
        PauseAnimation { duration: 1200 }  ScriptAction { script: shot("hover") }
        ScriptAction { script: globe.flatten() }
        PauseAnimation { duration: 2000 }  ScriptAction { script: shot("map") }
        PauseAnimation { duration: 300 }   ScriptAction { script: Qt.quit() }
    }
}
