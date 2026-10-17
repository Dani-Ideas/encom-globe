#include "particlerenderer.h"

#include "overlays.h"
#include "particleglobeitem.h"

#include <QFile>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Mirrors the uniform block in shaders/particles.* and shaders/shell.* (std140).
struct ParticleUniforms {
    float projection[16];
    float modelView[16];
    float rotation[16];
    float color[4];
    float mouse[2];
    float viewScale[2];
    float globe;
    float time;
    float turbulence;
    float mapScale;
    float pointSize;
    float mapPointSize;
    float pixelScale;
    float fogNear;
    float fogFar;
    float hover;
    float hoverRadius;
    float hoverPull;
    float hoverTurbulence;
    float layer;
    float shellRadius;
    float rimPower;
    float shellOpacity;
    float introTime;
    float introDuration;
    float introAltitude;
};
static_assert(sizeof(ParticleUniforms) == 304, "ParticleUniforms must match the std140 block");

constexpr int ParticleFloats = 10; // position(3) latLon(2) color(3) phase speed
constexpr float GlobeRadius = 500.f;
constexpr float FovDegrees = 50.f;

// grid-particles.bin (native/tools/grid2bin.js): tile centres plus real lat/lon.
std::vector<float> loadTiles()
{
    std::vector<float> tiles;
    QFile f(QStringLiteral(":/encomglobe/grid-particles.bin"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("EncomGlobe: grid-particles.bin not found in resources");
        return tiles;
    }
    const QByteArray data = f.readAll();
    constexpr qsizetype stride = 4 * (2 + 1 + 18);
    quint32 count = 0;
    if (data.size() >= 4)
        std::memcpy(&count, data.constData(), 4);
    if (data.size() < 4 + qsizetype(count) * stride) {
        qWarning("EncomGlobe: grid-particles.bin is truncated");
        return tiles;
    }
    tiles.reserve(count * 5);
    const char *p = data.constData() + 4;
    for (quint32 i = 0; i < count; ++i, p += stride) {
        float lat, lon, b[18];
        quint32 n;
        std::memcpy(&lat, p, 4);
        std::memcpy(&lon, p + 4, 4);
        std::memcpy(&n, p + 8, 4);
        std::memcpy(b, p + 12, sizeof(b));
        n = std::clamp<quint32>(n, 1, 6);
        float x = 0, y = 0, z = 0;
        for (quint32 j = 0; j < n; ++j) {
            x += b[j * 3];
            y += b[j * 3 + 1];
            z += b[j * 3 + 2];
        }
        tiles.insert(tiles.end(), { x / n, y / n, z / n, lat, lon });
    }
    return tiles;
}

float hueToRgb(float p, float q, float t)
{
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < 1.f / 2.f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * 6.f * (2.f / 3.f - t);
    return p;
}

// THREE.Color.setHSL with color management on (sRGB HSL in, linear out), as in
// ParticleGlobe.js; the raw shader outputs that linear value unchanged.
QVector3D hslToLinear(float h, float s, float l)
{
    using encom::srgbToLinear;
    if (s <= 0.f)
        return QVector3D(srgbToLinear(l), srgbToLinear(l), srgbToLinear(l));
    const float q = l <= 0.5f ? l * (1.f + s) : l + s - l * s;
    const float p = 2.f * l - q;
    return QVector3D(srgbToLinear(hueToRgb(p, q, h + 1.f / 3.f)), srgbToLinear(hueToRgb(p, q, h)),
                     srgbToLinear(hueToRgb(p, q, h - 1.f / 3.f)));
}

} // namespace

