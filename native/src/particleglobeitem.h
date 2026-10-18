#pragma once

#include "overlayscene.h"

#include <QColor>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QPointF>
#include <QTimer>
#include <QVector2D>
#include <QtQuick/QQuickRhiItem>

// Damped spring with motion.dev's parameters (stiffness, damping, mass, delay).
// Retargeting keeps the current velocity, so interrupted animations stay smooth.
struct Spring {
    double value = 0.0;
    double velocity = 0.0;
    double target = 0.0;
    double stiffness = 100.0;
    double damping = 10.0;
    double mass = 1.0;
    double delay = 0.0; // seconds before the spring starts moving

    void animateTo(double newTarget, double k, double c, double m, double delaySeconds = 0.0);
    void step(double seconds);
};

// Native port of src/ParticleGlobe.js: every land tile is a particle that springs
// between the spinning globe and a flat world map. The cursor is a gravity well,
// dragging rotates, and an optional atmosphere (rim-lit shell plus sparse particles)
// surrounds the globe. Pins, markers and satellites (OverlayScene) turn with the
// particles and hide while the globe is flattened.
class ParticleGlobeItem : public QQuickRhiItem
{
    Q_OBJECT
    Q_PROPERTY(QColor baseColor MEMBER m_baseColor NOTIFY settingsChanged)
    Q_PROPERTY(qreal globeScale MEMBER m_globeScale NOTIFY settingsChanged)
    Q_PROPERTY(int dayLength MEMBER m_dayLength NOTIFY settingsChanged)
    Q_PROPERTY(qreal tilt MEMBER m_tilt NOTIFY settingsChanged)
    // Degrees of longitude facing the viewer when the globe starts.
    Q_PROPERTY(qreal startLongitude MEMBER m_startLongitude NOTIFY settingsChanged)
    Q_PROPERTY(qreal pointSize MEMBER m_pointSize NOTIFY settingsChanged)
    Q_PROPERTY(qreal turbulence MEMBER m_turbulence NOTIFY settingsChanged)
    // Opening like the hex globe: spawn sweep plus spinning intro lines.
    Q_PROPERTY(int introDuration MEMBER m_introDuration NOTIFY settingsChanged)
    Q_PROPERTY(qreal introAltitude MEMBER m_introAltitude NOTIFY settingsChanged)
    Q_PROPERTY(QColor introLinesColor MEMBER m_introLinesColor NOTIFY settingsChanged)
    Q_PROPERTY(int introLinesCount MEMBER m_introLinesCount NOTIFY settingsChanged)
    Q_PROPERTY(qreal hoverRadius MEMBER m_hoverRadius NOTIFY settingsChanged)
    Q_PROPERTY(qreal hoverPull MEMBER m_hoverPull NOTIFY settingsChanged)
    Q_PROPERTY(qreal hoverTurbulence MEMBER m_hoverTurbulence NOTIFY settingsChanged)
    Q_PROPERTY(bool interactive MEMBER m_interactive NOTIFY settingsChanged)
    Q_PROPERTY(QColor pinColor MEMBER m_pinColor NOTIFY settingsChanged)
    Q_PROPERTY(QColor markerColor MEMBER m_markerColor NOTIFY settingsChanged)
    Q_PROPERTY(QColor satelliteColor MEMBER m_satelliteColor NOTIFY settingsChanged)
    Q_PROPERTY(QString fontFamily MEMBER m_fontFamily NOTIFY settingsChanged)
    Q_PROPERTY(int maxPins READ maxPins WRITE setMaxPins NOTIFY settingsChanged)
    Q_PROPERTY(int maxMarkers READ maxMarkers WRITE setMaxMarkers NOTIFY settingsChanged)
    // Drop the bundled data.js pins as the intro sweep passes them (as particles.html does).
    Q_PROPERTY(bool initialPins MEMBER m_initialPins NOTIFY settingsChanged)
    Q_PROPERTY(bool atmosphere MEMBER m_atmosphere NOTIFY settingsChanged)
    // Invalid (the default) means "use baseColor".
    Q_PROPERTY(QColor atmosphereColor MEMBER m_atmosphereColor NOTIFY settingsChanged)
    Q_PROPERTY(qreal atmosphereScale MEMBER m_atmosphereScale NOTIFY settingsChanged)
    Q_PROPERTY(qreal atmosphereOpacity MEMBER m_atmosphereOpacity NOTIFY settingsChanged)
    Q_PROPERTY(qreal atmosphereRimPower MEMBER m_atmosphereRimPower NOTIFY settingsChanged)
    Q_PROPERTY(int atmosphereParticles MEMBER m_atmosphereParticles NOTIFY settingsChanged)
    Q_PROPERTY(qreal atmospherePointSize MEMBER m_atmospherePointSize NOTIFY settingsChanged)
    Q_PROPERTY(qreal atmosphereParticleOpacity MEMBER m_atmosphereParticleOpacity NOTIFY settingsChanged)
    // ms between automatic globe/map switches; 0 = off.
    Q_PROPERTY(int autoMorph READ autoMorph WRITE setAutoMorph NOTIFY settingsChanged)
    Q_PROPERTY(int maxFps READ maxFps WRITE setMaxFps NOTIFY settingsChanged)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY settingsChanged)

