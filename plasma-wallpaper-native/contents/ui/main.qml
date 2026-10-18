import QtQuick
import org.kde.plasma.plasmoid
// El instalador copia el plugin a contents/encomglobe; importarlo por ruta relativa
// evita depender de QML_IMPORT_PATH.
import "../encomglobe"

WallpaperItem {
    // Espera antes de empezar la animación de entrada (ms). Al encender el equipo
    // todo arranca a la vez y la intro se ve trabada; así empieza cuando la
    // pantalla de bienvenida ya se fue y el sistema terminó de cargar.
    property int startDelay: 4000

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    ParticleGlobe {
        id: globe
        anchors.fill: parent
        baseColor: "#c6c7f5"
        dayLength: 28000
        // Ángulo inicial: longitud (grados) que queda de frente al arrancar
        // (-90 = América, 0 = Europa/África, 100 = Asia) e inclinación en radianes
        // (negativo muestra más el hemisferio sur, positivo más el norte).
        startLongitude: -30
        tilt: 0.28
        introDuration: 5000
        maxPins: 120
        maxMarkers: 2
        maxFps: 30
        sampleCount: 4

        // Pausado y oculto hasta startDelay (en pausa se verían las líneas de la
        // intro congeladas).
        running: false
        opacity: running ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 400 } }
    }

    Timer {
        interval: startDelay
        running: true
        onTriggered: globe.running = true
    }

    // Geolocalización por IP (sin el truco JSONP: QML no tiene restricciones CORS).
    // Si falla, reintenta cada hora.
    function locate() {
        var xhr = new XMLHttpRequest();
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE)
                return;
            try {
                var d = JSON.parse(xhr.responseText);
                if (d.latitude && d.longitude) {
                    locateRetry.stop();
                }
            } catch (e) {
                console.log("EncomGlobe: sin ubicación por IP:", e);
            }
        };
        xhr.open("GET", "https://ipapi.co/json/");
        xhr.send();
    }

    //Component.onCompleted: locate()

    Timer {
        id: locateRetry
        interval: 3600000
        repeat: true
        running: true
        onTriggered: locate()
    }

    // Marcadores conectados: Vancouver → Tokyo, igual que wallpaper.html.
    // Estos timers cuentan desde que el globo arranca, no desde que carga.
    Timer {
        interval: 3000
        running: globe.running
        onTriggered: {
            globe.addMarker(49.25, -123.1, "Vancouver");
            globe.addMarker(35.6895, 139.6917, "Tokyo", true);
        }
    }

    // Constelación de 6 satélites, igual que wallpaper.html
    Timer {
        interval: 5000
        running: globe.running
        onTriggered: globe.addConstellation([
            { lat:  45, lon: -120, altitude: 1.4 }, { lat:  45, lon:    0, altitude: 1.4 },
            { lat:  45, lon:  120, altitude: 1.4 }, { lat: -30, lon:  -60, altitude: 1.4 },
            { lat: -30, lon:   60, altitude: 1.4 }, { lat: -30, lon:  180, altitude: 1.4 }
        ], { coreColor: "#e31b1b", numWaves: 8 })
    }
}
