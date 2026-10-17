#pragma once

#include "overlayscene.h"

#include <QColor>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariant>
#include <QtQuick/QQuickRhiItem>

#include <optional>
#include <vector>

// Native port of the ENCOM globe (src/Globe.js): hex grid, intro lines, pins with
// smoke, markers with arcs, and satellites.
class GlobeItem : public QQuickRhiItem
{
    Q_OBJECT
    Q_PROPERTY(QColor baseColor MEMBER m_baseColor NOTIFY baseColorChanged)
    Q_PROPERTY(QColor pinColor MEMBER m_pinColor NOTIFY pinColorChanged)
    Q_PROPERTY(QColor markerColor MEMBER m_markerColor NOTIFY markerColorChanged)
    Q_PROPERTY(QColor satelliteColor MEMBER m_satelliteColor NOTIFY satelliteColorChanged)
    Q_PROPERTY(QString fontFamily MEMBER m_fontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(qreal globeScale MEMBER m_globeScale NOTIFY globeScaleChanged)
    Q_PROPERTY(qreal viewAngle MEMBER m_viewAngle NOTIFY viewAngleChanged)
    Q_PROPERTY(int dayLength MEMBER m_dayLength NOTIFY dayLengthChanged)
    Q_PROPERTY(int introDuration MEMBER m_introDuration NOTIFY introDurationChanged)
    Q_PROPERTY(qreal introAltitude MEMBER m_introAltitude NOTIFY introAltitudeChanged)
    Q_PROPERTY(QColor introLinesColor MEMBER m_introLinesColor NOTIFY introLinesColorChanged)
    Q_PROPERTY(int introLinesCount MEMBER m_introLinesCount NOTIFY introLinesCountChanged)
    Q_PROPERTY(qreal highlightLat MEMBER m_highlightLat NOTIFY highlightChanged)
    Q_PROPERTY(qreal highlightLon MEMBER m_highlightLon NOTIFY highlightChanged)
    Q_PROPERTY(bool highlightActive MEMBER m_highlightActive NOTIFY highlightChanged)
    Q_PROPERTY(int maxPins READ maxPins WRITE setMaxPins NOTIFY maxPinsChanged)
    Q_PROPERTY(int maxMarkers READ maxMarkers WRITE setMaxMarkers NOTIFY maxMarkersChanged)
    // Play the bundled data.js pin sequence during the intro, as index.html does.
    Q_PROPERTY(bool initialPins MEMBER m_initialPins NOTIFY initialPinsChanged)
    Q_PROPERTY(int maxFps READ maxFps WRITE setMaxFps NOTIFY maxFpsChanged)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)

public:
    explicit GlobeItem(QQuickItem *parent = nullptr);

    Q_INVOKABLE void addPin(double lat, double lon, const QString &text = QString());
    Q_INVOKABLE void addIpPin(double lat, double lon);
    // connected: draw an arc from the most recently added marker.
    Q_INVOKABLE void addMarker(double lat, double lon, const QString &text = QString(),
                               bool connected = false);
    // opts: numWaves, waveColor, coreColor, shieldColor, size (as in Satellite.js)
    Q_INVOKABLE void addSatellite(double lat, double lon, double altitude, const QVariantMap &opts = {});
    // satellites: [{lat, lon, altitude}, ...]; they share one animated texture.
    Q_INVOKABLE void addConstellation(const QVariantList &satellites, const QVariantMap &opts = {});

    int maxPins() const { return m_maxPins; }
    void setMaxPins(int maxPins);
    int maxMarkers() const { return m_maxMarkers; }
    void setMaxMarkers(int maxMarkers);
    int maxFps() const { return m_maxFps; }
    void setMaxFps(int fps);
    bool running() const { return m_running; }
    void setRunning(bool running);

signals:
    void baseColorChanged();
    void pinColorChanged();
    void markerColorChanged();
    void satelliteColorChanged();
    void fontFamilyChanged();
    void globeScaleChanged();
    void viewAngleChanged();
    void dayLengthChanged();
    void introDurationChanged();
    void introAltitudeChanged();
    void introLinesColorChanged();
    void introLinesCountChanged();
    void highlightChanged();
    void maxPinsChanged();
    void maxMarkersChanged();
    void initialPinsChanged();
    void maxFpsChanged();
    void runningChanged();

protected:
    QQuickRhiItemRenderer *createRenderer() override;
    void componentComplete() override;

private:
    friend class GlobeRenderer;

    void advance();
    void updateTimer();
    void syncSceneSettings();

    QColor m_baseColor = QColor(0xff, 0xcc, 0x00);
    QColor m_pinColor = QColor(0x00, 0xee, 0xee);
    QColor m_markerColor = QColor(0xff, 0xcc, 0x00);
    QColor m_satelliteColor = QColor(0xff, 0x00, 0x00);
    QString m_fontFamily = QStringLiteral("monospace");
    qreal m_globeScale = 1.0;
    qreal m_viewAngle = 0.0;
    int m_dayLength = 28000;
    int m_introDuration = 2000;
    qreal m_introAltitude = 1.10;
    QColor m_introLinesColor = QColor(0x8f, 0xd8, 0xd8);
    int m_introLinesCount = 60;
    qreal m_highlightLat = 0.0;
    qreal m_highlightLon = 0.0;
    bool m_highlightActive = false;
    int m_maxPins = 500;
    int m_maxMarkers = 4;
    bool m_initialPins = true;
    int m_maxFps = 30;
    bool m_running = true;

    QTimer m_timer;
    QElapsedTimer m_frameClock;
    double m_elapsedMs = 0.0;

    OverlayScene m_scene;
};