public:
    explicit ParticleGlobeItem(QQuickItem *parent = nullptr);

    Q_INVOKABLE void flatten();
    Q_INVOKABLE void toGlobe();
    Q_INVOKABLE void toggle();

    Q_INVOKABLE void addPin(double lat, double lon, const QString &text = QString());
    // connected: draw an arc from the most recently added marker.
    Q_INVOKABLE void addMarker(double lat, double lon, const QString &text = QString(),
                               bool connected = false);
    // opts: numWaves, waveColor, coreColor, shieldColor, size (as in Satellite.js)
    Q_INVOKABLE void addSatellite(double lat, double lon, double altitude, const QVariantMap &opts = {});
    // satellites: [{lat, lon, altitude}, ...]; they share one animated texture.
    Q_INVOKABLE void addConstellation(const QVariantList &satellites, const QVariantMap &opts = {});

    int maxPins() const { return m_scene.settings.maxPins; }
    void setMaxPins(int maxPins);
    int maxMarkers() const { return m_scene.settings.maxMarkers; }
    void setMaxMarkers(int maxMarkers);

    int autoMorph() const { return m_autoMorph; }
    void setAutoMorph(int ms);
    int maxFps() const { return m_maxFps; }
    void setMaxFps(int fps);
    bool running() const { return m_running; }
    void setRunning(bool running);

signals:
    void settingsChanged();

protected:
    QQuickRhiItemRenderer *createRenderer() override;
    void componentComplete() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverEnterEvent(QHoverEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;

private:
    friend class ParticleRenderer;

    void advance();
    void updateTimer();
    void setMouse(const QPointF &position);
    void setHover(bool on);
    void syncSceneSettings();

    QColor m_baseColor = QColor(0xff, 0xcc, 0x00);
    qreal m_globeScale = 1.0;
    int m_dayLength = 28000;
    qreal m_tilt = -0.28;
    qreal m_startLongitude = -90.0;
    qreal m_pointSize = 2.0;
    qreal m_turbulence = 4.0;
    int m_introDuration = 2000;
    qreal m_introAltitude = 1.10;
    QColor m_introLinesColor = QColor(0x8f, 0xd8, 0xd8);
    int m_introLinesCount = 60;
    qreal m_hoverRadius = 40.0;
    qreal m_hoverPull = -0.8;
    qreal m_hoverTurbulence = 60.0;
    bool m_interactive = false;
    QColor m_pinColor = QColor(0x00, 0xee, 0xee);
    QColor m_markerColor = QColor(0xff, 0xcc, 0x00);
    QColor m_satelliteColor = QColor(0xff, 0x00, 0x00);
    QString m_fontFamily = QStringLiteral("monospace");
    bool m_initialPins = true;
    bool m_atmosphere = true;
    QColor m_atmosphereColor;
    qreal m_atmosphereScale = 1.12;
    qreal m_atmosphereOpacity = 0.0; // shell rim; 0 = no shell, particles only
    qreal m_atmosphereRimPower = 3.0;
    int m_atmosphereParticles = 2500;
    qreal m_atmospherePointSize = 1.2;
    qreal m_atmosphereParticleOpacity = 0.5;
    int m_autoMorph = 0;
    int m_maxFps = 30;
    bool m_running = true;

    QTimer m_timer;
    QTimer m_autoTimer;
    QElapsedTimer m_frameClock;
    double m_elapsedMs = 0.0;

    OverlayScene m_scene;

    Spring m_globe;  // 1 = sphere, 0 = map
    Spring m_hover;  // cursor gravity strength
    bool m_flat = false;
    bool m_hovering = false;
    QVector2D m_mouseTarget;
    QVector2D m_mouse; // NDC, trails m_mouseTarget

    // Orientation: Rz(roll) * Rx(pitch) * Ry(yaw); yaw also carries the auto spin.
    double m_yaw = M_PI;
    double m_pitch = 0.0; // added to the tilt property
    double m_roll = 0.0;
    double m_velocityYaw = 0.0;   // radians per ms
    double m_velocityPitch = 0.0;
    double m_velocityRoll = 0.0;
    bool m_dragging = false;
    QPointF m_dragPos;
    qint64 m_dragTime = 0;
    QElapsedTimer m_dragClock;
};