void ParticleRenderer::initialize(QRhiCommandBuffer *)
{
    if (m_rhi != rhi()) {
        releaseResources();
        m_overlays.reset();
        m_rhi = rhi();
    }
    if (m_sampleCount != renderTarget()->sampleCount()) {
        m_sampleCount = renderTarget()->sampleCount();
        m_landPipeline.reset();
    }
    QRhiTexture *finalTex = m_sampleCount > 1 ? resolveTexture() : colorTexture();
    if (m_textureFormat != finalTex->format()) {
        m_textureFormat = finalTex->format();
        m_landPipeline.reset();
        m_overlays.invalidatePipelines();
    }
    if (m_tiles.empty())
        m_tiles = loadTiles();

    if (!m_landUniforms) {
        auto makeUniforms = [&](std::unique_ptr<QRhiBuffer> &buffer, std::unique_ptr<QRhiShaderResourceBindings> &srb) {
            buffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(ParticleUniforms)));
            buffer->create();
            srb.reset(m_rhi->newShaderResourceBindings());
            srb->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                    buffer.get()),
            });
            srb->create();
        };
        makeUniforms(m_landUniforms, m_landSrb);
        makeUniforms(m_atmosphereUniforms, m_atmosphereSrb);
    }

    if (!m_landPipeline) {
        QRhiVertexInputLayout particleLayout;
        particleLayout.setBindings({ { ParticleFloats * sizeof(float) } });
        particleLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
            { 0, 1, QRhiVertexInputAttribute::Float2, 3 * sizeof(float) },
            { 0, 2, QRhiVertexInputAttribute::Float3, 5 * sizeof(float) },
            { 0, 3, QRhiVertexInputAttribute::Float, 8 * sizeof(float) },
            { 0, 4, QRhiVertexInputAttribute::Float, 9 * sizeof(float) },
        });
        QRhiVertexInputLayout shellLayout;
        shellLayout.setBindings({ { 3 * sizeof(float) } });
        shellLayout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });

        encom::PipelineOptions land;
        land.topology = QRhiGraphicsPipeline::Points;
        encom::PipelineOptions atmosphere = land;
        atmosphere.depthWrite = false;
        encom::PipelineOptions shell;
        shell.depthWrite = false;
        shell.additive = true;
        shell.cullBack = true;
        m_landPipeline = encom::createPipeline(m_rhi, renderTarget(), QStringLiteral("particles"), particleLayout,
                                               m_landSrb.get(), land);
        m_atmospherePipeline = encom::createPipeline(m_rhi, renderTarget(), QStringLiteral("particles"),
                                                     particleLayout, m_landSrb.get(), atmosphere);
        m_shellPipeline = encom::createPipeline(m_rhi, renderTarget(), QStringLiteral("shell"), shellLayout,
                                                m_landSrb.get(), shell);
    }
}

void ParticleRenderer::releaseResources()
{
    m_landPipeline.reset();
    m_atmospherePipeline.reset();
    m_shellPipeline.reset();
    m_landSrb.reset();
    m_atmosphereSrb.reset();
    m_landUniforms.reset();
    m_atmosphereUniforms.reset();
    m_landBuffer.reset();
    m_atmosphereBuffer.reset();
    m_shellVertices.reset();
    m_shellIndices.reset();
    m_builtColor = QColor();
    m_builtAtmosphereCount = -1;
}

void ParticleRenderer::synchronize(QQuickRhiItem *rhiItem)
{
    auto *item = static_cast<ParticleGlobeItem *>(rhiItem);
    m_state.baseColor = item->m_baseColor;
    m_state.atmosphereColor = item->m_atmosphereColor.isValid() ? item->m_atmosphereColor : item->m_baseColor;
    m_state.elapsedMs = item->m_elapsedMs;
    m_state.globeScale = item->m_globeScale > 0 ? item->m_globeScale : 1.0;
    m_state.pointSize = float(item->m_pointSize);
    m_state.turbulence = float(item->m_turbulence);
    m_state.hoverRadius = float(std::max<qreal>(1.0, item->m_hoverRadius));
    m_state.hoverPull = float(item->m_hoverPull);
    m_state.hoverTurbulence = float(item->m_hoverTurbulence);
    m_state.atmosphere = item->m_atmosphere;
    m_state.atmosphereScale = float(std::max<qreal>(1.03, item->m_atmosphereScale));
    m_state.atmosphereOpacity = float(item->m_atmosphereOpacity);
    m_state.atmosphereRimPower = float(item->m_atmosphereRimPower);
    m_state.atmosphereParticles = std::max(0, item->m_atmosphereParticles);
    m_state.atmospherePointSize = float(item->m_atmospherePointSize);
    m_state.atmosphereParticleOpacity = float(item->m_atmosphereParticleOpacity);
    m_state.globe = float(item->m_globe.value);
    m_state.hover = float(item->m_hover.value);
    m_state.mouse = item->m_mouse;

    QMatrix4x4 rotation;
    rotation.rotate(float(qRadiansToDegrees(item->m_roll)), 0, 0, 1);
    rotation.rotate(float(qRadiansToDegrees(item->m_tilt + item->m_pitch)), 1, 0, 0);
    rotation.rotate(float(qRadiansToDegrees(item->m_yaw)), 0, 1, 0);
    m_state.rotation = rotation;
    m_state.yaw = item->m_yaw;
    m_state.introDuration = item->m_introDuration;
    m_state.introAltitude = float(item->m_introAltitude);
    m_state.introLinesColor = item->m_introLinesColor;
    m_state.introLinesCount = item->m_introLinesCount;
    m_state.devicePixelRatio = item->window() ? float(item->window()->effectiveDevicePixelRatio()) : 1.0f;
    m_state.scene = item->m_scene;
}

