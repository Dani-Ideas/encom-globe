#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector3D>

#include <vector>

// Scene state for pins, markers and satellites (ports of src/Pin.js, src/Marker.js and
// src/Satellite.js), plus the canvas-drawing helpers from src/utils.js. The item
// mutates this on the GUI thread; the renderer gets a copy in synchronize().
namespace encom {

constexpr double GlobeRadius = 500.0;

// Satellite spritesheet layout (Satellite.js createCanvas / TextureAnimator).
constexpr int SatelliteFrames = 50;
constexpr int SatelliteRows = 10;
constexpr int SatelliteCols = SatelliteFrames / SatelliteRows;
constexpr int SatellitePixels = 100;
constexpr double SatelliteFrameMs = 80.0;

// Marker.js arc: lineSegments points drawn one by one over drawTime.
constexpr int MarkerSegments = 150;
constexpr double MarkerDrawMs = 2000.0;
constexpr double MarkerStepMs = MarkerDrawMs / MarkerSegments;

struct PinState {
    quint64 id = 0;
    double lat = 0.0;
    double lon = 0.0;
    QString text;
    float altitude = 1.2f;      // where the top dot and label sit
    double created = 0.0;       // scene time, ms
    bool topVisible = false;
    bool labelVisible = false;
    bool smokeVisible = false;
    double smokeStarted = 0.0;
    // Elastic tween of the line's outer end, in globe radii.
    float lineFrom = 1.0f;
    float lineTo = 1.2f;
    double lineStarted = 0.0;
    float topScale = 20.0f;
    QVector3D lineColor;        // linear, like THREE.Color
    QVector3D topTint;          // raw canvas color multiplying the white dot texture
    QImage label;               // RGBA8888, null when there is no text
    bool ip = false;            // the pulsing "you are here" pin
    // Label collision circle (Globe.js latLon2d + Quadtree2).
    QPointF pos2d;
    double rad = 1.0;
};

struct MarkerState {
    quint64 id = 0;
    quint64 previousId = 0;     // 0 when not connected
    double lat = 0.0;
    double lon = 0.0;
    float altitude = 1.2f;
    double created = 0.0;
    // Arc from the previous marker (MarkerSegments + 1 points, already scaled);
    // empty when not connected.
    std::vector<QVector3D> arc;
    std::vector<QVector3D> arcDotted;
    double retractStarted = -1.0; // >= 0 once the previous marker was removed
    QVector3D lineColor;          // linear
    QImage label;                 // RGBA8888, upper-case text with underline
    QImage sprite;                // RGBA8888, ring + dot
};

struct SatelliteState {
    double lat = 0.0;
    double lon = 0.0;
    double altitude = 1.0;
};

// A constellation shares one spritesheet and one animation clock.
struct SatelliteGroup {
    quint64 id = 0;
    QImage sheet;               // RGBA8888
    double started = 0.0;
    int repeatAt = 0;
    float size = 1.0f;
    std::vector<SatelliteState> satellites;
};

QVector3D mapPoint(double lat, double lon, double scale = GlobeRadius);
float elasticOut(float k);
float srgbToLinear(float c);
QVector3D linearRgb(const QColor &color);
QVector3D rawRgb(const QColor &color);

int satelliteRepeatAt(int numWaves);
int satelliteFrame(double elapsedMs, int repeatAt);

QImage createLabelImage(const QString &text, const QString &fontFamily, int sizePt, const QColor &color,
                        const QColor &underlineColor = QColor());
QImage createDotImage();
QImage createMarkerImage(const QColor &color);
void markerArc(double fromLat, double fromLon, double toLat, double toLon,
               std::vector<QVector3D> &arc, std::vector<QVector3D> &arcDotted);
QImage createSatelliteSheet(int numWaves, const QColor &waveColor, const QColor &coreColor,
                            const QColor &shieldColor);

} // namespace encom
