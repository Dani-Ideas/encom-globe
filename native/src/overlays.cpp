#include "overlays.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace encom {

QVector3D mapPoint(double lat, double lon, double scale)
{
    const double phi = (90.0 - lat) * M_PI / 180.0;
    const double theta = (180.0 - lon) * M_PI / 180.0;
    return QVector3D(float(scale * std::sin(phi) * std::cos(theta)),
                     float(scale * std::cos(phi)),
                     float(scale * std::sin(phi) * std::sin(theta)));
}

// TWEEN.Easing.Elastic.Out
float elasticOut(float k)
{
    if (k <= 0.f)
        return 0.f;
    if (k >= 1.f)
        return 1.f;
    return std::pow(2.f, -10.f * k) * std::sin((k - 0.1f) * 5.f * float(M_PI)) + 1.f;
}

float srgbToLinear(float c)
{
    return c < 0.04045f ? c * 0.0773993808f : std::pow(c * 0.9478672986f + 0.0521327014f, 2.4f);
}

QVector3D linearRgb(const QColor &color)
{
    return QVector3D(srgbToLinear(float(color.redF())), srgbToLinear(float(color.greenF())),
                     srgbToLinear(float(color.blueF())));
}

QVector3D rawRgb(const QColor &color)
{
    return QVector3D(float(color.redF()), float(color.greenF()), float(color.blueF()));
}

int satelliteRepeatAt(int numWaves)
{
    const int waveStart = SatelliteFrames / 8;
    return int(std::floor(SatelliteFrames - 2.0 * (SatelliteFrames - waveStart) / numWaves)) + 1;
}

// TextureAnimator: play every frame once, then loop from repeatAt to the end.
int satelliteFrame(double elapsedMs, int repeatAt)
{
    const int n = int(std::max(0.0, elapsedMs) / SatelliteFrameMs);
    if (n < SatelliteFrames)
        return n;
    const int loop = std::max(1, SatelliteFrames - repeatAt);
    return repeatAt + (n - SatelliteFrames) % loop;
}

// utils.createLabel: the canvas is sized from the point size the way the original
// does it (height = size + 10, even), with an optional underline below the text.
QImage createLabelImage(const QString &text, const QString &fontFamily, int sizePt, const QColor &color,
                        const QColor &underlineColor)
{
    QFont font(fontFamily);
    font.setPixelSize(sizePt * 4 / 3); // CSS pt -> px
    const QFontMetricsF fm(font);
    const qreal advance = fm.horizontalAdvance(text);
    int width = int(advance);
    int height = sizePt + 10;
    if (width % 2)
        ++width;
    if (height % 2)
        ++height;
    if (underlineColor.isValid())
        height += 30;
    if (width <= 0)
        return QImage();

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainterPath path;
    path.addText((width - advance) / 2.0, height / 2.0 + (fm.ascent() - fm.descent()) / 2.0, font, text);

    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QPen outline(Qt::black, 6);
    outline.setJoinStyle(Qt::MiterJoin);
    outline.setMiterLimit(2);
    p.strokePath(path, outline);
    p.fillPath(path, color);

    if (underlineColor.isValid()) {
        QPen underline(underlineColor, 4);
        underline.setCapStyle(Qt::FlatCap);
        p.setPen(underline);
        p.drawLine(QPointF(0, height - 10), QPointF(width - 1, height - 10));
    }
    p.end();

    return image.convertToFormat(QImage::Format_RGBA8888);
}

// Marker.js createMarkerTexture: a ring and a filled dot.
QImage createMarkerImage(const QColor &color)
{
    QImage image(30, 30, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 3));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(15, 15), 10, 10);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QPointF(15, 15), 6, 6);
    p.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}

// The arc's point list from Marker.js. Latitude dips toward the equator midway; the
// dotted copy sits 1 degree higher (except at the ends) and slightly lower in altitude.
// fmod keeps the sign of the dividend, like JavaScript's %.
void markerArc(double fromLat, double fromLon, double toLat, double toLon,
               std::vector<QVector3D> &arc, std::vector<QVector3D> &arcDotted)
{
    const int segments = MarkerSegments;
    const double latdist = (toLat - fromLat) / segments;
    const double londist = (toLon - fromLon) / segments;
    arc.clear();
    arcDotted.clear();
    for (int j = 0; j <= segments; ++j) {
        const double lat = (std::fmod(90 + fromLat + j * latdist, 180.0) - 90)
                * (0.5 + std::cos(j * (5 * M_PI / 2) / segments) / 2)
            + (j * toLat / segments / 2);
        const double lon = std::fmod(180 + fromLon + j * londist, 360.0) - 180;
        arc.push_back(mapPoint(lat, lon) * 1.2f);
        const bool end = j == 0 || j == segments;
        arcDotted.push_back(mapPoint(end ? lat : lat + 1, lon) * 1.19f);
    }
}

// Pin.js createTopCanvas, drawn in white so one texture serves every pin color.
QImage createDotImage()
{
    QImage image(20, 20, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(10, 10), 5, 5);
    p.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}