// Same hue set and per-particle randomness as ParticleGlobe.js _createParticles.
void ParticleRenderer::buildLand(QRhiResourceUpdateBatch *u)
{
    float h, s, l;
    m_state.baseColor.toHsl().getHslF(&h, &s, &l);
    h = std::max(h, 0.f);
    auto *rng = QRandomGenerator::global();
    QVector3D palette[10];
    for (int i = 0; i < 10; ++i) {
        palette[i] = hslToLinear(std::fmod(h + (i - 5) * 0.02f + 1.f, 1.f), s,
                                 std::min(1.f, l + float(rng->generateDouble()) / 3.f));
    }

    const int count = int(m_tiles.size() / 5);
    std::vector<float> v;
    v.reserve(count * ParticleFloats);
    for (int i = 0; i < count; ++i) {
        const float *t = &m_tiles[i * 5];
        const QVector3D &c = palette[rng->bounded(10)];
        v.insert(v.end(), { t[0], t[1], t[2], t[3], t[4], c.x(), c.y(), c.z(),
                            float(rng->generateDouble() * 2 * M_PI), float(0.4 + rng->generateDouble() * 0.8) });
    }

    m_landBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                        quint32(std::max<size_t>(1, v.size()) * sizeof(float))));
    m_landBuffer->create();
    if (!v.empty())
        u->uploadStaticBuffer(m_landBuffer.get(), v.data());
    m_landCount = count;
    m_builtColor = m_state.baseColor;
}

// Uniform directions, spread through the layer between the globe and the shell.
void ParticleRenderer::buildAtmosphere(QRhiResourceUpdateBatch *u)
{
    auto *rng = QRandomGenerator::global();
    const int count = m_state.atmosphereParticles;
    std::vector<float> v;
    v.reserve(count * ParticleFloats);
    for (int i = 0; i < count; ++i) {
        const double z = rng->generateDouble() * 2 - 1;
        const double angle = rng->generateDouble() * 2 * M_PI;
        const double ring = std::sqrt(1 - z * z);
        const double r = GlobeRadius * (1.02 + rng->generateDouble() * (m_state.atmosphereScale - 1.02));
        v.insert(v.end(), { float(r * ring * std::cos(angle)), float(r * z), float(r * ring * std::sin(angle)),
                            0.f, 0.f, 0.f, 0.f, 0.f,
                            float(rng->generateDouble() * 2 * M_PI), float(0.2 + rng->generateDouble() * 0.5) });
    }

    m_atmosphereBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                              quint32(std::max<size_t>(1, v.size()) * sizeof(float))));
    m_atmosphereBuffer->create();
    if (!v.empty())
        u->uploadStaticBuffer(m_atmosphereBuffer.get(), v.data());
    m_atmosphereCount = count;
    m_builtAtmosphereCount = count;
    m_builtAtmosphereScale = m_state.atmosphereScale;
}

