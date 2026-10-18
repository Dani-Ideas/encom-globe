#include "particleglobeitem.h"

#include "particlerenderer.h"

#include <QHoverEvent>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace {

// The springs used by src/ParticleGlobe.js (motion.dev's example values).
struct SpringParams {
    double stiffness, damping, mass, delay;
};
constexpr SpringParams FlattenSpring { 120, 18, 0.9, 0 };
constexpr SpringParams GlobeSpring { 70, 16, 1, 0 };
constexpr SpringParams HoverSpring { 60, 14, 1, 0 };

constexpr double MaxThrowSpeed = 0.01; // radians per ms, ~1.6 turns per second

void animate(Spring &spring, double target, const SpringParams &p)
{
    spring.animateTo(target, p.stiffness, p.damping, p.mass, p.delay);
}

} // namespace

void Spring::animateTo(double newTarget, double k, double c, double m, double delaySeconds)
{
    target = newTarget;
    stiffness = k;
    damping = c;
    mass = m;
    delay = delaySeconds;
}

void Spring::step(double seconds)
{
    if (delay > 0.0) {
        const double waited = std::min(delay, seconds);
        delay -= waited;
        seconds -= waited;
    }
    // Small fixed substeps keep the integration stable at any frame rate.
    constexpr double h = 1.0 / 240.0;
    while (seconds > 0.0) {
        const double dt = std::min(h, seconds);
        const double acceleration = (-stiffness * (value - target) - damping * velocity) / mass;
        velocity += acceleration * dt;
        value += velocity * dt;
        seconds -= dt;
    }
    if (std::abs(value - target) < 1e-4 && std::abs(velocity) < 1e-3) {
        value = target;
        velocity = 0.0;
    }
}

ParticleGlobeItem::ParticleGlobeItem(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);

    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(1000 / m_maxFps);
    connect(&m_timer, &QTimer::timeout, this, &ParticleGlobeItem::advance);
    connect(&m_autoTimer, &QTimer::timeout, this, &ParticleGlobeItem::toggle);
    connect(this, &ParticleGlobeItem::settingsChanged, this, &QQuickItem::update);
    connect(this, &QQuickItem::visibleChanged, this, &ParticleGlobeItem::updateTimer);

    // Open as the globe; the spawn sweep in the shader is the intro.
    m_globe.value = m_globe.target = 1.0;
    updateTimer();
}

void ParticleGlobeItem::flatten()
{
    m_flat = true;
    animate(m_globe, 0.0, FlattenSpring);
}

void ParticleGlobeItem::toGlobe()
{
    m_flat = false;
    animate(m_globe, 1.0, GlobeSpring);
}

void ParticleGlobeItem::toggle()
{
    if (m_flat)
        toGlobe();
    else
        flatten();
}

void ParticleGlobeItem::componentComplete()
{
    QQuickRhiItem::componentComplete();
    // The longitude facing the camera is 90° - yaw (see mapPoint).
    m_yaw = qDegreesToRadians(90.0 - m_startLongitude);
    // As in the hex globe: pins drop in as the spawn sweep passes their longitude.
    if (m_initialPins)
        m_scene.loadInitialPins(m_introDuration, 500.0);
}

// The QML properties are the source of truth; the scene reads them when adding.
void ParticleGlobeItem::syncSceneSettings()
{
    m_scene.settings.pinColor = m_pinColor;
    m_scene.settings.markerColor = m_markerColor;
    m_scene.settings.satelliteColor = m_satelliteColor;
    m_scene.settings.fontFamily = m_fontFamily;
}

void ParticleGlobeItem::addPin(double lat, double lon, const QString &text)
{
    syncSceneSettings();
    m_scene.addPin(m_elapsedMs, lat, lon, text);
    update();
}

void ParticleGlobeItem::addMarker(double lat, double lon, const QString &text, bool connected)
{
    syncSceneSettings();
    m_scene.addMarker(m_elapsedMs, lat, lon, text, connected);
    update();
}

void ParticleGlobeItem::addSatellite(double lat, double lon, double altitude, const QVariantMap &opts)
{
    addConstellation({ QVariantMap{ { QStringLiteral("lat"), lat },
                                    { QStringLiteral("lon"), lon },
                                    { QStringLiteral("altitude"), altitude } } },
                     opts);
}

void ParticleGlobeItem::addConstellation(const QVariantList &satellites, const QVariantMap &opts)
{
    syncSceneSettings();
    m_scene.addConstellation(m_elapsedMs, satellites, opts);
    update();
}

void ParticleGlobeItem::setMaxPins(int maxPins)
{
    if (std::max(0, maxPins) == m_scene.settings.maxPins)
        return;
    m_scene.setMaxPins(maxPins);
    emit settingsChanged();
}

void ParticleGlobeItem::setMaxMarkers(int maxMarkers)
{
    if (std::max(0, maxMarkers) == m_scene.settings.maxMarkers)
        return;
    m_scene.setMaxMarkers(maxMarkers, m_elapsedMs);
    emit settingsChanged();
}

void ParticleGlobeItem::setAutoMorph(int ms)
{
    ms = std::max(0, ms);
    if (ms == m_autoMorph)
        return;
    m_autoMorph = ms;
    if (m_autoMorph > 0)
        m_autoTimer.start(m_autoMorph);
    else
        m_autoTimer.stop();
    emit settingsChanged();
}