namespace {

// CanvasRenderingContext2D.arc (clockwise, radians, y down) as a fresh path.
void strokeCanvasArc(QPainter &p, const QPen &pen, double cx, double cy, double r, double a0, double a1)
{
    if (r <= 0.0)
        return;
    double sweep = a1 - a0;
    if (sweep >= 2 * M_PI)
        sweep = 2 * M_PI;
    else if (sweep < 0)
        sweep = std::fmod(sweep, 2 * M_PI) + 2 * M_PI;

    const QRectF rect(cx - r, cy - r, 2 * r, 2 * r);
    const double startDeg = -a0 * 180.0 / M_PI;
    QPainterPath path;
    path.arcMoveTo(rect, startDeg);
    path.arcTo(rect, startDeg, -sweep * 180.0 / M_PI);
    p.strokePath(path, pen);
}

} // namespace

// Port of Satellite.js createCanvas. The original's comment applies here too: the
// numbers were tuned by eye, so they are copied rather than cleaned up.
QImage createSatelliteSheet(int numWaves, const QColor &waveColor, const QColor &coreColor,
                            const QColor &shieldColor)
{
    numWaves = std::max(1, numWaves);
    const int numFrames = SatelliteFrames;
    const int pixels = SatellitePixels;
    const int rows = SatelliteRows;
    const int cols = SatelliteCols;
    const int waveStart = numFrames / 8;
    const int waveInterval = (numFrames - waveStart) / numWaves;
    const double waveDist = pixels - 25;
    const double distPerFrame = waveDist / (numFrames - waveStart);

    QImage image(numFrames * pixels / rows, pixels * rows, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);

    QPen shieldPen(shieldColor, 2);
    shieldPen.setCapStyle(Qt::FlatCap);

    double offsetx = 0, offsety = 0;
    int curRow = 0;

    for (int i = 0; i < numFrames; ++i) {
        if (i - curRow * cols >= cols) {
            offsetx = 0;
            offsety += pixels;
            ++curRow;
        }

        const double cx = offsetx + 25;
        const double cy = offsety + pixels / 2;

        const double buffer = M_PI / 16;
        const double start = -M_PI + M_PI / 4;
        double radius = 8;
        const int repeatAt = satelliteRepeatAt(numWaves);

        if (i < waveStart)
            radius = radius * i / waveStart;

        const int swirlDone = int(std::floor((repeatAt - waveStart) / 2.0)) + waveStart;

        for (int n = 0; n < 4; ++n) {
            if (i < waveStart || i >= numFrames) {
                strokeCanvasArc(p, shieldPen, cx, cy, radius, n * M_PI / 2 + start + buffer,
                                n * M_PI / 2 + start + M_PI / 2 - 2 * buffer);
            } else if (i > waveStart && i < swirlDone) {
                const double movementPerStep = (3 * M_PI / 2) / (swirlDone - waveStart);
                const double startAngle = -M_PI + M_PI / 4 + buffer + movementPerStep * (i - waveStart);
                strokeCanvasArc(p, shieldPen, cx, cy, radius,
                                std::max(n * M_PI / 2 + start, startAngle),
                                std::max(n * M_PI / 2 + start + M_PI / 2 - 2 * buffer,
                                         startAngle + M_PI / 2 - 2 * buffer));
            } else if (i >= swirlDone && i < repeatAt) {
                const double movementPerStep = (n * 2 * M_PI / 4) / (repeatAt - swirlDone);
                const double startAngle = M_PI / 2 + M_PI / 4 + buffer + movementPerStep * (i - swirlDone);
                strokeCanvasArc(p, shieldPen, cx, cy, radius, startAngle, startAngle + M_PI / 2 - 2 * buffer);
            } else if (i >= repeatAt && i < (numFrames - repeatAt) / 2.0 + repeatAt) {
                const double movementPerStep = (M_PI / 2) / ((numFrames - repeatAt) / 2.0);
                const double startAngle = n * (M_PI / 2) + M_PI / 4 + buffer + movementPerStep * (i - repeatAt);
                strokeCanvasArc(p, shieldPen, cx, cy, radius, startAngle, startAngle + M_PI / 2 - 2 * buffer);
            } else {
                strokeCanvasArc(p, shieldPen, cx, cy, radius, n * M_PI / 2 + start + buffer,
                                n * M_PI / 2 + start + M_PI / 2 - 2 * buffer);
            }
        }

        // waves going out
        for (int wi = 0; wi < numWaves; ++wi) {
            const int frameOn = i - (waveInterval * wi) - waveStart;
            if (frameOn > 0 && frameOn * distPerFrame < pixels - 25) {
                QColor c = waveColor;
                c.setAlphaF(float(std::clamp(0.9 - frameOn * distPerFrame / (pixels - 25), 0.0, 1.0)));
                QPen wavePen(c, 2);
                wavePen.setCapStyle(Qt::FlatCap);
                strokeCanvasArc(p, wavePen, cx, cy, frameOn * distPerFrame, -M_PI / 12, M_PI / 12);
            }
        }

        // core
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawEllipse(QPointF(cx, cy), 3, 3);
        p.setBrush(Qt::NoBrush);
        QPen corePen(coreColor, 2);
        strokeCanvasArc(p, corePen, cx, cy, i < waveStart ? 3.0 * i / waveStart : 3.0, 0, 2 * M_PI);

        offsetx += pixels;
    }
    p.end();

    return image.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace encom
