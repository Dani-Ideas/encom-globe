#include "globeitem.h"

#include "globerenderer.h"

#include <algorithm>

GlobeItem::GlobeItem(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(1000 / m_maxFps);
    connect(&m_timer, &QTimer::timeout, this, &GlobeItem::advance);

    for (auto signal : { &GlobeItem::baseColorChanged, &GlobeItem::globeScaleChanged,
                         &GlobeItem::viewAngleChanged, &GlobeItem::dayLengthChanged,
                         &GlobeItem::introDurationChanged, &GlobeItem::introAltitudeChanged,
                         &GlobeItem::highlightChanged }) {
        connect(this, signal, this, &QQuickItem::update);
    }
    connect(this, &QQuickItem::visibleChanged, this, &GlobeItem::updateTimer);
    updateTimer();
}

void GlobeItem::componentComplete()
{
    QQuickRhiItem::componentComplete();
    // Globe constructor + addInitialData: pins drop in as the intro sweeps past their longitude.
    if (m_initialPins)
        m_scene.loadInitialPins(m_introDuration, 500.0);
}

// The QML properties are the source of truth; the scene reads them when adding.
void GlobeItem::syncSceneSettings()
{
    m_scene.settings.pinColor = m_pinColor;
    m_scene.settings.markerColor = m_markerColor;
    m_scene.settings.satelliteColor = m_satelliteColor;
    m_scene.settings.fontFamily = m_fontFamily;
}

void GlobeItem::addPin(double lat, double lon, const QString &text)
{
    syncSceneSettings();
    m_scene.addPin(m_elapsedMs, lat, lon, text);
    update();
}

void GlobeItem::addIpPin(double lat, double lon)
{
    m_scene.addIpPin(m_elapsedMs, lat, lon);
    m_highlightLat = lat;
    m_highlightLon = lon;
    m_highlightActive = true;
    emit highlightChanged();
}

void GlobeItem::addMarker(double lat, double lon, const QString &text, bool connected)
{
    syncSceneSettings();
    m_scene.addMarker(m_elapsedMs, lat, lon, text, connected);
    update();
}

void GlobeItem::addSatellite(double lat, double lon, double altitude, const QVariantMap &opts)
{
    addConstellation({ QVariantMap{ { QStringLiteral("lat"), lat },
                                    { QStringLiteral("lon"), lon },
                                    { QStringLiteral("altitude"), altitude } } },
                     opts);
}

void GlobeItem::addConstellation(const QVariantList &satellites, const QVariantMap &opts)
{
    syncSceneSettings();
    m_scene.addConstellation(m_elapsedMs, satellites, opts);
    update();
}

void GlobeItem::setMaxPins(int maxPins)
{
    maxPins = std::max(0, maxPins);
    if (maxPins == m_maxPins)
        return;
    m_maxPins = maxPins;
    m_scene.setMaxPins(maxPins);
    update();
    emit maxPinsChanged();
}

void GlobeItem::setMaxMarkers(int maxMarkers)
{
    maxMarkers = std::max(0, maxMarkers);
    if (maxMarkers == m_maxMarkers)
        return;
    m_maxMarkers = maxMarkers;
    m_scene.setMaxMarkers(maxMarkers, m_elapsedMs);
    update();
    emit maxMarkersChanged();
}

void GlobeItem::setMaxFps(int fps)
{
    fps = std::clamp(fps, 1, 240);
    if (fps == m_maxFps)
        return;
    m_maxFps = fps;
    m_timer.setInterval(1000 / m_maxFps);
    emit maxFpsChanged();
}

void GlobeItem::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    updateTimer();
    emit runningChanged();
}

void GlobeItem::updateTimer()
{
    if (m_running && isVisible()) {
        if (!m_timer.isActive()) {
            m_frameClock.invalidate();
            m_timer.start();
        }
    } else {
        m_timer.stop();
    }
}

void GlobeItem::advance()
{
    if (m_frameClock.isValid())
        m_elapsedMs += double(m_frameClock.restart());
    else
        m_frameClock.start();

    syncSceneSettings();
    m_scene.releasePendingPins(m_elapsedMs);

    update();
}

QQuickRhiItemRenderer *GlobeItem::createRenderer()
{
    return new GlobeRenderer;
}