// Unit UV sphere; shell.vert scales it to the shell radius.
void ParticleRenderer::buildShell(QRhiResourceUpdateBatch *u)
{
    constexpr int segments = 96, rings = 48;
    std::vector<float> vertices;
    std::vector<quint16> indices;
    for (int r = 0; r <= rings; ++r) {
        const double phi = M_PI * r / rings;
        for (int s = 0; s <= segments; ++s) {
            const double theta = 2 * M_PI * s / segments;
            vertices.insert(vertices.end(), { float(std::sin(phi) * std::cos(theta)), float(std::cos(phi)),
                                              float(std::sin(phi) * std::sin(theta)) });
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const quint16 a = quint16(r * (segments + 1) + s), b = quint16(a + segments + 1);
            // Counter-clockwise seen from outside, so back-face culling keeps the near side.
            indices.insert(indices.end(), { a, quint16(a + 1), b, quint16(a + 1), quint16(b + 1), b });
        }
    }

    m_shellVertices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                           quint32(vertices.size() * sizeof(float))));
    m_shellVertices->create();
    u->uploadStaticBuffer(m_shellVertices.get(), vertices.data());
    m_shellIndices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                          quint32(indices.size() * sizeof(quint16))));
    m_shellIndices->create();
    u->uploadStaticBuffer(m_shellIndices.get(), indices.data());
    m_shellIndexCount = int(indices.size());
}