void ParticleGlobeItem::setMaxFps(int fps)
{
    fps = std::clamp(fps, 1, 240);
    if (fps == m_maxFps)
        return;
    m_maxFps = fps;
    m_timer.setInterval(1000 / m_maxFps);
    emit settingsChanged();
}

void ParticleGlobeItem::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    updateTimer();
    emit settingsChanged();
}

void ParticleGlobeItem::updateTimer()
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

void ParticleGlobeItem::advance()
{
    double dt = 0.0;
    if (m_frameClock.isValid())
        dt = std::min(100.0, double(m_frameClock.restart()));
    else
        m_frameClock.start();
    m_elapsedMs += dt;

    if (!m_dragging) {
        // Inertia from the last drag, decaying with a ~300 ms time constant.
        const double decay = std::exp(-dt / 300.0);
        m_yaw += m_velocityYaw * dt;
        m_pitch += m_velocityPitch * dt;
        m_roll += m_velocityRoll * dt;
        m_velocityYaw *= decay;
        m_velocityPitch *= decay;
        m_velocityRoll *= decay;
        // Automatic spin around the globe's own axis.
        m_yaw += 2.0 * M_PI * dt / std::max(1, m_dayLength);
    }

    syncSceneSettings();
    m_scene.releasePendingPins(m_elapsedMs);

    m_globe.step(dt / 1000.0);
    m_hover.step(dt / 1000.0);

    // The well trails the cursor with a ~80 ms lag, which reads as weight.
    const float follow = float(1.0 - std::exp(-dt / 80.0));
    m_mouse += (m_mouseTarget - m_mouse) * follow;

    update();
}

void ParticleGlobeItem::setMouse(const QPointF &position)
{
    m_mouseTarget = QVector2D(float(position.x() / std::max(1.0, width()) * 2.0 - 1.0),
                              float(1.0 - position.y() / std::max(1.0, height()) * 2.0));
    if (!m_hovering)
        m_mouse = m_mouseTarget; // start the well under the cursor instead of sliding in
    setHover(true);
}

void ParticleGlobeItem::setHover(bool on)
{
    if (on == m_hovering)
        return;
    m_hovering = on;
    animate(m_hover, on ? 1.0 : 0.0, HoverSpring);
}

void ParticleGlobeItem::hoverEnterEvent(QHoverEvent *event)
{
    setMouse(event->position());
}

void ParticleGlobeItem::hoverMoveEvent(QHoverEvent *event)
{
    setMouse(event->position());
}

void ParticleGlobeItem::hoverLeaveEvent(QHoverEvent *)
{
    if (!m_dragging)
        setHover(false);
}

// Drag rotates: horizontal -> Y axis, vertical -> X axis, Shift + horizontal -> Z axis.
// A drag across the full item height turns the globe half a revolution.
void ParticleGlobeItem::mousePressEvent(QMouseEvent *event)
{
    if (!m_interactive) {
        event->ignore();
        return;
    }
    m_dragging = true;
    m_dragPos = event->position();
    if (!m_dragClock.isValid())
        m_dragClock.start();
    m_dragTime = m_dragClock.elapsed();
    m_velocityYaw = m_velocityPitch = m_velocityRoll = 0.0;
    setMouse(event->position());
}

void ParticleGlobeItem::mouseMoveEvent(QMouseEvent *event)
{
    setMouse(event->position());
    if (!m_dragging)
        return;

    const qint64 now = m_dragClock.elapsed();
    const double dt = std::max<qint64>(1, now - m_dragTime);
    const double perPixel = M_PI / std::max(1.0, height());
    const double dx = (event->position().x() - m_dragPos.x()) * perPixel;
    const double dy = (event->position().y() - m_dragPos.y()) * perPixel;
    const bool roll = event->modifiers() & Qt::ShiftModifier;
    const double dYaw = roll ? 0.0 : dx, dPitch = roll ? 0.0 : dy, dRoll = roll ? -dx : 0.0;

    m_yaw += dYaw;
    m_pitch += dPitch;
    m_roll += dRoll;
    // Smoothed so a final jittery event doesn't decide the throw, and capped because
    // events that arrive within the same millisecond would make it explode.
    auto throwSpeed = [&](double velocity, double delta) {
        return std::clamp(0.6 * velocity + 0.4 * delta / dt, -MaxThrowSpeed, MaxThrowSpeed);
    };
    m_velocityYaw = throwSpeed(m_velocityYaw, dYaw);
    m_velocityPitch = throwSpeed(m_velocityPitch, dPitch);
    m_velocityRoll = throwSpeed(m_velocityRoll, dRoll);

    m_dragPos = event->position();
    m_dragTime = now;
}

void ParticleGlobeItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    // Holding still before letting go means no throw.
    if (m_dragClock.elapsed() - m_dragTime > 80)
        m_velocityYaw = m_velocityPitch = m_velocityRoll = 0.0;
    m_dragging = false;
    if (!contains(event->position()))
        setHover(false);
}

QQuickRhiItemRenderer *ParticleGlobeItem::createRenderer()
{
    return new ParticleRenderer;
}
