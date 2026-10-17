#pragma once

#include "overlays.h"

#include <QColor>
#include <QString>
#include <QVariant>

#include <optional>
#include <vector>

// Pins, markers and satellites shared by GlobeItem and ParticleGlobeItem (the
// addPin/addMarker/addSatellite/addConstellation logic of src/Globe.js). Lives on the
// GUI thread; renderers get a copy in synchronize(). Times are scene milliseconds.
class OverlayScene
{
public:
    struct Settings {
        QColor pinColor = QColor(0x00, 0xee, 0xee);
        QColor markerColor = QColor(0xff, 0xcc, 0x00);
        QColor satelliteColor = QColor(0xff, 0x00, 0x00);
        QString fontFamily = QStringLiteral("monospace");
        int maxPins = 500;
        int maxMarkers = 4;
    };

    void addPin(double now, double lat, double lon, const QString &text);
    void addIpPin(double now, double lat, double lon);
    void addMarker(double now, double lat, double lon, const QString &text, bool connected);
    void addConstellation(double now, const QVariantList &satellites, const QVariantMap &opts);
    void setMaxPins(int maxPins);
    void setMaxMarkers(int maxMarkers, double now);

    // The bundled data.js pin sequence: each pin drops in at
    // delayMs + sweepMs * (180 + lng) / 360, in Globe.js order.
    void loadInitialPins(double sweepMs, double delayMs);
    void releasePendingPins(double now);

    Settings settings;
    std::vector<encom::PinState> pins;
    std::optional<encom::PinState> ipPin;
    std::vector<encom::MarkerState> markers;
    std::vector<encom::SatelliteGroup> satelliteGroups;

private:
    struct PendingPin {
        double lat;
        double lon;
        QString label;
        double when;
    };

    void lowerPin(encom::PinState &pin, double now);
    void trimPins();
    void trimMarkers(double now);

    quint64 m_nextId = 1;
    std::vector<PendingPin> m_pendingPins;
    QImage m_markerImage; // shared by every marker, like scene._encom_markerTexture
    QColor m_markerImageColor;
};
