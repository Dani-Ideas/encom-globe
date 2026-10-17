#include "overlayscene.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

using namespace encom;

namespace {

double random01()
{
    return QRandomGenerator::global()->generateDouble();
}

QColor colorOption(const QVariantMap &opts, const QString &key, const QColor &fallback)
{
    const QColor c = opts.value(key).value<QColor>();
    return c.isValid() ? c : fallback;
}

} // namespace

void OverlayScene::loadInitialPins(double sweepMs, double delayMs)
{
    QFile f(QStringLiteral(":/encomglobe/data.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("EncomGlobe: data.json not found in resources");
        return;
    }
    const QJsonArray data = QJsonDocument::fromJson(f.readAll()).array();
    m_pendingPins.clear();
    for (const QJsonValue &value : data) {
        const QJsonObject o = value.toObject();
        const double lng = o.value(QLatin1String("lng")).toDouble();
        m_pendingPins.push_back({ o.value(QLatin1String("lat")).toDouble(), lng,
                                  o.value(QLatin1String("label")).toString(),
                                  delayMs + sweepMs * ((180.0 + lng) / 360.0) });
    }
    // Same order as Globe.js: sorted descending, consumed from the back.
    std::stable_sort(m_pendingPins.begin(), m_pendingPins.end(), [](const PendingPin &a, const PendingPin &b) {
        return (a.lon - a.label.size() * 2) > (b.lon - b.label.size() * 2);
    });
}

void OverlayScene::releasePendingPins(double now)
{
    while (!m_pendingPins.empty() && m_pendingPins.back().when < now) {
        const PendingPin next = m_pendingPins.back();
        m_pendingPins.pop_back();
        addPin(now, next.lat, next.lon, next.label);
    }
}

void OverlayScene::addPin(double now, double lat, double lon, const QString &text)
{
    const bool hasText = !text.isEmpty();

    PinState pin;
    pin.id = m_nextId++;
    pin.lat = lat;
    pin.lon = lon;
    pin.text = text;
    pin.altitude = 1.2f;
    if (!hasText)
        pin.altitude -= float(0.05 + random01() * 0.05);
    pin.created = now;
    pin.topVisible = pin.labelVisible = pin.smokeVisible = hasText;
    pin.smokeStarted = now;
    pin.lineFrom = 1.f;
    pin.lineTo = pin.altitude;
    pin.lineStarted = now;
    pin.lineColor = linearRgb(settings.pinColor);
    pin.topTint = rawRgb(settings.pinColor);
    if (hasText)
        pin.label = createLabelImage(text, settings.fontFamily, 18, Qt::white);
    // latLon2d, truncated like parseInt.
    pin.pos2d = QPointF(std::trunc(lat + 90), std::trunc(lon + 180));
    pin.rad = hasText ? 2 + (std::abs(lat) / 90) * 15 : 1;

    // Label collisions: if every labelled pin we overlap is older than 5 s, lower
    // those; otherwise lower the new one.
    if (hasText) {
        int collisions = 0;
        int tooYoung = 0;
        std::vector<PinState *> older;
        for (PinState &other : pins) {
            if (other.text.isEmpty())
                continue;
            if (QLineF(other.pos2d, pin.pos2d).length() >= other.rad + pin.rad)
                continue;
            ++collisions;
            if (now - other.created > 5000)
                older.push_back(&other);
            else
                ++tooYoung;
        }
        if (collisions > 0 && tooYoung == 0) {
            for (PinState *other : older)
                lowerPin(*other, now);
        } else if (collisions > 0) {
            lowerPin(pin, now);
        }
    }

    pins.push_back(std::move(pin));
    trimPins();
}

// hideLabel + hideSmoke + hideTop + changeAltitude(1.1..1.15)
void OverlayScene::lowerPin(PinState &pin, double now)
{
    pin.labelVisible = pin.topVisible = pin.smokeVisible = false;
    const float altitude = float(random01() * 0.05 + 1.1);
    pin.lineFrom = pin.altitude;
    pin.lineTo = altitude;
    pin.lineStarted = now;
    pin.altitude = altitude;
}

void OverlayScene::trimPins()
{
    if (int(pins.size()) > settings.maxPins)
        pins.erase(pins.begin(), pins.end() - settings.maxPins);
}

void OverlayScene::setMaxPins(int maxPins)
{
    settings.maxPins = std::max(0, maxPins);
    trimPins();
}

void OverlayScene::addIpPin(double now, double lat, double lon)
{
    PinState pin;
    pin.id = m_nextId++;
    pin.lat = lat;
    pin.lon = lon;
    pin.altitude = 1.8f;
    pin.created = now;
    pin.topVisible = true;
    pin.lineFrom = 1.f;
    pin.lineTo = pin.altitude;
    pin.lineStarted = now;
    pin.topScale = 28.f;
    pin.ip = true;
    ipPin = std::move(pin);
}

void OverlayScene::addMarker(double now, double lat, double lon, const QString &text, bool connected)
{
    if (m_markerImage.isNull() || m_markerImageColor != settings.markerColor) {
        m_markerImage = createMarkerImage(settings.markerColor);
        m_markerImageColor = settings.markerColor;
    }

    MarkerState marker;
    marker.id = m_nextId++;
    marker.lat = lat;
    marker.lon = lon;
    marker.created = now;
    marker.lineColor = linearRgb(settings.markerColor);
    marker.label = createLabelImage(text.toUpper(), settings.fontFamily, 20, Qt::white, settings.markerColor);
    marker.sprite = m_markerImage;
    if (connected && !markers.empty()) {
        const MarkerState &previous = markers.back();
        marker.previousId = previous.id;
        markerArc(previous.lat, previous.lon, lat, lon, marker.arc, marker.arcDotted);
    }

    markers.push_back(std::move(marker));
    trimMarkers(now);
}

// Marker.remove: the marker disappears at once, and the arcs of markers connected
// to it are pulled in from their start.
void OverlayScene::trimMarkers(double now)
{
    while (int(markers.size()) > settings.maxMarkers) {
        const quint64 removed = markers.front().id;
        markers.erase(markers.begin());
        for (MarkerState &marker : markers) {
            if (marker.previousId == removed && marker.retractStarted < 0)
                marker.retractStarted = now;
        }
    }
}

void OverlayScene::setMaxMarkers(int maxMarkers, double now)
{
    settings.maxMarkers = std::max(0, maxMarkers);
    trimMarkers(now);
}

void OverlayScene::addConstellation(double now, const QVariantList &satellites, const QVariantMap &opts)
{
    const int numWaves = opts.value(QStringLiteral("numWaves"), 8).toInt();

    SatelliteGroup group;
    group.id = m_nextId++;
    group.started = now;
    group.repeatAt = satelliteRepeatAt(std::max(1, numWaves));
    group.size = opts.value(QStringLiteral("size"), 1.0).toFloat();
    group.sheet = createSatelliteSheet(numWaves,
                                       colorOption(opts, QStringLiteral("waveColor"), Qt::white),
                                       colorOption(opts, QStringLiteral("coreColor"), settings.satelliteColor),
                                       colorOption(opts, QStringLiteral("shieldColor"), Qt::white));
    for (const QVariant &v : satellites) {
        const QVariantMap s = v.toMap();
        group.satellites.push_back({ s.value(QStringLiteral("lat")).toDouble(),
                                     s.value(QStringLiteral("lon")).toDouble(),
                                     s.value(QStringLiteral("altitude"), 1.0).toDouble() });
    }
    if (!group.satellites.empty())
        satelliteGroups.push_back(std::move(group));
}