void ParticleRenderer::render(QRhiCommandBuffer *cb)
{
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();

    if (!m_tiles.empty() && (!m_landBuffer || m_builtColor != m_state.baseColor))
        buildLand(u);
    if (m_state.atmosphere && (!m_atmosphereBuffer || m_builtAtmosphereCount != m_state.atmosphereParticles
                               || m_builtAtmosphereScale != m_state.atmosphereScale))
        buildAtmosphere(u);
    const bool shell = m_state.atmosphere && m_state.atmosphereOpacity > 0.f;
    if (shell && !m_shellVertices)
        buildShell(u);

    // Fixed camera on +z; the particles rotate instead.
    const QSize size = renderTarget()->pixelSize();
    const float aspect = size.width() / float(std::max(1, size.height()));
    const float distance = float(1700.0 / m_state.globeScale);
    const float tanHalfFov = std::tan(qDegreesToRadians(FovDegrees / 2));

    QMatrix4x4 view;
    view.lookAt(QVector3D(0, 0, distance), QVector3D(0, 0, 0), QVector3D(0, 1, 0));
    QMatrix4x4 projection = m_rhi->clipSpaceCorrMatrix();
    projection.perspective(FovDegrees, aspect, 1.f, distance + 1000.f);

    // Fit the 2:1 map to the viewport; estimate tile spacing from the tile count
    // (~32% of the sphere is land) to keep a small gap between dots on the map.
    const float visibleHeight = 2 * distance * tanHalfFov;
    const float mapScale = 0.85f * std::min(visibleHeight * aspect, 2 * visibleHeight) / 360.f;
    const float spacingDegrees = std::sqrt(41253.f * 0.32f / float(std::max(1, m_landCount)));
    const QVector3D atmosphere = encom::linearRgb(m_state.atmosphereColor);

    ParticleUniforms uniforms = {};
    std::memcpy(uniforms.projection, projection.constData(), sizeof(uniforms.projection));
    std::memcpy(uniforms.modelView, view.constData(), sizeof(uniforms.modelView));
    std::memcpy(uniforms.rotation, m_state.rotation.constData(), sizeof(uniforms.rotation));
    uniforms.color[0] = atmosphere.x();
    uniforms.color[1] = atmosphere.y();
    uniforms.color[2] = atmosphere.z();
    uniforms.color[3] = m_state.atmosphereParticleOpacity;
    uniforms.mouse[0] = m_state.mouse.x();
    uniforms.mouse[1] = m_state.mouse.y();
    uniforms.viewScale[0] = tanHalfFov * aspect;
    uniforms.viewScale[1] = tanHalfFov;
    uniforms.globe = m_state.globe;
    uniforms.time = float(std::fmod(m_state.elapsedMs / 1000.0, 3600.0));
    uniforms.turbulence = m_state.turbulence;
    uniforms.mapScale = mapScale;
    uniforms.pointSize = m_state.pointSize;
    uniforms.mapPointSize = std::min(m_state.pointSize, mapScale * spacingDegrees * 0.6f);
    uniforms.pixelScale = size.height() / (2 * tanHalfFov);
    uniforms.fogNear = distance;
    uniforms.fogFar = distance + 300.f;
    uniforms.hover = m_state.hover;
    uniforms.hoverRadius = m_state.hoverRadius;
    uniforms.hoverPull = m_state.hoverPull;
    uniforms.hoverTurbulence = m_state.hoverTurbulence;
    uniforms.layer = 0.f;
    uniforms.shellRadius = GlobeRadius * m_state.atmosphereScale;
    uniforms.rimPower = m_state.atmosphereRimPower;
    uniforms.shellOpacity = m_state.atmosphereOpacity;
    // The sweep only needs the first seconds; clamping keeps float precision safe.
    uniforms.introTime = float(std::min(m_state.elapsedMs, 1.0e7));
    uniforms.introDuration = float(m_state.introDuration);
    uniforms.introAltitude = m_state.introAltitude;
    u->updateDynamicBuffer(m_landUniforms.get(), 0, sizeof(ParticleUniforms), &uniforms);

    uniforms.layer = 1.f;
    uniforms.pointSize = m_state.atmospherePointSize;
    u->updateDynamicBuffer(m_atmosphereUniforms.get(), 0, sizeof(ParticleUniforms), &uniforms);

    // Particles and overlays share one back-to-front pass (three.js sorts them
    // together); the particle clouds sit at the origin, created first.
    std::vector<encom::Draw> transparent;
    transparent.push_back({ m_landPipeline.get(), m_landSrb.get(), &m_landBuffer, 0, quint32(m_landCount),
                            distance, encom::OrderHostFirst });
    if (m_state.atmosphere && m_state.globe > 0.f) {
        transparent.push_back({ m_atmospherePipeline.get(), m_atmosphereSrb.get(), &m_atmosphereBuffer, 0,
                                quint32(m_atmosphereCount), distance, encom::OrderHostSecond });
    }

    // Overlays follow the particles' rotation and hide while the globe is flat.
    const bool overlays = m_state.globe > 0.6f;
    if (overlays) {
        encom::OverlayFrame frame;
        frame.projection = projection;
        frame.modelView = view * m_state.rotation;
        frame.pixelSize = size;
        frame.timeMs = m_state.elapsedMs;
        // The hex globe orbits its camera; here the globe turns instead, which puts
        // the camera at the equivalent orbit angle yaw + 90 degrees.
        frame.cameraAngle = m_state.yaw + M_PI / 2;
        frame.fogNear = distance;
        frame.fogFar = distance + 300.f;
        frame.devicePixelRatio = m_state.devicePixelRatio;
        frame.baseColor = m_state.baseColor;
        frame.introLines = true;
        frame.introDuration = m_state.introDuration;
        frame.introAltitude = m_state.introAltitude;
        frame.introLinesColor = m_state.introLinesColor;
        frame.introLinesCount = m_state.introLinesCount;
        m_overlays.prepare(m_rhi, renderTarget(), u, m_state.scene, frame, encom::GlobeUniforms {}, transparent);
    }

    const QRhiViewport viewport(0, 0, size.width(), size.height());
    cb->beginPass(renderTarget(), Qt::black, { 1.0f, 0 }, u);
    if (overlays)
        m_overlays.drawOpaque(cb, viewport);
    encom::drawSorted(cb, viewport, transparent);

    if (shell && m_state.globe > 0.f) {
        cb->setGraphicsPipeline(m_shellPipeline.get());
        cb->setViewport(viewport);
        cb->setShaderResources(m_atmosphereSrb.get());
        const QRhiCommandBuffer::VertexInput input(m_shellVertices.get(), 0);
        cb->setVertexInput(0, 1, &input, m_shellIndices.get(), 0, QRhiCommandBuffer::IndexUInt16);
        cb->drawIndexed(quint32(m_shellIndexCount));
    }

    cb->endPass();
}
